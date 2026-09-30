#include "rendergraph.hpp"

#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>

// -----------------------------------------------------------------------------
// -- impl details
// -----------------------------------------------------------------------------

namespace
{
	struct ImplNodeInstance {
		uint32_t id;
		rg::NodeType type;
		std::vector<rg::ParameterValue> parameterValues;
		int32_t posX = 0;
		int32_t posY = 0;
	};

	// nodeA.pinA (output) -> nodeB.pinB (input)
	struct ImplLink {
		uint32_t id;
		uint32_t nodeA;
		uint32_t pinA;
		uint32_t nodeB;
		uint32_t pinB;
	};

	// everything that undo/redo restores; copied whole for every undo step
	struct ImplGraphState {
		// ordered so iteration (and later compilation) is deterministic
		std::map<uint32_t, ImplNodeInstance> nodes;
		std::vector<ImplLink> links;
	};

	struct ImplGraph {
		ImplGraphState state;

		std::vector<ImplGraphState> undoStack;
		std::vector<ImplGraphState> redoStack;
		uint32_t transactionDepth = 0;
		bool transactionRecorded = false;

		// never restored by undo, so ids are never reused
		uint32_t nextNodeId = 1;
		uint32_t nextLinkId = 1;

		// ui state
		ImNodesEditorContext * editor = nullptr;
		// model positions need to be pushed to imnodes next frame
		bool positionsDirty = true;
	};

	constexpr size_t kUndoStackMax = 256;
}

static std::vector<rg::NodeTypeCi> sImplNodeTypes; // indexed by id - 1
static std::unordered_map<std::string, uint32_t> sImplNodeTypeUniqueNameToId;
static std::unordered_map<uint32_t, std::unique_ptr<ImplGraph>> sImplGraphs;

// -----------------------------------------------------------------------------

static rg::NodeTypeCi const & impl_node_type(rg::NodeType const type) {
	assert(type.id > 0 && type.id <= sImplNodeTypes.size());
	return sImplNodeTypes[type.id - 1];
}

static ImplGraph & impl_graph(rg::Graph const graph) {
	return *sImplGraphs.at(graph.id);
}

static ImplNodeInstance const * impl_node_find(
	ImplGraph const & g,
	uint32_t const nodeId
) {
	auto const it = g.state.nodes.find(nodeId);
	return it == g.state.nodes.end() ? nullptr : &it->second;
}

static ImplLink const * impl_link_find_into(
	ImplGraph const & g,
	uint32_t const nodeB,
	uint32_t const pinB
) {
	for (ImplLink const & link : g.state.links) {
		if (link.nodeB == nodeB && link.pinB == pinB) {
			return &link;
		}
	}
	return nullptr;
}

// Records an undo step before a mutation. Call only once the edit is known to
// succeed. Inside a transaction only the first mutation records.
static void impl_edit_record(ImplGraph & g) {
	g.positionsDirty = true;
	if (g.transactionDepth > 0) {
		if (g.transactionRecorded) { return; }
		g.transactionRecorded = true;
	}
	g.undoStack.push_back(g.state);
	if (g.undoStack.size() > kUndoStackMax) {
		g.undoStack.erase(g.undoStack.begin());
	}
	g.redoStack.clear();
}

// images and external images are both images; where the resource comes from
// doesn't change what can be connected
static bool impl_pin_kinds_compatible(rg::PinKind const a, rg::PinKind const b) {
	auto const isImage = [](rg::PinKind const k) {
		return k == rg::PinKind::image || k == rg::PinKind::externalImage;
	};
	return a == b || (isImage(a) && isImage(b));
}

