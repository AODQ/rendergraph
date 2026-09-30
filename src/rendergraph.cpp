#include "rendergraph.hpp"

#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <unordered_map>

// -----------------------------------------------------------------------------
// -- impl types
// -----------------------------------------------------------------------------

namespace
{
	struct ImplNodeInstance {
		uint32_t id;
		rg::NodeType type;
		// empty uses the node type's name
		std::string name;
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
		struct {
			uint32_t contextNode = 0; // right-clicked
			uint32_t contextLink = 0; // right-clicked
			uint32_t pressNode = 0; // left-pressed; opens properties on click
			uint32_t propertiesNode = 0;
			char nameBuffer[128] = {};
		} ui;
	};
}

// -----------------------------------------------------------------------------
// -- impl state
// -----------------------------------------------------------------------------

static constexpr size_t kUndoStackMax = 256;

static std::vector<rg::NodeTypeCi> sImplNodeTypes; // indexed by id - 1
static std::unordered_map<std::string, uint32_t> sImplNodeTypeUniqueNameToId;
static std::unordered_map<uint32_t, std::unique_ptr<ImplGraph>> sImplGraphs;

// -----------------------------------------------------------------------------
// -- impl lookup
// -----------------------------------------------------------------------------

static auto impl_node_type(
	rg::NodeType const hType
) -> rg::NodeTypeCi const &
{
	assert(hType.id > 0 && hType.id <= sImplNodeTypes.size());
	return sImplNodeTypes[hType.id - 1];
}

static auto impl_graph(
	rg::Graph const hGraph
) -> ImplGraph &
{
	return *sImplGraphs.at(hGraph.id);
}

static auto impl_node_find(
	ImplGraph const & graph,
	uint32_t const nodeId
) -> ImplNodeInstance const *
{
	auto const it = graph.state.nodes.find(nodeId);
	return it == graph.state.nodes.end() ? nullptr : &it->second;
}

static auto impl_link_find_into(
	ImplGraph const & graph,
	uint32_t const nodeB,
	uint32_t const pinB
) -> ImplLink const *
{
	for (ImplLink const & link : graph.state.links) {
		if (link.nodeB == nodeB && link.pinB == pinB) {
			return &link;
		}
	}
	return nullptr;
}

// -----------------------------------------------------------------------------
// -- impl undo recording
// -----------------------------------------------------------------------------

// Records an undo step before a mutation. Call only once the edit is known to
// succeed. Inside a transaction only the first mutation records.
static auto impl_edit_record(
	ImplGraph & graph
) -> void
{
	graph.positionsDirty = true;
	if (graph.transactionDepth > 0) {
		if (graph.transactionRecorded) { return; }
		graph.transactionRecorded = true;
	}
	graph.undoStack.push_back(graph.state);
	if (graph.undoStack.size() > kUndoStackMax) {
		graph.undoStack.erase(graph.undoStack.begin());
	}
	graph.redoStack.clear();
}

// -----------------------------------------------------------------------------
// -- impl link validation
// -----------------------------------------------------------------------------

// images and external images are both images; where the resource comes from
// doesn't change what can be connected
static auto impl_pin_kinds_compatible(
	rg::PinKind const a,
	rg::PinKind const b
) -> bool
{
	auto const isImage = [](rg::PinKind const k) {
		return k == rg::PinKind::image || k == rg::PinKind::externalImage;
	};
	return a == b || (isImage(a) && isImage(b));
}

// is 'target' reachable from 'start' following links forward? Links into
// previous-frame inputs are ignored, as they don't order passes within a frame.
static auto impl_node_reachable(
	ImplGraph const & graph,
	uint32_t const start,
	uint32_t const target
) -> bool
{
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
		for (ImplLink const & link : graph.state.links) {
			if (link.nodeA != nodeId) { continue; }
			ImplNodeInstance const & nodeB = graph.state.nodes.at(link.nodeB);
			rg::PinDesc const & pinB = impl_node_type(nodeB.type).pinIn[link.pinB];
			if (pinB.pinIn.temporal == rg::PinResourceTemporal::previousFrame) {
				continue;
			}
			stack.push_back(link.nodeB);
		}
	}
	return false;
}

