#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../frame_graph/render_task.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../d3d12/d3d12_rt_pipeline_registry.hpp"
#include "../d3d12/d3d12_root_signature_registry.hpp"
#include "../engine_registry.hpp"

#include "../render_tasks/d3d12_deferred_main.hpp"


namespace wr
{
	struct RaytracingData
	{
		d3d12::DescriptorHeap* out_rt_heap;

		d3d12::ShaderTable* out_raygen_shader_table;
		d3d12::ShaderTable* out_miss_shader_table;
		d3d12::ShaderTable* out_hitgroup_shader_table;
		d3d12::StateObject* out_state_object;
		d3d12::RootSignature* out_root_signature;
		D3D12ConstantBufferHandle* out_cb_camera_handle;

		bool out_init;
	};
	using RaytracingTask = RenderTask<RaytracingData>;

	namespace internal
	{

		inline void SetupRaytracingTask(RenderSystem & render_system, RaytracingTask & task, RaytracingData & data)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);
			auto& device = n_render_system.m_device;
			auto n_render_target = static_cast<d3d12::RenderTarget*>(task.GetRenderTarget<RenderTarget>());

			n_render_target->m_render_targets[0]->SetName(L"Raytracing Target");

			data.out_init = true;

			// top level, bottom level and output buffer. (even though I don't use bottom level.)
			d3d12::desc::DescriptorHeapDesc heap_desc;
			heap_desc.m_num_descriptors = 3;
			heap_desc.m_type = DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.m_shader_visible = true;
			heap_desc.m_versions = 1;
			data.out_rt_heap = d3d12::CreateDescriptorHeap(device, heap_desc);
			SetName(data.out_rt_heap, L"Raytracing Task Descriptor Heap");

			auto cpu_handle = d3d12::GetCPUHandle(data.out_rt_heap, 0);
			d3d12::Offset(cpu_handle, 0, data.out_rt_heap->m_increment_size);
			d3d12::CreateUAVFromRTV(n_render_target, cpu_handle, 1, n_render_target->m_create_info.m_rtv_formats.data());

			// Camera constant buffer
			data.out_cb_camera_handle = static_cast<D3D12ConstantBufferHandle*>(n_render_system.m_raytracing_cb_pool->Create(sizeof(temp::ProjectionView_CBData)));

			// Pipeline State Object
			auto rt_registry = RTPipelineRegistry::Get();
			data.out_state_object = static_cast<D3D12StateObject*>(rt_registry.Find(state_objects::state_object))->m_native;

			// Root Signature
			auto rs_registry = RootSignatureRegistry::Get();
			data.out_root_signature = static_cast<D3D12RootSignature*>(rs_registry.Find(root_signatures::rt_test_global))->m_native;

