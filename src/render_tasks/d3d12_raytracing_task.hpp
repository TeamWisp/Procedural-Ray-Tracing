// Copyright 2019 Breda University of Applied Sciences and Team Wisp (Viktor Zoutman, Emilio Laiso, Jens Hagen, Meine Zeinstra, Tahar Meijs, Koen Buitenhuis, Niels Brunekreef, Darius Bouma, Florian Schut)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../rt_pipeline_registry.hpp"
#include "../root_signature_registry.hpp"

#include "../render_tasks/d3d12_build_acceleration_structures.hpp"
#include "../engine_registry.hpp"

#include "../scene_graph/skybox_node.hpp"
#include "../render_tasks/d3d12_deferred_main.hpp"
#include "../imgui_tools.hpp"
#include "../render_tasks/d3d12_cubemap_convolution.hpp"

namespace wr
{
	struct RaytracingData
	{
		d3d12::AccelerationStructure out_tlas;

		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_raygen_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_miss_shader_table = { nullptr, nullptr, nullptr };
		std::array<d3d12::ShaderTable*, d3d12::settings::num_back_buffers> out_hitgroup_shader_table = { nullptr, nullptr, nullptr };
		d3d12::StateObject* out_state_object = nullptr;
		d3d12::RootSignature* out_root_signature = nullptr;
		D3D12ConstantBufferHandle* out_cb_camera_handle = nullptr;

		DescriptorAllocation out_uav_from_rtv;