static auto impl_link_check(
	ImplGraph const & graph,
	uint32_t const nodeAId,
	uint32_t const pinA,
	uint32_t const nodeBId,
	uint32_t const pinB,
	bool const allowReplace
) -> bool
{
	ImplNodeInstance const * const nodeA = impl_node_find(graph, nodeAId);
	ImplNodeInstance const * const nodeB = impl_node_find(graph, nodeBId);
	if (!nodeA || !nodeB) { return false; }

	rg::NodeTypeCi const & typeA = impl_node_type(nodeA->type);
	rg::NodeTypeCi const & typeB = impl_node_type(nodeB->type);
	if (pinA >= typeA.pinOut.size() || pinB >= typeB.pinIn.size()) {
		return false;
	}

	rg::PinDesc const & descA = typeA.pinOut[pinA];
	rg::PinDesc const & descB = typeB.pinIn[pinB];
	if (!impl_pin_kinds_compatible(descA.kind, descB.kind)) { return false; }

	// with allowReplace the caller removes the input's existing link first
	if (!allowReplace && impl_link_find_into(graph, nodeBId, pinB)) {
		return false;
	}

	// a previous-frame input can't form a cycle, even with its own node
	bool const temporal = (
		descB.pinIn.temporal == rg::PinResourceTemporal::previousFrame
	);
	if (!temporal && impl_node_reachable(graph, nodeBId, nodeAId)) {
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// -- impl edits
// -----------------------------------------------------------------------------

static auto impl_link_destroy(
	ImplGraph & graph,
	uint32_t const linkId
) -> void
{
	std::erase_if(graph.state.links, [&](ImplLink const & link) {
		return link.id == linkId;
	});
}

// -----------------------------------------------------------------------------
// -- node types
// -----------------------------------------------------------------------------

auto rg::register_node_type(
	NodeTypeCi const & ci
) -> rg::NodeType
{
	assert(
		!sImplNodeTypeUniqueNameToId.contains(ci.uniqueName)
		&& "node type registered twice"
	);
	sImplNodeTypes.push_back(ci);
	uint32_t const id = static_cast<uint32_t>(sImplNodeTypes.size());
	sImplNodeTypeUniqueNameToId[ci.uniqueName] = id;
	return { id };
}

auto rg::node_type_find(
	std::string const & uniqueName
) -> rg::NodeType
{
	auto const it = sImplNodeTypeUniqueNameToId.find(uniqueName);
	return { it == sImplNodeTypeUniqueNameToId.end() ? 0u : it->second };
}

// -----------------------------------------------------------------------------
// -- graph
// -----------------------------------------------------------------------------

auto rg::graph_create() -> rg::Graph
{
	static uint32_t sNextGraphId = 1;
	uint32_t const id = sNextGraphId++;
	auto graph = std::make_unique<ImplGraph>();
	graph->editor = ImNodes::EditorContextCreate();
	sImplGraphs[id] = std::move(graph);
	return { id };
}

auto rg::graph_destroy(
	Graph const hGraph
) -> void
{
	ImNodes::EditorContextFree(impl_graph(hGraph).editor);
	sImplGraphs.erase(hGraph.id);
}

// -----------------------------------------------------------------------------
// -- edits: nodes
// -----------------------------------------------------------------------------

auto rg::node_instance_create(
	Graph const hGraph,
	std::string const & uniqueName,
	int32_t const positionX,
	int32_t const positionY,
	std::vector<rg::ParameterValue> const & parameterValues
) -> rg::NodeInstance
{
	ImplGraph & graph = impl_graph(hGraph);
	NodeType const type = node_type_find(uniqueName);
	assert(type.id != 0 && "node type not registered");
	NodeTypeCi const & ci = impl_node_type(type);

	ImplNodeInstance node = {
		.id = graph.nextNodeId++,
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

	impl_edit_record(graph);
	graph.state.nodes[node.id] = node;
	return { .id = node.id, .type = type };
}

auto rg::node_instance_destroy(
	Graph const hGraph,
	NodeInstance const hNode
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!impl_node_find(graph, hNode.id)) { return; }
	impl_edit_record(graph);
	std::erase_if(graph.state.links, [&](ImplLink const & link) {
		return link.nodeA == hNode.id || link.nodeB == hNode.id;
	});
	graph.state.nodes.erase(hNode.id);
}

auto rg::node_instance_set_position(
	Graph const hGraph,
	NodeInstance const hNode,
	int32_t const positionX,
	int32_t const positionY
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImplNodeInstance const * const found = impl_node_find(graph, hNode.id);
	if (!found) { return; }
	if (found->posX == positionX && found->posY == positionY) { return; }
	impl_edit_record(graph);
	ImplNodeInstance & n = graph.state.nodes.at(hNode.id);
	n.posX = positionX;
	n.posY = positionY;
}

auto rg::node_instance_set_name(
	Graph const hGraph,
	NodeInstance const hNode,
	std::string const & name
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImplNodeInstance const * const found = impl_node_find(graph, hNode.id);
	if (!found || found->name == name) { return; }
	impl_edit_record(graph);
	graph.state.nodes.at(hNode.id).name = name;
}

// -----------------------------------------------------------------------------
// -- edits: links
// -----------------------------------------------------------------------------

auto rg::node_instance_can_link(
	Graph const hGraph,
	NodeInstance const hNodeA,
	uint32_t const pinA,
	NodeInstance const hNodeB,
	uint32_t const pinB
) -> bool
{
	return impl_link_check(
		impl_graph(hGraph), hNodeA.id, pinA, hNodeB.id, pinB, false
	);
}

auto rg::node_instance_link(
	Graph const hGraph,
	NodeInstance const hNodeA,
	uint32_t const pinA,
	NodeInstance const hNodeB,
	uint32_t const pinB
) -> rg::Link
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!impl_link_check(graph, hNodeA.id, pinA, hNodeB.id, pinB, false)) {
		return { 0 };
	}
	impl_edit_record(graph);
	ImplLink const link = {
		.id = graph.nextLinkId++,
		.nodeA = hNodeA.id,
		.pinA = pinA,
		.nodeB = hNodeB.id,
		.pinB = pinB,
	};
	graph.state.links.push_back(link);
	return { link.id };
}

auto rg::link_destroy(
	Graph const hGraph,
	Link const hLink
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	bool const exists = std::any_of(
		graph.state.links.begin(), graph.state.links.end(),
		[&](ImplLink const & l) { return l.id == hLink.id; }
	);
	if (!exists) { return; }
	impl_edit_record(graph);
	impl_link_destroy(graph, hLink.id);
}

// -----------------------------------------------------------------------------
// -- edits: transactions
// -----------------------------------------------------------------------------

auto rg::transaction_begin(
	Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (graph.transactionDepth++ == 0) {
		graph.transactionRecorded = false;
	}
}

auto rg::transaction_end(
	Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	assert(graph.transactionDepth > 0 && "unbalanced transaction_end");
	--graph.transactionDepth;
}

// -----------------------------------------------------------------------------
// -- undo
// -----------------------------------------------------------------------------

