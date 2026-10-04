// demo graph

#include "rendergraph.hpp"

#include <initializer_list>

#include "log.hpp"

// -----------------------------------------------------------------------------
// -- implementation helpers
// -----------------------------------------------------------------------------

static auto impl_param_f32(
	char const * const name,
	f32 const defaultValue,
	f32 const minValue,
	f32 const maxValue
) -> rg::ParameterDesc
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

static auto impl_param_u32(
	char const * const name,
	u32 const defaultValue,
	u32 const minValue,
	u32 const maxValue
) -> rg::ParameterDesc
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

static auto impl_param_bool(
	char const * const name,
	bool const defaultValue
) -> rg::ParameterDesc
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

static auto impl_param_enum(
	char const * const name,
	u32 const defaultIndex,
	std::initializer_list<char const *> const values
) -> rg::ParameterDesc
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

static auto impl_register_demo_node_types() -> void
{
	// -------------------------------------------------------------------------
	// Global layout
	// -------------------------------------------------------------------------

	rg::register_global_layout(
		rg::GlobalLayoutCi{
			.fields = std::vector<rg::GlobalFieldDesc>{
				rg::GlobalFieldDesc{
					.name = "renderCamera",
					.bufferType = "Camera",
				},
				rg::GlobalFieldDesc{
					.name = "scene",
					.bufferType = "MeshClusterList",
				},
				rg::GlobalFieldDesc{
					.name = "lightClusters",
					.bufferType = "LightClusterList",
				},
				rg::GlobalFieldDesc{
					.name = "exposure",
					.bufferType = "Exposure",
				},
			},
		}
	);

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
				rg::PinDesc{
					.name = "Scene",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::CopyDst,
					},
					.bufferType = "MeshClusterList",
				},
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Camera",
			.uniqueName = "engine.camera",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Camera",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "Camera",
				},
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "GPU Culling",
			.uniqueName = "engine.gpu_culling",
			.queue = rg::NodeTypeQueue::compute,
			.pinIn = std::vector<rg::PinDesc>{},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Visible Instances",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "MeshClusterList",
				},
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
				rg::PinDesc{
					.name = "Visible Instances",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::IndirectBuffer,
					},
					.bufferType = "MeshClusterList",
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Hi-Z",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.mipLevels = 8,
				},
			},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);

	// -------------------------------------------------------------------------
	// Shadows
	// -------------------------------------------------------------------------

	rg::register_node_type(
		rg::NodeTypeCi{
			.name = "Shadow Maps",
			.uniqueName = "engine.shadow_maps",
			.queue = rg::NodeTypeQueue::graphics,
			.pinIn = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Shadow Instances",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::IndirectBuffer,
					},
					.bufferType = "MeshClusterList",
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Shadow Atlas",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Visible Instances",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::IndirectBuffer,
					},
					.bufferType = "MeshClusterList",
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Albedo",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Material",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Motion",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rg16,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthRead,
					},
				},
				rg::PinDesc{
					.name = "Albedo",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Material",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Albedo",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Material",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "Hi-Z",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Light Clusters",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "LightClusterList",
				},
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
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "AO",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::r8,
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Hi-Z",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Reflections",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Albedo",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Normal",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Material",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthRead,
					},
				},
				rg::PinDesc{
					.name = "AO",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r8,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Reflections",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.required = false,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Shadow Atlas",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Shadow Atlas",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Particles",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
					.bufferType = "ParticleList",
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Particles",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "ParticleList",
				},
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
				rg::PinDesc{
					.name = "Particles",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::IndirectBuffer,
					},
					.bufferType = "ParticleList",
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthRead,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::DepthRead,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Histogram",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "Histogram",
				},
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
				rg::PinDesc{
					.name = "Histogram",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
					.bufferType = "Histogram",
				},
				rg::PinDesc{
					.name = "Previous Exposure",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::in,
					.pinIn = {
						.temporal = rg::PinResourceTemporal::previousFrame,
						.accessMode = rg::AccessMode::StorageRead,
					},
					.bufferType = "Exposure",
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Exposure",
					.kind = rg::PinKind::buffer,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.bufferType = "Exposure",
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Bloom",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "Input",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Output",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
					.scaleWidth = 0.5f,
					.scaleHeight = 0.5f,
				},
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
				rg::PinDesc{
					.name = "Low",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "High",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Output",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Motion",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rg16,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "Depth",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::r32f,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
				rg::PinDesc{
					.name = "History",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.temporal = rg::PinResourceTemporal::previousFrame,
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Output",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
				},
				rg::PinDesc{
					.name = "Bloom",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::StorageRead,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.accessMode = rg::AccessMode::StorageWrite,
					},
				},
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
				rg::PinDesc{
					.name = "HDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.accessMode = rg::AccessMode::Sampled,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "LDR",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "Color",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{
				rg::PinDesc{
					.name = "Color",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::out,
					.pinOut = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::ColorAttachment,
					},
				},
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
				rg::PinDesc{
					.name = "Color",
					.kind = rg::PinKind::image,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::CopySrc,
					},
				},
				rg::PinDesc{
					.name = "Swapchain",
					.kind = rg::PinKind::externalImage,
					.dir = rg::PinDir::in,
					.pinIn = {
						.format = rg::ImageFormat::rgba8,
						.accessMode = rg::AccessMode::CopyDst,
					},
				},
			},
			.pinOut = std::vector<rg::PinDesc>{},
			.parameters = std::vector<rg::ParameterDesc>{},
		}
	);
}