// is 'target' reachable from 'start' following links forward? Links into
// previous-frame inputs are ignored, as they don't order passes within a frame.
static bool impl_node_reachable(
	ImplGraph const & g,
	uint32_t const start,
	uint32_t const target
) {
	std::vector<uint32_t> stack = { start };
	std::vector<uint32_t> visited;
	while (!stack.empty()) {
		uint32_t const nodeId = stack.back();
		stack.pop_back();
		if (nodeId == target) { return true; }
		if (std::find(visited.begin(), visited.end(), nodeId) != visited.end()) {
			continue;
		}
		visited.push_back(nodeId);
		for (ImplLink const & link : g.state.links) {
			if (link.nodeA != nodeId) { continue; }
			ImplNodeInstance const & nodeB = g.state.nodes.at(link.nodeB);
			rg::PinDesc const & pinB = impl_node_type(nodeB.type).pinIn[link.pinB];
			if (pinB.pinIn.temporal == rg::PinResourceTemporal::previousFrame) {
				continue;
			}
			stack.push_back(link.nodeB);
		}
	}
	return false;
}

static bool impl_link_check(
	ImplGraph const & g,
	uint32_t const nodeAId,
	uint32_t const pinA,
	uint32_t const nodeBId,
	uint32_t const pinB,
	bool const allowReplace
) {
	ImplNodeInstance const * const nodeA = impl_node_find(g, nodeAId);
	ImplNodeInstance const * const nodeB = impl_node_find(g, nodeBId);
	if (!nodeA || !nodeB) { return false; }

	rg::NodeTypeCi const & typeA = impl_node_type(nodeA->type);
	rg::NodeTypeCi const & typeB = impl_node_type(nodeB->type);
	if (pinA >= typeA.pinOut.size() || pinB >= typeB.pinIn.size()) {
		return false;
	}

	rg::PinDesc const & descA = typeA.pinOut[pinA];
	rg::PinDesc const & descB = typeB.pinIn[pinB];
	if (!impl_pin_kinds_compatible(descA.kind, descB.kind)) { return false; }

	if (!allowReplace && impl_link_find_into(g, nodeBId, pinB)) {
		return false;
	}

	// a previous-frame input can't form a cycle, even with its own node
	bool const temporal = (
		descB.pinIn.temporal == rg::PinResourceTemporal::previousFrame
	);
	if (!temporal && impl_node_reachable(g, nodeBId, nodeAId)) {
		return false;
	}

	return true;
}

static void impl_link_destroy(ImplGraph & g, uint32_t const linkId) {
	std::erase_if(g.state.links, [&](ImplLink const & link) {
		return link.id == linkId;
	});
}

// -----------------------------------------------------------------------------
// -- node types
// -----------------------------------------------------------------------------

rg::NodeType rg::register_node_type(NodeTypeCi const & ci) {
	assert(
		!sImplNodeTypeUniqueNameToId.contains(ci.uniqueName)
		&& "node type registered twice"
	);
	sImplNodeTypes.push_back(ci);
	uint32_t const id = static_cast<uint32_t>(sImplNodeTypes.size());
	sImplNodeTypeUniqueNameToId[ci.uniqueName] = id;
	return { id };
}

rg::NodeType rg::node_type_find(std::string const & uniqueName) {
	auto const it = sImplNodeTypeUniqueNameToId.find(uniqueName);
	return { it == sImplNodeTypeUniqueNameToId.end() ? 0u : it->second };
}

// -----------------------------------------------------------------------------
// -- graph
// -----------------------------------------------------------------------------

rg::Graph rg::graph_create() {
	static uint32_t sNextGraphId = 1;
	uint32_t const id = sNextGraphId++;
	auto g = std::make_unique<ImplGraph>();
	g->editor = ImNodes::EditorContextCreate();
	sImplGraphs[id] = std::move(g);
	return { id };
}

void rg::graph_destroy(Graph const graph) {
	ImNodes::EditorContextFree(impl_graph(graph).editor);
	sImplGraphs.erase(graph.id);
}

// -----------------------------------------------------------------------------
// -- edits
// -----------------------------------------------------------------------------

rg::NodeInstance rg::node_instance_create(
	Graph const graph,
	std::string const & uniqueName,
	int32_t const positionX,
	int32_t const positionY,
	std::vector<rg::ParameterValue> const & parameterValues
) {
	ImplGraph & g = impl_graph(graph);
	NodeType const type = node_type_find(uniqueName);
	assert(type.id != 0 && "node type not registered");
	NodeTypeCi const & ci = impl_node_type(type);

	ImplNodeInstance node = {
		.id = g.nextNodeId++,
		.type = type,
		.parameterValues = parameterValues,
		.posX = positionX,
		.posY = positionY,
	};
	if (node.parameterValues.empty()) {
		for (ParameterDesc const & param : ci.parameters) {
			node.parameterValues.push_back(param.defaultValue);
		}
	}
	assert(node.parameterValues.size() == ci.parameters.size());

	impl_edit_record(g);
	g.state.nodes[node.id] = node;
	return { .id = node.id, .type = type };
}

