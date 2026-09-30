// demo graph

#include "rendergraph.hpp"

#include <initializer_list>

// -----------------------------------------------------------------------------
// -- implementation helpers
// -----------------------------------------------------------------------------

static rg::PinDesc impl_pin_in(
	char const * const name,
	rg::PinKind const kind,
	rg::ImageFormat const format = rg::ImageFormat::rgba16,
	rg::AccessMode const accessMode = rg::AccessMode::Sampled,
	bool const required = true,
	rg::PinResourceTemporal const temporal =
		rg::PinResourceTemporal::currentFrame,
	f32 const scaleWidth = 1.0f,
	f32 const scaleHeight = 1.0f,
	u32 const mipLevels = 1
)
{
	return rg::PinDesc{
		.name = name,
		.kind = kind,
		.dir = rg::PinDir::in,
		.pinIn = {
			.required = required,
			.temporal = temporal,
			.format = format,
			.accessMode = accessMode,
		},
		.pinOut = {},
		.scaleWidth = scaleWidth,
		.scaleHeight = scaleHeight,
		.mipLevels = mipLevels,
	};
}

static rg::PinDesc impl_pin_out(
	char const * const name,
	rg::PinKind const kind,
	rg::ImageFormat const format = rg::ImageFormat::rgba16,
	rg::AccessMode const accessMode = rg::AccessMode::StorageWrite,
	bool const allowMultiple = false,
	f32 const scaleWidth = 1.0f,
	f32 const scaleHeight = 1.0f,
	u32 const mipLevels = 1
)
{
	return rg::PinDesc{
		.name = name,
		.kind = kind,
		.dir = rg::PinDir::out,
		.pinIn = {},
		.pinOut = {
			.allowMultiple = allowMultiple,
			.format = format,
			.accessMode = accessMode,
		},
		.scaleWidth = scaleWidth,
		.scaleHeight = scaleHeight,
		.mipLevels = mipLevels,
	};
}

static rg::ParameterDesc impl_param_f32(
	char const * const name,
	f32 const defaultValue,
	f32 const minValue,
	f32 const maxValue
)
{
	return rg::ParameterDesc{
		.name = name,
		.kind = rg::ParameterKind::f32,
		.defaultValue = {
			.f32 = {
				.value = defaultValue,
			},
		},
		.minValue = {
			.f32 = {
				.value = minValue,
			},
		},
		.maxValue = {
			.f32 = {
				.value = maxValue,
			},
		},
		.enumValues = std::vector<std::string>{},
	};
}

static rg::ParameterDesc impl_param_u32(
	char const * const name,
	u32 const defaultValue,
	u32 const minValue,
	u32 const maxValue
)
{
	return rg::ParameterDesc{
		.name = name,
		.kind = rg::ParameterKind::u32,
		.defaultValue = {
			.u32 = {
				.value = defaultValue,
			},
		},
		.minValue = {
			.u32 = {
				.value = minValue,
			},
		},
		.maxValue = {
			.u32 = {
				.value = maxValue,
			},
		},
		.enumValues = std::vector<std::string>{},
	};
}

static rg::ParameterDesc impl_param_bool(
	char const * const name,
	bool const defaultValue
)
{
	return rg::ParameterDesc{
		.name = name,
		.kind = rg::ParameterKind::boolean,
		.defaultValue = {
			.boolean = {
				.value = defaultValue,
			},
		},
		.minValue = {
			.boolean = {
				.value = false,
			},
		},
		.maxValue = {
			.boolean = {
				.value = true,
			},
		},
		.enumValues = std::vector<std::string>{},
	};
}

static rg::ParameterDesc impl_param_enum(
	char const * const name,
	u32 const defaultIndex,
	std::initializer_list<char const *> const values
)
{
	std::vector<std::string> enumValues;

	for (char const * const value : values)
		enumValues.emplace_back(value);

	return rg::ParameterDesc{
		.name = name,
		.kind = rg::ParameterKind::enumeration,
		.defaultValue = {
			.u32 = {
				.value = defaultIndex,
			},
		},
		.minValue = {},
		.maxValue = {},
		.enumValues = std::move(enumValues),
	};
}

// -----------------------------------------------------------------------------
// -- node type registration
// -----------------------------------------------------------------------------

