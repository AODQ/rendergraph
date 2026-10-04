#pragma once

#include <cstdint>

#include <vector>
#include <string>

using i32 = int32_t;
using u32 = uint32_t;
using f32 = float;

namespace rg
{
	// --------------------------------------------------------------------------
	// -- pin
	// --------------------------------------------------------------------------

	enum struct PinDir {
		in,
		out
	};
	enum struct PinKind {
		image,
		buffer,
		externalImage,
	};

	enum struct PinResourceTemporal {
		currentFrame,
		previousFrame,
	};

	enum struct ImageFormat {
		r8,
		r16,
		r32f,
		rg8,
		rg16,
		rg32f,
		rgb8,
		rgb16,
		rgb32f,
		rgba8,
		rgba16,
		rgba32f,
	};

	enum struct AccessMode {
		Sampled,
		StorageRead,
		StorageWrite,
		ColorAttachment,
		DepthWrite,
		DepthRead,
		CopySrc,
		CopyDst,
		IndirectBuffer,
		UniformBuffer,
	};

	struct PinDesc {
		std::string name;
		PinKind kind;
		PinDir dir;

		struct {
			bool required = true;
			PinResourceTemporal temporal = PinResourceTemporal::currentFrame;
			ImageFormat format = ImageFormat::rgba16;
			AccessMode accessMode = AccessMode::Sampled;
		} pinIn;

		struct {
			ImageFormat format = ImageFormat::rgba16;
			AccessMode accessMode = AccessMode::StorageWrite;
		} pinOut;

		f32 scaleWidth = 1.0f;
		f32 scaleHeight = 1.0f;
		u32 mipLevels = 1;

		std::string bufferType = "";
	};

	// --------------------------------------------------------------------------
	// -- parameter
	// --------------------------------------------------------------------------

	enum struct ParameterKind {
		f32, i32, u32, boolean, enumeration,
	};
	struct ParameterValue {
		struct {
			f32 value;
		} f32;
		struct {
			i32 value;
		} i32;
		struct {
			u32 value;
		} u32;
		struct {
			bool value;
		} boolean;
	};
	struct ParameterDesc {
		std::string name;
		ParameterKind kind;
		ParameterValue defaultValue;
		ParameterValue minValue;
		ParameterValue maxValue;
		std::vector<std::string> enumValues;
	};

	// --------------------------------------------------------------------------
	// -- nodes
	// --------------------------------------------------------------------------

	enum struct NodeTypeQueue {
		graphics,
		compute,
		copy,
	};

	struct NodeTypeCi {
		std::string name;
		std::string uniqueName;

		NodeTypeQueue queue;

		std::vector<PinDesc> pinIn;
		std::vector<PinDesc> pinOut;

		std::vector<ParameterDesc> parameters;
	};

	// --------------------------------------------------------------------------
	// -- global layout
	// --------------------------------------------------------------------------

	struct GlobalFieldDesc {
		std::string name;
		// matches the bufferType of the pins that can link to the field
		std::string bufferType;
	};

	// flat list of named fields that groups can override
	struct GlobalLayoutCi {
		std::vector<GlobalFieldDesc> fields;
	};

	// --------------------------------------------------------------------------
	// -- handles
	// --------------------------------------------------------------------------

	// id 0 is always invalid
	struct NodeType { uint32_t id; };
	struct Graph { uint32_t id; };
	struct Link { uint32_t id; };

	struct NodeInstance {
		uint32_t id;
		NodeType type;
	};

	// --------------------------------------------------------------------------
	// -- node types
	// --------------------------------------------------------------------------

	// The node-types are code-driven and process-wide; they are shared by all
	// graphs. Expectation is all node types are registered before loading
	// anything from file.
	NodeType register_node_type(NodeTypeCi const & ci);

	// returns { 0 } if not registered
	NodeType node_type_find(std::string const & uniqueName);

	// --------------------------------------------------------------------------
	// -- global layout registration
	// --------------------------------------------------------------------------

	// The global layout is process-wide and shared by all graphs. It can be
	// registered only once.
	void register_global_layout(GlobalLayoutCi const & ci);

	// --------------------------------------------------------------------------
	// -- graph
	// --------------------------------------------------------------------------

	// requires the ImNodes context to exist
	Graph graph_create();
	void graph_destroy(Graph const hGraph);

	// --------------------------------------------------------------------------
	// -- group
	// --------------------------------------------------------------------------

	struct Group {
		uint32_t id { 0 };
	};