auto rg::undo(
	Graph const hGraph
) -> bool
{
	ImplGraph & graph = impl_graph(hGraph);
	assert(graph.transactionDepth == 0 && "undo inside a transaction");
	if (graph.undoStack.empty()) { return false; }
	graph.redoStack.push_back(std::move(graph.state));
	graph.state = std::move(graph.undoStack.back());
	graph.undoStack.pop_back();
	graph.positionsDirty = true;
	return true;
}

auto rg::redo(
	Graph const hGraph
) -> bool
{
	ImplGraph & graph = impl_graph(hGraph);
	assert(graph.transactionDepth == 0 && "redo inside a transaction");
	if (graph.redoStack.empty()) { return false; }
	graph.undoStack.push_back(std::move(graph.state));
	graph.state = std::move(graph.redoStack.back());
	graph.redoStack.pop_back();
	graph.positionsDirty = true;
	return true;
}

auto rg::undo_history_clear(
	Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	graph.undoStack.clear();
	graph.redoStack.clear();
}

// -----------------------------------------------------------------------------
// -- load/save
// -----------------------------------------------------------------------------

auto rg::load_from_file(
	Graph const hGraph,
	std::string const & path
) -> void
{
}

// -----------------------------------------------------------------------------
// -- ui impl: pin ids
// -----------------------------------------------------------------------------

// imnodes needs a unique int per pin; derive it from the model rather than
// storing one, so pins need no identity of their own
static auto impl_ui_pin_id(
	uint32_t const nodeId,
	rg::PinDir const dir,
	uint32_t const index
) -> int
{
	assert(nodeId < (1u << 15) && index < (1u << 15));
	uint32_t const dirBit = dir == rg::PinDir::out ? 1u : 0u;
	return static_cast<int>((nodeId << 16) | (dirBit << 15) | index);
}

namespace
{
	struct ImplUiPin {
		uint32_t nodeId;
		rg::PinDir dir;
		uint32_t index;
	};
}

static auto impl_ui_pin_decode(
	int const pinId
) -> ImplUiPin
{
	uint32_t const v = static_cast<uint32_t>(pinId);
	return {
		.nodeId = v >> 16,
		.dir = (v >> 15) & 1u ? rg::PinDir::out : rg::PinDir::in,
		.index = v & 0x7FFFu,
	};
}

// -----------------------------------------------------------------------------
// -- ui impl: labels
// -----------------------------------------------------------------------------

static auto impl_ui_node_name(
	ImplNodeInstance const & node
) -> char const *
{
	return (
		node.name.empty()
		? impl_node_type(node.type).name.c_str()
		: node.name.c_str()
	);
}

static auto impl_to_string(
	rg::PinKind const v
) -> char const *
{
	switch (v) {
		case rg::PinKind::image: return "image";
		case rg::PinKind::buffer: return "buffer";
		case rg::PinKind::externalImage: return "external image";
	}
	return "?";
}

static auto impl_to_string(
	rg::ImageFormat const v
) -> char const *
{
	switch (v) {
		case rg::ImageFormat::r8: return "r8";
		case rg::ImageFormat::r16: return "r16";
		case rg::ImageFormat::r32f: return "r32f";
		case rg::ImageFormat::rg8: return "rg8";
		case rg::ImageFormat::rg16: return "rg16";
		case rg::ImageFormat::rg32f: return "rg32f";
		case rg::ImageFormat::rgb8: return "rgb8";
		case rg::ImageFormat::rgb16: return "rgb16";
		case rg::ImageFormat::rgb32f: return "rgb32f";
		case rg::ImageFormat::rgba8: return "rgba8";
		case rg::ImageFormat::rgba16: return "rgba16";
		case rg::ImageFormat::rgba32f: return "rgba32f";
	}
	return "?";
}

static auto impl_to_string(
	rg::AccessMode const v
) -> char const *
{
	switch (v) {
		case rg::AccessMode::Sampled: return "sampled";
		case rg::AccessMode::StorageRead: return "storage read";
		case rg::AccessMode::StorageWrite: return "storage write";
		case rg::AccessMode::ColorAttachment: return "color attachment";
		case rg::AccessMode::DepthWrite: return "depth write";
		case rg::AccessMode::DepthRead: return "depth read";
		case rg::AccessMode::CopySrc: return "copy src";
		case rg::AccessMode::CopyDst: return "copy dst";
		case rg::AccessMode::IndirectBuffer: return "indirect buffer";
		case rg::AccessMode::UniformBuffer: return "uniform buffer";
	}
	return "?";
}

static auto impl_to_string(
	rg::NodeTypeQueue const v
) -> char const *
{
	switch (v) {
		case rg::NodeTypeQueue::graphics: return "graphics";
		case rg::NodeTypeQueue::compute: return "compute";
		case rg::NodeTypeQueue::copy: return "copy";
	}
	return "?";
}

static auto impl_ui_parameter_value_text(
	rg::ParameterDesc const & desc,
	rg::ParameterValue const & value
) -> void
{
	switch (desc.kind) {
		case rg::ParameterKind::f32:
			ImGui::Text(
				"%g  [%g, %g]",
				value.f32.value, desc.minValue.f32.value, desc.maxValue.f32.value
			);
			break;
		case rg::ParameterKind::i32:
			ImGui::Text(
				"%d  [%d, %d]",
				value.i32.value, desc.minValue.i32.value, desc.maxValue.i32.value
			);
			break;
		case rg::ParameterKind::u32:
			ImGui::Text(
				"%u  [%u, %u]",
				value.u32.value, desc.minValue.u32.value, desc.maxValue.u32.value
			);
			break;
		case rg::ParameterKind::boolean:
			ImGui::TextUnformatted(value.boolean.value ? "true" : "false");
			break;
		case rg::ParameterKind::enumeration: {
			// enumeration values are stored as their u32 index
			uint32_t const index = value.u32.value;
			ImGui::TextUnformatted(
				index < desc.enumValues.size()
				? desc.enumValues[index].c_str()
				: "<invalid>"
			);
			break;
		}
	}
}