			// Raygen Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 1;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);
				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "RaygenEntry");

				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				// Create Table
				data.out_raygen_shader_table = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_raygen_shader_table, shader_record);
			}

			// Miss Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 1;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);
				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "MissEntry");

				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				// Create Table
				data.out_miss_shader_table = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_miss_shader_table, shader_record);
			}

			// Hit Group Shader Table
			{
				// Create Record(s)
				UINT shader_record_count = 1;
				auto shader_identifier_size = d3d12::GetShaderIdentifierSize(device, data.out_state_object);
				auto shader_identifier = d3d12::GetShaderIdentifier(device, data.out_state_object, "MyHitGroup");

				auto shader_record = d3d12::CreateShaderRecord(shader_identifier, shader_identifier_size);

				// Create Table
				data.out_hitgroup_shader_table = d3d12::CreateShaderTable(device, shader_record_count, shader_identifier_size);
				d3d12::AddShaderRecord(data.out_hitgroup_shader_table, shader_record);
			}
		}

		d3d12::AccelerationStructure tlas;
		std::vector<std::pair<d3d12::AccelerationStructure, DirectX::XMMATRIX>> blas_list;

		inline void ExecuteRaytracingTask(RenderSystem & render_system, RaytracingTask & task, SceneGraph & scene_graph, RaytracingData & data)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);
			auto device = n_render_system.m_device;
			auto cmd_list = task.GetCommandList<d3d12::CommandList>().first;

			auto frame_idx = n_render_system.GetFrameIdx();

			if (n_render_system.m_render_window.has_value())
			{
				// Initialize requirements
				if (data.out_init)
				{
					auto model_pools = n_render_system.m_model_pools;
					// Transition all model pools for accel structure creation
					for (auto& pool : model_pools)
					{
						d3d12::Transition(cmd_list, pool->GetVertexStagingBuffer(), ResourceState::VERTEX_AND_CONSTANT_BUFFER, ResourceState::NON_PIXEL_SHADER_RESOURCE);
						d3d12::Transition(cmd_list, pool->GetIndexStagingBuffer(), ResourceState::INDEX_BUFFER, ResourceState::NON_PIXEL_SHADER_RESOURCE);
					}

					// Create Geometry from scene graph
					{
						scene_graph.Optimize();
						auto batches = scene_graph.GetBatches();

						for (auto& batch : batches)
						{
							auto n_model_pool = static_cast<D3D12ModelPool*>(batch.first->m_model_pool);
							auto vb = n_model_pool->GetVertexStagingBuffer();
							auto ib = n_model_pool->GetIndexStagingBuffer();
							auto model = batch.first;

							for (auto& mesh : model->m_meshes)
							{
								auto n_mesh = static_cast<D3D12ModelPool*>(model->m_model_pool)->GetMeshData(mesh.first->id);

								d3d12::desc::GeometryDesc obj;
								obj.index_buffer = ib;
								obj.vertex_buffer = vb;

								obj.m_indices_offset = n_mesh->m_index_staging_buffer_offset;
								obj.m_num_indices = n_mesh->m_index_count;
								obj.m_vertices_offset = n_mesh->m_vertex_staging_buffer_offset;
								obj.m_num_vertices = n_mesh->m_vertex_count;
								obj.m_vertex_stride = n_mesh->m_vertex_staging_buffer_stride;

								auto blas = d3d12::CreateBottomLevelAccelerationStructures(device, cmd_list, data.out_rt_heap, { obj });
								cmd_list->m_native->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(blas.m_native));
								blas.m_native->SetName(L"Bottomlevelaccel");

								for (auto i = 0; i < batch.second.num_instances; i++)
								{
									auto transform = batch.second.data.objects[i].m_model;
									blas_list.push_back({ blas, transform });
								}
							}
						}
					}

					tlas = d3d12::CreateTopLevelAccelerationStructure(device, cmd_list, data.out_rt_heap, blas_list);
					tlas.m_native->SetName(L"Highlevelaccel");

					data.out_init = false;
				}

				// Update camera cb
				auto camera = scene_graph.GetActiveCamera();
				temp::RayTracingCamera_CBData cam_data;
				cam_data.m_camera_position = camera->m_position;
				cam_data.m_inverse_view_projection = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, camera->m_view * camera->m_projection));
				n_render_system.m_camera_pool->Update(data.out_cb_camera_handle, sizeof(temp::RayTracingCamera_CBData), 0, frame_idx, (std::uint8_t*)&cam_data);

				d3d12::BindRaytracingPipeline(cmd_list, data.out_state_object);

				d3d12::BindDescriptorHeaps(cmd_list, { data.out_rt_heap }, 0);
				d3d12::BindComputeDescriptorTable(cmd_list, d3d12::GetGPUHandle(data.out_rt_heap, 0), 0);
				d3d12::BindComputeConstantBuffer(cmd_list, data.out_cb_camera_handle->m_native, 2, 0);
				d3d12::BindComputeShaderResourceView(cmd_list, tlas.m_native, 1);

				d3d12::DispatchRays(cmd_list, data.out_hitgroup_shader_table, data.out_miss_shader_table, data.out_raygen_shader_table, 1280, 720, 1);
			}
		}

		inline void DestroyRaytracingTask(RaytracingTask & task, RaytracingData& data)
		{
		}

	} /* internal */


	//! Used to create a new defferred task.
	[[nodiscard]] inline std::unique_ptr<RaytracingTask> GetRaytracingTask()
	{
		auto ptr = std::make_unique<RaytracingTask>(nullptr, "Deferred Render Task", RenderTaskType::COMPUTE, true,
			RenderTargetProperties{
				RenderTargetProperties::IsRenderWindow(false),
				RenderTargetProperties::Width(std::nullopt),
				RenderTargetProperties::Height(std::nullopt),
				RenderTargetProperties::ExecuteResourceState(ResourceState::UNORDERED_ACCESS),
				RenderTargetProperties::FinishedResourceState(ResourceState::COPY_SOURCE),
				RenderTargetProperties::CreateDSVBuffer(false),
				RenderTargetProperties::DSVFormat(Format::UNKNOWN),
				RenderTargetProperties::RTVFormats({ Format::R8G8B8A8_UNORM }),
				RenderTargetProperties::NumRTVFormats(1),
			},
			[](RenderSystem & render_system, RaytracingTask & task, RaytracingData & data, bool) { internal::SetupRaytracingTask(render_system, task, data); },
			[](RenderSystem & render_system, RaytracingTask & task, SceneGraph & scene_graph, RaytracingData & data) { internal::ExecuteRaytracingTask(render_system, task, scene_graph, data); },
			[](RaytracingTask & task, RaytracingData & data, bool) { internal::DestroyRaytracingTask(task, data); }
		);

		return ptr;
	}

} /* wr */