	auto graph_group_create(
		rg::Graph const hGraph,
		std::string const & name,
		rg::Group const hParentGroup = {0},
		int32_t const positionX = 0,
		int32_t const positionY = 0,
		int32_t const width = 200,
		int32_t const height = 200
	) -> Group;

	auto graph_group_default() -> rg::Group;

	// nodes and child groups of the group remain
	auto graph_group_destroy(
		rg::Graph const hGraph,
		rg::Group const hGroup
	) -> void;

	auto graph_group_set_name(
		rg::Graph const hGraph,
		rg::Group const hGroup,
		std::string const & name
	) -> void;

	// Enables or disables the override of a global layout field on the group.
	// An enabled field adds a pin to the group. Disabling destroys the links
	// into that pin. Fields start disabled.
	auto graph_group_set_field_enabled(
		rg::Graph const hGraph,
		rg::Group const hGroup,
		uint32_t const field,
		bool const enabled
	) -> void;

	// --------------------------------------------------------------------------
	// -- edits: nodes
	// --------------------------------------------------------------------------

	// add a node instance of the given type to the graph at given position.
	// If hGroup is non-zero, will add to be a child of the associated group.
	// if the position is outside of that group's bounds, the edit will
	// be reverted and returned NodeInstance will be 0.
	NodeInstance node_instance_create(
		rg::Graph const hGraph,
		std::string const & uniqueName,
		rg::Group const hGroup = { 0 },
		int32_t const positionX = 0,
		int32_t const positionY = 0,
		std::vector<rg::ParameterValue> const & parameterValues = {}
	);

	// also destroys all links attached to the node
	void node_instance_destroy(
		rg::Graph const hGraph,
		rg::NodeInstance const hNode
	);

	void node_instance_set_position(
		rg::Graph const hGraph,
		rg::NodeInstance const hNode,
		int32_t const positionX,
		int32_t const positionY
	);

	// empty name displays the node type's name
	void node_instance_set_name(
		Graph const hGraph,
		NodeInstance const hNode,
		std::string const & name
	);

	// --------------------------------------------------------------------------
	// -- edits: links
	// --------------------------------------------------------------------------

	// returns if hNodeA.pinA -> hNodeB.pinB can be linked.
	// pinA is the output index of its pins, pinB is the input index of its pins.
	// fails on kind mismatch, already-linked input or a cycle.
	// links into a previous-frame input can be exempt from the cycle check
	bool node_instance_can_link(
		Graph const hGraph,
		NodeInstance const hNodeA,
		uint32_t const pinA,
		NodeInstance const hNodeB,
		uint32_t const pinB
	);

	// links hNodeA.pinA -> hNodeB.pinB
	// pinA/pinB is the pin offset/index
	// returns { 0 } if node_instance_can_link fails
	Link node_instance_link(
		Graph const hGraph,
		NodeInstance const hNodeA,
		uint32_t const pinA,
		NodeInstance const hNodeB,
		uint32_t const pinB
	);

	// returns if hNodeA.pinA -> hGroup.field can be linked.
	// fails on a disabled field, bufferType mismatch, already-linked field or
	// if hNodeA is in the group or reachable from a node in the group.
	bool group_field_can_link(
		Graph const hGraph,
		NodeInstance const hNodeA,
		uint32_t const pinA,
		Group const hGroup,
		uint32_t const field
	);

	// links hNodeA.pinA -> hGroup.field
	// returns { 0 } if group_field_can_link fails
	Link group_field_link(
		Graph const hGraph,
		NodeInstance const hNodeA,
		uint32_t const pinA,
		Group const hGroup,
		uint32_t const field
	);

	void link_destroy(Graph const hGraph, Link const hLink);

	// --------------------------------------------------------------------------
	// -- edits: transactions
	// --------------------------------------------------------------------------

	// transactions nest; only the outermost one ends the undo step
	void transaction_begin(Graph const hGraph);
	void transaction_end(Graph const hGraph);

	// --------------------------------------------------------------------------
	// -- undo
	// --------------------------------------------------------------------------

	bool undo(Graph const hGraph);
	bool redo(Graph const hGraph);
	void undo_history_clear(Graph const hGraph);

	// --------------------------------------------------------------------------
	// -- load/save
	// --------------------------------------------------------------------------

	void load_from_file(Graph const hGraph, std::string const & path);
	void save_to_file(Graph const hGraph, std::string const & path);

	// fills the graph with a demo for testing
	void demo_graph(Graph const hGraph);

	// --------------------------------------------------------------------------
	// -- ui
	// --------------------------------------------------------------------------

	void ui_draw(Graph const hGraph);
}