// -----------------------------------------------------------------------------
// -- ui impl: theme
// -----------------------------------------------------------------------------

namespace
{
	// terminal palette: black background with amber, teal and green accents
	struct ImplUiPalette {
		ImU32 background;
		ImU32 surface; // title bars, lifted slightly from the background
		ImU32 border;
		ImU32 grid;
		ImU32 gridPrimary;
		ImU32 text;
		ImU32 dim; // secondary text, eg formats
		ImU32 accent; // amber
		ImU32 secondary; // teal
		ImU32 tertiary; // green
		ImU32 rule; // dark red; selection
	};

	struct ImplUiThemeColor {
		ImNodesCol col;
		ImU32 color;
	};

	struct ImplUiThemeVar {
		ImNodesStyleVar var;
		float value;
	};
}

constexpr ImplUiPalette kUiPalette = {
	.background = IM_COL32(0, 0, 0, 255),
	.surface = IM_COL32(18, 18, 18, 255),
	.border = IM_COL32(58, 58, 58, 255),
	.grid = IM_COL32(14, 14, 14, 255),
	.gridPrimary = IM_COL32(26, 26, 26, 255),
	.text = IM_COL32(208, 208, 208, 255),
	.dim = IM_COL32(108, 108, 108, 255),
	.accent = IM_COL32(215, 164, 95, 255),
	.secondary = IM_COL32(63, 167, 176, 255),
	.tertiary = IM_COL32(135, 215, 95, 255),
	.rule = IM_COL32(95, 10, 10, 255),
};

constexpr ImplUiThemeColor kUiThemeColors[] = {
	{ ImNodesCol_NodeBackground, kUiPalette.background },
	{ ImNodesCol_NodeBackgroundHovered, kUiPalette.background },
	{ ImNodesCol_NodeBackgroundSelected, kUiPalette.background },
	{ ImNodesCol_NodeOutline, kUiPalette.border },
	{ ImNodesCol_TitleBar, kUiPalette.surface },
	{ ImNodesCol_TitleBarHovered, kUiPalette.surface },
	{ ImNodesCol_TitleBarSelected, kUiPalette.rule },
	{ ImNodesCol_BoxSelector, IM_COL32(215, 164, 95, 30) },
	{ ImNodesCol_BoxSelectorOutline, kUiPalette.accent },
	{ ImNodesCol_GridBackground, kUiPalette.background },
	{ ImNodesCol_GridLine, kUiPalette.grid },
	{ ImNodesCol_GridLinePrimary, kUiPalette.gridPrimary },
	{ ImNodesCol_MiniMapBackground, kUiPalette.background },
	{ ImNodesCol_MiniMapBackgroundHovered, kUiPalette.background },
	{ ImNodesCol_MiniMapOutline, kUiPalette.border },
	{ ImNodesCol_MiniMapOutlineHovered, kUiPalette.border },
	{ ImNodesCol_MiniMapNodeBackground, kUiPalette.surface },
	{ ImNodesCol_MiniMapNodeBackgroundHovered, kUiPalette.border },
	{ ImNodesCol_MiniMapNodeBackgroundSelected, kUiPalette.rule },
	{ ImNodesCol_MiniMapNodeOutline, kUiPalette.border },
	{ ImNodesCol_MiniMapLink, kUiPalette.dim },
	{ ImNodesCol_MiniMapLinkSelected, kUiPalette.accent },
	{ ImNodesCol_MiniMapCanvas, IM_COL32(215, 164, 95, 20) },
	{ ImNodesCol_MiniMapCanvasOutline, kUiPalette.accent },
};

// square corners, hairline borders, tight padding
constexpr ImplUiThemeVar kUiThemeVars[] = {
	{ ImNodesStyleVar_GridSpacing, 24.0f },
	{ ImNodesStyleVar_NodeCornerRounding, 0.0f },
	{ ImNodesStyleVar_NodeBorderThickness, 1.0f },
	{ ImNodesStyleVar_LinkThickness, 1.5f },
	{ ImNodesStyleVar_PinCircleRadius, 3.5f },
	{ ImNodesStyleVar_PinQuadSideLength, 6.5f },
	{ ImNodesStyleVar_PinTriangleSideLength, 8.0f },
	{ ImNodesStyleVar_PinLineThickness, 1.0f },
};

constexpr ImVec2 kUiThemeNodePadding = ImVec2(8.0f, 4.0f);

// wraps BeginNodeEditor/EndNodeEditor; the theme only applies to the editor
static auto impl_ui_theme_push() -> void
{
	for (ImplUiThemeColor const & c : kUiThemeColors) {
		ImNodes::PushColorStyle(c.col, c.color);
	}
	for (ImplUiThemeVar const & v : kUiThemeVars) {
		ImNodes::PushStyleVar(v.var, v.value);
	}
	ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, kUiThemeNodePadding);
	ImGui::PushStyleColor(ImGuiCol_Text, kUiPalette.text);
}

static auto impl_ui_theme_pop() -> void
{
	ImGui::PopStyleColor();
	ImNodes::PopStyleVar(static_cast<int>(std::size(kUiThemeVars)) + 1);
	for (size_t i = 0; i < std::size(kUiThemeColors); ++i) {
		ImNodes::PopColorStyle();
	}
}

