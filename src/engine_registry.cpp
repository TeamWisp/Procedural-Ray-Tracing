#pragma once

#include "engine_registry.hpp"

#include "pipeline_registry.hpp"
#include "root_signature_registry.hpp"
#include "shader_registry.hpp"
#include "rt_pipeline_registry.hpp"
#include "d3d12/d3d12_structs.hpp"

// Register something into a registry.
#define REGISTER(type, registry) decltype(type) type = registry::Get().Register
// Decscriptor Range Array
#define DESC_RANGE_ARRAY(name, ...) std::vector<CD3DX12_DESCRIPTOR_RANGE> name { __VA_ARGS__ };
// Descriptor Range
#define DESC_RANGE(...) [] { return GetRange(__VA_ARGS__); }()
// Descriptor Range Hardcoded
#define DESC_RANGE_H(...) [] { CD3DX12_DESCRIPTOR_RANGE r; r.Init(__VA_ARGS__); return r; }()
// Root parameter
#define ROOT_PARAM(func) [] { return func; }()
// Root paramter hard coded
#define ROOT_PARAM_H(func) [] { CD3DX12_ROOT_PARAMETER d; d.func; return d; }()
// Root Parameter for descriptor tables
#define ROOT_PARAM_DESC_TABLE(arr, visibility) [] { CD3DX12_ROOT_PARAMETER d; d.InitAsDescriptorTable(arr.size(), arr.data(), visibility); return d; }()

namespace wr
{
	using namespace rs_layout;

	//BDRF Lut Precalculation Root Signature
	DESC_RANGE_ARRAY(ranges_brdf,
		DESC_RANGE(params::brdf_lut, Type::UAV_RANGE, params::BRDF_LutE::OUTPUT),
	);
	
