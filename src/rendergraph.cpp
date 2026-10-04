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
#include <set>

#include "log.hpp"

// -----------------------------------------------------------------------------
// -- imgui: group
// -----------------------------------------------------------------------------

namespace
{
	struct ImplGroup
	{
		// -- metadata
		uint32_t id = 0;
		std::string name;

		// -- positions
		float x = 0.0f;
		float y = 0.0f;
		float w = 260.0f;
		float h = 180.0f;

		// -- data
		rg::Group hParentGroup = {};
		std::vector<rg::Group> hChildrenGroup;
		std::vector<rg::NodeInstance> hNodes;

		// -- global layout
		// indices of the fields this group overrides, in enable order
		std::vector<uint32_t> enabledFields;

		bool collapsed = false;

		uint32_t color = 0x3A3A46u;
	};

	struct ImplGraph;
	struct ImplGraphState;

	struct ImplGroupValidateFni {
		// if non-null, store offending ids
		std::vector<uint32_t> * outNodes = nullptr;
		// if non-null, store offending ids
		std::vector<uint32_t> * outGroups = nullptr;
	};
}

auto impl_group_contains(
	ImplGroup const & group,
	float const x,
	float const y
) -> bool;

auto impl_group(
	ImplGraph & graph,
	int32_t const groupId
) -> ImplGroup *;

auto impl_group_translate(
	ImplGraph & graph,
	ImplGroup & group,
	float const dx,
	float const dy
) -> void;

// true if every node and group is either fully inside or fully outside of
// every other group
auto impl_group_validate(
	ImplGraphState const & state,
	ImplGroupValidateFni const & fni = {}
) -> bool;

// rebuild group parents, children and nodes from positions; state must be valid
auto impl_group_recalculate(
	ImplGraphState & state
) -> void;

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
		// size from imnodes' last draw
		float width = 0.0f;
		float height = 0.0f;
	};

	// nodeA.pinA (output) -> nodeB.pinB (input)
	// or, if groupB is non-zero, nodeA.pinA (output) -> groupB.fieldB
	struct ImplLink {
		uint32_t id;
		uint32_t nodeA;
		uint32_t pinA;
		// 0 if the link ends at a group field
		uint32_t nodeB;
		uint32_t pinB;
		// 0 if the link ends at a node pin
		uint32_t groupB = 0;
		uint32_t fieldB = 0;
	};

	// current graph state
	struct ImplGraphState {
		// all group entries, element 0 is the parent container
		std::map<uint32_t, ImplGroup> groups;
		// flat list of all nodes
		std::unordered_map<uint32_t, ImplNodeInstance> nodes;
		std::vector<ImplLink> links;
	};

	struct ImplGraph {
		ImplGraphState state;

		std::vector<ImplGraphState> undoStack;
		std::vector<ImplGraphState> redoStack;
		uint32_t transactionDepth = 0;
		bool transactionRecorded = false;
		// set when an edit is rejected and reverted
		bool editReverted = false;

		// next id to assign to a new node
		uint32_t nextNodeId = 1;

		// next id to assign to a new link
		uint32_t nextLinkId = 1;

		// ui state
		ImNodesEditorContext * editor = nullptr;

		bool positionsDirty = true;
		struct {
			// right-clicked
			uint32_t contextNode = 0;
			// right-clicked
			uint32_t contextLink = 0;
			uint32_t propertiesNode = 0;
			// double-clicked; opens properties on mouse release
			uint32_t propertiesNodePending = 0;
			char nameBuffer[128] = {};

			// -- camera panning when the left mouse was pressed
			ImVec2 pressPanning = {};

			// -- track state during group header drag and resize
			bool groupDragRecorded = false;
			ImVec2 groupDragPosDelta = {};
			ImVec2 groupDragGridScreenPos = {};

			// -- group popups
			struct {
				// right-clicked
				uint32_t contextGroup = 0;
				uint32_t renameGroup = 0;
			} popup;

			// -- selection
			struct {
				std::vector<uint32_t> groups;

				// -- track state during box selection
				bool boxActive = false;
				ImVec2 boxStartGridPos = {};
				std::vector<uint32_t> boxBaseGroups;
			} selected;

		} ui;
	};
}

// -----------------------------------------------------------------------------
// -- impl state
// -----------------------------------------------------------------------------

static constexpr size_t kUndoStackMax = 256;

static std::vector<rg::NodeTypeCi> sImplNodeTypes; // indexed by id - 1
static std::unordered_map<std::string, uint32_t> sImplNodeTypeUniqueNameToId;
static rg::GlobalLayoutCi sImplGlobalLayout;
static bool sImplGlobalLayoutRegistered = false;
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