static auto impl_ui_text(
	ImU32 const color,
	char const * const text
) -> void
{
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
}

// -----------------------------------------------------------------------------
// -- ui impl: colors
// -----------------------------------------------------------------------------

namespace
{
	struct ImplUiKindStyle {
		ImU32 color;
		ImU32 hovered;
		ImU32 selected;
		ImNodesPinShape shape;
	};
}

// moves each channel toward white by t (0..1), keeping alpha
static auto impl_ui_color_lighten(
	ImU32 const color,
	float const t
) -> ImU32
{
	ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
	c.x += (1.0f - c.x) * t;
	c.y += (1.0f - c.y) * t;
	c.z += (1.0f - c.z) * t;
	return ImGui::ColorConvertFloat4ToU32(c);
}

static auto impl_ui_color_alpha(
	ImU32 const color,
	float const alpha
) -> ImU32
{
	ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
	c.w *= alpha;
	return ImGui::ColorConvertFloat4ToU32(c);
}

// pins and links are styled by the resource kind they carry; the shape keeps
// kinds distinguishable without relying on color alone
static auto impl_ui_kind_style(
	rg::PinKind const kind
) -> ImplUiKindStyle
{
	ImU32 color = 0;
	ImNodesPinShape shape = ImNodesPinShape_CircleFilled;
	switch (kind) {
		case rg::PinKind::image:
			color = kUiPalette.secondary;
			shape = ImNodesPinShape_CircleFilled;
			break;
		case rg::PinKind::buffer:
			color = kUiPalette.accent;
			shape = ImNodesPinShape_QuadFilled;
			break;
		case rg::PinKind::externalImage:
			color = kUiPalette.tertiary;
			shape = ImNodesPinShape_TriangleFilled;
			break;
	}
	return {
		.color = color,
		.hovered = impl_ui_color_lighten(color, 0.35f),
		.selected = impl_ui_color_lighten(color, 0.6f),
		.shape = shape,
	};
}

// filled shapes have an outline-only counterpart one enum value below
static auto impl_ui_pin_shape_hollow(
	ImNodesPinShape const shape
) -> ImNodesPinShape
{
	switch (shape) {
		case ImNodesPinShape_CircleFilled: return ImNodesPinShape_Circle;
		case ImNodesPinShape_TriangleFilled: return ImNodesPinShape_Triangle;
		case ImNodesPinShape_QuadFilled: return ImNodesPinShape_Quad;
		default: return shape;
	}
}

// previous-frame links point "backwards" in the graph; draw them fainter so
// they don't read as cycles
constexpr float kUiTemporalLinkAlpha = 0.4f;

// -----------------------------------------------------------------------------
// -- ui impl: node drawing
// -----------------------------------------------------------------------------

// the dim line under a pin; only images have a format worth showing
static auto impl_ui_pin_detail(
	rg::PinDesc const & pin
) -> char const *
{
	if (pin.kind == rg::PinKind::buffer) { return nullptr; }
	return impl_to_string(
		pin.dir == rg::PinDir::in ? pin.pinIn.format : pin.pinOut.format
	);
}

static auto impl_ui_text_width(
	char const * const text
) -> float
{
	return text ? ImGui::CalcTextSize(text).x : 0.0f;
}