static void impl_register_demo_node_types()
{
	// -------------------------------------------------------------------------
	// Scene / geometry
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Scene Upload",
			.uniqueName = "engine.scene_upload",
			.queue = rg::NodeTypeQueue::copy,
			.pinIn = std::vector<rg::PinDesc>{},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Scene",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::CopyDst
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "GPU Culling",
			.uniqueName = "engine.gpu_culling",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Scene",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Visible Instances",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Depth Prepass",
			.uniqueName = "engine.depth_prepass",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Visible Instances",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::IndirectBuffer
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Hi-Z Build",
			.uniqueName = "engine.hiz_build",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Hi-Z",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::StorageWrite,
					false,
					1.0f,
					1.0f,
					8
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Shadows
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Shadow Culling",
			.uniqueName = "engine.shadow_culling",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Scene",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Shadow Instances",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Shadow Maps",
			.uniqueName = "engine.shadow_maps",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Shadow Instances",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::IndirectBuffer
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Shadow Atlas",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// GBuffer / materials
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "GBuffer",
			.uniqueName = "engine.gbuffer",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Visible Instances",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::IndirectBuffer
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Albedo",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Material",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Motion",
					rg::PinKind::image,
					rg::ImageFormat::rg16,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Decals",
			.uniqueName = "engine.decals",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthRead
				),
				impl_pin_in(
					"Albedo",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_in(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_in(
					"Material",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Albedo",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_out(
					"Material",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Lighting
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Cluster Build",
			.uniqueName = "engine.cluster_build",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Scene",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
				impl_pin_in(
					"Hi-Z",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Light Clusters",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "SSAO",
			.uniqueName = "engine.ssao",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"AO",
					rg::PinKind::image,
					rg::ImageFormat::r8,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32("Radius", 1.0f, 0.0f, 10.0f),
				impl_param_f32("Power", 1.5f, 0.0f, 4.0f),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "SSR",
			.uniqueName = "engine.ssr",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Hi-Z",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Reflections",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Max Distance",
					50.0f,
					0.0f,
					500.0f
				),
				impl_param_u32(
					"Max Steps",
					32,
					1,
					256
				),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Opaque Lighting",
			.uniqueName = "engine.opaque_lighting",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Albedo",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Normal",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Material",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthRead
				),
				impl_pin_in(
					"AO",
					rg::PinKind::image,
					rg::ImageFormat::r8,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Reflections",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled,
					false
				),
				impl_pin_in(
					"Shadow Atlas",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Light Clusters",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Atmosphere / particles / transparency
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Volumetric Fog",
			.uniqueName = "engine.volumetric_fog",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Shadow Atlas",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Density",
					0.02f,
					0.0f,
					1.0f
				),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Particle Simulation",
			.uniqueName = "engine.particle_simulation",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Particles",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
				impl_pin_in(
					"Scene",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Particles",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Particle Render",
			.uniqueName = "engine.particle_render",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Particles",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::IndirectBuffer
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Transparency",
			.uniqueName = "engine.transparency",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::DepthRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Exposure
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Luminance Histogram",
			.uniqueName = "engine.luminance_histogram",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Histogram",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Auto Exposure",
			.uniqueName = "engine.auto_exposure",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Histogram",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
				impl_pin_in(
					"Previous Exposure",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead,
					true,
					rg::PinResourceTemporal::previousFrame
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Exposure",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Adapt Speed",
					1.0f,
					0.0f,
					20.0f
				),
			},
		}
	);

	// -------------------------------------------------------------------------
	// Bloom
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Bloom Extract",
			.uniqueName = "engine.bloom_extract",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Bloom",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Threshold",
					1.0f,
					0.0f,
					20.0f
				),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Bloom Downsample",
			.uniqueName = "engine.bloom_downsample",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Input",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Output",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite,
					false,
					0.5f,
					0.5f
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Bloom Upsample",
			.uniqueName = "engine.bloom_upsample",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Low",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"High",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Output",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Strength",
					0.8f,
					0.0f,
					4.0f
				),
			},
		}
	);

	// -------------------------------------------------------------------------
	// Temporal AA / upscale
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "TAAU",
			.uniqueName = "engine.taau",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Motion",
					rg::PinKind::image,
					rg::ImageFormat::rg16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Depth",
					rg::PinKind::image,
					rg::ImageFormat::r32f,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"History",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled,
					true,
					rg::PinResourceTemporal::previousFrame
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Output",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Feedback",
					0.9f,
					0.0f,
					1.0f
				),
				impl_param_f32(
					"Sharpness",
					0.2f,
					0.0f,
					1.0f
				),
			},
		}
	);

	// -------------------------------------------------------------------------
	// Final post
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Bloom Composite",
			.uniqueName = "engine.bloom_composite",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
				impl_pin_in(
					"Bloom",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_f32(
					"Intensity",
					0.08f,
					0.0f,
					4.0f
				),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Color Grade",
			.uniqueName = "engine.color_grade",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageRead
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::StorageWrite
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Tone Map",
			.uniqueName = "engine.tonemap",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"HDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba16,
					rg::AccessMode::Sampled
				),
				impl_pin_in(
					"Exposure",
					rg::PinKind::buffer,
					rg::ImageFormat::rgba16,
					rg::AccessMode::UniformBuffer
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"LDR",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{
				impl_param_enum(
					"Operator",
					0,
					{
						"ACES",
						"AgX",
						"Reinhard",
						"Neutral",
					}
				),
				impl_param_f32(
					"Exposure Bias",
					0.0f,
					-8.0f,
					8.0f
				),
			},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "UI Composite",
			.uniqueName = "engine.ui_composite",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Color",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
			},
			.pinOut = std::vector<rg::PinDesc>{
				impl_pin_out(
					"Color",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::ColorAttachment
				),
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Present
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Present",
			.uniqueName = "engine.present",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				impl_pin_in(
					"Color",
					rg::PinKind::image,
					rg::ImageFormat::rgba8,
					rg::AccessMode::CopySrc
				),
				impl_pin_in(
					"Swapchain",
					rg::PinKind::externalImage,
					rg::ImageFormat::rgba8,
					rg::AccessMode::CopyDst
				),
			},
			.pinOut = std::vector<rg::PinDesc>{},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);
}