static auto impl_link_find_into_group(
	ImplGraph const & graph,
	uint32_t const groupB,
	uint32_t const fieldB
) -> ImplLink const *
{
	for (ImplLink const & link : graph.state.links) {
		if (link.groupB == groupB && link.fieldB == fieldB) {
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
	graph.undoStack.emplace_back(graph.state);
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
static auto impl_pin_compatible(
	rg::PinDesc const a,
	rg::PinDesc const b
) -> bool
{
	auto const isImage = [](rg::PinKind const k) {
		return k == rg::PinKind::image || k == rg::PinKind::externalImage;
	};
	bool const bothImages = isImage(a.kind) && isImage(b.kind);
	if (bothImages)
	{
		return true;
	}
	if (a.kind == rg::PinKind::buffer && b.kind == rg::PinKind::buffer)
	{
		return a.bufferType == b.bufferType;
	}
	return false;
}

// appends the ids of the nodes in the group and all of its nested groups
static auto impl_group_collect_nodes(
	ImplGraph const & graph,
	uint32_t const groupId,
	std::vector<uint32_t> & outNodes
) -> void
{
	ImplGroup const & group = graph.state.groups.at(groupId);
	for (rg::NodeInstance const hNode : group.hNodes) {
		outNodes.emplace_back(hNode.id);
	}
	for (rg::Group const hChild : group.hChildrenGroup) {
		impl_group_collect_nodes(graph, hChild.id, outNodes);
	}
}

// is 'target' reachable from 'start' following links forward? Links into
// previous-frame inputs are ignored, as they don't order passes within a frame.
// A link into a group field leads to all nodes in the group.
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
		visited.emplace_back(nodeId);
		for (ImplLink const & link : graph.state.links) {
			if (link.nodeA != nodeId) { continue; }

			// -- follow a group field link to the nodes in the group
			if (link.groupB != 0) {
				impl_group_collect_nodes(graph, link.groupB, stack);
				continue;
			}

			// -- follow a node pin link
			ImplNodeInstance const & nodeB = graph.state.nodes.at(link.nodeB);
			rg::PinDesc const & pinB = impl_node_type(nodeB.type).pinIn[link.pinB];
			if (pinB.pinIn.temporal == rg::PinResourceTemporal::previousFrame) {
				continue;
			}
			stack.emplace_back(link.nodeB);
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
	if (!impl_pin_compatible(descA, descB)) { return false; }

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

// returns if nodeA is reachable from the group's nodes
static auto impl_group_field_forms_cycle(
	ImplGraph const & graph,
	uint32_t const nodeAId,
	uint32_t const groupId
) -> bool
{
	std::vector<uint32_t> groupNodes;
	impl_group_collect_nodes(graph, groupId, groupNodes);
	for (uint32_t const groupNodeId : groupNodes) {
		if (impl_node_reachable(graph, groupNodeId, nodeAId)) {
			return true;
		}
	}
	return false;
}

static auto impl_group_field_link_check(
	ImplGraph const & graph,
	uint32_t const nodeAId,
	uint32_t const pinA,
	uint32_t const groupId,
	uint32_t const field,
	bool const allowReplace
) -> bool
{
	ImplNodeInstance const * const nodeA = impl_node_find(graph, nodeAId);
	auto const groupIt = graph.state.groups.find(groupId);
	if (!nodeA || groupId == 0 || groupIt == graph.state.groups.end()) {
		return false;
	}

	// -- check the pin and the field
	rg::NodeTypeCi const & typeA = impl_node_type(nodeA->type);
	if (pinA >= typeA.pinOut.size()) { return false; }
	if (field >= sImplGlobalLayout.fields.size()) { return false; }

	std::vector<uint32_t> const & enabled = groupIt->second.enabledFields;
	if (std::find(enabled.begin(), enabled.end(), field) == enabled.end()) {
		return false;
	}

	rg::PinDesc const & descA = typeA.pinOut[pinA];
	rg::GlobalFieldDesc const & descB = sImplGlobalLayout.fields[field];
	bool const compatible = (
		descA.kind == rg::PinKind::buffer
		&& descA.bufferType == descB.bufferType
	);
	if (!compatible) { return false; }

	// -- check the field is not already linked
	// with allowReplace the caller removes the field's existing link first
	if (!allowReplace && impl_link_find_into_group(graph, groupId, field)) {
		return false;
	}

	// -- check for a cycle
	return !impl_group_field_forms_cycle(graph, nodeAId, groupId);
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
	sImplNodeTypes.emplace_back(ci);
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
// -- global layout
// -----------------------------------------------------------------------------

auto rg::register_global_layout(
	GlobalLayoutCi const & ci
) -> void
{
	rg_assert(!sImplGlobalLayoutRegistered, "global layout registered twice");
	sImplGlobalLayout = ci;
	sImplGlobalLayoutRegistered = true;
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
	// always has a group of 0 for parent container
	sImplGraphs[id]->state.groups.emplace(
		0,
		ImplGroup {
			.id = 0,
			.name = "Parent",
			.x = -990000.0f,
			.y = -990000.0f,
			.w = +990000.0f,
			.h = +990000.0f,
		}
	);
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
// -- imgui: group
// -----------------------------------------------------------------------------

auto impl_group_contains(
	ImplGroup const & group,
	float const x,
	float const y
) -> bool
{
	return (
		x >= group.x && x <= group.x + group.w 
		&& y >= group.y && y <= group.y + group.h
	);
}

auto impl_group(
	ImplGraph & graph,
	int32_t const groupId
) -> ImplGroup *
{
	auto const it = graph.state.groups.find(groupId);
	return it == graph.state.groups.end() ? nullptr : &it->second;
}

auto impl_group_translate(
	ImplGraph & graph,
	ImplGroup & group,
	float const dx,
	float const dy
) -> void
{
	group.x += dx;
	group.y += dy;
	for (rg::Group const hChild : group.hChildrenGroup) {
		ImplGroup & child = *impl_group(graph, hChild.id);
		impl_group_translate(graph, child, dx, dy);
	}
	for (rg::NodeInstance const hNode : group.hNodes) {
		ImplNodeInstance & node = graph.state.nodes.at(hNode.id);
		node.posX += static_cast<int32_t>(dx);
		node.posY += static_cast<int32_t>(dy);
	}
}

// width of group's pin column
constexpr float kGroupPinColumnWidth = 120.0f;

namespace
{
	struct ImplRect {
		float x;
		float y;
		float w;
		float h;
	};
}

static auto impl_rect_group(
	ImplGroup const & group
) -> ImplRect
{
	return { group.x, group.y, group.w, group.h };
}

// the group's rect excluding its pin column
static auto impl_rect_group_content(
	ImplGroup const & group
) -> ImplRect
{
	return {
		group.x + kGroupPinColumnWidth,
		group.y,
		group.w - kGroupPinColumnWidth,
		group.h
	};
}

static auto impl_rect_node(
	ImplNodeInstance const & node
) -> ImplRect
{
	return { (float)node.posX, (float)node.posY, node.width, node.height };
}

static auto impl_rect_area(
	ImplRect const rect
) -> float
{
	return rect.w * rect.h;
}

static auto impl_rect_overlaps(
	ImplRect const a,
	ImplRect const b
) -> bool
{
	return (
		a.x < b.x + b.w && b.x < a.x + a.w
		&& a.y < b.y + b.h && b.y < a.y + a.h
	);
}

static auto impl_rect_contains(
	ImplRect const outer,
	ImplRect const inner
) -> bool
{
	return (
		inner.x >= outer.x && inner.x + inner.w <= outer.x + outer.w
		&& inner.y >= outer.y && inner.y + inner.h <= outer.y + outer.h
	);
}

auto impl_group_validate(
	ImplGraphState const & state,
	ImplGroupValidateFni const & fni
) -> bool
{
	auto const storeOffender = [](
		std::vector<uint32_t> * const out,
		uint32_t const offenderId
	) {
		if (
			out && std::find(out->begin(), out->end(), offenderId) == out->end()
		) {
			out->emplace_back(offenderId);
		}
	};
	bool const breakOnFirstInvalid = !fni.outNodes && !fni.outGroups;
	bool valid = true;

	for (auto const & [groupId, group] : state.groups) {
		// group 0 contains everything
		if (groupId == 0) { continue; }
		ImplRect const groupRect = impl_rect_group(group);
		ImplRect const contentRect = impl_rect_group_content(group);

		// -- validate node partially intersecting group
		for (auto const & [nodeId, node] : state.nodes) {
			ImplRect const nodeRect = impl_rect_node(node);
			// a node overlapping the group must be inside its content rect
			if (
				!impl_rect_overlaps(groupRect, nodeRect)
				|| impl_rect_contains(contentRect, nodeRect)
			) {
				continue;
			}
			valid = false;
			storeOffender(fni.outNodes, nodeId);
			storeOffender(fni.outGroups, groupId);
			if (breakOnFirstInvalid) { return false; }
		}

		// -- validate group partially intersecting group
		for (auto const & [otherGroupId, otherGroup] : state.groups) {
			if (otherGroupId == 0 || otherGroupId <= groupId) { continue; }
			ImplRect const otherRect = impl_rect_group(otherGroup);
			ImplRect const otherContentRect = (
				impl_rect_group_content(otherGroup)
			);
			if (!impl_rect_overlaps(groupRect, otherRect)) { continue; }

			// an identical rect is not nested, so it is a partial intersection.
			// A group inside the other's pin column is not nested.
			bool const nested = (
				(
					impl_rect_contains(contentRect, otherRect)
					&& impl_rect_area(groupRect) > impl_rect_area(otherRect)
				)
				|| (
					impl_rect_contains(otherContentRect, groupRect)
					&& impl_rect_area(otherRect) > impl_rect_area(groupRect)
				)
			);
			if (nested) { continue; }
			valid = false;
			storeOffender(fni.outGroups, groupId);
			storeOffender(fni.outGroups, otherGroupId);
			if (breakOnFirstInvalid) { return false; }
		}
	}
	return valid;
}

auto impl_group_recalculate(
	ImplGraphState & state
) -> void
{
	// finds the parent group of the node (if hGroup is 0) or group
	auto const findParentGroup = [&](
		ImplRect const rect,
		rg::Group const hGroup
	) -> rg::Group
	{
		// -- find best fitting parent group
		rg::Group hParentGroup = { 0 };
		float parentArea = 0.0f;
		for (auto const & [candidateId, candidate] : state.groups) {
			if (candidateId == 0 || candidateId == hGroup.id) { continue; }

			// -- check if candidate contains this node or group
			ImplRect const candidateRect = impl_rect_group(candidate);
			if (
				!impl_rect_contains(impl_rect_group_content(candidate), rect)
			) {
				continue;
			}

			float const candidateArea = impl_rect_area(candidateRect);
			if (hParentGroup.id == 0 || candidateArea < parentArea) {
				hParentGroup = { candidateId };
				parentArea = candidateArea;
			}
		}
		return hParentGroup;
	};

	// -- clear groups
	for (auto & [groupId, group] : state.groups) {
		group.hChildrenGroup.clear();
		group.hNodes.clear();
	}

	// -- assign parent to each group
	for (auto & [groupId, group] : state.groups) {
		if (groupId == 0) { continue; }
		group.hParentGroup = findParentGroup(
			impl_rect_group(group), rg::Group { .id = group.id }
		);
		(
			state
			.groups.at(group.hParentGroup.id)
			.hChildrenGroup.emplace_back(rg::Group { .id = group.id })
		);
	}

	// -- sort node ids
	std::vector<rg::NodeInstance> const hNodes = [&]() {
		std::vector<rg::NodeInstance> sorted;
		for (auto const & [nodeId, node] : state.nodes) {
			sorted.emplace_back(
				rg::NodeInstance { .id = nodeId, .type = node.type }
			);
		}
		std::sort(
			sorted.begin(),
			sorted.end(),
			[](auto const & a, auto const & b) { return a.id < b.id; }
		);
		return sorted;
	}();

	// -- assign each node to its parent group in id order
	for (rg::NodeInstance const hNode : hNodes) {
		ImplNodeInstance const & node = state.nodes.at(hNode.id);
		rg::Group const hParentGroup = findParentGroup(
			impl_rect_node(node), rg::Group { 0 }
		);
		state.groups.at(hParentGroup.id).hNodes.emplace_back(hNode);
	}
}

// true if no group field link forms a cycle; group membership must be current
static auto impl_group_links_validate(
	ImplGraph const & graph
) -> bool
{
	for (ImplLink const & link : graph.state.links) {
		if (link.groupB == 0) { continue; }
		if (impl_group_field_forms_cycle(graph, link.nodeA, link.groupB)) {
			return false;
		}
	}
	return true;
}

// validates the groups then recalculates them. If invalid, reverts the last
// recorded edit and returns false.
static auto impl_edit_resolve(
	ImplGraph & graph
) -> bool
{
	// -- do not resolve while in a transaction
	if (graph.transactionDepth > 0) { return true; }

	// -- revert the last recorded edit if a group is partially intersected
	auto const revert = [&](char const * const reason) {
		rg_error("edit reverted: %s", reason);
		graph.state = std::move(graph.undoStack.back());
		graph.undoStack.pop_back();
		graph.positionsDirty = true;
		graph.editReverted = true;
	};
	if (!impl_group_validate(graph.state)) {
		revert("partially intersects a group");
		return false;
	}

	// -- update group membership
	impl_group_recalculate(graph.state);

	// -- revert the last recorded edit if a group field link forms a cycle
	if (!impl_group_links_validate(graph)) {
		revert("links a node into a group field it depends on");
		return false;
	}
	return true;
}

// -----------------------------------------------------------------------------
// -- edits: group
// -----------------------------------------------------------------------------

auto rg::graph_group_create(
	rg::Graph const hGraph,
	std::string const & name,
	rg::Group const hParentGroup,
	int32_t const positionX,
	int32_t const positionY,
	int32_t const width,
	int32_t const height
) -> Group
{
	ImplGraph & graph = impl_graph(hGraph);
	impl_edit_record(graph);
	ImplGroup const group = {
		.id = graph.state.groups.rbegin()->first + 1,
		.name = name,
		.x = static_cast<float>(positionX),
		.y = static_cast<float>(positionY),
		.w = static_cast<float>(width),
		.h = static_cast<float>(height),
		.hParentGroup = hParentGroup,
	};
	auto const hGroup = rg::Group { .id = group.id };

	// -- add to parent group
	graph.state.groups.at(hParentGroup.id).hChildrenGroup.emplace_back(hGroup);
	// -- add to flat list
	graph.state.groups.emplace(hGroup.id, group);

	if (!impl_edit_resolve(graph)) {
		return rg::graph_group_default();
	}
	return hGroup;
}

auto rg::graph_group_default() -> rg::Group
{
	return { 0 };
}

auto rg::graph_group_destroy(
	rg::Graph const hGraph,
	rg::Group const hGroup
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (hGroup.id == 0 || !impl_group(graph, hGroup.id)) { return; }
	impl_edit_record(graph);
	// -- delete links into the group's fields
	std::erase_if(graph.state.links, [&](ImplLink const & link) {
		return link.groupB == hGroup.id;
	});
	// -- delete from flat list
	graph.state.groups.erase(hGroup.id);
	impl_edit_resolve(graph);
}

auto rg::graph_group_set_field_enabled(
	rg::Graph const hGraph,
	rg::Group const hGroup,
	uint32_t const field,
	bool const enabled
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImplGroup * const group = impl_group(graph, hGroup.id);
	if (!group || hGroup.id == 0) { return; }
	if (field >= sImplGlobalLayout.fields.size()) { return; }

	std::vector<uint32_t> & enabledFields = group->enabledFields;
	bool const wasEnabled = (
		std::find(enabledFields.begin(), enabledFields.end(), field)
		!= enabledFields.end()
	);
	if (wasEnabled == enabled) { return; }
	impl_edit_record(graph);

	// -- enable the field
	if (enabled) {
		enabledFields.emplace_back(field);
		return;
	}

	// -- disable the field and delete its links
	std::erase(enabledFields, field);
	std::erase_if(graph.state.links, [&](ImplLink const & link) {
		return link.groupB == hGroup.id && link.fieldB == field;
	});
}

auto rg::graph_group_set_name(
	rg::Graph const hGraph,
	rg::Group const hGroup,
	std::string const & name
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImplGroup * const group = impl_group(graph, hGroup.id);
	if (!group || group->name == name) { return; }
	impl_edit_record(graph);
	group->name = name;
}

// -----------------------------------------------------------------------------
// -- edits: nodes
// -----------------------------------------------------------------------------

auto rg::node_instance_create(
	Graph const hGraph,
	std::string const & uniqueName,
	rg::Group const hGroup,
	int32_t const positionX,
	int32_t const positionY,
	std::vector<rg::ParameterValue> const & parameterValues
) -> rg::NodeInstance
{
	ImplGraph & graph = impl_graph(hGraph);
	NodeType const type = node_type_find(uniqueName);
	rg_assert(type.id != 0, "node type not registered: %s", uniqueName.c_str());
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
			node.parameterValues.emplace_back(param.defaultValue);
		}
	}
	assert(node.parameterValues.size() == ci.parameters.size());

	impl_edit_record(graph);
	// add to the flat list
	graph.state.nodes[node.id] = node;
	// then add to the group
	auto const hNodeInstance = rg::NodeInstance { .id = node.id, .type = type, };
	graph.state.groups.at(hGroup.id).hNodes.emplace_back(hNodeInstance);

	if (!impl_edit_resolve(graph)) {
		return rg::NodeInstance { .id = 0 };
	}
	return hNodeInstance;
}

auto rg::node_instance_destroy(
	Graph const hGraph,
	NodeInstance const hNode
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!impl_node_find(graph, hNode.id)) { return; }
	impl_edit_record(graph);
	// -- delete links
	std::erase_if(graph.state.links, [&](ImplLink const & link) {
		return link.nodeA == hNode.id || link.nodeB == hNode.id;
	});
	// -- delete from flat list
	graph.state.nodes.erase(hNode.id);
	// -- delete from group
	for (auto & [groupId, group] : graph.state.groups) {
		std::erase_if(group.hNodes, [&](NodeInstance const & node) {
			return node.id == hNode.id;
		});
	}
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
	impl_edit_resolve(graph);
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
	return (
		impl_link_check(
			impl_graph(hGraph), hNodeA.id, pinA, hNodeB.id, pinB, false
		)
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
	graph.state.links.emplace_back(link);
	return { link.id };
}

auto rg::group_field_can_link(
	Graph const hGraph,
	NodeInstance const hNodeA,
	uint32_t const pinA,
	Group const hGroup,
	uint32_t const field
) -> bool
{
	return impl_group_field_link_check(
		impl_graph(hGraph), hNodeA.id, pinA, hGroup.id, field, false
	);
}

auto rg::group_field_link(
	Graph const hGraph,
	NodeInstance const hNodeA,
	uint32_t const pinA,
	Group const hGroup,
	uint32_t const field
) -> rg::Link
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!impl_group_field_link_check(
		graph, hNodeA.id, pinA, hGroup.id, field, false
	)) {
		return { 0 };
	}
	impl_edit_record(graph);
	ImplLink const link = {
		.id = graph.nextLinkId++,
		.nodeA = hNodeA.id,
		.pinA = pinA,
		.nodeB = 0,
		.pinB = 0,
		.groupB = hGroup.id,
		.fieldB = field,
	};
	graph.state.links.emplace_back(link);
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
	if (graph.transactionDepth == 0 && graph.transactionRecorded) {
		impl_edit_resolve(graph);
	}
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
	graph.redoStack.emplace_back(std::move(graph.state));
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
	graph.undoStack.emplace_back(std::move(graph.state));
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
	assert(nodeId < (1u << 14) && index < (1u << 15));
	uint32_t const dirBit = dir == rg::PinDir::out ? 1u : 0u;
	return static_cast<int>((nodeId << 16) | (dirBit << 15) | index);
}

// bit 30 marks a group field pin, which node pins never set
constexpr uint32_t kUiGroupPinBit = 1u << 30;

static auto impl_ui_group_pin_id(
	uint32_t const groupId,
	uint32_t const field
) -> int
{
	assert(groupId < (1u << 14) && field < (1u << 15));
	return static_cast<int>(kUiGroupPinBit | (groupId << 16) | field);
}

namespace
{
	struct ImplUiPin {
		// node id, or group id if isGroupField
		uint32_t nodeId;
		rg::PinDir dir;
		// pin index, or field index if isGroupField
		uint32_t index;
		bool isGroupField;
	};
}

static auto impl_ui_pin_decode(
	int const pinId
) -> ImplUiPin
{
	uint32_t const v = static_cast<uint32_t>(pinId);
	if (v & kUiGroupPinBit) {
		return {
			.nodeId = (v >> 16) & 0x3FFFu,
			.dir = rg::PinDir::in,
			.index = v & 0x7FFFu,
			.isGroupField = true,
		};
	}
	return {
		.nodeId = v >> 16,
		.dir = (v >> 15) & 1u ? rg::PinDir::out : rg::PinDir::in,
		.index = v & 0x7FFFu,
		.isGroupField = false,
	};
}

// imnodes node ids at or above this belong to the node holding a group's pins
// The group id is the node id minus this value
constexpr uint32_t kUiGroupNodeIdBase = 1u << 30;

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
		ImU32 error; // bright red; illegal layout
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
	.tertiary = IM_COL32(95, 175, 55, 255),
	.rule = IM_COL32(95, 10, 10, 255),
	.error = IM_COL32(220, 55, 45, 255),
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

constexpr float kUiHighlightLinkThickness = 3.5f;
constexpr float kUiGroupHeaderHeight = 20.0f;
constexpr float kUiGroupHandleSize = 12.0f;
constexpr float kUiGroupMinWidth = kGroupPinColumnWidth + 80.0f;
constexpr float kUiGroupMinHeight = 60.0f;
// grid distance from the group's top left corner to its pin node
constexpr float kUiGroupPinOffset = 8.0f;

// -----------------------------------------------------------------------------
// -- ui impl: illegal layout
// -----------------------------------------------------------------------------

namespace
{
	struct ImplUiOffenders {
		std::vector<uint32_t> nodes;
		std::vector<uint32_t> groups;
	};
}

// finds the nodes and groups partially intersecting a group, using the
// positions currently shown
static auto impl_ui_find_offenders(
	ImplGraph const & graph
) -> ImplUiOffenders
{
	ImplUiOffenders offenders;
	ImplGroupValidateFni const fni = { &offenders.nodes, &offenders.groups };

	// -- validate the stored positions
	if (
		!ImGui::IsMouseDown(ImGuiMouseButton_Left)
		|| graph.ui.groupDragRecorded
	) {
		impl_group_validate(graph.state, fni);
		return offenders;
	}

	// -- validate the positions imnodes is moving nodes to
	ImplGraphState state = graph.state;
	for (auto & [nodeId, node] : state.nodes) {
		ImVec2 const pos = ImNodes::GetNodeGridSpacePos((int)nodeId);
		node.posX = (int32_t)std::lround(pos.x);
		node.posY = (int32_t)std::lround(pos.y);
	}
	impl_group_validate(state, fni);
	return offenders;
}

namespace
{
	struct ImplUiGroupInput {
		// -- group interacted with this frame, 0 if none
		uint32_t pressedGroup = 0;
		uint32_t doubleClickedGroup = 0;
		uint32_t rightClickedGroup = 0;

		// -- pressed a node that was already selected
		bool pressedSelectedNode = false;

		// -- screen position of grid 0,0
		ImVec2 gridScreenPos = {};
	};
}

namespace
{
	struct ImplUiHighlight {
		std::vector<uint32_t> nodes;
		std::vector<uint32_t> links;
	};
}

// finds the links of the selected nodes and the nodes those links connect to
static auto impl_ui_find_highlight(
	ImplGraph const & graph
) -> ImplUiHighlight
{
	auto const isSelected = [](uint32_t const nodeId) -> bool {
		// -- find selected nodes
		std::vector<int32_t> selected((size_t)ImNodes::NumSelectedNodes());
		if (!selected.empty()) { ImNodes::GetSelectedNodes(selected.data()); }

		// -- check if the node is among them
		auto const it = (
			std::find(selected.begin(), selected.end(), (int32_t)nodeId)
		);
		return it != selected.end();
	};

	// -- find links touching a selected node and the nodes at their ends
	ImplUiHighlight highlight;
	for (ImplLink const & link : graph.state.links) {
		bool const selectedA = isSelected(link.nodeA);
		bool const selectedB = isSelected(link.nodeB);
		if (!selectedA && !selectedB) { continue; }
		highlight.links.emplace_back(link.id);
		if (!selectedA) { highlight.nodes.emplace_back(link.nodeA); }
		if (!selectedB && link.groupB == 0) {
			highlight.nodes.emplace_back(link.nodeB);
		}
	}
	return highlight;
}

// stores the size imnodes measured for each node
static auto impl_ui_update_node_sizes(
	ImplGraph & graph
) -> void
{
	for (auto & [nodeId, node] : graph.state.nodes) {
		ImVec2 const size = ImNodes::GetNodeDimensions((int)nodeId);
		node.width = size.x;
		node.height = size.y;
	}
}

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

static auto impl_ui_draw_node(
	ImplGraph const & graph,
	ImplUiOffenders const & offenders,
	ImplUiHighlight const & highlight,
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

	// -- measure
	float const gap = impl_ui_text_width("    ");
	float width = impl_ui_text_width(name) + gap + impl_ui_text_width(queue);
	for (
		size_t row = 0; row < numRows; ++row
	) {
		rg::PinDesc const * const pinIn = (
			row < numIn ? &nodeType.pinIn[row] : nullptr
		);
		rg::PinDesc const * const pinOut = (
			row < numOut ? &nodeType.pinOut[row] : nullptr
		);
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
	bool const offending = (
		std::find(offenders.nodes.begin(), offenders.nodes.end(), node.id)
		!= offenders.nodes.end()
	);
	bool const connected = (
		std::find(highlight.nodes.begin(), highlight.nodes.end(), node.id)
		!= highlight.nodes.end()
	);
	bool const outlined = selected || offending || connected;
	if (outlined) {
		ImU32 outlineColor = kUiPalette.secondary;
		if (selected) { outlineColor = kUiPalette.accent; }
		if (offending) { outlineColor = kUiPalette.error; }
		ImNodes::PushColorStyle(ImNodesCol_NodeOutline, outlineColor);
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
		rg::PinDesc const * const pinIn = (
			row < numIn ? &nodeType.pinIn[row] : nullptr
		);
		rg::PinDesc const * const pinOut = (
			row < numOut ? &nodeType.pinOut[row] : nullptr
		);

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
		char const * const detailIn = (
			pinIn ? impl_ui_pin_detail(*pinIn) : nullptr
		);
		char const * const detailOut = (
			pinOut ? impl_ui_pin_detail(*pinOut) : nullptr
		);
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
	if (outlined) {
		ImNodes::PopColorStyle();
	}
}

// draws the transparent node holding a pin for each enabled field of the group
static auto impl_ui_draw_group_pins(
	ImplGroup const & group
) -> void
{
	if (group.enabledFields.empty()) { return; }
	int const nodeId = (int)(kUiGroupNodeIdBase + group.id);
	ImplUiKindStyle const style = impl_ui_kind_style(rg::PinKind::buffer);

	// -- hide the node background and outline
	ImU32 const clear = IM_COL32(0, 0, 0, 0);
	ImNodes::PushColorStyle(ImNodesCol_NodeBackground, clear);
	ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, clear);
	ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, clear);
	ImNodes::PushColorStyle(ImNodesCol_NodeOutline, clear);
	ImNodes::BeginNode(nodeId);

	// -- place the node below the group header
	ImNodes::SetNodeDraggable(nodeId, false);
	ImNodes::SetNodeGridSpacePos(
		nodeId,
		ImVec2(
			group.x + kUiGroupPinOffset,
			group.y + kUiGroupHeaderHeight + kUiGroupPinOffset
		)
	);

	// -- pins
	for (uint32_t const field : group.enabledFields) {
		rg::GlobalFieldDesc const & desc = sImplGlobalLayout.fields[field];
		ImNodes::PushColorStyle(ImNodesCol_Pin, style.color);
		ImNodes::PushColorStyle(ImNodesCol_PinHovered, style.hovered);
		ImNodes::BeginInputAttribute(
			impl_ui_group_pin_id(group.id, field), style.shape
		);
		ImGui::TextUnformatted(desc.name.c_str());
		ImNodes::EndInputAttribute();
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
	}

	ImNodes::EndNode();
	for (int32_t i = 0; i < 4; ++i) {
		ImNodes::PopColorStyle();
	}
}

static auto impl_ui_group_is_selected(
	ImplGraph const & graph,
	rg::Group const hGroup
) -> bool
{
	std::vector<uint32_t> const & groups = graph.ui.selected.groups;
	return std::find(groups.begin(), groups.end(), hGroup.id) != groups.end();
}

// apply selection to the specified group
static auto impl_ui_group_select(
	ImplGraph & graph,
	rg::Group const hGroup
) -> void
{
	ImGuiIO const & io = ImGui::GetIO();
	bool const modifier = io.KeyShift || io.KeyCtrl || io.KeySuper;
	bool const selected = impl_ui_group_is_selected(graph, hGroup);
	std::vector<uint32_t> & groups = graph.ui.selected.groups;

	// -- toggle
	if (modifier) {
		if (selected) {
			std::erase(groups, hGroup.id);
		} else {
			groups.emplace_back(hGroup.id);
		}
		return;
	}

	// -- replace the selection, unless the group is already in it
	if (!selected) {
		groups.clear();
		groups.emplace_back(hGroup.id);
		ImNodes::ClearNodeSelection();
	}
}

// integer grid pixels the mouse moved since the previous call of this function
// any fractional component will be carried into the next call
static auto impl_ui_group_drag_delta(
	ImplGraph & graph,
	ImplUiGroupInput const & input
) -> ImVec2
{
	// -- include the camera moving under the mouse
	ImVec2 & posDelta = graph.ui.groupDragPosDelta;
	ImVec2 const & lastGridScreenPos = graph.ui.groupDragGridScreenPos;
	posDelta.x += (
		ImGui::GetIO().MouseDelta.x
		- (input.gridScreenPos.x - lastGridScreenPos.x)
	);
	posDelta.y += (
		ImGui::GetIO().MouseDelta.y
		- (input.gridScreenPos.y - lastGridScreenPos.y)
	);
	graph.ui.groupDragGridScreenPos = input.gridScreenPos;
	ImVec2 const delta = { std::trunc(posDelta.x), std::trunc(posDelta.y) };
	posDelta.x -= delta.x;
	posDelta.y -= delta.y;
	return delta;
}

// record an undo step if the group drag has not recorded one yet
static auto impl_ui_group_drag_record(
	ImplGraph & graph
) -> void
{
	if (graph.ui.groupDragRecorded) { return; }
	impl_edit_record(graph);
	graph.ui.groupDragRecorded = true;
}

// translates the selected groups and nodes
static auto impl_ui_selection_translate(
	ImplGraph & graph,
	ImVec2 const delta
) -> void
{
	// true if any ancestor of the group is selected
	auto const hasSelectedAncestor = [&](rg::Group const hGroup) -> bool {
		rg::Group hAncestor = graph.state.groups.at(hGroup.id).hParentGroup;
		while (hAncestor.id != 0) {
			if (impl_ui_group_is_selected(graph, hAncestor)) { return true; }
			hAncestor = graph.state.groups.at(hAncestor.id).hParentGroup;
		}
		return false;
	};

	// -- translate selected groups, which moves their children
	for (uint32_t const groupId : graph.ui.selected.groups) {
		ImplGroup * const group = impl_group(graph, groupId);
		if (!group || hasSelectedAncestor({ groupId })) { continue; }
		impl_group_translate(graph, *group, delta.x, delta.y);
	}

	// -- translate selected nodes that no selected group moves
	for (auto const & [groupId, group] : graph.state.groups) {
		bool const moved = (
			impl_ui_group_is_selected(graph, { groupId })
			|| hasSelectedAncestor({ groupId })
		);
		if (moved) { continue; }
		for (rg::NodeInstance const hNode : group.hNodes) {
			if (!ImNodes::IsNodeSelected((int)hNode.id)) { continue; }
			ImplNodeInstance & node = graph.state.nodes.at(hNode.id);
			node.posX += (int32_t)delta.x;
			node.posY += (int32_t)delta.y;
		}
	}
}

static auto impl_ui_draw_group(
	ImplGraph & graph,
	ImplUiOffenders const & offenders,
	ImplUiHighlight const & highlight,
	ImplUiGroupInput & input,
	ImplGroup & group
) -> void
{
	ImVec2 const origin = input.gridScreenPos;
	ImDrawList & drawList = *ImGui::GetWindowDrawList();

	// -- draw on the channel beneath the links
	drawList.ChannelsSetCurrent(0);

	// -- draw group container
	if (group.id != 0) {
		ImGui::PushID(group.id);
		ImVec2 const oriUl = { origin.x + group.x, origin.y + group.y };
		ImVec2 const oriHeaderLr = {
			origin.x + group.x + group.w,
			origin.y + group.y + kUiGroupHeaderHeight
		};
		ImVec2 const oriLr = {
			origin.x + group.x + group.w,
			origin.y + group.y + group.h
		};

		// container
		drawList.AddRectFilled(oriUl, oriLr, kUiPalette.surface, 4.0f);
		// header
		drawList.AddRectFilled(oriUl, oriHeaderLr, kUiPalette.tertiary);
		// pin column divider
		drawList.AddLine(
			ImVec2 { oriUl.x + kGroupPinColumnWidth, oriHeaderLr.y },
			ImVec2 { oriUl.x + kGroupPinColumnWidth, oriLr.y },
			kUiPalette.dim
		);
		// header text
		drawList.AddText(
			ImVec2 { oriUl.x + 4.0f, oriUl.y + 2.0f },
			kUiPalette.text,
			group.name.c_str()
		);

		// -- outline if selected or illegal
		bool const selected = impl_ui_group_is_selected(graph, { group.id });
		std::vector<uint32_t> const & offendingGroups = offenders.groups;
		bool const offending = (
			std::find(offendingGroups.begin(), offendingGroups.end(), group.id)
			!= offendingGroups.end()
		);
		if (selected) {
			drawList.AddRect(oriUl, oriLr, kUiPalette.accent, 4.0f, 0, 2.0f);
		}
		if (offending) {
			drawList.AddRect(oriUl, oriLr, kUiPalette.error, 4.0f, 0, 2.0f);
		}

		// -- draw an invisible button for imnode
		ImGui::SetCursorScreenPos(oriUl);
		ImGui::InvisibleButton("bar", ImVec2(group.w, kUiGroupHeaderHeight));
		bool const headerHeld = ImGui::IsItemActive();
		if (ImGui::IsItemActivated()) {
			input.pressedGroup = group.id;
			graph.ui.groupDragGridScreenPos = input.gridScreenPos;
			impl_ui_group_select(graph, { group.id });
		}
		if (
			ImGui::IsItemHovered()
			&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
		) {
			input.doubleClickedGroup = group.id;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			input.rightClickedGroup = group.id;
		}

		// -- drag the group, moving all children
		if (headerHeld) {
			ImVec2 const delta = impl_ui_group_drag_delta(graph, input);
			if (delta.x != 0.0f || delta.y != 0.0f) {
				impl_ui_group_drag_record(graph);
				graph.positionsDirty = true;
				if (impl_ui_group_is_selected(graph, { group.id })) {
					impl_ui_selection_translate(graph, delta);
				} else {
					impl_group_translate(graph, group, delta.x, delta.y);
				}
			}
		}

		// -- draw a handle in the bottom right corner to resize
		drawList.AddTriangleFilled(
			ImVec2 { oriLr.x, oriLr.y - kUiGroupHandleSize },
			oriLr,
			ImVec2 { oriLr.x - kUiGroupHandleSize, oriLr.y },
			kUiPalette.dim
		);
		ImGui::SetCursorScreenPos(
			ImVec2 {
				oriLr.x - kUiGroupHandleSize,
				oriLr.y - kUiGroupHandleSize
			}
		);
		ImGui::InvisibleButton(
			"resize", ImVec2(kUiGroupHandleSize, kUiGroupHandleSize)
		);
		if (ImGui::IsItemActivated()) {
			input.pressedGroup = group.id;
			graph.ui.groupDragGridScreenPos = input.gridScreenPos;
		}
		if (ImGui::IsItemActive()) {
			ImVec2 const delta = impl_ui_group_drag_delta(graph, input);
			if (delta.x != 0.0f || delta.y != 0.0f) {
				impl_ui_group_drag_record(graph);
				group.w = std::max(kUiGroupMinWidth, group.w + delta.x);
				group.h = std::max(kUiGroupMinHeight, group.h + delta.y);
			}
		}

		ImGui::PopID();
	}

	// -- draw the pins of the enabled fields
	if (group.id != 0) {
		impl_ui_draw_group_pins(group);
	}

	// -- draw child groups
	for (auto const & childGroup : group.hChildrenGroup) {
		impl_ui_draw_group(
			graph,
			offenders,
			highlight,
			input,
			graph.state.groups.at(childGroup.id)
		);
	}

	// -- draw nodes
	for (rg::NodeInstance const & hNode : group.hNodes) {
		ImplNodeInstance const & node = graph.state.nodes.at(hNode.id);
		impl_ui_draw_node(graph, offenders, highlight, node);
	}
}

static auto impl_ui_draw_rendergraph(
	ImplGraph & graph,
	ImplUiOffenders const & offenders,
	ImplUiHighlight const & highlight,
	ImplUiGroupInput & groupInput
) -> void
{
	// -- find grid 0,0 before drawing moves the cursor
	groupInput.gridScreenPos = {
		ImGui::GetCursorScreenPos().x + ImNodes::EditorContextGetPanning().x,
		ImGui::GetCursorScreenPos().y + ImNodes::EditorContextGetPanning().y,
	};

	// -- render group/nodes
	impl_ui_draw_group(
		graph, offenders, highlight, groupInput, graph.state.groups.at(0)
	);

	// -- render links; colored by the resource kind the output produces
	for (ImplLink const & link : graph.state.links) {
		ImplNodeInstance const & nodeA = graph.state.nodes.at(link.nodeA);
		rg::PinDesc const & pinA = impl_node_type(nodeA.type).pinOut[link.pinA];
		ImplUiKindStyle const style = impl_ui_kind_style(pinA.kind);

		// -- fade links into previous-frame inputs; group fields never are
		bool const temporal = (
			link.groupB == 0
			&& impl_node_type(graph.state.nodes.at(link.nodeB).type)
				.pinIn[link.pinB].pinIn.temporal
				== rg::PinResourceTemporal::previousFrame
		);
		float const alpha = temporal ? kUiTemporalLinkAlpha : 1.0f;
		int const pinBId = (
			link.groupB != 0
			? impl_ui_group_pin_id(link.groupB, link.fieldB)
			: impl_ui_pin_id(link.nodeB, rg::PinDir::in, link.pinB)
		);
		bool const highlighted = (
			std::find(highlight.links.begin(), highlight.links.end(), link.id)
			!= highlight.links.end()
		);
		ImU32 const linkColor = (
			highlighted
			? style.selected
			: impl_ui_color_alpha(style.color, alpha)
		);
		if (highlighted) {
			ImNodes::PushStyleVar(
				ImNodesStyleVar_LinkThickness, kUiHighlightLinkThickness
			);
		}
		ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
		ImNodes::PushColorStyle(
			ImNodesCol_LinkHovered, impl_ui_color_alpha(style.hovered, alpha)
		);
		ImNodes::PushColorStyle(ImNodesCol_LinkSelected, style.selected);
		ImNodes::Link(
			static_cast<int>(link.id),
			impl_ui_pin_id(link.nodeA, rg::PinDir::out, link.pinA),
			pinBId
		);
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
		if (highlighted) {
			ImNodes::PopStyleVar();
		}
	}
}

// -----------------------------------------------------------------------------
// -- ui impl: properties popup
// -----------------------------------------------------------------------------

static auto impl_ui_group_open_rename(
	ImplGraph & graph,
	rg::Group const hGroup
) -> void
{
	ImplGroup const * const group = impl_group(graph, hGroup.id);
	if (!group) { return; }
	graph.ui.popup.renameGroup = hGroup.id;
	snprintf(
		graph.ui.nameBuffer,
		sizeof(graph.ui.nameBuffer),
		"%s", group->name.c_str()
	);
	ImGui::OpenPopup("rg_group_rename");
}

static auto impl_ui_draw_group_rename(
	rg::Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!ImGui::BeginPopup("rg_group_rename")) { return; }

	// -- close if the group no longer exists
	rg::Group const hGroup = { graph.ui.popup.renameGroup };
	if (!impl_group(graph, hGroup.id)) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	// -- name field
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(240.0f);
	bool const entered = ImGui::InputText(
		"name",
		graph.ui.nameBuffer,
		sizeof(graph.ui.nameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue
	);

	// -- commit rename once editing finishes
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		rg::graph_group_set_name(hGraph, hGroup, graph.ui.nameBuffer);
	}

	// -- close on enter
	if (entered) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

static auto impl_ui_node_open_properties(
	ImplGraph & graph,
	uint32_t const nodeId
) -> void
{
	ImplNodeInstance const * const node = impl_node_find(graph, nodeId);
	if (!node) { return; }
	graph.ui.propertiesNode = nodeId;
	snprintf(
		graph.ui.nameBuffer,
		sizeof(graph.ui.nameBuffer),
		"%s", node->name.c_str()
	);
	ImGui::OpenPopup("rg_node_properties");
}

static auto impl_ui_draw_properties(
	rg::Graph const hGraph
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (!ImGui::BeginPopup("rg_node_properties")) { return; }

	// the node can vanish under the popup, eg via undo
	ImplNodeInstance const * const node = (
		impl_node_find(graph, graph.ui.propertiesNode)
	);
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
		"name",
		nodeType.name.c_str(),
		graph.ui.nameBuffer,
		sizeof(graph.ui.nameBuffer)
	);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		rg::node_instance_set_name(hGraph, hNode, graph.ui.nameBuffer);
	}

	ImGui::Text(
		"type   %s (%s)",
		nodeType.name.c_str(), nodeType.uniqueName.c_str()
	);
	ImGui::Text("queue  %s", impl_to_string(nodeType.queue));
	ImGui::Text("id     %u", node->id);

	ImGuiTableFlags const tableFlags = (
		ImGuiTableFlags_Borders
		| ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
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
				ImGui::Text(
					"%s%s",
					pin.name.c_str(), pin.pinIn.required ? "" : " (optional)"
				);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.kind));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.pinIn.format));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.pinIn.accessMode));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(previous ? "previous" : "current");
				ImGui::TableNextColumn();
				ImGui::Text(
					"%gx%g, %u mips",
					pin.scaleWidth, pin.scaleHeight, pin.mipLevels
				);
				ImGui::TableNextColumn();
				ImplLink const * const link = (
					impl_link_find_into(graph, node->id, i)
				);
				if (link) {
					ImplNodeInstance const & from = (
						graph.state.nodes.at(link->nodeA)
					);
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
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(pin.name.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.kind));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.pinOut.format));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(impl_to_string(pin.pinOut.accessMode));
				ImGui::TableNextColumn();
				ImGui::Text(
					"%gx%g, %u mips",
					pin.scaleWidth, pin.scaleHeight, pin.mipLevels
				);
				ImGui::TableNextColumn();
				ImGui::Text("%d", static_cast<int>(numLinks));
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