// Statusline title (name left, queue right), then one row per pin index:
// input i on the left, output i on the right, each with a dim format line.
static auto impl_ui_draw_node(
	ImplGraph const & graph,
	ImplNodeInstance const & node
) -> void
{
	rg::NodeTypeCi const & nodeType = impl_node_type(node.type);
	int const nodeId = static_cast<int>(node.id);
	char const * const name = impl_ui_node_name(node);
	char const * const queue = impl_to_string(nodeType.queue);
	size_t const numIn = nodeType.pinIn.size();
	size_t const numOut = nodeType.pinOut.size();
	size_t const numRows = std::max(numIn, numOut);

	// -- measure; columns are separated by at least a gap
	float const gap = impl_ui_text_width("    ");
	float width = impl_ui_text_width(name) + gap + impl_ui_text_width(queue);
	for (size_t row = 0; row < numRows; ++row) {
		rg::PinDesc const * const pinIn = row < numIn ? &nodeType.pinIn[row] : nullptr;
		rg::PinDesc const * const pinOut = row < numOut ? &nodeType.pinOut[row] : nullptr;
		float const nameWidth = (
			(pinIn ? impl_ui_text_width(pinIn->name.c_str()) : 0.0f)
			+ gap
			+ (pinOut ? impl_ui_text_width(pinOut->name.c_str()) : 0.0f)
		);
		float const detailWidth = (
			(pinIn ? impl_ui_text_width(impl_ui_pin_detail(*pinIn)) : 0.0f)
			+ gap
			+ (pinOut ? impl_ui_text_width(impl_ui_pin_detail(*pinOut)) : 0.0f)
		);
		width = std::max(width, std::max(nameWidth, detailWidth));
	}

	bool const selected = ImNodes::IsNodeSelected(nodeId);
	if (selected) {
		ImNodes::PushColorStyle(ImNodesCol_NodeOutline, kUiPalette.accent);
	}
	ImNodes::BeginNode(nodeId);

	if (graph.positionsDirty) {
		ImNodes::SetNodeGridSpacePos(
			nodeId,
			ImVec2(static_cast<float>(node.posX), static_cast<float>(node.posY))
		);
	}

	// -- title
	ImNodes::BeginNodeTitleBar();
	{
		ImVec2 const start = ImGui::GetCursorScreenPos();
		impl_ui_text(kUiPalette.accent, name);
		ImGui::SetCursorScreenPos(
			ImVec2(start.x + width - impl_ui_text_width(queue), start.y)
		);
		impl_ui_text(kUiPalette.dim, queue);
	}
	ImNodes::EndNodeTitleBar();

	// -- rows
	float const left = ImGui::GetCursorScreenPos().x;
	float const right = left + width;
	float const lineStep = ImGui::GetTextLineHeightWithSpacing();
	float y = ImGui::GetCursorScreenPos().y;
	for (size_t row = 0; row < numRows; ++row) {
		uint32_t const index = static_cast<uint32_t>(row);
		rg::PinDesc const * const pinIn = row < numIn ? &nodeType.pinIn[row] : nullptr;
		rg::PinDesc const * const pinOut = row < numOut ? &nodeType.pinOut[row] : nullptr;

		// previous-frame inputs are hollow
		if (pinIn) {
			ImplUiKindStyle const style = impl_ui_kind_style(pinIn->kind);
			bool const temporal = (
				pinIn->pinIn.temporal == rg::PinResourceTemporal::previousFrame
			);
			ImGui::SetCursorScreenPos(ImVec2(left, y));
			ImNodes::PushColorStyle(ImNodesCol_Pin, style.color);
			ImNodes::PushColorStyle(ImNodesCol_PinHovered, style.hovered);
			ImNodes::BeginInputAttribute(
				impl_ui_pin_id(node.id, rg::PinDir::in, index),
				temporal ? impl_ui_pin_shape_hollow(style.shape) : style.shape
			);
			ImGui::TextUnformatted(pinIn->name.c_str());
			ImNodes::EndInputAttribute();
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();
		}

		if (pinOut) {
			ImplUiKindStyle const style = impl_ui_kind_style(pinOut->kind);
			float const nameWidth = impl_ui_text_width(pinOut->name.c_str());
			ImGui::SetCursorScreenPos(ImVec2(right - nameWidth, y));
			ImNodes::PushColorStyle(ImNodesCol_Pin, style.color);
			ImNodes::PushColorStyle(ImNodesCol_PinHovered, style.hovered);
			ImNodes::BeginOutputAttribute(
				impl_ui_pin_id(node.id, rg::PinDir::out, index), style.shape
			);
			ImGui::TextUnformatted(pinOut->name.c_str());
			ImNodes::EndOutputAttribute();
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();
		}
		y += lineStep;

		// -- detail line; skipped when neither side has one
		char const * const detailIn = pinIn ? impl_ui_pin_detail(*pinIn) : nullptr;
		char const * const detailOut = pinOut ? impl_ui_pin_detail(*pinOut) : nullptr;
		if (detailIn) {
			ImGui::SetCursorScreenPos(ImVec2(left, y));
			impl_ui_text(kUiPalette.dim, detailIn);
		}
		if (detailOut) {
			ImGui::SetCursorScreenPos(
				ImVec2(right - impl_ui_text_width(detailOut), y)
			);
			impl_ui_text(kUiPalette.dim, detailOut);
		}
		if (detailIn || detailOut) {
			y += lineStep;
		}
	}

	// imgui needs an item after moving the cursor to extend the node's bounds
	ImGui::SetCursorScreenPos(ImVec2(left, y));
	ImGui::Dummy(ImVec2(width, 0.0f));

	ImNodes::EndNode();
	if (selected) {
		ImNodes::PopColorStyle();
	}
}

static auto impl_ui_draw_rendergraph(
	ImplGraph const & graph
) -> void
{
	// -- render nodes
	for (auto const & [id, node] : graph.state.nodes) {
		impl_ui_draw_node(graph, node);
	}

	// -- render links; colored by the resource kind the output produces
	for (ImplLink const & link : graph.state.links) {
		ImplNodeInstance const & nodeA = graph.state.nodes.at(link.nodeA);
		ImplNodeInstance const & nodeB = graph.state.nodes.at(link.nodeB);
		rg::PinDesc const & pinA = impl_node_type(nodeA.type).pinOut[link.pinA];
		rg::PinDesc const & pinB = impl_node_type(nodeB.type).pinIn[link.pinB];
		ImplUiKindStyle const style = impl_ui_kind_style(pinA.kind);
		float const alpha = (
			pinB.pinIn.temporal == rg::PinResourceTemporal::previousFrame
			? kUiTemporalLinkAlpha
			: 1.0f
		);
		ImNodes::PushColorStyle(
			ImNodesCol_Link, impl_ui_color_alpha(style.color, alpha)
		);
		ImNodes::PushColorStyle(
			ImNodesCol_LinkHovered, impl_ui_color_alpha(style.hovered, alpha)
		);
		ImNodes::PushColorStyle(ImNodesCol_LinkSelected, style.selected);
		ImNodes::Link(
			static_cast<int>(link.id),
			impl_ui_pin_id(link.nodeA, rg::PinDir::out, link.pinA),
			impl_ui_pin_id(link.nodeB, rg::PinDir::in, link.pinB)
		);
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
	}
}

// -----------------------------------------------------------------------------
// -- ui impl: properties popup
// -----------------------------------------------------------------------------

static auto impl_ui_open_properties(
	ImplGraph & graph,
	uint32_t const nodeId
) -> void
{
	ImplNodeInstance const * const node = impl_node_find(graph, nodeId);
	if (!node) { return; }
	graph.ui.propertiesNode = nodeId;
	snprintf(graph.ui.nameBuffer, sizeof(graph.ui.nameBuffer), "%s", node->name.c_str());
	ImGui::OpenPopup("rg_node_properties");
}

