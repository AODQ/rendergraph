#include "rendergraph.hpp"

#include <imgui.h>
#include <imnodes.h>
#include <unordered_map>

// -----------------------------------------------------------------------------
// -- impl details
// -----------------------------------------------------------------------------

namespace
{
	struct ImplNodeType {
		uint32_t id;
		std::string name;
		std::string uniqueName;
		std::vector<rg::PinDesc> pinIn;
		std::vector<rg::PinDesc> pinOut;
	};

	struct ImplNodeInstance {
		uint32_t id;
		rg::NodeType type;
		std::vector<uint32_t> pinInIds;
		std::vector<uint32_t> pinOutIds;
		std::vector<rg::ParameterValue> parameterValues;

		// TODO delete these when editor queue is implemented
		bool isFirstFrame = false;
		int32_t posX = 0;
		int32_t posY = 0;
	};

	struct ImplLink {
		uint32_t id;
		uint32_t pinInId;
		uint32_t pinOutId;
	};
}

static std::unordered_map<uint32_t, ImplNodeType> sImplNodeTypes;
static std::unordered_map<std::string, uint32_t> sImplNodeTypeUniqueNameToId;
static std::unordered_map<uint32_t, ImplNodeInstance> sImplNodeInstances;
static std::vector<ImplLink> sImplLinks;

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// -- create
// -----------------------------------------------------------------------------

uint32_t rg::node_instance_create(
	std::string const & uniqueName,
	int32_t const positionX,
	int32_t const positionY,
	std::vector<rg::ParameterValue> const & parameterValues
) {
	static uint32_t sNextNodeInstanceId = 1;
	static uint32_t sNextPinId = 1;
	uint32_t const id = sNextNodeInstanceId++;
	uint32_t const typeId = sImplNodeTypeUniqueNameToId.at(uniqueName);
	ImplNodeType & nodeType = sImplNodeTypes.at(typeId);
	ImplNodeInstance nodeInstance = {
		.id = id,
		.type = { typeId },
		.parameterValues = parameterValues,
		.isFirstFrame = true,
		.posX = positionX,
		.posY = positionY,
	};
	for (rg::PinDesc const & pinIn : nodeType.pinIn) {
		nodeInstance.pinInIds.push_back(sNextPinId++);
	}
	for (rg::PinDesc const & pinOut : nodeType.pinOut) {
		nodeInstance.pinOutIds.push_back(sNextPinId++);
	}
	sImplNodeInstances[id] = nodeInstance;
	return id;
}

void rg::node_instance_link(
	uint32_t const & hNodeA,
	uint32_t const & pinA,
	uint32_t const & hNodeB,
	uint32_t const & pinB
) {
	ImplNodeInstance const nodeA = sImplNodeInstances.at(hNodeA);
	ImplNodeInstance const nodeB = sImplNodeInstances.at(hNodeB);
	auto const pinIn = nodeA.pinOutIds[pinA];
	auto const pinOut = nodeB.pinInIds[pinB];
	ImplLink const link = {
		.id = static_cast<uint32_t>(sImplLinks.size() + 1),
		.pinInId = pinIn,
		.pinOutId = pinOut,
	};
	sImplLinks.emplace_back(link);
}

void rg::register_node_type(NodeTypeCi const & ci) {
	static uint32_t sNextNodeTypeId = 1;
	uint32_t const id = sNextNodeTypeId++;
	sImplNodeTypes[id] = {
		.id = id,
		.name = ci.name,
		.uniqueName = ci.uniqueName,
		.pinIn = ci.pinIn,
		.pinOut = ci.pinOut,
	};
	sImplNodeTypeUniqueNameToId[ci.uniqueName] = id;
}

// -----------------------------------------------------------------------------
// -- load/save
// -----------------------------------------------------------------------------

void rg::load_from_file(std::string const & path) {
}

// -----------------------------------------------------------------------------
// -- ui impl detail
// -----------------------------------------------------------------------------

void impl_ui_draw_node(ImplNodeInstance const & node) {
	ImplNodeType const & nodeType = sImplNodeTypes.at(node.type.id);
	ImNodes::BeginNode(node.id);

	if (node.isFirstFrame) {
		ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.posX, node.posY));
	}

	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted(nodeType.name.c_str());
	ImNodes::EndNodeTitleBar();

	ImGui::Dummy(ImVec2(0.0f, 5.0f));

	// -- render input pins
	for (size_t i = 0; i < node.pinInIds.size(); ++i) {
		rg::PinDesc const & pinInType = nodeType.pinIn[i];
		uint32_t const pinId = node.pinInIds[i];
		ImNodes::BeginInputAttribute(pinId);
		ImGui::TextUnformatted(pinInType.name.c_str());
		ImNodes::EndInputAttribute();
	}

	// -- render output pins
	for (size_t i = 0; i < node.pinOutIds.size(); ++i) {
		rg::PinDesc const & pinOutType = nodeType.pinOut[i];
		uint32_t const pinId = node.pinOutIds[i];
		ImNodes::BeginOutputAttribute(pinId);
		ImGui::TextUnformatted(pinOutType.name.c_str());
		ImNodes::EndOutputAttribute();
	}

	ImNodes::EndNode();
}

void impl_ui_draw_rendergraph() {
	ImGui::Begin("Render Graph");
	ImNodes::BeginNodeEditor();

	// -- render nodes
	for (auto const & [id, node] : sImplNodeInstances) {
		impl_ui_draw_node(node);
	}

	// -- render links
	for (ImplLink const & link : sImplLinks) {
		ImNodes::Link(link.id, link.pinInId, link.pinOutId);
	}

	// -- right click menu
	{
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			printf("right click\n");
			// int32_t const hoveredNode = ImNodes::IsEditorHovered();
			if (ImNodes::IsEditorHovered())
			{
				printf("open popuper\n");
				ImGui::OpenPopup("rg_context_menu");
			}
		}
		if (ImGui::BeginPopup("rg_context_menu")) {
			if (ImGui::BeginMenu("add node")) {
				for (auto const & [id, nodeType] : sImplNodeTypes) {
					if (!ImGui::MenuItem(nodeType.name.c_str())) {
						continue;
					}
					uint32_t const instance = (
						rg::node_instance_create(nodeType.uniqueName)
					);
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}

	ImNodes::EndNodeEditor();
	ImGui::End();
}

// -----------------------------------------------------------------------------
// -- ui public
// -----------------------------------------------------------------------------

void rg::ui_draw() {
	impl_ui_draw_rendergraph();

	// -- check for created links
	{
		int32_t startedId = 0;
		int32_t endedId = 0;
		if (ImNodes::IsLinkCreated(&startedId, &endedId)) {
			sImplLinks.emplace_back( ImplLink {
				.id = static_cast<uint32_t>(sImplLinks.size() + 1),
				.pinInId = static_cast<uint32_t>(startedId),
				.pinOutId = static_cast<uint32_t>(endedId),
			});
		}
	}
}