// -----------------------------------------------------------------------------
// -- demo graph checks
// -----------------------------------------------------------------------------

// stores the line of the first edit that failed to add
template <typename Handle>
static auto impl_demo_check(
	int32_t & failedLine,
	int32_t const line,
	Handle const handle
) -> Handle
{
	if (handle.id == 0 && failedLine == 0) {
		failedLine = line;
	}
	return handle;
}

// These macros store failures in the local failedLine.
#define DEMO_GROUP_CREATE(...) impl_demo_check( \
	failedLine, __LINE__, rg::graph_group_create(__VA_ARGS__) \
)
#define DEMO_NODE_CREATE(...) impl_demo_check( \
	failedLine, __LINE__, rg::node_instance_create(__VA_ARGS__) \
)
#define DEMO_LINK(...) impl_demo_check( \
	failedLine, __LINE__, rg::node_instance_link(__VA_ARGS__) \
)
#define DEMO_GROUP_LINK(...) impl_demo_check( \
	failedLine, __LINE__, rg::group_field_link(__VA_ARGS__) \
)

// -----------------------------------------------------------------------------
// -- demo graph
// -----------------------------------------------------------------------------

// indices into the demo's global layout
constexpr uint32_t kDemoFieldRenderCamera = 0;
constexpr uint32_t kDemoFieldScene = 1;
constexpr uint32_t kDemoFieldLightClusters = 2;
constexpr uint32_t kDemoFieldExposure = 3;