	REGISTER(root_signatures::brdf_lut, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({ 
			ROOT_PARAM_DESC_TABLE(ranges_brdf, D3D12_SHADER_VISIBILITY_ALL)
		}),
		RootSignatureDescription::Samplers({ })
	});


	//Basic Deferred Pass Root Signature
	DESC_RANGE_ARRAY(ranges_basic,
		DESC_RANGE(params::basic, Type::SRV_RANGE, params::BasicE::ALBEDO),
		DESC_RANGE(params::basic, Type::SRV_RANGE, params::BasicE::NORMAL),
		DESC_RANGE(params::basic, Type::SRV_RANGE, params::BasicE::ROUGHNESS),
		DESC_RANGE(params::basic, Type::SRV_RANGE, params::BasicE::METALLIC),
	);

	REGISTER(root_signatures::basic, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetCBV(params::basic, params::BasicE::CAMERA_PROPERTIES, D3D12_SHADER_VISIBILITY_VERTEX)),
			ROOT_PARAM(GetCBV(params::basic, params::BasicE::OBJECT_PROPERTIES, D3D12_SHADER_VISIBILITY_VERTEX)),
			ROOT_PARAM_DESC_TABLE(ranges_basic, D3D12_SHADER_VISIBILITY_PIXEL),
			ROOT_PARAM(GetCBV(params::basic,params::BasicE::MATERIAL_PROPERTIES,D3D12_SHADER_VISIBILITY_PIXEL)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_WRAP }
		})
	});

	//Deferred Composition Root Signature
	DESC_RANGE_ARRAY(srv_ranges,
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::GBUFFER_ALBEDO_ROUGHNESS),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::GBUFFER_NORMAL_METALLIC),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::GBUFFER_DEPTH),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::LIGHT_BUFFER),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::SKY_BOX),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::IRRADIANCE_MAP),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::PREF_ENV_MAP),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::BRDF_LUT),
		DESC_RANGE(params::deferred_composition, Type::SRV_RANGE, params::DeferredCompositionE::BUFFER_REFLECTION_SHADOW),
		DESC_RANGE(params::deferred_composition, Type::UAV_RANGE, params::DeferredCompositionE::OUTPUT),
	);

	REGISTER(root_signatures::deferred_composition, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetCBV(params::deferred_composition, params::DeferredCompositionE::CAMERA_PROPERTIES)),
			ROOT_PARAM_DESC_TABLE(srv_ranges, D3D12_SHADER_VISIBILITY_ALL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP },
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP }
		})
	});


	//MipMapping Root Signature
	DESC_RANGE_ARRAY(mip_in_out_ranges,
		DESC_RANGE(params::mip_mapping, Type::SRV_RANGE, params::MipMappingE::SOURCE),
		DESC_RANGE(params::mip_mapping, Type::UAV_RANGE, params::MipMappingE::DEST),
	);
	REGISTER(root_signatures::mip_mapping, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetConstants(params::mip_mapping, params::MipMappingE::CBUFFER)),
			ROOT_PARAM_DESC_TABLE(mip_in_out_ranges, D3D12_SHADER_VISIBILITY_ALL)
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP }
		})
	});

	//Prefiltering Root Signature
	DESC_RANGE_ARRAY(prefilter_in_out_ranges,
		DESC_RANGE(params::cubemap_prefiltering, Type::SRV_RANGE, params::CubemapPrefilteringE::SOURCE),
		DESC_RANGE(params::cubemap_prefiltering, Type::UAV_RANGE, params::CubemapPrefilteringE::DEST),
		);
	REGISTER(root_signatures::cubemap_prefiltering, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetConstants(params::cubemap_prefiltering, params::CubemapPrefilteringE::CBUFFER)),
			ROOT_PARAM_DESC_TABLE(prefilter_in_out_ranges, D3D12_SHADER_VISIBILITY_ALL)
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP }
		})
	});


	//Cubemap conversion root signature
	DESC_RANGE_ARRAY(cubemap_tasks_ranges,
		DESC_RANGE(params::cubemap_conversion, Type::SRV_RANGE, params::CubemapConversionE::EQUIRECTANGULAR_TEXTURE),
	);
	REGISTER(root_signatures::cubemap_conversion, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetConstants(params::cubemap_conversion, params::CubemapConversionE::IDX)),
			ROOT_PARAM(GetCBV(params::cubemap_conversion, params::CubemapConversionE::CAMERA_PROPERTIES)),
			ROOT_PARAM_DESC_TABLE(cubemap_tasks_ranges, D3D12_SHADER_VISIBILITY_PIXEL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP }
		})
	});
	
	//Cubemap convolution root signature
	DESC_RANGE_ARRAY(cubemap_convolution_ranges,
		DESC_RANGE(params::cubemap_conversion, Type::SRV_RANGE, params::CubemapConvolutionE::ENVIRONMENT_CUBEMAP),
	);
	REGISTER(root_signatures::cubemap_convolution, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM(GetConstants(params::cubemap_convolution, params::CubemapConvolutionE::IDX)),
			ROOT_PARAM(GetCBV(params::cubemap_convolution, params::CubemapConvolutionE::CAMERA_PROPERTIES)),
			ROOT_PARAM_DESC_TABLE(cubemap_tasks_ranges, D3D12_SHADER_VISIBILITY_PIXEL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP }
		})
	});


	REGISTER(shaders::brdf_lut_cs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/brdf_lut_cs.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
	});

	REGISTER(shaders::basic_vs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/basic.hlsl"),
		ShaderDescription::Entry("main_vs"),
		ShaderDescription::Type(ShaderType::VERTEX_SHADER)
	});

	REGISTER(shaders::basic_ps, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/basic.hlsl"),
		ShaderDescription::Entry("main_ps"),
		ShaderDescription::Type(ShaderType::PIXEL_SHADER)
	});

	REGISTER(shaders::fullscreen_quad_vs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/fullscreen_quad.hlsl"),
		ShaderDescription::Entry("main_vs"),
		ShaderDescription::Type(ShaderType::VERTEX_SHADER)
	});

	REGISTER(shaders::deferred_composition_cs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/deferred_composition.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
	});

	REGISTER(shaders::mip_mapping_cs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/generate_mips_cs.hlsl"),
		ShaderDescription::Entry("main"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
	});

	REGISTER(shaders::equirect_to_cubemap_vs, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/equirect_to_cubemap_conversion.hlsl"),
		ShaderDescription::Entry("main_vs"),
		ShaderDescription::Type(ShaderType::VERTEX_SHADER)
	});

	REGISTER(shaders::equirect_to_cubemap_ps, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/equirect_to_cubemap_conversion.hlsl"),
		ShaderDescription::Entry("main_ps"),
		ShaderDescription::Type(ShaderType::PIXEL_SHADER)
	});

	REGISTER(shaders::cubemap_convolution_ps, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/cubemap_convolution.hlsl"),
		ShaderDescription::Entry("main_ps"),
		ShaderDescription::Type(ShaderType::PIXEL_SHADER)
	});

	REGISTER(shaders::cubemap_prefiltering_cs, ShaderRegistry)({
	ShaderDescription::Path("resources/shaders/prefilter_env_map_cs.hlsl"),
	ShaderDescription::Entry("main_cs"),
	ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
	});

	REGISTER(pipelines::brdf_lut_precalculation, PipelineRegistry)<Vertex2D> ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::brdf_lut_cs),
		PipelineDescription::RootSignature(root_signatures::brdf_lut),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R16G16_FLOAT }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		}
	);

	REGISTER(pipelines::basic_deferred, PipelineRegistry)<VertexColor>({
		PipelineDescription::VertexShader(shaders::basic_vs),
		PipelineDescription::PixelShader(shaders::basic_ps),
		PipelineDescription::ComputeShader(std::nullopt),
		PipelineDescription::RootSignature(root_signatures::basic),
		PipelineDescription::DSVFormat(Format::D32_FLOAT),
		PipelineDescription::RTVFormats({ Format::R32G32B32A32_FLOAT, Format::R32G32B32A32_FLOAT }),
		PipelineDescription::NumRTVFormats(3),
		PipelineDescription::Type(PipelineType::GRAPHICS_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(true),
		PipelineDescription::CounterClockwise(false),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	REGISTER(pipelines::deferred_composition, PipelineRegistry)<Vertex2D>({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::deferred_composition_cs),
		PipelineDescription::RootSignature(root_signatures::deferred_composition),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R8G8B8A8_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	REGISTER(pipelines::mip_mapping, PipelineRegistry)<VertexColor>({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::mip_mapping_cs),
		PipelineDescription::RootSignature(root_signatures::mip_mapping),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ }),
		PipelineDescription::NumRTVFormats(0),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	REGISTER(pipelines::equirect_to_cubemap, PipelineRegistry)<Vertex>(
	{
		PipelineDescription::VertexShader(shaders::equirect_to_cubemap_vs),
		PipelineDescription::PixelShader(shaders::equirect_to_cubemap_ps),
		PipelineDescription::ComputeShader(std::nullopt),
		PipelineDescription::RootSignature(root_signatures::cubemap_conversion),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R32G32B32A32_FLOAT }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::GRAPHICS_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_NONE),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(false),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	REGISTER(pipelines::cubemap_convolution, PipelineRegistry)<Vertex>(
	{
		PipelineDescription::VertexShader(shaders::equirect_to_cubemap_vs),
		PipelineDescription::PixelShader(shaders::cubemap_convolution_ps),
		PipelineDescription::ComputeShader(std::nullopt),
		PipelineDescription::RootSignature(root_signatures::cubemap_convolution),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R32G32B32A32_FLOAT }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::GRAPHICS_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_NONE),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(false),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	REGISTER(pipelines::cubemap_prefiltering, PipelineRegistry) < Vertex > (
	{
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::cubemap_prefiltering_cs),
		PipelineDescription::RootSignature(root_signatures::cubemap_prefiltering),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::UNKNOWN }),
		PipelineDescription::NumRTVFormats(0),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_NONE),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(false),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	/* ### Depth of field ### */

	// Cone of confusion
	REGISTER(shaders::dof_coc, ShaderRegistry) ({
		ShaderDescription::Path("resources/shaders/dof_coc.hlsl"),
			ShaderDescription::Entry("main_cs"),
			ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
		});

	DESC_RANGE_ARRAY(dofcoc_r,
		DESC_RANGE(params::dof_coc, Type::SRV_RANGE, params::DoFCoCE::GDEPTH),
		DESC_RANGE(params::dof_coc, Type::UAV_RANGE, params::DoFCoCE::OUTPUT),
		);

	REGISTER(root_signatures::dof_coc, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(dofcoc_r, D3D12_SHADER_VISIBILITY_ALL),
			ROOT_PARAM(GetCBV(params::dof_coc, params::DoFCoCE::CAMERA_PROPERTIES)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP}
		})
		});

	REGISTER(pipelines::dof_coc, PipelineRegistry) < Vertex2D > ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::dof_coc),
		PipelineDescription::RootSignature(root_signatures::dof_coc),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R8G8B8A8_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		});

	// Down Scale texture
	REGISTER(shaders::dof_down_scale, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/dof_down_scale.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
		});

	DESC_RANGE_ARRAY(dscale_r,
		DESC_RANGE(params::dof_down_scale, Type::SRV_RANGE, params::DoFDownScaleE::SOURCE),
		DESC_RANGE(params::dof_down_scale, Type::UAV_RANGE, params::DoFDownScaleE::OUTPUT),
		DESC_RANGE(params::dof_down_scale, Type::SRV_RANGE, params::DoFDownScaleE::COC),
		);

	REGISTER(root_signatures::dof_down_scale, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(dscale_r, D3D12_SHADER_VISIBILITY_ALL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_ANISOTROPIC, TextureAddressMode::TAM_CLAMP},
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP}
		})
		});

	REGISTER(pipelines::dof_down_scale, PipelineRegistry) < Vertex2D > ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::dof_down_scale),
		PipelineDescription::RootSignature(root_signatures::dof_down_scale),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R8G8B8A8_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		});

	//dof bokeh
	REGISTER(shaders::dof_bokeh, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/dof_bokeh.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
		});

	DESC_RANGE_ARRAY(dof_bokeh_r,
		DESC_RANGE(params::dof_bokeh, Type::SRV_RANGE, params::DoFBokehE::SOURCE),
		DESC_RANGE(params::dof_bokeh, Type::UAV_RANGE, params::DoFBokehE::OUTPUT),
		);

	REGISTER(root_signatures::dof_bokeh, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(dof_bokeh_r, D3D12_SHADER_VISIBILITY_ALL),
			ROOT_PARAM(GetCBV(params::dof_bokeh, params::DoFBokehE::CAMERA_PROPERTIES)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP},
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP}
		})
		});

	REGISTER(pipelines::dof_bokeh, PipelineRegistry) < Vertex2D > ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::dof_bokeh),
		PipelineDescription::RootSignature(root_signatures::dof_bokeh),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R16G16B16A16_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		});

	//dof bokeh post filter
	REGISTER(shaders::dof_bokeh_post_filter, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/dof_bokeh_post_filter.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
		});

	DESC_RANGE_ARRAY(dof_bokeh_post_filter_r,
		DESC_RANGE(params::dof_bokeh_post_filter, Type::SRV_RANGE, params::DoFBokehPostFilterE::SOURCE),
		DESC_RANGE(params::dof_bokeh_post_filter, Type::UAV_RANGE, params::DoFBokehPostFilterE::OUTPUT),
		);

	REGISTER(root_signatures::dof_bokeh_post_filter, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(dof_bokeh_post_filter_r, D3D12_SHADER_VISIBILITY_ALL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_ANISOTROPIC, TextureAddressMode::TAM_CLAMP},
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP}
		})
		});

	REGISTER(pipelines::dof_bokeh_post_filter, PipelineRegistry) < Vertex2D > ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::dof_bokeh_post_filter),
		PipelineDescription::RootSignature(root_signatures::dof_bokeh_post_filter),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R8G8B8A8_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		});

	// depth of field composition
	REGISTER(shaders::dof_composition, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/dof_composition.hlsl"),
		ShaderDescription::Entry("main_cs"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
		});

	DESC_RANGE_ARRAY(dof_composition_r,
		DESC_RANGE(params::dof_composition, Type::SRV_RANGE, params::DoFCompositionE::SOURCE),
		DESC_RANGE(params::dof_composition, Type::UAV_RANGE, params::DoFCompositionE::OUTPUT),
		DESC_RANGE(params::dof_composition, Type::SRV_RANGE, params::DoFCompositionE::BOKEH),
		DESC_RANGE(params::dof_composition, Type::SRV_RANGE, params::DoFCompositionE::COC),
		);

	REGISTER(root_signatures::dof_composition, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(dof_composition_r, D3D12_SHADER_VISIBILITY_ALL),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_CLAMP},
			{ TextureFilter::FILTER_LINEAR, TextureAddressMode::TAM_CLAMP}
		})
		});

	REGISTER(pipelines::dof_composition, PipelineRegistry) < Vertex2D > ({
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::dof_composition),
		PipelineDescription::RootSignature(root_signatures::dof_composition),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ Format::R8G8B8A8_UNORM }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_BACK),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
		});


	/* ### Raytracing ### */
	REGISTER(shaders::post_processing, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/post_processing.hlsl"),
		ShaderDescription::Entry("main"),
		ShaderDescription::Type(ShaderType::DIRECT_COMPUTE_SHADER)
	});

	REGISTER(shaders::rt_lib, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/raytracing.hlsl"),
		ShaderDescription::Entry("RaygenEntry"),
		ShaderDescription::Type(ShaderType::LIBRARY_SHADER)
	});

	DESC_RANGE_ARRAY(accum_r,
		DESC_RANGE(params::post_processing, Type::SRV_RANGE, params::PostProcessingE::SOURCE),
		DESC_RANGE(params::post_processing, Type::UAV_RANGE, params::PostProcessingE::DEST),
	);

	REGISTER(root_signatures::post_processing, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(accum_r, D3D12_SHADER_VISIBILITY_ALL),
			ROOT_PARAM(GetConstants(params::post_processing, params::PostProcessingE::HDR_SUPPORT)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_POINT, TextureAddressMode::TAM_BORDER }
		})
	});

	REGISTER(pipelines::post_processing, PipelineRegistry)<Vertex2D>(
	{
		PipelineDescription::VertexShader(std::nullopt),
		PipelineDescription::PixelShader(std::nullopt),
		PipelineDescription::ComputeShader(shaders::post_processing),
		PipelineDescription::RootSignature(root_signatures::post_processing),
		PipelineDescription::DSVFormat(Format::UNKNOWN),
		PipelineDescription::RTVFormats({ d3d12::settings::back_buffer_format }),
		PipelineDescription::NumRTVFormats(1),
		PipelineDescription::Type(PipelineType::COMPUTE_PIPELINE),
		PipelineDescription::CullMode(CullMode::CULL_NONE),
		PipelineDescription::Depth(false),
		PipelineDescription::CounterClockwise(true),
		PipelineDescription::TopologyType(TopologyType::TRIANGLE)
	});

	DESC_RANGE_ARRAY(r,
		DESC_RANGE(params::full_raytracing, Type::UAV_RANGE, params::FullRaytracingE::OUTPUT),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::INDICES),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::LIGHTS),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::MATERIALS),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::OFFSETS),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::SKYBOX),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::IRRADIANCE_MAP),
		DESC_RANGE(params::full_raytracing, Type::SRV_RANGE, params::FullRaytracingE::TEXTURES),
		DESC_RANGE_H(D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, d3d12::settings::fallback_ptrs_offset),
	);

	REGISTER(root_signatures::rt_test_global, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(r, D3D12_SHADER_VISIBILITY_ALL),
			ROOT_PARAM(GetSRV(params::full_raytracing, params::FullRaytracingE::ACCELERATION_STRUCTURE)),
			ROOT_PARAM(GetCBV(params::full_raytracing, params::FullRaytracingE::CAMERA_PROPERTIES)),
			ROOT_PARAM(GetSRV(params::full_raytracing, params::FullRaytracingE::VERTICES)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_ANISOTROPIC, TextureAddressMode::TAM_WRAP }
		}),
		RootSignatureDescription::ForRTX(true)
	});

	StateObjectDescription::LibraryDesc rt_full_lib = []()
	{
		StateObjectDescription::LibraryDesc lib;
		lib.shader_handle = shaders::rt_lib;
		lib.exports.push_back(L"RaygenEntry");
		lib.exports.push_back(L"ClosestHitEntry");
		lib.exports.push_back(L"MissEntry");
		lib.exports.push_back(L"ShadowClosestHitEntry");
		lib.exports.push_back(L"ShadowMissEntry");
		lib.m_hit_groups.push_back({ L"MyHitGroup", L"ClosestHitEntry" });
		lib.m_hit_groups.push_back({ L"ShadowHitGroup", L"ShadowClosestHitEntry" });

		return lib;
	}();
	
	REGISTER(state_objects::state_object, RTPipelineRegistry)(
	{
		StateObjectDescription::D3D12StateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE),
		StateObjectDescription::Library(rt_full_lib),
		StateObjectDescription::MaxPayloadSize((sizeof(float)* 7) + sizeof(unsigned int)),
		StateObjectDescription::MaxAttributeSize(sizeof(float)* 4),
		StateObjectDescription::MaxRecursionDepth(3),
		StateObjectDescription::GlobalRootSignature(root_signatures::rt_test_global),
		StateObjectDescription::LocalRootSignatures(std::nullopt),
	});

	/* ### Hybrid Raytracing ### */
	REGISTER(shaders::rt_hybrid_lib, ShaderRegistry)({
		ShaderDescription::Path("resources/shaders/rt_hybrid.hlsl"),
		ShaderDescription::Entry("RaygenEntry"),
		ShaderDescription::Type(ShaderType::LIBRARY_SHADER)
	});

	DESC_RANGE_ARRAY(rt_hybrid_ranges,
		DESC_RANGE(params::rt_hybrid, Type::UAV_RANGE, params::RTHybridE::OUTPUT),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::INDICES),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::LIGHTS),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::MATERIALS),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::OFFSETS),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::SKYBOX),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::IRRADIANCE_MAP),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::TEXTURES),
		DESC_RANGE(params::rt_hybrid, Type::SRV_RANGE, params::RTHybridE::GBUFFERS),
		DESC_RANGE_H(D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9, d3d12::settings::fallback_ptrs_offset),
	);

	REGISTER(root_signatures::rt_hybrid_global, RootSignatureRegistry)({
		RootSignatureDescription::Parameters({
			ROOT_PARAM_DESC_TABLE(rt_hybrid_ranges, D3D12_SHADER_VISIBILITY_ALL),
			ROOT_PARAM(GetSRV(params::rt_hybrid, params::RTHybridE::ACCELERATION_STRUCTURE)),
			ROOT_PARAM(GetCBV(params::rt_hybrid, params::RTHybridE::CAMERA_PROPERTIES)),
			ROOT_PARAM(GetSRV(params::rt_hybrid, params::RTHybridE::VERTICES)),
		}),
		RootSignatureDescription::Samplers({
			{ TextureFilter::FILTER_ANISOTROPIC, TextureAddressMode::TAM_WRAP }
		}),
		RootSignatureDescription::RTXLocal(true)
	});

	StateObjectDescription::LibraryDesc rt_hybrid_so_library = []()
	{
		StateObjectDescription::LibraryDesc lib;
		lib.shader_handle = shaders::rt_hybrid_lib;
		lib.exports.push_back(L"RaygenEntry");
		lib.exports.push_back(L"ReflectionHit");
		lib.exports.push_back(L"ReflectionMiss");
		lib.exports.push_back(L"ShadowClosestHitEntry");
		lib.exports.push_back(L"ShadowMissEntry");
		lib.m_hit_groups.push_back({L"ReflectionHitGroup", L"ReflectionHit"});
		lib.m_hit_groups.push_back({L"ShadowHitGroup", L"ShadowClosestHitEntry"});

		return lib;
	}();
	REGISTER(state_objects::rt_hybrid_state_object, RTPipelineRegistry)(
	{
		StateObjectDescription::D3D12StateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE),
		StateObjectDescription::Library(rt_hybrid_so_library),
		StateObjectDescription::MaxPayloadSize((sizeof(float) * 6) + (sizeof(unsigned int) * 1)),
		StateObjectDescription::MaxAttributeSize(sizeof(float) * 4),
		StateObjectDescription::MaxRecursionDepth(3),
		StateObjectDescription::GlobalRootSignature(root_signatures::rt_hybrid_global),
		StateObjectDescription::LocalRootSignatures(std::nullopt),
	});

} /* wr */