static auto impl_ui_draw_properties(
	rg::Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!ImGui::BeginPopup("rg_node_properties")) { return; }

	// the node can vanish under the popup, eg via undo
	ImplNodeInstance const * const node = impl_node_find(graph, graph.ui.propertiesNode);
	if (!node) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	rg::NodeTypeCi const & nodeType = impl_node_type(node->type);
	rg::NodeInstance const hNode = { .id = node->id, .type = node->type };

	// -- name; committed once editing finishes so a rename is one undo step
	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputTextWithHint(
		"name", nodeType.name.c_str(), graph.ui.nameBuffer, sizeof(graph.ui.nameBuffer)
	);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		rg::node_instance_set_name(hGraph, hNode, graph.ui.nameBuffer);
	}

	ImGui::Text("type   %s (%s)", nodeType.name.c_str(), nodeType.uniqueName.c_str());
	ImGui::Text("queue  %s", impl_to_string(nodeType.queue));
	ImGui::Text("id     %u", node->id);

	ImGuiTableFlags const tableFlags = (
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
	);

	// -- inputs
	if (!nodeType.pinIn.empty()) {
		ImGui::SeparatorText("inputs");
		if (ImGui::BeginTable("inputs", 7, tableFlags)) {
			for (char const * col : {
				"name", "kind", "format", "access", "frame", "size", "linked from",
			}) {
				ImGui::TableSetupColumn(col);
			}
			ImGui::TableHeadersRow();
			for (uint32_t i = 0; i < nodeType.pinIn.size(); ++i) {
				rg::PinDesc const & pin = nodeType.pinIn[i];
				bool const previous = (
					pin.pinIn.temporal == rg::PinResourceTemporal::previousFrame
				);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s%s", pin.name.c_str(), pin.pinIn.required ? "" : " (optional)");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.kind));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.pinIn.format));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.pinIn.accessMode));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(previous ? "previous" : "current");
				ImGui::TableNextColumn();
				ImGui::Text("%gx%g, %u mips", pin.scaleWidth, pin.scaleHeight, pin.mipLevels);
				ImGui::TableNextColumn();
				if (ImplLink const * link = impl_link_find_into(graph, node->id, i)) {
					ImplNodeInstance const & from = graph.state.nodes.at(link->nodeA);
					ImGui::Text(
						"%s.%s",
						impl_ui_node_name(from),
						impl_node_type(from.type).pinOut[link->pinA].name.c_str()
					);
				} else {
					ImGui::TextDisabled("-");
				}
			}
			ImGui::EndTable();
		}
	}

	// -- outputs
	if (!nodeType.pinOut.empty()) {
		ImGui::SeparatorText("outputs");
		if (ImGui::BeginTable("outputs", 6, tableFlags)) {
			for (char const * col : {
				"name", "kind", "format", "access", "size", "links",
			}) {
				ImGui::TableSetupColumn(col);
			}
			ImGui::TableHeadersRow();
			for (uint32_t i = 0; i < nodeType.pinOut.size(); ++i) {
				rg::PinDesc const & pin = nodeType.pinOut[i];
				auto const numLinks = std::count_if(
					graph.state.links.begin(), graph.state.links.end(),
					[&](ImplLink const & l) {
						return l.nodeA == node->id && l.pinA == i;
					}
				);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(pin.name.c_str());
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.kind));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.pinOut.format));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(impl_to_string(pin.pinOut.accessMode));
				ImGui::TableNextColumn();
				ImGui::Text("%gx%g, %u mips", pin.scaleWidth, pin.scaleHeight, pin.mipLevels);
				ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(numLinks));
			}
			ImGui::EndTable();
		}
	}

	// -- parameters
	if (!nodeType.parameters.empty()) {
		ImGui::SeparatorText("parameters");
		if (ImGui::BeginTable("parameters", 2, tableFlags)) {
			for (size_t i = 0; i < nodeType.parameters.size(); ++i) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(nodeType.parameters[i].name.c_str());
				ImGui::TableNextColumn();
				impl_ui_parameter_value_text(
					nodeType.parameters[i], node->parameterValues[i]
				);
			}
			ImGui::EndTable();
		}
	}

	ImGui::EndPopup();
}

// -----------------------------------------------------------------------------
// -- ui impl: input
// -----------------------------------------------------------------------------