void rg::node_instance_destroy(Graph const graph, NodeInstance const node) {
	ImplGraph & g = impl_graph(graph);
	if (!impl_node_find(g, node.id)) { return; }
	impl_edit_record(g);
	std::erase_if(g.state.links, [&](ImplLink const & link) {
		return link.nodeA == node.id || link.nodeB == node.id;
	});
	g.state.nodes.erase(node.id);
}

void rg::node_instance_set_position(
	Graph const graph,
	NodeInstance const node,
	int32_t const positionX,
	int32_t const positionY
) {
	ImplGraph & g = impl_graph(graph);
	ImplNodeInstance const * const found = impl_node_find(g, node.id);
	if (!found) { return; }
	if (found->posX == positionX && found->posY == positionY) { return; }
	impl_edit_record(g);
	ImplNodeInstance & n = g.state.nodes.at(node.id);
	n.posX = positionX;
	n.posY = positionY;
}

bool rg::node_instance_can_link(
	Graph const graph,
	NodeInstance const nodeA,
	uint32_t const pinA,
	NodeInstance const nodeB,
	uint32_t const pinB
) {
	return impl_link_check(
		impl_graph(graph), nodeA.id, pinA, nodeB.id, pinB, false
	);
}

rg::Link rg::node_instance_link(
	Graph const graph,
	NodeInstance const nodeA,
	uint32_t const pinA,
	NodeInstance const nodeB,
	uint32_t const pinB
) {
	ImplGraph & g = impl_graph(graph);
	if (!impl_link_check(g, nodeA.id, pinA, nodeB.id, pinB, false)) {
		return { 0 };
	}
	impl_edit_record(g);
	ImplLink const link = {
		.id = g.nextLinkId++,
		.nodeA = nodeA.id,
		.pinA = pinA,
		.nodeB = nodeB.id,
		.pinB = pinB,
	};
	g.state.links.push_back(link);
	return { link.id };
}

void rg::link_destroy(Graph const graph, Link const link) {
	ImplGraph & g = impl_graph(graph);
	bool const exists = std::any_of(
		g.state.links.begin(), g.state.links.end(),
		[&](ImplLink const & l) { return l.id == link.id; }
	);
	if (!exists) { return; }
	impl_edit_record(g);
	impl_link_destroy(g, link.id);
}

void rg::transaction_begin(Graph const graph) {
	ImplGraph & g = impl_graph(graph);
	if (g.transactionDepth++ == 0) {
		g.transactionRecorded = false;
	}
}

void rg::transaction_end(Graph const graph) {
	ImplGraph & g = impl_graph(graph);
	assert(g.transactionDepth > 0 && "unbalanced transaction_end");
	--g.transactionDepth;
}

// -----------------------------------------------------------------------------
// -- undo
// -----------------------------------------------------------------------------

bool rg::undo(Graph const graph) {
	ImplGraph & g = impl_graph(graph);
	assert(g.transactionDepth == 0 && "undo inside a transaction");
	if (g.undoStack.empty()) { return false; }
	g.redoStack.push_back(std::move(g.state));
	g.state = std::move(g.undoStack.back());
	g.undoStack.pop_back();
	g.positionsDirty = true;
	return true;
}

bool rg::redo(Graph const graph) {
	ImplGraph & g = impl_graph(graph);
	assert(g.transactionDepth == 0 && "redo inside a transaction");
	if (g.redoStack.empty()) { return false; }
	g.undoStack.push_back(std::move(g.state));
	g.state = std::move(g.redoStack.back());
	g.redoStack.pop_back();
	g.positionsDirty = true;
	return true;
}

