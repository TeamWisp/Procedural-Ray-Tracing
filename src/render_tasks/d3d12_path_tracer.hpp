#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../d3d12/d3d12_rt_pipeline_registry.hpp"
#include "../d3d12/d3d12_root_signature_registry.hpp"
#include "../engine_registry.hpp"

#include "../render_tasks/d3d12_deferred_main.hpp"
#include "../render_tasks/d3d12_build_acceleration_structures.hpp"
#include "../imgui_tools.hpp"

namespace wr
{
	struct PathTracerData
	{
		d3d12::AccelerationStructure out_tlas = {};

		// Shader tables
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_raygen_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_miss_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_hitgroup_shader_table = { nullptr, nullptr, nullptr };

		// Pipeline objects
		d3d12::StateObject* out_state_object = nullptr;

		// Structures and buffers
		D3D12ConstantBufferHandle* out_cb_camera_handle = nullptr;
		d3d12::RenderTarget* out_deferred_main_rt = nullptr;

		DirectX::XMVECTOR last_cam_pos = {};
		DirectX::XMVECTOR last_cam_rot = {};

		DescriptorAllocation out_output_alloc;
		DescriptorAllocation out_gbuffer_albedo_alloc;
		DescriptorAllocation out_gbuffer_normal_alloc;
		DescriptorAllocation out_gbuffer_depth_alloc;

		bool tlas_requires_init = true;
	};

	namespace internal
	{

