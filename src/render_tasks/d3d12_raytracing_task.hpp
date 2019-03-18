#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../d3d12/d3d12_rt_pipeline_registry.hpp"
#include "../d3d12/d3d12_root_signature_registry.hpp"
#include "../render_tasks/d3d12_build_acceleration_structures.hpp"
#include "../engine_registry.hpp"
#include "../util/math.hpp"

#include "../scene_graph/skybox_node.hpp"
#include "../render_tasks/d3d12_deferred_main.hpp"
#include "../imgui_tools.hpp"
#include "../render_tasks/d3d12_cubemap_convolution.hpp"

namespace wr
{
	struct RaytracingData
	{
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_raygen_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_miss_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_hitgroup_shader_table = { nullptr, nullptr, nullptr };
		d3d12::StateObject* out_state_object;
		d3d12::RootSignature* out_root_signature;
		D3D12ConstantBufferHandle* out_cb_camera_handle;

		DescriptorAllocation out_uav_from_rtv;
	};

	namespace internal
	{

		inline void CreateShaderTables(d3d12::Device* device, RaytracingData& data, int frame_idx)
		{
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

			// Raygen Shader Table
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

			// Miss Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);

				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "MissEntry");
				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				auto shadow_shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ShadowMissEntry");
				auto shadow_shader_record = d3d12::CreateShaderRecord(shadow_shader_identifier, shader_identifier_size);

				// Create Table
				data.out_miss_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);

