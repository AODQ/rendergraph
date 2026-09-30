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
			bool required;
			PinResourceTemporal temporal;
			ImageFormat format;
			AccessMode accessMode;
		} pinIn;

		struct {
			ImageFormat format;
			AccessMode accessMode;
		} pinOut;

		f32 scaleWidth;
		f32 scaleHeight;
		u32 mipLevels;
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
	// -- graph
	// --------------------------------------------------------------------------

	// requires the ImNodes context to exist
	Graph graph_create();
	void graph_destroy(Graph const hGraph);

	// --------------------------------------------------------------------------
	// -- edits: nodes
	// --------------------------------------------------------------------------

	NodeInstance node_instance_create(
		Graph const hGraph,
		std::string const & uniqueName,
		int32_t const positionX = 0,
		int32_t const positionY = 0,
		std::vector<rg::ParameterValue> const & parameterValues = {}
	);

	// also destroys all links attached to the node
	void node_instance_destroy(Graph const hGraph, NodeInstance const hNode);

	void node_instance_set_position(
		Graph const hGraph,
		NodeInstance const hNode,
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