		inline void CreateShaderTables(d3d12::Device* device, PathTracerData& data, int frame_idx)
		{
			// Delete existing shader table
			if (data.out_miss_shader_table[frame_idx])
			{
				d3d12::Destroy(data.out_miss_shader_table[frame_idx]);
			}
			if (data.out_hitgroup_shader_table[frame_idx])
			{
				d3d12::Destroy(data.out_hitgroup_shader_table[frame_idx]);
			}
			if (data.out_raygen_shader_table[frame_idx])
			{
				d3d12::Destroy(data.out_raygen_shader_table[frame_idx]);
			}

			// Set up Raygen Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 1;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);
				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "RaygenEntry");

				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				// Create Table
				data.out_raygen_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_raygen_shader_table[frame_idx], shader_record);
			}

			// Set up Miss Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);

				auto shadow_miss_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ShadowMissEntry");
				auto shadow_miss_record = d3d12::CreateShaderRecord(shadow_miss_identifier, shader_identifier_size);

				auto reflection_miss_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ReflectionMiss");
				auto reflection_miss_record = d3d12::CreateShaderRecord(reflection_miss_identifier, shader_identifier_size);

				// Create Table(s)
				data.out_miss_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_miss_shader_table[frame_idx], reflection_miss_record);
				d3d12::AddShaderRecord(data.out_miss_shader_table[frame_idx], shadow_miss_record);
			}

			// Set up Hit Group Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);

				auto shadow_hit_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ShadowHitGroup");
				auto shadow_hit_record = d3d12::CreateShaderRecord(shadow_hit_identifier, shader_identifier_size);

				auto reflection_hit_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ReflectionHitGroup");
				auto reflection_hit_record = d3d12::CreateShaderRecord(reflection_hit_identifier, shader_identifier_size);

				// Create Table(s)
				data.out_hitgroup_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_hitgroup_shader_table[frame_idx], reflection_hit_record);
				d3d12::AddShaderRecord(data.out_hitgroup_shader_table[frame_idx], shadow_hit_record);
			}
		}

		inline void SetupPathTracerTask(RenderSystem & render_system, FrameGraph & fg, RenderTaskHandle & handle, bool resize)
		{
			if (fg.HasTask<RTHybridData>())
			{
				fg.GetPredecessorData<RTHybridData>();
			}

			// Initialize variables
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);
			auto& device = n_render_system.m_device;
			auto& data = fg.GetData<PathTracerData>(handle);
			auto n_render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);
			d3d12::SetName(n_render_target, L"Path Tracing Render Target");

			if (!resize)
			{
				// Get AS build data
				auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();

				data.out_output_alloc = std::move(as_build_data.out_allocator->Allocate());
				data.out_gbuffer_albedo_alloc = std::move(as_build_data.out_allocator->Allocate());
				data.out_gbuffer_normal_alloc = std::move(as_build_data.out_allocator->Allocate());
				data.out_gbuffer_depth_alloc = std::move(as_build_data.out_allocator->Allocate());

				data.tlas_requires_init = true;
			}

			// Versioning
			for (int frame_idx = 0; frame_idx < 1; ++frame_idx)
			{
				// Bind output texture
				d3d12::DescHeapCPUHandle rtv_handle = data.out_output_alloc.GetDescriptorHandle();
				d3d12::CreateUAVFromSpecificRTV(n_render_target, rtv_handle, frame_idx, n_render_target->m_create_info.m_rtv_formats[frame_idx]);

				// Bind g-buffers (albedo, normal, depth)
				auto albedo_handle = data.out_gbuffer_albedo_alloc.GetDescriptorHandle();
				auto normal_handle = data.out_gbuffer_normal_alloc.GetDescriptorHandle();
				auto depth_handle = data.out_gbuffer_depth_alloc.GetDescriptorHandle();

				auto deferred_main_rt = data.out_deferred_main_rt = static_cast<d3d12::RenderTarget*>(fg.GetPredecessorRenderTarget<DeferredMainTaskData>());

				d3d12::CreateSRVFromSpecificRTV(deferred_main_rt, albedo_handle, 0, deferred_main_rt->m_create_info.m_rtv_formats[0]);
				d3d12::CreateSRVFromSpecificRTV(deferred_main_rt, normal_handle, 1, deferred_main_rt->m_create_info.m_rtv_formats[1]);

				d3d12::CreateSRVFromDSV(deferred_main_rt, depth_handle);
			}

			if (!resize)
			{
				// Camera constant buffer
				data.out_cb_camera_handle = static_cast<D3D12ConstantBufferHandle*>(n_render_system.m_raytracing_cb_pool->Create(sizeof(temp::RTHybridCamera_CBData)));

				// Pipeline State Object
				auto& rt_registry = RTPipelineRegistry::Get();
				data.out_state_object = static_cast<D3D12StateObject*>(rt_registry.Find(state_objects::path_tracer_state_object))->m_native;
			}

			// Create Shader Tables
			CreateShaderTables(device, data, 0);
			CreateShaderTables(device, data, 1);
			CreateShaderTables(device, data, 2);
		}

		inline void ExecutePathTracerTask(RenderSystem & render_system, FrameGraph & fg, SceneGraph & scene_graph, RenderTaskHandle & handle)
		{
			if (fg.HasTask<RTHybridData>())
			{
				fg.GetPredecessorData<RTHybridData>();
			}

			// Initialize variables
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);
			auto window = n_render_system.m_window.value();
			auto device = n_render_system.m_device;
			auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
			auto& data = fg.GetData<PathTracerData>(handle);
			auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();

			d3d12::CreateOrUpdateTLAS(device, cmd_list, data.tlas_requires_init, data.out_tlas, as_build_data.out_blas_list);

			// Reset accmulation if nessessary
			if (DirectX::XMVector3Length(DirectX::XMVectorSubtract(scene_graph.GetActiveCamera()->m_position, data.last_cam_pos)).m128_f32[0] > 0.01)
			{
				data.last_cam_pos = scene_graph.GetActiveCamera()->m_position;
				n_render_system.temp_rough = -1;
			}

			if (DirectX::XMVector3Length(DirectX::XMVectorSubtract(scene_graph.GetActiveCamera()->m_rotation_radians, data.last_cam_rot)).m128_f32[0] > 0.001)
			{
				data.last_cam_rot = scene_graph.GetActiveCamera()->m_rotation_radians;
				n_render_system.temp_rough = -1;
			}

			// Wait for AS to be built
			{
				auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(as_build_data.out_tlas.m_native);
				cmd_list->m_native->ResourceBarrier(1, &barrier);
			}

			if (n_render_system.m_render_window.has_value())
			{
				auto frame_idx = n_render_system.GetFrameIdx();


				d3d12::BindRaytracingPipeline(cmd_list, data.out_state_object, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);

				// Bind output, indices and materials, offsets, etc
				auto out_uav_handle = data.out_output_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderUAV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::OUTPUT)), out_uav_handle);

				auto out_scene_ib_handle = as_build_data.out_scene_ib_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::INDICES)), out_scene_ib_handle);

				auto out_scene_mat_handle = as_build_data.out_scene_mat_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::MATERIALS)), out_scene_mat_handle);

				auto out_scene_offset_handle = as_build_data.out_scene_offset_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::OFFSETS)), out_scene_offset_handle);

				auto out_albedo_gbuffer_handle = data.out_gbuffer_albedo_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::GBUFFERS)) + 0, out_albedo_gbuffer_handle);

				auto out_normal_gbuffer_handle = data.out_gbuffer_normal_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::GBUFFERS)) + 1, out_normal_gbuffer_handle);

				auto out_scene_depth_handle = data.out_gbuffer_depth_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::GBUFFERS)) + 2, out_scene_depth_handle);

				/*
				To keep the CopyDescriptors function happy, we need to fill the descriptor table with valid descriptors
				We fill the table with a single descriptor, then overwrite some spots with the he correct textures
				If a spot is unused, then a default descriptor will be still bound, but not used in the shaders.
				Since the renderer creates a texture pool that can be used by the render tasks, and
				the texture pool also has default textures for albedo/roughness/etc... one of those textures is a good
				candidate for this.
				*/
				{
					auto texture_pool = render_system.GetDefaultTexturePool();

					if (texture_pool == nullptr)
					{
						LOGC("ERROR: Texture Pool in Raytracing Task is nullptr. This is not supposed to happen.");
					}

					auto texture_handle = texture_pool->GetDefaultAlbedo();
					auto* texture_resource = static_cast<wr::d3d12::TextureResource*>(texture_pool->GetTextureResource(texture_handle));

					size_t num_textures_in_heap = COMPILATION_EVAL(rs_layout::GetSize(params::path_tracing, params::PathTracingE::TEXTURES));
					unsigned int heap_loc_start = COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::TEXTURES));

					for (size_t i = 0; i < num_textures_in_heap; ++i)
					{
						d3d12::SetRTShaderSRV(cmd_list, 0, static_cast<std::uint32_t>(heap_loc_start + i), texture_resource);
					}
				}

				// Fill descriptor heap with textures used by the scene
				for (auto handle : as_build_data.out_material_handles)
				{
					auto* material_internal = handle.m_pool->GetMaterial(handle);

					auto set_srv = [&data, material_internal, cmd_list](auto texture_handle)
					{
						if (!texture_handle.m_pool)
							return;

						auto* texture_internal = static_cast<wr::d3d12::TextureResource*>(texture_handle.m_pool->GetTextureResource(texture_handle));

						d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::rt_hybrid, params::RTHybridE::TEXTURES)) + static_cast<std::uint32_t>(texture_handle.m_id), texture_internal);
					};

					if (material_internal->HasTexture(TextureType::ALBEDO))
						set_srv(material_internal->GetTexture(TextureType::ALBEDO));
					
					if (material_internal->HasTexture(TextureType::NORMAL))
						set_srv(material_internal->GetTexture(TextureType::NORMAL));

					if (material_internal->HasTexture(TextureType::METALLIC))
						set_srv(material_internal->GetTexture(TextureType::METALLIC));

					if (material_internal->HasTexture(TextureType::ROUGHNESS))
						set_srv(material_internal->GetTexture(TextureType::ROUGHNESS));

					if (material_internal->HasTexture(TextureType::EMISSIVE))
						set_srv(material_internal->GetTexture(TextureType::EMISSIVE));

					if (material_internal->HasTexture(TextureType::AO))
						set_srv(material_internal->GetTexture(TextureType::AO));
				}

				// Get light buffer
				if (static_cast<D3D12StructuredBufferHandle*>(scene_graph.GetLightBuffer())->m_native->m_states[frame_idx] != ResourceState::NON_PIXEL_SHADER_RESOURCE)
				{
					static_cast<D3D12StructuredBufferPool*>(scene_graph.GetLightBuffer()->m_pool)->SetBufferState(scene_graph.GetLightBuffer(), ResourceState::NON_PIXEL_SHADER_RESOURCE);
				}

				DescriptorAllocation light_alloc = std::move(as_build_data.out_allocator->Allocate());
				d3d12::DescHeapCPUHandle light_handle = light_alloc.GetDescriptorHandle();
				d3d12::CreateSRVFromStructuredBuffer(static_cast<D3D12StructuredBufferHandle*>(scene_graph.GetLightBuffer())->m_native, light_handle, frame_idx);

				d3d12::DescHeapCPUHandle light_handle2 = light_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::LIGHTS)), light_handle2);

				// Update offset data
				n_render_system.m_raytracing_offset_sb_pool->Update(as_build_data.out_sb_offset_handle, (void*)as_build_data.out_offsets.data(), sizeof(temp::RayTracingOffset_CBData) * as_build_data.out_offsets.size(), 0);

				// Update material data
				if (as_build_data.out_materials_require_update)
				{
					n_render_system.m_raytracing_material_sb_pool->Update(as_build_data.out_sb_material_handle, (void*)as_build_data.out_materials.data(), sizeof(temp::RayTracingMaterial_CBData) * as_build_data.out_materials.size(), 0);
				}

				// Update camera constant buffer
				auto camera = scene_graph.GetActiveCamera();
				temp::RTHybridCamera_CBData cam_data{};
				cam_data.m_inverse_view = DirectX::XMMatrixInverse(nullptr, camera->m_view);
				cam_data.m_inverse_projection = DirectX::XMMatrixInverse(nullptr, camera->m_projection);
				cam_data.m_inv_vp = DirectX::XMMatrixInverse(nullptr, camera->m_view * camera->m_projection);
				cam_data.m_intensity = n_render_system.temp_intensity;
				cam_data.m_frame_idx = ++n_render_system.temp_rough;
				n_render_system.m_camera_pool->Update(data.out_cb_camera_handle, sizeof(temp::RTHybridCamera_CBData), 0, frame_idx, (std::uint8_t*)&cam_data); // FIXME: Uhh wrong pool?

				// Make sure the convolution pass wrote to the skybox.
				fg.GetPredecessorData<CubemapConvolutionTaskData>();

				// Get skybox
				if (scene_graph.m_skybox.has_value())
				{
					auto skybox_t = static_cast<d3d12::TextureResource*>(scene_graph.m_skybox.value().m_pool->GetTextureResource(scene_graph.m_skybox.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::SKYBOX)), skybox_t);
				}

				// Get Pre-filtered environment
				if (scene_graph.m_skybox.has_value())
				{
					auto irradiance_t = static_cast<d3d12::TextureResource*>(scene_graph.GetCurrentSkybox()->m_prefiltered_env_map->m_pool->GetTextureResource(scene_graph.GetCurrentSkybox()->m_prefiltered_env_map.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::PREF_ENV_MAP)), irradiance_t);
				}

				// Get brdf lookup texture
				if (scene_graph.m_skybox.has_value())
				{
					auto brdf_lut_text = static_cast<d3d12::TextureResource*>(n_render_system.m_brdf_lut.value().m_pool->GetTextureResource(n_render_system.m_brdf_lut.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::BRDF_LUT)), brdf_lut_text);
				}

				// Get Environment Map
				if (scene_graph.m_skybox.has_value())
				{
					auto irradiance_t = static_cast<d3d12::TextureResource*>(scene_graph.GetCurrentSkybox()->m_irradiance->m_pool->GetTextureResource(scene_graph.GetCurrentSkybox()->m_irradiance.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::path_tracing, params::PathTracingE::IRRADIANCE_MAP)), irradiance_t);
				}

				// Transition depth to NON_PIXEL_RESOURCE
				d3d12::TransitionDepth(cmd_list, data.out_deferred_main_rt, ResourceState::DEPTH_WRITE, ResourceState::NON_PIXEL_SHADER_RESOURCE);

				d3d12::BindDescriptorHeap(cmd_list, cmd_list->m_rt_descriptor_heap.get()->GetHeap(), DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV, frame_idx, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);
				d3d12::BindDescriptorHeaps(cmd_list, frame_idx, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);
				d3d12::BindComputeConstantBuffer(cmd_list, data.out_cb_camera_handle->m_native, 2, frame_idx);

				if (d3d12::GetRaytracingType(device) == RaytracingType::NATIVE)
				{
					d3d12::BindComputeShaderResourceView(cmd_list, as_build_data.out_tlas.m_native, 1);
				}
				else if (d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK)
				{
					cmd_list->m_native_fallback->SetTopLevelAccelerationStructure(0, as_build_data.out_tlas.m_fallback_tlas_ptr);
				}

				d3d12::BindComputeShaderResourceView(cmd_list, as_build_data.out_scene_vb->m_buffer, 3);

				//#ifdef _DEBUG
				CreateShaderTables(device, data, frame_idx);
				//#endif

				// Dispatch hybrid ray tracing rays
				d3d12::DispatchRays(cmd_list, data.out_hitgroup_shader_table[frame_idx], data.out_miss_shader_table[frame_idx], data.out_raygen_shader_table[frame_idx], window->GetWidth(), window->GetHeight(), 1, frame_idx);

				// Transition depth back to DEPTH_WRITE
				d3d12::TransitionDepth(cmd_list, data.out_deferred_main_rt, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::DEPTH_WRITE);
			}
		}

		inline void DestroyPathTracerTask(FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			if (!resize)
			{
				auto& data = fg.GetData<PathTracerData>(handle);

				// Small hack to force the allocations to go out of scope, which will tell the allocator to free them
				std::move(data.out_output_alloc);
				std::move(data.out_gbuffer_albedo_alloc);
				std::move(data.out_gbuffer_normal_alloc);
				std::move(data.out_gbuffer_depth_alloc);
			}
		}


	} /* internal */

	inline void AddPathTracerTask(FrameGraph& fg)
	{
		RenderTargetProperties rt_properties
		{
			RenderTargetProperties::IsRenderWindow(false),
			RenderTargetProperties::Width(std::nullopt),
			RenderTargetProperties::Height(std::nullopt),
			RenderTargetProperties::ExecuteResourceState(ResourceState::UNORDERED_ACCESS),
			RenderTargetProperties::FinishedResourceState(ResourceState::COPY_SOURCE),
			RenderTargetProperties::CreateDSVBuffer(false),
			RenderTargetProperties::DSVFormat(Format::UNKNOWN),
			RenderTargetProperties::RTVFormats({ wr::Format::R16G16B16A16_FLOAT }),
			RenderTargetProperties::NumRTVFormats(1),
			RenderTargetProperties::Clear(false),
			RenderTargetProperties::ClearDepth(false),
			RenderTargetProperties::ResourceName(L"Path Tracer")
		};

		RenderTaskDesc desc;
		desc.m_setup_func = [](RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			internal::SetupPathTracerTask(rs, fg, handle, resize);
		};
		desc.m_execute_func = [](RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle)
		{
			internal::ExecutePathTracerTask(rs, fg, sg, handle);
		};
		desc.m_destroy_func = [](FrameGraph & fg, RenderTaskHandle handle, bool resize)
		{
			internal::DestroyPathTracerTask(fg, handle, resize);
		};
		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::COMPUTE;
		desc.m_allow_multithreading = true;

		fg.AddTask<PathTracerData>(desc, FG_DEPS(1, DeferredMainTaskData));
	}

} /* wr */