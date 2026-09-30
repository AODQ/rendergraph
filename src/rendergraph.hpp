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
			bool allowMultiple;
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

	struct NodeType { uint32_t id; };

	struct NodeInstance {
		uint32_t id;
		NodeType type;
	};

	// typically won't have to call this when using the rendergraph api, only
	// if generating the graph programatically
	// TODO replace uint32_t with handle type
	uint32_t node_instance_create(
		std::string const & uniqueName,
		int32_t const positionX = 0,
		int32_t const positionY = 0,
		std::vector<rg::ParameterValue> const & parameterValues = {}
	);

	// links nodeA.pinA -> nodeB.pinB
	// pinA/pinB is the pin offset/index
	void node_instance_link(
		uint32_t const & hNodeA,
		uint32_t const & pinA,
		uint32_t const & hNodeB,
		uint32_t const & pinB
	);

	// --------------------------------------------------------------------------
	// -- load/save
	// --------------------------------------------------------------------------

	// expectation is all node types are registered before loading anything from
	// file.
	// The node-types are code-driven while the node-instances are data-driven.
	void register_node_type(NodeTypeCi const & ci);

	void load_from_file(std::string const & path);
	void save_to_file(std::string const & path);

	// loads a demo graph for testing
	void demo_graph();

	// --------------------------------------------------------------------------
	// -- ui
	// --------------------------------------------------------------------------

	void ui_draw();
}