				d3d12::AddShaderRecord(data.out_miss_shader_table[frame_idx], shader_record);
				d3d12::AddShaderRecord(data.out_miss_shader_table[frame_idx], shadow_shader_record);
			}

			// Hit Group Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);

				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "MyHitGroup");
				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				auto shadow_shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "ShadowHitGroup");
				auto shadow_shader_record = d3d12::CreateShaderRecord(shadow_shader_identifier, shader_identifier_size);

				// Create Table
				data.out_hitgroup_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count,
					shader_identifier_size);

				d3d12::AddShaderRecord(data.out_hitgroup_shader_table[frame_idx], shader_record);
				d3d12::AddShaderRecord(data.out_hitgroup_shader_table[frame_idx], shadow_shader_record);
			}
		}

		inline void SetupRaytracingTask(RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& device = n_render_system.m_device;
			auto& data = fg.GetData<RaytracingData>(handle);
			auto n_render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);
			d3d12::SetName(n_render_target, L"Raytracing Target");

			if (!resize)
			{
				auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
				auto pred_cmd_list = fg.GetPredecessorCommandList<wr::ASBuildData>();

				cmd_list->m_rt_descriptor_heap = static_cast<d3d12::CommandList*>(pred_cmd_list)->m_rt_descriptor_heap;

				// Pipeline State Object
				auto& rt_registry = RTPipelineRegistry::Get();
				data.out_state_object = static_cast<D3D12StateObject*>(rt_registry.Find(state_objects::state_object))->m_native;

				// Root Signature
				auto& rs_registry = RootSignatureRegistry::Get();
				data.out_root_signature = static_cast<D3D12RootSignature*>(rs_registry.Find(root_signatures::rt_test_global))->m_native;

				auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();

				// Camera constant buffer
				data.out_cb_camera_handle = static_cast<D3D12ConstantBufferHandle*>(n_render_system.m_raytracing_cb_pool->Create(sizeof(temp::RayTracingCamera_CBData)));

				data.out_uav_from_rtv = std::move(as_build_data.out_allocator->Allocate());
			}

			for (auto frame_idx = 0; frame_idx < 1; frame_idx++)
			{
				d3d12::DescHeapCPUHandle handle = data.out_uav_from_rtv.GetDescriptorHandle();
				d3d12::CreateUAVFromSpecificRTV(n_render_target, handle, frame_idx, n_render_target->m_create_info.m_rtv_formats[frame_idx]);
			}

			CreateShaderTables(device, data, 0);
			CreateShaderTables(device, data, 1);
			CreateShaderTables(device, data, 2);
		}

		inline void ExecuteRaytracingTask(RenderSystem& rs, FrameGraph& fg, SceneGraph& scene_graph, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto window = n_render_system.m_window.value();
			auto device = n_render_system.m_device;
			auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
			auto& data = fg.GetData<RaytracingData>(handle);
			auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();

			d3d12::DescriptorHeap* desc_heap = cmd_list->m_rt_descriptor_heap->GetHeap();

			cmd_list->m_native->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(as_build_data.out_tlas.m_native));

			if (n_render_system.m_render_window.has_value())
			{
				auto frame_idx = n_render_system.GetFrameIdx();

				d3d12::BindRaytracingPipeline(cmd_list, data.out_state_object, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);

				auto x = data.out_uav_from_rtv.GetDescriptorHandle();
				d3d12::SetRTShaderUAV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::OUTPUT)), x);
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::INDICES)), as_build_data.out_scene_ib_alloc.GetDescriptorHandle());
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::MATERIALS)), as_build_data.out_scene_mat_alloc.GetDescriptorHandle());
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::OFFSETS)), as_build_data.out_scene_offset_alloc.GetDescriptorHandle());

				/*
				To keep the CopyDescriptors function happy, we need to fill the descriptor table with valid descriptors
				We fill the table with a single descriptor, then overwrite some spots with the he correct textures
				If a spot is unused, then a default descriptor will be still bound, but not used in the shaders.
				Since the renderer creates a texture pool that can be used by the render tasks, and
				the texture pool also has default textures for albedo/roughness/etc... one of those textures is a good
				candidate for this.
				*/
				{
					auto texture_pool = rs.GetDefaultTexturePool();

					if (texture_pool == nullptr)
					{
						LOGC("ERROR: Texture Pool in Raytracing Task is nullptr. This is not supposed to happen.");
					}

					auto texture_handle = texture_pool->GetDefaultAlbedo();
					auto* texture_resource = static_cast<wr::d3d12::TextureResource*>(texture_pool->GetTexture(texture_handle.m_id));

					size_t num_textures_in_heap = COMPILATION_EVAL(rs_layout::GetSize(params::full_raytracing, params::FullRaytracingE::TEXTURES));
					unsigned int heap_loc_start = COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::TEXTURES));

					for (size_t i = 0; i < num_textures_in_heap; ++i)
					{
						d3d12::SetRTShaderSRV(cmd_list, 0, heap_loc_start + i, texture_resource);
					}
				}

				// Fill descriptor heap with actual textures used by the scene
				for (auto handle : as_build_data.out_material_handles)
				{
					auto* material_internal = handle->m_pool->GetMaterial(handle->m_id);

					auto set_srv = [&data, material_internal, cmd_list](auto texture_handle)
					{
						auto* texture_internal = static_cast<wr::d3d12::TextureResource*>(texture_handle.m_pool->GetTexture(texture_handle.m_id));

						d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::TEXTURES)) + texture_handle.m_id, texture_internal);
					};

					set_srv(material_internal->GetAlbedo());
					set_srv(material_internal->GetMetallic());
					set_srv(material_internal->GetNormal());
					set_srv(material_internal->GetRoughness());
				}

				// Get light buffer
				{
					if (static_cast<D3D12StructuredBufferHandle*>(scene_graph.GetLightBuffer())->m_native->m_states[frame_idx] != ResourceState::NON_PIXEL_SHADER_RESOURCE)
					{
						static_cast<D3D12StructuredBufferPool*>(scene_graph.GetLightBuffer()->m_pool)->SetBufferState(scene_graph.GetLightBuffer(), ResourceState::NON_PIXEL_SHADER_RESOURCE);
					}

					DescriptorAllocation light_alloc = std::move(as_build_data.out_allocator->Allocate());
					d3d12::DescHeapCPUHandle light_handle = light_alloc.GetDescriptorHandle();
					d3d12::CreateSRVFromStructuredBuffer(static_cast<D3D12StructuredBufferHandle*>(scene_graph.GetLightBuffer())->m_native, light_handle, frame_idx);

					d3d12::DescHeapCPUHandle light_handle2 = light_alloc.GetDescriptorHandle();
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::LIGHTS)), light_handle2);
				}

				// Get skybox
				if (scene_graph.m_skybox.has_value())
				{
					auto skybox_t = static_cast<d3d12::TextureResource*>(scene_graph.m_skybox.value().m_pool->GetTexture(scene_graph.m_skybox.value().m_id));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::SKYBOX)), skybox_t);
				}

				// Get Environment Map
				if (scene_graph.m_skybox.has_value())
				{
					auto irradiance_t = static_cast<d3d12::TextureResource*>(scene_graph.GetCurrentSkybox()->m_irradiance->m_pool->GetTexture(scene_graph.GetCurrentSkybox()->m_irradiance->m_id));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::IRRADIANCE_MAP)), irradiance_t);
				}

				// Update offset data
				n_render_system.m_raytracing_offset_sb_pool->Update(as_build_data.out_sb_offset_handle, (void*)as_build_data.out_offsets.data(), sizeof(temp::RayTracingOffset_CBData) * as_build_data.out_offsets.size(), 0);

				// Update material data
				if (as_build_data.out_materials_require_update)
				{
					n_render_system.m_raytracing_material_sb_pool->Update(as_build_data.out_sb_material_handle, (void*)as_build_data.out_materials.data(), sizeof(temp::RayTracingMaterial_CBData) * as_build_data.out_materials.size(), 0);
				}

				// Update camera constant buffer
				auto camera = scene_graph.GetActiveCamera();
				temp::RayTracingCamera_CBData cam_data;
				cam_data.m_view = camera->m_view;
				cam_data.m_camera_position = camera->m_position;
				cam_data.m_inverse_view_projection = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, camera->m_view * camera->m_projection));
				cam_data.metal = n_render_system.temp_metal;
				cam_data.light_radius = n_render_system.light_radius;
				cam_data.intensity = n_render_system.temp_intensity;
				n_render_system.m_camera_pool->Update(data.out_cb_camera_handle, sizeof(temp::RayTracingCamera_CBData), 0, frame_idx, (std::uint8_t*)&cam_data); // FIXME: Uhh wrong pool?

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

				unsigned int verts_loc = rs_layout::GetHeapLoc(params::rt_hybrid, params::RTHybridE::VERTICES);
				d3d12::BindComputeShaderResourceView(cmd_list, as_build_data.out_scene_vb->m_buffer, verts_loc);