void rg::undo_history_clear(Graph const graph) {
	ImplGraph & g = impl_graph(graph);
	g.undoStack.clear();
	g.redoStack.clear();
}

// -----------------------------------------------------------------------------
// -- load/save
// -----------------------------------------------------------------------------

void rg::load_from_file(Graph const graph, std::string const & path) {
}

// -----------------------------------------------------------------------------
// -- ui impl detail
// -----------------------------------------------------------------------------

// imnodes needs a unique int per pin; derive it from the model rather than
// storing one, so pins need no identity of their own
static int impl_ui_pin_id(
	uint32_t const nodeId,
	rg::PinDir const dir,
	uint32_t const index
) {
	assert(nodeId < (1u << 15) && index < (1u << 15));
	uint32_t const dirBit = dir == rg::PinDir::out ? 1u : 0u;
	return static_cast<int>((nodeId << 16) | (dirBit << 15) | index);
}

struct ImplUiPin {
	uint32_t nodeId;
	rg::PinDir dir;
	uint32_t index;
};

static ImplUiPin impl_ui_pin_decode(int const pinId) {
	uint32_t const v = static_cast<uint32_t>(pinId);
	return {
		.nodeId = v >> 16,
		.dir = (v >> 15) & 1u ? rg::PinDir::out : rg::PinDir::in,
		.index = v & 0x7FFFu,
	};
}

static void impl_ui_draw_node(ImplGraph const & g, ImplNodeInstance const & node) {
	rg::NodeTypeCi const & nodeType = impl_node_type(node.type);
	int const nodeId = static_cast<int>(node.id);
	ImNodes::BeginNode(nodeId);

	if (g.positionsDirty) {
		ImNodes::SetNodeGridSpacePos(
			nodeId,
			ImVec2(static_cast<float>(node.posX), static_cast<float>(node.posY))
		);
	}

	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted(nodeType.name.c_str());
	ImNodes::EndNodeTitleBar();

	ImGui::Dummy(ImVec2(0.0f, 5.0f));

	// -- render input pins
	for (uint32_t i = 0; i < nodeType.pinIn.size(); ++i) {
		ImNodes::BeginInputAttribute(impl_ui_pin_id(node.id, rg::PinDir::in, i));
		ImGui::TextUnformatted(nodeType.pinIn[i].name.c_str());
		ImNodes::EndInputAttribute();
	}

	// -- render output pins
	for (uint32_t i = 0; i < nodeType.pinOut.size(); ++i) {
		ImNodes::BeginOutputAttribute(impl_ui_pin_id(node.id, rg::PinDir::out, i));
		ImGui::TextUnformatted(nodeType.pinOut[i].name.c_str());
		ImNodes::EndOutputAttribute();
	}

	ImNodes::EndNode();
}

static void impl_ui_draw_rendergraph(ImplGraph const & g) {
	// -- render nodes
	for (auto const & [id, node] : g.state.nodes) {
		impl_ui_draw_node(g, node);
	}

	// -- render links
	for (ImplLink const & link : g.state.links) {
		ImNodes::Link(
			static_cast<int>(link.id),
			impl_ui_pin_id(link.nodeA, rg::PinDir::out, link.pinA),
			impl_ui_pin_id(link.nodeB, rg::PinDir::in, link.pinB)
		);
	}
}