// -----------------------------------------------------------------------------
// -- demo graph
// -----------------------------------------------------------------------------

void rg::demo_graph()
{
	impl_register_demo_node_types();

	[[maybe_unused]] const uint32_t sceneUpload = (
		rg::node_instance_create(
			"engine.scene_upload",
			-1200,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t culling = (
		rg::node_instance_create(
			"engine.gpu_culling",
			-1000,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t depth = (
		rg::node_instance_create(
			"engine.depth_prepass",
			-800,
			-300,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t hiz = (
		rg::node_instance_create(
			"engine.hiz_build",
			-600,
			-300,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t shadowCulling = (
		rg::node_instance_create(
			"engine.shadow_culling",
			-800,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t shadowMaps = (
		rg::node_instance_create(
			"engine.shadow_maps",
			-600,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t gbuffer = (
		rg::node_instance_create(
			"engine.gbuffer",
			-600,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t decals = (
		rg::node_instance_create(
			"engine.decals",
			-350,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t ssao = (
		rg::node_instance_create(
			"engine.ssao",
			-350,
			-400,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 1.0f,
					},
				},
				rg::ParameterValue{
					.f32 = {
						.value = 1.5f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t clusterBuild = (
		rg::node_instance_create(
			"engine.cluster_build",
			-350,
			-150,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t ssr = (
		rg::node_instance_create(
			"engine.ssr",
			-100,
			-400,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 50.0f,
					},
				},
				rg::ParameterValue{
					.u32 = {
						.value = 32,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t opaqueLighting = (
		rg::node_instance_create(
			"engine.opaque_lighting",
			-100,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t volumetricFog = (
		rg::node_instance_create(
			"engine.volumetric_fog",
			150,
			-250,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.02f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t particleSimulation = (
		rg::node_instance_create(
			"engine.particle_simulation",
			-100,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t particleRender = (
		rg::node_instance_create(
			"engine.particle_render",
			150,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t transparency = (
		rg::node_instance_create(
			"engine.transparency",
			400,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t histogram = (
		rg::node_instance_create(
			"engine.luminance_histogram",
			600,
			-350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t exposure = (
		rg::node_instance_create(
			"engine.auto_exposure",
			800,
			-350,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 1.0f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t bloomExtract = (
		rg::node_instance_create(
			"engine.bloom_extract",
			600,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 1.0f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t bloomDownsample0 = (
		rg::node_instance_create(
			"engine.bloom_downsample",
			800,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t bloomDownsample1 = (
		rg::node_instance_create(
			"engine.bloom_downsample",
			1000,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t bloomDownsample2 = (
		rg::node_instance_create(
			"engine.bloom_downsample",
			1200,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t bloomUpsample0 = (
		rg::node_instance_create(
			"engine.bloom_upsample",
			1400,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.8f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t bloomUpsample1 = (
		rg::node_instance_create(
			"engine.bloom_upsample",
			1600,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.8f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t bloomUpsample2 = (
		rg::node_instance_create(
			"engine.bloom_upsample",
			1800,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.8f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t taau = (
		rg::node_instance_create(
			"engine.taau",
			800,
			300,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.9f,
					},
				},
				rg::ParameterValue{
					.f32 = {
						.value = 0.2f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t bloomComposite = (
		rg::node_instance_create(
			"engine.bloom_composite",
			2000,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.f32 = {
						.value = 0.08f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t colorGrade = (
		rg::node_instance_create(
			"engine.color_grade",
			2200,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t toneMap = (
		rg::node_instance_create(
			"engine.tonemap",
			2400,
			50,
			std::vector<rg::ParameterValue>{
				rg::ParameterValue{
					.u32 = {
						.value = 0,
					},
				},
				rg::ParameterValue{
					.f32 = {
						.value = 0.0f,
					},
				},
			}
		)
	);

	[[maybe_unused]] const uint32_t uiComposite = (
		rg::node_instance_create(
			"engine.ui_composite",
			2600,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const uint32_t present = (
		rg::node_instance_create(
			"engine.present",
			2800,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	// -- links
	rg::node_instance_link(sceneUpload, 0, culling, 0);

	rg::node_instance_link(culling, 0, depth, 0);
	rg::node_instance_link(culling, 0, shadowCulling, 0);
	rg::node_instance_link(culling, 0, gbuffer, 0);
	rg::node_instance_link(culling, 0, clusterBuild, 0);
	rg::node_instance_link(culling, 0, particleSimulation, 1);

	rg::node_instance_link(depth, 0, hiz, 0);
	rg::node_instance_link(depth, 0, ssao, 0);

	rg::node_instance_link(shadowCulling, 0, shadowMaps, 0);

	rg::node_instance_link(gbuffer, 0, decals, 1);
	rg::node_instance_link(gbuffer, 1, decals, 2);
	rg::node_instance_link(gbuffer, 2, decals, 3);
	rg::node_instance_link(gbuffer, 3, taau, 1);
	rg::node_instance_link(gbuffer, 4, decals, 0);
	rg::node_instance_link(gbuffer, 4, ssao, 0);
	rg::node_instance_link(gbuffer, 4, opaqueLighting, 3);
	rg::node_instance_link(gbuffer, 4, particleRender, 1);
	rg::node_instance_link(gbuffer, 4, taau, 2);

	rg::node_instance_link(decals, 0, opaqueLighting, 0);
	rg::node_instance_link(decals, 1, opaqueLighting, 1);
	rg::node_instance_link(decals, 2, opaqueLighting, 2);

	rg::node_instance_link(hiz, 0, clusterBuild, 1);
	rg::node_instance_link(hiz, 0, ssr, 3);

	rg::node_instance_link(ssao, 0, opaqueLighting, 4);

	rg::node_instance_link(shadowMaps, 0, opaqueLighting, 6);

	rg::node_instance_link(clusterBuild, 0, opaqueLighting, 7);

	rg::node_instance_link(opaqueLighting, 0, volumetricFog, 0);
	rg::node_instance_link(opaqueLighting, 0, ssr, 0);
	rg::node_instance_link(opaqueLighting, 0, histogram, 0);
	rg::node_instance_link(opaqueLighting, 0, bloomExtract, 0);
	rg::node_instance_link(opaqueLighting, 0, transparency, 0);

	rg::node_instance_link(particleSimulation, 0, particleRender, 0);

	rg::node_instance_link(volumetricFog, 0, transparency, 0);

	rg::node_instance_link(histogram, 0, exposure, 0);

	rg::node_instance_link(bloomExtract, 0, bloomDownsample0, 0);
	rg::node_instance_link(bloomDownsample0, 0, bloomDownsample1, 0);
	rg::node_instance_link(bloomDownsample1, 0, bloomDownsample2, 0);

	rg::node_instance_link(
		bloomDownsample2,
		0,
		bloomUpsample0,
		0
	);

	rg::node_instance_link(
		bloomDownsample1,
		0,
		bloomUpsample0,
		1
	);

	rg::node_instance_link(
		bloomUpsample0,
		0,
		bloomUpsample1,
		0
	);

	rg::node_instance_link(
		bloomDownsample0,
		0,
		bloomUpsample1,
		1
	);

	rg::node_instance_link(
		bloomUpsample1,
		0,
		bloomUpsample2,
		0
	);

	rg::node_instance_link(
		bloomExtract,
		0,
		bloomUpsample2,
		1
	);

	rg::node_instance_link(transparency, 0, taau, 0);

	rg::node_instance_link(taau, 0, bloomComposite, 0);

	rg::node_instance_link(bloomUpsample2, 0, bloomComposite, 1);

	rg::node_instance_link(bloomComposite, 0, colorGrade, 0);

	rg::node_instance_link(colorGrade, 0, toneMap, 0);

	rg::node_instance_link(exposure, 0, toneMap, 1);

	rg::node_instance_link(toneMap, 0, uiComposite, 0);

	rg::node_instance_link(uiComposite, 0, present, 0);
}