static auto impl_ui_handle_context_menu(
	rg::Graph const hGraph,
	ImplUiGroupInput const & groupInput,
	bool const editorHovered
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		int linkId = 0;
		int nodeId = 0;
		if (ImNodes::IsLinkHovered(&linkId)) {
			graph.ui.contextLink = static_cast<uint32_t>(linkId);
			ImGui::OpenPopup("rg_link_menu");
		} else if (
			ImNodes::IsNodeHovered(&nodeId)
			&& (uint32_t)nodeId >= kUiGroupNodeIdBase
		) {
			graph.ui.popup.contextGroup = (uint32_t)nodeId - kUiGroupNodeIdBase;
			ImGui::OpenPopup("rg_group_menu");
		} else if (ImNodes::IsNodeHovered(&nodeId)) {
			graph.ui.contextNode = static_cast<uint32_t>(nodeId);
			ImGui::OpenPopup("rg_node_menu");
		} else if (groupInput.rightClickedGroup != 0) {
			graph.ui.popup.contextGroup = groupInput.rightClickedGroup;
			ImGui::OpenPopup("rg_group_menu");
		} else if (editorHovered) {
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
	if (openProperties) {
		impl_ui_node_open_properties(graph, graph.ui.contextNode);
	}

	if (ImGui::BeginPopup("rg_group_menu")) {
		rg::Group const hContextGroup = { graph.ui.popup.contextGroup };
		ImplGroup const * const contextGroup = (
			impl_group(graph, hContextGroup.id)
		);
		if (contextGroup && ImGui::BeginMenu("override fields")) {
			uint32_t const numFields = (
				(uint32_t)sImplGlobalLayout.fields.size()
			);
			for (uint32_t field = 0; field < numFields; ++field) {
				std::vector<uint32_t> const & enabledFields = (
					contextGroup->enabledFields
				);
				bool const enabled = (
					std::find(enabledFields.begin(), enabledFields.end(), field)
					!= enabledFields.end()
				);
				char const * const name = (
					sImplGlobalLayout.fields[field].name.c_str()
				);
				if (ImGui::MenuItem(name, nullptr, enabled)) {
					rg::graph_group_set_field_enabled(
						hGraph, hContextGroup, field, !enabled
					);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("delete group")) {
			rg::Group const hGroup = { graph.ui.popup.contextGroup };
			rg::graph_group_destroy(hGraph, hGroup);
			std::erase(graph.ui.selected.groups, hGroup.id);
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("rg_context_menu")) {
		if (ImGui::MenuItem("add group")) {
			ImVec2 const mouse = ImGui::GetMousePosOnOpeningCurrentPopup();
			rg::graph_group_create(
				hGraph,
				"Group",
				rg::graph_group_default(),
				(int32_t)std::lround(mouse.x - groupInput.gridScreenPos.x),
				(int32_t)std::lround(mouse.y - groupInput.gridScreenPos.y),
				260,
				180
			);
		}
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
}

// Interaction queries; imnodes answers node/link queries only after
// EndNodeEditor, but editor hover only inside it, so that's passed in.
static auto impl_ui_handle_input(
	rg::Graph const hGraph,
	ImplUiGroupInput const & groupInput,
	bool const editorHovered
) -> void
{
	ImplGraph & graph = impl_graph(hGraph);
	ImGuiIO const & io = ImGui::GetIO();

	// -- created links
	{
		int startedId = 0;
		int endedId = 0;
		if (ImNodes::IsLinkCreated(&startedId, &endedId)) {
			ImplUiPin a = impl_ui_pin_decode(startedId);
			ImplUiPin b = impl_ui_pin_decode(endedId);
			if (a.dir == rg::PinDir::in) { std::swap(a, b); }
			bool const toGroupField = b.isGroupField;

			// -- link into a group field, replacing the field's old link
			bool const validGroupField = (
				toGroupField && !a.isGroupField
				&& a.dir == rg::PinDir::out
				&& impl_group_field_link_check(
					graph, a.nodeId, a.index, b.nodeId, b.index, true
				)
			);
			if (validGroupField) {
				rg::transaction_begin(hGraph);
				ImplLink const * const old = (
					impl_link_find_into_group(graph, b.nodeId, b.index)
				);
				if (old) {
					rg::link_destroy(hGraph, { old->id });
				}
				rg::group_field_link(
					hGraph, { a.nodeId }, a.index, { b.nodeId }, b.index
				);
				rg::transaction_end(hGraph);
			}

			// -- link into a node pin
			bool const valid = (
				!toGroupField && !a.isGroupField
				&& a.dir == rg::PinDir::out && b.dir == rg::PinDir::in
				&& impl_link_check(
					graph, a.nodeId, a.index, b.nodeId, b.index, true
				)
			);
			if (valid) {
				rg::transaction_begin(hGraph);
				ImplLink const * const old = (
					impl_link_find_into(graph, b.nodeId, b.index)
				);
				if (old) {
					rg::link_destroy(hGraph, { old->id });
				}
				rg::node_instance_link(
					hGraph, { a.nodeId }, a.index, { b.nodeId }, b.index
				);
				rg::transaction_end(hGraph);
			}
		}
	}

	// -- store camera panning on press
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		graph.ui.pressPanning = ImNodes::EditorContextGetPanning();
	}

	// -- node properties on double click
	{
		int nodeId = 0;
		int pinId = 0;
		bool const onNode = (
			ImNodes::IsNodeHovered(&nodeId) && !ImNodes::IsPinHovered(&pinId)
		);
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && onNode) {
			graph.ui.propertiesNodePending = static_cast<uint32_t>(nodeId);
		}

		// -- open once the mouse is released
		if (
			ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& graph.ui.propertiesNodePending != 0
		) {
			impl_ui_node_open_properties(graph, graph.ui.propertiesNodePending);
			graph.ui.propertiesNodePending = 0;
		}
	}

	// -- node translation
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		rg::transaction_begin(hGraph);
		graph.transactionRecorded = graph.ui.groupDragRecorded;
		graph.ui.groupDragRecorded = false;
		graph.ui.groupDragPosDelta = {};
		for (auto const & [id, node] : graph.state.nodes) {
			ImVec2 const pos = ImNodes::GetNodeGridSpacePos(static_cast<int>(id));
			rg::node_instance_set_position(
				hGraph,
				{ id },
				static_cast<int32_t>(std::lround(pos.x)),
				static_cast<int32_t>(std::lround(pos.y))
			);
		}
		// setting positions only mirrors imnodes; nothing to push back
		graph.positionsDirty = false;
		graph.editReverted = false;
		rg::transaction_end(hGraph);

		// -- restore camera if the edit was reverted
		if (graph.editReverted) {
			ImNodes::EditorContextResetPanning(graph.ui.pressPanning);
		}
	}

	// -- clear group selection when clicking away from groups
	{
		bool const modifier = io.KeyShift || io.KeyCtrl || io.KeySuper;
		if (
			ImGui::IsMouseClicked(ImGuiMouseButton_Left)
			&& editorHovered
			&& !modifier
			&& groupInput.pressedGroup == 0
			&& !groupInput.pressedSelectedNode
		) {
			graph.ui.selected.groups.clear();
		}
	}

	// -- start box selection when pressing on empty canvas
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		int hoveredId = 0;
		bool const onElement = (
			ImNodes::IsNodeHovered(&hoveredId)
			|| ImNodes::IsPinHovered(&hoveredId)
			|| ImNodes::IsLinkHovered(&hoveredId)
		);
		graph.ui.selected.boxActive = (
			editorHovered && !onElement && groupInput.pressedGroup == 0
		);
		graph.ui.selected.boxBaseGroups = graph.ui.selected.groups;
		graph.ui.selected.boxStartGridPos = {
			io.MouseClickedPos[0].x - groupInput.gridScreenPos.x,
			io.MouseClickedPos[0].y - groupInput.gridScreenPos.y,
		};
	}

	// -- select the groups whose header the box touches
	if (
		graph.ui.selected.boxActive
		&& ImGui::IsMouseDown(ImGuiMouseButton_Left)
	) {
		ImVec2 const boxA = {
			graph.ui.selected.boxStartGridPos.x + groupInput.gridScreenPos.x,
			graph.ui.selected.boxStartGridPos.y + groupInput.gridScreenPos.y,
		};
		ImVec2 const boxB = io.MousePos;
		ImVec2 const boxMin = {
			std::min(boxA.x, boxB.x), std::min(boxA.y, boxB.y)
		};
		ImVec2 const boxMax = {
			std::max(boxA.x, boxB.x), std::max(boxA.y, boxB.y)
		};
		graph.ui.selected.groups = graph.ui.selected.boxBaseGroups;
		for (auto const & [groupId, group] : graph.state.groups) {
			if (groupId == 0) { continue; }
			ImVec2 const headerMin = {
				groupInput.gridScreenPos.x + group.x,
				groupInput.gridScreenPos.y + group.y,
			};
			ImVec2 const headerMax = {
				headerMin.x + group.w,
				headerMin.y + kUiGroupHeaderHeight,
			};
			bool const touched = (
				boxMin.x < headerMax.x && headerMin.x < boxMax.x
				&& boxMin.y < headerMax.y && headerMin.y < boxMax.y
			);
			if (touched && !impl_ui_group_is_selected(graph, { groupId })) {
				graph.ui.selected.groups.emplace_back(groupId);
			}
		}
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		graph.ui.selected.boxActive = false;
	}

	// -- move selected groups along with selected nodes dragged by imnodes
	if (
		!graph.ui.selected.groups.empty()
		&& ImGui::IsMouseDown(ImGuiMouseButton_Left)
	) {
		std::vector<int> nodes((size_t)ImNodes::NumSelectedNodes());
		if (!nodes.empty()) { ImNodes::GetSelectedNodes(nodes.data()); }

		// -- find how far imnodes moved a node since the last frame
		ImVec2 delta = {};
		for (int const nodeId : nodes) {
			if ((uint32_t)nodeId >= kUiGroupNodeIdBase) { continue; }
			ImVec2 const pos = ImNodes::GetNodeGridSpacePos(nodeId);
			ImplNodeInstance const & node = graph.state.nodes.at(nodeId);
			delta = {
				(float)std::lround(pos.x) - (float)node.posX,
				(float)std::lround(pos.y) - (float)node.posY,
			};
			if (delta.x != 0.0f || delta.y != 0.0f) { break; }
		}
		if (delta.x != 0.0f || delta.y != 0.0f) {
			impl_ui_group_drag_record(graph);
			impl_ui_selection_translate(graph, delta);

			// -- push positions now, as the release below reads them
			for (auto const & [nodeId, node] : graph.state.nodes) {
				ImNodes::SetNodeGridSpacePos(
					(int)nodeId, ImVec2((float)node.posX, (float)node.posY)
				);
			}
		}
	}

	// -- rename group on double click
	if (groupInput.doubleClickedGroup != 0) {
		impl_ui_group_open_rename(graph, { groupInput.doubleClickedGroup });
	}

	// -- selection deleted
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
		for (uint32_t const groupId : graph.ui.selected.groups) {
			rg::graph_group_destroy(hGraph, { groupId });
		}
		rg::transaction_end(hGraph);
		graph.ui.selected.groups.clear();
		ImNodes::ClearLinkSelection();
		ImNodes::ClearNodeSelection();
	}

	// -- undo/redo
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
			graph.ui.selected.groups.clear();
			ImNodes::ClearLinkSelection();
			ImNodes::ClearNodeSelection();
		}
	}

	// -- WASD panning
	{
		bool const focused = (
			ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
		);
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

	// -- right click menus
	impl_ui_handle_context_menu(hGraph, groupInput, editorHovered);

	// -- draw properties
	impl_ui_draw_properties(hGraph);
	impl_ui_draw_group_rename(hGraph);
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

	ImplUiOffenders const offenders = impl_ui_find_offenders(graph);
	ImplUiHighlight const highlight = impl_ui_find_highlight(graph);

	// -- find if a selected node is pressed, before imnodes updates selection
	ImplUiGroupInput groupInput;
	int hoveredNodeId = 0;
	groupInput.pressedSelectedNode = (
		ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& ImNodes::IsNodeHovered(&hoveredNodeId)
		&& ImNodes::IsNodeSelected(hoveredNodeId)
	);

	impl_ui_theme_push();
	ImNodes::BeginNodeEditor();
	impl_ui_draw_rendergraph(graph, offenders, highlight, groupInput);
	graph.positionsDirty = false;

	// -- minimap; imnodes needs it after all nodes and links
	ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);

	// checks the current window, so it must be asked while in the canvas
	bool const editorHovered = ImNodes::IsEditorHovered();

	ImNodes::EndNodeEditor();
	impl_ui_theme_pop();
	impl_ui_update_node_sizes(graph);

	impl_ui_handle_input(hGraph, groupInput, editorHovered);
	ImGui::End();
}