auto rg::demo_graph(
	rg::Graph const hGraph
) -> void
{
	int32_t failedLine = 0;

	// node types are process-wide, so only register them once
	if (rg::node_type_find("engine.scene_upload").id == 0) {
		impl_register_demo_node_types();
	}

	// -- groups
	auto const mainGroup = DEMO_GROUP_CREATE(
		hGraph,
		"Main Camera",
		rg::graph_group_default(),
		-1110, -450,
		2210, 1210
	);
	auto const shadowGroup = DEMO_GROUP_CREATE(
		hGraph,
		"Shadow",
		rg::graph_group_default(),
		-970, 300,
		620, 200
	);
	auto const lightingGroup = DEMO_GROUP_CREATE(
		hGraph,
		"Lighting",
		rg::graph_group_default(),
		-100, -40,
		400, 420
	);
	auto const postGroup = DEMO_GROUP_CREATE(
		hGraph,
		"Post",
		rg::graph_group_default(),
		2850, 0,
		620, 200
	);
	auto const uiGroup = DEMO_GROUP_CREATE(
		hGraph,
		"UI Camera",
		rg::graph_group_default(),
		3550, 0,
		400, 200
	);

	// -- global layout
	rg::graph_group_set_field_enabled(
		hGraph, mainGroup, kDemoFieldRenderCamera, true
	);
	rg::graph_group_set_field_enabled(
		hGraph, uiGroup, kDemoFieldRenderCamera, true
	);
	rg::graph_group_set_field_enabled(
		hGraph, shadowGroup, kDemoFieldRenderCamera, true
	);
	rg::graph_group_set_field_enabled(
		hGraph, shadowGroup, kDemoFieldScene, true
	);
	rg::graph_group_set_field_enabled(
		hGraph, lightingGroup, kDemoFieldLightClusters, true
	);
	rg::graph_group_set_field_enabled(
		hGraph, postGroup, kDemoFieldExposure, true
	);

	// -- nodes
	[[maybe_unused]] const rg::NodeInstance mainCamera = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.camera",
			rg::graph_group_default(),
			-1450,
			-300,
			std::vector<rg::ParameterValue>{}
		)
	);
	rg::node_instance_set_name(hGraph, mainCamera, "Main Camera");

	[[maybe_unused]] const rg::NodeInstance lightCamera = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.camera",
			rg::graph_group_default(),
			-1450,
			300,
			std::vector<rg::ParameterValue>{}
		)
	);
	rg::node_instance_set_name(hGraph, lightCamera, "Light Camera");

	[[maybe_unused]] const rg::NodeInstance uiCamera = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.camera",
			rg::graph_group_default(),
			3300,
			300,
			std::vector<rg::ParameterValue>{}
		)
	);
	rg::node_instance_set_name(hGraph, uiCamera, "UI Camera");

	[[maybe_unused]] const rg::NodeInstance shadowProxy = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.scene_upload",
			rg::graph_group_default(),
			-1450,
			420,
			std::vector<rg::ParameterValue>{}
		)
	);
	rg::node_instance_set_name(hGraph, shadowProxy, "Shadow Proxy");

	[[maybe_unused]] const rg::NodeInstance culling = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.gpu_culling",
			rg::graph_group_default(),
			-960,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance depth = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.depth_prepass",
			rg::graph_group_default(),
			-800,
			-300,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance hiz = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.hiz_build",
			rg::graph_group_default(),
			-600,
			-300,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance shadowCulling = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.gpu_culling",
			shadowGroup,
			-800,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance shadowMaps = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.shadow_maps",
			shadowGroup,
			-600,
			350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance gbuffer = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.gbuffer",
			rg::graph_group_default(),
			-600,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance decals = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.decals",
			rg::graph_group_default(),
			-350,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance ssao = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.ssao",
			rg::graph_group_default(),
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

	[[maybe_unused]] const rg::NodeInstance clusterBuild = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.cluster_build",
			rg::graph_group_default(),
			-350,
			-150,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance ssr = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.ssr",
			rg::graph_group_default(),
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

	[[maybe_unused]] const rg::NodeInstance opaqueLighting = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.opaque_lighting",
			lightingGroup,
			40,
			20,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance volumetricFog = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.volumetric_fog",
			rg::graph_group_default(),
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

	[[maybe_unused]] const rg::NodeInstance particleSimulation = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.particle_simulation",
			rg::graph_group_default(),
			-100,
			560,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance particleRender = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.particle_render",
			rg::graph_group_default(),
			150,
			560,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance transparency = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.transparency",
			rg::graph_group_default(),
			400,
			0,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance histogram = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.luminance_histogram",
			rg::graph_group_default(),
			1200,
			-350,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance exposure = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.auto_exposure",
			rg::graph_group_default(),
			1400,
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

	[[maybe_unused]] const rg::NodeInstance bloomExtract = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_extract",
			rg::graph_group_default(),
			1200,
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

	[[maybe_unused]] const rg::NodeInstance bloomDownsample0 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_downsample",
			rg::graph_group_default(),
			1400,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance bloomDownsample1 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_downsample",
			rg::graph_group_default(),
			1600,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance bloomDownsample2 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_downsample",
			rg::graph_group_default(),
			1800,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance bloomUpsample0 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_upsample",
			rg::graph_group_default(),
			2000,
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

	[[maybe_unused]] const rg::NodeInstance bloomUpsample1 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_upsample",
			rg::graph_group_default(),
			2200,
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

	[[maybe_unused]] const rg::NodeInstance bloomUpsample2 = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_upsample",
			rg::graph_group_default(),
			2400,
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

	[[maybe_unused]] const rg::NodeInstance taau = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.taau",
			rg::graph_group_default(),
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

	[[maybe_unused]] const rg::NodeInstance bloomComposite = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.bloom_composite",
			rg::graph_group_default(),
			2600,
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

	[[maybe_unused]] const rg::NodeInstance colorGrade = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.color_grade",
			postGroup,
			2990,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance toneMap = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.tonemap",
			postGroup,
			3220,
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

	[[maybe_unused]] const rg::NodeInstance uiComposite = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.ui_composite",
			uiGroup,
			3690,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	[[maybe_unused]] const rg::NodeInstance present = (
		DEMO_NODE_CREATE(
			hGraph,
			"engine.present",
			rg::graph_group_default(),
			4050,
			50,
			std::vector<rg::ParameterValue>{}
		)
	);

	// -- links
	DEMO_LINK(hGraph, culling, 0, depth, 0);
	DEMO_LINK(hGraph, culling, 0, gbuffer, 0);

	DEMO_LINK(hGraph, depth, 0, hiz, 0);
	DEMO_LINK(hGraph, depth, 0, ssao, 0);

	DEMO_LINK(hGraph, shadowCulling, 0, shadowMaps, 0);

	DEMO_LINK(hGraph, gbuffer, 0, decals, 1);
	DEMO_LINK(hGraph, gbuffer, 1, decals, 2);
	DEMO_LINK(hGraph, gbuffer, 1, ssao, 1);
	DEMO_LINK(hGraph, gbuffer, 1, ssr, 2);
	DEMO_LINK(hGraph, gbuffer, 2, decals, 3);
	DEMO_LINK(hGraph, gbuffer, 3, taau, 1);
	DEMO_LINK(hGraph, gbuffer, 4, decals, 0);
	DEMO_LINK(hGraph, gbuffer, 4, opaqueLighting, 3);
	DEMO_LINK(hGraph, gbuffer, 4, ssr, 1);
	DEMO_LINK(hGraph, gbuffer, 4, volumetricFog, 1);
	DEMO_LINK(hGraph, gbuffer, 4, particleRender, 1);
	DEMO_LINK(hGraph, gbuffer, 4, transparency, 1);
	DEMO_LINK(hGraph, gbuffer, 4, taau, 2);

	DEMO_LINK(hGraph, decals, 0, opaqueLighting, 0);
	DEMO_LINK(hGraph, decals, 1, opaqueLighting, 1);
	DEMO_LINK(hGraph, decals, 2, opaqueLighting, 2);

	DEMO_LINK(hGraph, hiz, 0, clusterBuild, 0);
	DEMO_LINK(hGraph, hiz, 0, ssr, 3);

	DEMO_LINK(hGraph, ssao, 0, opaqueLighting, 4);

	DEMO_LINK(hGraph, shadowMaps, 0, opaqueLighting, 6);
	DEMO_LINK(hGraph, shadowMaps, 0, volumetricFog, 2);

	DEMO_LINK(hGraph, opaqueLighting, 0, volumetricFog, 0);
	DEMO_LINK(hGraph, opaqueLighting, 0, ssr, 0);
	DEMO_LINK(hGraph, opaqueLighting, 0, histogram, 0);
	DEMO_LINK(hGraph, opaqueLighting, 0, bloomExtract, 0);

	DEMO_LINK(hGraph, particleSimulation, 0, particleRender, 0);

	DEMO_LINK(hGraph, volumetricFog, 0, transparency, 0);

	DEMO_LINK(hGraph, histogram, 0, exposure, 0);
	DEMO_LINK(hGraph, exposure, 0, exposure, 1);

	// -- group field links
	DEMO_GROUP_LINK(
		hGraph, mainCamera, 0, mainGroup, kDemoFieldRenderCamera
	);
	DEMO_GROUP_LINK(
		hGraph, lightCamera, 0, shadowGroup, kDemoFieldRenderCamera
	);
	DEMO_GROUP_LINK(hGraph, uiCamera, 0, uiGroup, kDemoFieldRenderCamera);
	DEMO_GROUP_LINK(hGraph, shadowProxy, 0, shadowGroup, kDemoFieldScene);
	DEMO_GROUP_LINK(
		hGraph, clusterBuild, 0, lightingGroup, kDemoFieldLightClusters
	);
	DEMO_GROUP_LINK(hGraph, exposure, 0, postGroup, kDemoFieldExposure);

	DEMO_LINK(hGraph, bloomExtract, 0, bloomDownsample0, 0);
	DEMO_LINK(hGraph, bloomDownsample0, 0, bloomDownsample1, 0);
	DEMO_LINK(hGraph, bloomDownsample1, 0, bloomDownsample2, 0);

	DEMO_LINK(hGraph, bloomDownsample2, 0, bloomUpsample0, 0);
	DEMO_LINK(hGraph, bloomDownsample1, 0, bloomUpsample0, 1);

	DEMO_LINK(hGraph, bloomUpsample0, 0, bloomUpsample1, 0);
	DEMO_LINK(hGraph, bloomDownsample0, 0, bloomUpsample1, 1);

	DEMO_LINK(hGraph, bloomUpsample1, 0, bloomUpsample2, 0);
	DEMO_LINK(hGraph, bloomExtract, 0, bloomUpsample2, 1);

	DEMO_LINK(hGraph, transparency, 0, taau, 0);
	DEMO_LINK(hGraph, taau, 0, taau, 3);

	DEMO_LINK(hGraph, taau, 0, bloomComposite, 0);
	DEMO_LINK(hGraph, bloomUpsample2, 0, bloomComposite, 1);

	DEMO_LINK(hGraph, bloomComposite, 0, colorGrade, 0);
	DEMO_LINK(hGraph, colorGrade, 0, toneMap, 0);
	DEMO_LINK(hGraph, toneMap, 0, uiComposite, 0);
	DEMO_LINK(hGraph, uiComposite, 0, present, 0);

	// -- check everything was added
	rg_assert(
		failedLine == 0, "demo graph failed to add at line %d", failedLine
	);
}