// All interaction queries; imnodes only answers these after EndNodeEditor.
static void impl_ui_handle_input(rg::Graph const graph) {
	ImplGraph & g = impl_graph(graph);

	// -- created links; dropping onto a linked input replaces its link
	{
		int startedId = 0;
		int endedId = 0;
		if (ImNodes::IsLinkCreated(&startedId, &endedId)) {
			ImplUiPin a = impl_ui_pin_decode(startedId);
			ImplUiPin b = impl_ui_pin_decode(endedId);
			if (a.dir == rg::PinDir::in) { std::swap(a, b); }
			bool const valid = (
				a.dir == rg::PinDir::out && b.dir == rg::PinDir::in
				&& impl_link_check(g, a.nodeId, a.index, b.nodeId, b.index, true)
			);
			if (valid) {
				rg::transaction_begin(graph);
				if (ImplLink const * old = impl_link_find_into(g, b.nodeId, b.index)) {
					rg::link_destroy(graph, { old->id });
				}
				rg::node_instance_link(
					graph, { a.nodeId }, a.index, { b.nodeId }, b.index
				);
				rg::transaction_end(graph);
			}
		}
	}

	// -- node moves; recorded once the drag ends
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		rg::transaction_begin(graph);
		for (auto const & [id, node] : g.state.nodes) {
			ImVec2 const pos = ImNodes::GetNodeGridSpacePos(static_cast<int>(id));
			rg::node_instance_set_position(
				graph,
				{ id },
				static_cast<int32_t>(std::lround(pos.x)),
				static_cast<int32_t>(std::lround(pos.y))
			);
		}
		rg::transaction_end(graph);
		// setting positions only mirrors imnodes; nothing to push back
		g.positionsDirty = false;
	}

	// -- delete selection
	bool const deletePressed = (
		ImGui::Shortcut(ImGuiKey_Delete) || ImGui::Shortcut(ImGuiKey_Backspace)
	);
	if (deletePressed) {
		int const numLinks = ImNodes::NumSelectedLinks();
		int const numNodes = ImNodes::NumSelectedNodes();
		std::vector<int> links(static_cast<size_t>(numLinks));
		std::vector<int> nodes(static_cast<size_t>(numNodes));
		if (numLinks > 0) { ImNodes::GetSelectedLinks(links.data()); }
		if (numNodes > 0) { ImNodes::GetSelectedNodes(nodes.data()); }

		rg::transaction_begin(graph);
		for (int const id : links) {
			rg::link_destroy(graph, { static_cast<uint32_t>(id) });
		}
		for (int const id : nodes) {
			rg::node_instance_destroy(graph, { static_cast<uint32_t>(id) });
		}
		rg::transaction_end(graph);
		ImNodes::ClearLinkSelection();
		ImNodes::ClearNodeSelection();
	}

	// -- undo/redo; ImGuiMod_Ctrl is cmd on macOS
	{
		bool changed = false;
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) {
			changed = rg::undo(graph);
		}
		if (
			ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)
			|| ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)
		) {
			changed = rg::redo(graph);
		}
		if (changed) {
			ImNodes::ClearLinkSelection();
			ImNodes::ClearNodeSelection();
		}
	}

	// -- right click menu
	if (
		ImNodes::IsEditorHovered()
		&& ImGui::IsMouseClicked(ImGuiMouseButton_Right)
	) {
		ImGui::OpenPopup("rg_context_menu");
	}
	if (ImGui::BeginPopup("rg_context_menu")) {
		if (ImGui::BeginMenu("add node")) {
			for (rg::NodeTypeCi const & nodeType : sImplNodeTypes) {
				if (!ImGui::MenuItem(nodeType.name.c_str())) {
					continue;
				}
				// let imnodes convert the click position to grid space, then
				// store it; one undo step for create + place
				rg::transaction_begin(graph);
				rg::NodeInstance const node = (
					rg::node_instance_create(graph, nodeType.uniqueName)
				);
				int const nodeId = static_cast<int>(node.id);
				ImNodes::SetNodeScreenSpacePos(
					nodeId, ImGui::GetMousePosOnOpeningCurrentPopup()
				);
				ImVec2 const pos = ImNodes::GetNodeGridSpacePos(nodeId);
				rg::node_instance_set_position(
					graph,
					node,
					static_cast<int32_t>(std::lround(pos.x)),
					static_cast<int32_t>(std::lround(pos.y))
				);
				rg::transaction_end(graph);
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
}

// -----------------------------------------------------------------------------
// -- ui public
// -----------------------------------------------------------------------------

void rg::ui_draw(Graph const graph) {
	ImplGraph & g = impl_graph(graph);

	ImGui::Begin("Render Graph");
	ImNodes::EditorContextSet(g.editor);
	ImNodes::BeginNodeEditor();
	impl_ui_draw_rendergraph(g);
	g.positionsDirty = false;
	ImNodes::EndNodeEditor();

	impl_ui_handle_input(graph);
	ImGui::End();
}