		bool tlas_requires_init = false;
		DirectX::XMVECTOR last_cam_pos = {};
		DirectX::XMVECTOR last_cam_rot = {};
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
				std::uint32_t shader_record_count = 1;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device);
				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "RaygenEntry");

				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				// Create Table
				data.out_raygen_shader_table[frame_idx] = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_raygen_shader_table[frame_idx], shader_record);
			}

			// Miss Shader Table
			{
				// Create Record(s)
				std::uint32_t shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device);

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
				std::uint32_t shader_record_count = 2;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device);

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
				// Pipeline State Object
				auto& rt_registry = RTPipelineRegistry::Get();
				data.out_state_object = static_cast<d3d12::StateObject*>(rt_registry.Find(state_objects::state_object));

				// Root Signature
				auto& rs_registry = RootSignatureRegistry::Get();
				data.out_root_signature = static_cast<d3d12::RootSignature*>(rs_registry.Find(root_signatures::rt_test_global));

				auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();

				// Camera constant buffer
				data.out_cb_camera_handle = static_cast<D3D12ConstantBufferHandle*>(n_render_system.m_raytracing_cb_pool->Create(sizeof(temp::RayTracingCamera_CBData)));

				data.out_uav_from_rtv = std::move(as_build_data.out_allocator->Allocate());

				data.tlas_requires_init = true;

				CreateShaderTables(device, data, 0);
				CreateShaderTables(device, data, 1);
				CreateShaderTables(device, data, 2);
			}

			for (auto frame_idx = 0; frame_idx < 1; frame_idx++)
			{
				d3d12::DescHeapCPUHandle desc_handle = data.out_uav_from_rtv.GetDescriptorHandle();
				d3d12::CreateUAVFromSpecificRTV(n_render_target, desc_handle, frame_idx, n_render_target->m_create_info.m_rtv_formats[frame_idx]);
			}
		}

		inline void ExecuteRaytracingTask(RenderSystem& rs, FrameGraph& fg, SceneGraph& scene_graph, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto window = n_render_system.m_window.value();
			auto render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);
			auto device = n_render_system.m_device;
			auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
			auto& data = fg.GetData<RaytracingData>(handle);
			auto& as_build_data = fg.GetPredecessorData<wr::ASBuildData>();
			fg.WaitForPredecessorTask<CubemapConvolutionTaskData>();
			auto frame_idx = n_render_system.GetFrameIdx();
			float scalar = 1.0f;

			// Rebuild acceleratrion structure a 2e time for fallback
			if (d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK)
			{
				d3d12::CreateOrUpdateTLAS(device, cmd_list, data.tlas_requires_init, data.out_tlas, as_build_data.out_blas_list, frame_idx);

				d3d12::UAVBarrierAS(cmd_list, as_build_data.out_tlas, frame_idx);
			}

			if (n_render_system.m_render_window.has_value())
			{

				d3d12::BindRaytracingPipeline(cmd_list, data.out_state_object, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);

				auto uav_from_rtv_handle = data.out_uav_from_rtv.GetDescriptorHandle();
				d3d12::SetRTShaderUAV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::OUTPUT)), uav_from_rtv_handle);
				auto scene_ib_handle = as_build_data.out_scene_ib_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::INDICES)), scene_ib_handle);
				auto scene_mat_handle = as_build_data.out_scene_mat_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::MATERIALS)), scene_mat_handle);
				auto scene_offset_handle = as_build_data.out_scene_offset_alloc.GetDescriptorHandle();
				d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::OFFSETS)), scene_offset_handle);

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

				/*
				To keep the CopyDescriptors function happy, we need to fill the descriptor table with valid descriptors
				We fill the table with a single descriptor, then overwrite some spots with the he correct textures
				If a spot is unused, then a default descriptor will be still bound, but not used in the shaders.
				Since the renderer creates a texture pool that can be used by the render tasks, and
				the texture pool also has default textures for albedo/roughness/etc... one of those textures is a good
				candidate for this.
				*/
				{
					auto texture_handle = n_render_system.GetDefaultAlbedo();
					auto* texture_resource = static_cast<wr::d3d12::TextureResource*>(texture_handle.m_pool->GetTextureResource(texture_handle));

					size_t num_textures_in_heap = COMPILATION_EVAL(rs_layout::GetSize(params::full_raytracing, params::FullRaytracingE::TEXTURES));
					unsigned int heap_loc_start = COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::TEXTURES));

					for (size_t i = 0; i < num_textures_in_heap; ++i)
					{
						d3d12::SetRTShaderSRV(cmd_list, 0, static_cast<std::uint32_t>(heap_loc_start + i), texture_resource);
					}
				}

				// Fill descriptor heap with textures used by the scene
				for (auto material_handle : as_build_data.out_material_handles)
				{
					auto* material_internal = material_handle.m_pool->GetMaterial(material_handle);

					auto set_srv = [&data, material_internal, cmd_list](auto texture_handle)
					{
						auto* texture_internal = static_cast<wr::d3d12::TextureResource*>(texture_handle.m_pool->GetTextureResource(texture_handle));

						d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::TEXTURES)) + static_cast<std::uint32_t>(texture_handle.m_id), texture_internal);
					};

					std::array<TextureType, static_cast<size_t>(TextureType::COUNT)> types = { TextureType::ALBEDO, TextureType::NORMAL,
																							   TextureType::ROUGHNESS, TextureType::METALLIC,
																							   TextureType::EMISSIVE, TextureType::AO };

					for (auto t : types)
					{
						if (material_internal->HasTexture(t))
							set_srv(material_internal->GetTexture(t));
					}
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
				if (SkyboxNode* skybox = scene_graph.GetCurrentSkybox().get())
				{
					auto skybox_t = static_cast<d3d12::TextureResource*>(skybox->m_skybox->m_pool->GetTextureResource(skybox->m_skybox.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::SKYBOX)), skybox_t);

					// Get Environment Map
					auto irradiance_t = static_cast<d3d12::TextureResource*>(skybox->m_irradiance->m_pool->GetTextureResource(skybox->m_irradiance.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::IRRADIANCE_MAP)), irradiance_t);
				}

				{
					// Get brdf lookup texture
					auto brdf_lut_texture = static_cast<d3d12::TextureResource*>(n_render_system.m_brdf_lut.value().m_pool->GetTextureResource(n_render_system.m_brdf_lut.value()));
					d3d12::SetRTShaderSRV(cmd_list, 0, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::full_raytracing, params::FullRaytracingE::BRDF_LUT)), brdf_lut_texture);
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
				cam_data.focal_radius = camera->m_f_number;
				cam_data.focal_length = camera->m_focal_length;
				cam_data.frame_idx = ++n_render_system.temp_rough;
				cam_data.intensity = n_render_system.temp_intensity;
				n_render_system.m_camera_pool->Update(data.out_cb_camera_handle, sizeof(temp::RayTracingCamera_CBData), 0, frame_idx, (std::uint8_t*)&cam_data); // FIXME: Uhh wrong pool?

				d3d12::BindDescriptorHeap(cmd_list, cmd_list->m_rt_descriptor_heap.get()->GetHeap(), DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV, frame_idx, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);
				d3d12::BindDescriptorHeaps(cmd_list, d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK);
				d3d12::BindComputeConstantBuffer(cmd_list, data.out_cb_camera_handle->m_native, 2, frame_idx);

				if (d3d12::GetRaytracingType(device) == RaytracingType::NATIVE)
				{
					d3d12::BindComputeShaderResourceView(cmd_list, as_build_data.out_tlas.m_natives[frame_idx], 1);
				}
				else if (d3d12::GetRaytracingType(device) == RaytracingType::FALLBACK)
				{
					cmd_list->m_native_fallback->SetTopLevelAccelerationStructure(0, as_build_data.out_tlas.m_fallback_tlas_ptr);
				}

				/*unsigned int verts_loc = rs_layout::GetHeapLoc(params::FullRaytracingE, params::FullRaytracingE::VERTICES);
				This should be the Parameter index not the heap location, it was only working due to a ridiculous amount of luck and should be fixed, or we completely missunderstand this stuff...
				Much love, Meine and Florian*/
				if (!as_build_data.out_blas_list.empty())
				{
					d3d12::BindComputeShaderResourceView(cmd_list, as_build_data.out_scene_vb->m_buffer, 3);
				}

//#ifdef _DEBUG
				CreateShaderTables(device, data, frame_idx);
//#endif
				scalar = fg.GetRenderTargetResolutionScale(handle);

				d3d12::DispatchRays(cmd_list,
					data.out_hitgroup_shader_table[frame_idx], 
					data.out_miss_shader_table[frame_idx], 
					data.out_raygen_shader_table[frame_idx], 
					static_cast<std::uint32_t>(std::ceil(scalar * d3d12::GetRenderTargetWidth(render_target))),
					static_cast<std::uint32_t>(std::ceil(scalar * d3d12::GetRenderTargetHeight(render_target))),
					1, 
					0);
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
			RenderTargetProperties::RTVFormats({wr::Format::R16G16B16A16_UNORM }),
			RenderTargetProperties::NumRTVFormats(1),
			RenderTargetProperties::Clear(false),
			RenderTargetProperties::ClearDepth(false),
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

		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::COMPUTE;
		desc.m_allow_multithreading = true;

		frame_graph.AddTask<RaytracingData>(desc, L"Full Raytracing");
	}

} /* wr */