//#ifdef _DEBUG
				CreateShaderTables(device, data, frame_idx);
//#endif

				d3d12::DispatchRays(cmd_list, data.out_hitgroup_shader_table[frame_idx], data.out_miss_shader_table[frame_idx], data.out_raygen_shader_table[frame_idx], window->GetWidth(), window->GetHeight(), 1, 0);
			}
		}

		inline void DestroyRaytracingTask(FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			if (!resize)
			{
				auto& data = fg.GetData<RaytracingData>(handle);

				// Small hack to force the allocations to go out of scope, which will tell the allocator to free them
				DescriptorAllocation temp1 = std::move(data.out_uav_from_rtv);
			}
		}

	} /* internal */

	inline void AddRaytracingTask(FrameGraph& frame_graph)
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
			RenderTargetProperties::RTVFormats({ Format::R32G32B32A32_FLOAT }),
			RenderTargetProperties::NumRTVFormats(1),
			RenderTargetProperties::Clear(true),
			RenderTargetProperties::ClearDepth(true)
		};

		RenderTaskDesc desc;
		desc.m_setup_func = [](RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize) {
			internal::SetupRaytracingTask(rs, fg, handle, resize);
		};
		desc.m_execute_func = [](RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle) {
			internal::ExecuteRaytracingTask(rs, fg, sg, handle);
		};
		desc.m_destroy_func = [](FrameGraph& fg, RenderTaskHandle handle, bool resize) {
			internal::DestroyRaytracingTask(fg, handle, resize);
		};
		desc.m_name = "Raytracing";
		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::COMPUTE;
		desc.m_allow_multithreading = true;

		frame_graph.AddTask<RaytracingData>(desc);
	}

} /* wr */