// All interaction queries; imnodes only answers these after EndNodeEditor.
static auto impl_ui_handle_input(
	rg::Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImGuiIO const & io = ImGui::GetIO();

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
				&& impl_link_check(graph, a.nodeId, a.index, b.nodeId, b.index, true)
			);
			if (valid) {
				rg::transaction_begin(hGraph);
				if (ImplLink const * old = impl_link_find_into(graph, b.nodeId, b.index)) {
					rg::link_destroy(hGraph, { old->id });
				}
				rg::node_instance_link(
					hGraph, { a.nodeId }, a.index, { b.nodeId }, b.index
				);
				rg::transaction_end(hGraph);
			}
		}
	}

	// -- left click on a node (without dragging it) opens its properties
	{
		bool const modifier = io.KeyShift || io.KeyCtrl || io.KeySuper;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			int nodeId = 0;
			int pinId = 0;
			bool const onNode = (
				ImNodes::IsNodeHovered(&nodeId) && !ImNodes::IsPinHovered(&pinId)
			);
			graph.ui.pressNode = onNode && !modifier ? static_cast<uint32_t>(nodeId) : 0;
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && graph.ui.pressNode) {
			float const threshold = io.MouseDragThreshold;
			if (io.MouseDragMaxDistanceSqr[0] < threshold * threshold) {
				impl_ui_open_properties(graph, graph.ui.pressNode);
			}
			graph.ui.pressNode = 0;
		}
	}

	// -- node moves; recorded once the drag ends
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		rg::transaction_begin(hGraph);
		for (auto const & [id, node] : graph.state.nodes) {
			ImVec2 const pos = ImNodes::GetNodeGridSpacePos(static_cast<int>(id));
			rg::node_instance_set_position(
				hGraph,
				{ id },
				static_cast<int32_t>(std::lround(pos.x)),
				static_cast<int32_t>(std::lround(pos.y))
			);
		}
		rg::transaction_end(hGraph);
		// setting positions only mirrors imnodes; nothing to push back
		graph.positionsDirty = false;
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

		rg::transaction_begin(hGraph);
		for (int const id : links) {
			rg::link_destroy(hGraph, { static_cast<uint32_t>(id) });
		}
		for (int const id : nodes) {
			rg::node_instance_destroy(hGraph, { static_cast<uint32_t>(id) });
		}
		rg::transaction_end(hGraph);
		ImNodes::ClearLinkSelection();
		ImNodes::ClearNodeSelection();
	}

	// -- undo/redo; ImGuiMod_Ctrl is cmd on macOS
	{
		bool changed = false;
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) {
			changed = rg::undo(hGraph);
		}
		if (
			ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)
			|| ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)
		) {
			changed = rg::redo(hGraph);
		}
		if (changed) {
			ImNodes::ClearLinkSelection();
			ImNodes::ClearNodeSelection();
		}
	}

	// -- WASD panning, while the editor has focus and nothing takes text
	{
		bool const focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		bool const modifier = io.KeyCtrl || io.KeySuper || io.KeyAlt;
		if (focused && !modifier && !io.WantTextInput) {
			float const speed = 800.0f * io.DeltaTime; // pixels per second
			ImVec2 delta = ImVec2(0.0f, 0.0f);
			if (ImGui::IsKeyDown(ImGuiKey_W)) { delta.y += speed; }
			if (ImGui::IsKeyDown(ImGuiKey_S)) { delta.y -= speed; }
			if (ImGui::IsKeyDown(ImGuiKey_A)) { delta.x += speed; }
			if (ImGui::IsKeyDown(ImGuiKey_D)) { delta.x -= speed; }
			if (delta.x != 0.0f || delta.y != 0.0f) {
				ImVec2 const panning = ImNodes::EditorContextGetPanning();
				ImNodes::EditorContextResetPanning(
					ImVec2(panning.x + delta.x, panning.y + delta.y)
				);
			}
		}
	}

	// -- right click menus; links and nodes take priority over the canvas
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		int linkId = 0;
		int nodeId = 0;
		if (ImNodes::IsLinkHovered(&linkId)) {
			graph.ui.contextLink = static_cast<uint32_t>(linkId);
			ImGui::OpenPopup("rg_link_menu");
		} else if (ImNodes::IsNodeHovered(&nodeId)) {
			graph.ui.contextNode = static_cast<uint32_t>(nodeId);
			ImGui::OpenPopup("rg_node_menu");
		} else if (ImNodes::IsEditorHovered()) {
			ImGui::OpenPopup("rg_context_menu");
		}
	}

	if (ImGui::BeginPopup("rg_link_menu")) {
		if (ImGui::MenuItem("delete link")) {
			rg::link_destroy(hGraph, { graph.ui.contextLink });
			ImNodes::ClearLinkSelection();
		}
		ImGui::EndPopup();
	}

	bool openProperties = false;
	if (ImGui::BeginPopup("rg_node_menu")) {
		if (ImGui::MenuItem("properties")) {
			openProperties = true;
		}
		if (ImGui::MenuItem("delete node")) {
			rg::node_instance_destroy(hGraph, { graph.ui.contextNode });
			ImNodes::ClearNodeSelection();
		}
		ImGui::EndPopup();
	}
	// opened outside the menu so it isn't a child of the closing popup
	if (openProperties) {
		impl_ui_open_properties(graph, graph.ui.contextNode);
	}

	if (ImGui::BeginPopup("rg_context_menu")) {
		if (ImGui::BeginMenu("add node")) {
			for (rg::NodeTypeCi const & nodeType : sImplNodeTypes) {
				if (!ImGui::MenuItem(nodeType.name.c_str())) {
					continue;
				}
				// let imnodes convert the click position to grid space, then
				// store it; one undo step for create + place
				rg::transaction_begin(hGraph);
				rg::NodeInstance const hNode = (
					rg::node_instance_create(hGraph, nodeType.uniqueName)
				);
				int const nodeId = static_cast<int>(hNode.id);
				ImNodes::SetNodeScreenSpacePos(
					nodeId, ImGui::GetMousePosOnOpeningCurrentPopup()
				);
				ImVec2 const pos = ImNodes::GetNodeGridSpacePos(nodeId);
				rg::node_instance_set_position(
					hGraph,
					hNode,
					static_cast<int32_t>(std::lround(pos.x)),
					static_cast<int32_t>(std::lround(pos.y))
				);
				rg::transaction_end(hGraph);
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	impl_ui_draw_properties(hGraph);
}

// -----------------------------------------------------------------------------
// -- ui public
// -----------------------------------------------------------------------------

auto rg::ui_draw(
	Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, kUiPalette.background);
	ImGui::Begin("Render Graph");
	ImGui::PopStyleColor();
	ImNodes::EditorContextSet(graph.editor);

	impl_ui_theme_push();
	ImNodes::BeginNodeEditor();
	impl_ui_draw_rendergraph(graph);
	graph.positionsDirty = false;

	// -- minimap; imnodes needs it after all nodes and links
	ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);

	ImNodes::EndNodeEditor();
	impl_ui_theme_pop();

	impl_ui_handle_input(hGraph);
	ImGui::End();
}
