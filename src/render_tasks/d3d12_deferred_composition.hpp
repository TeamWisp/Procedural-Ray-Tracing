#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_resource_pool_constant_buffer.hpp"
#include "../d3d12/d3d12_resource_pool_structured_buffer.hpp"
#include "../frame_graph/render_task.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../d3d12/d3d12_pipeline_registry.hpp"
#include "../engine_registry.hpp"

#include "../render_tasks/d3d12_deferred_main.hpp"

namespace wr
{
	struct DeferredCompositionTaskData
	{
		D3D12Pipeline* in_pipeline;
		d3d12::DescriptorHeap* out_srv_heap;
		d3d12::RenderTarget* out_deferred_main_rt;

		std::array<d3d12::CommandList*, d3d12::settings::num_back_buffers> out_bundle_cmd_lists;
		bool out_requires_bundle_recording;
	};
	using DeferredCompositionRenderTask_t = RenderTask<DeferredCompositionTaskData>;

	namespace internal
	{

		inline void RecordDrawCommands(D3D12RenderSystem& render_system, d3d12::CommandList* cmd_list, d3d12::HeapResource* camera_cb, DeferredCompositionTaskData const & data, unsigned int frame_idx)
		{
			d3d12::BindPipeline(cmd_list, data.in_pipeline->m_native);
			d3d12::SetPrimitiveTopology(cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

			d3d12::BindDescriptorHeaps(cmd_list, { data.out_srv_heap }, frame_idx);

			d3d12::BindConstantBuffer(cmd_list, camera_cb, 0, frame_idx);

			auto gpu_handle = d3d12::GetGPUHandle(data.out_srv_heap, frame_idx);
			d3d12::BindDescriptorTable(cmd_list, gpu_handle, 1);

			d3d12::BindVertexBuffer(cmd_list,
				render_system.m_fullscreen_quad_vb,
				0,
				render_system.m_fullscreen_quad_vb->m_size,
				render_system.m_fullscreen_quad_vb->m_stride_in_bytes);

			d3d12::Draw(cmd_list, 4, 1);
		}

		inline void SetupDeferredTask(RenderSystem & render_system, DeferredCompositionRenderTask_t & task, DeferredCompositionTaskData & data)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);
			auto* fg = task.GetFrameGraph();

			auto& ps_registry = PipelineRegistry::Get();
			data.in_pipeline = (D3D12Pipeline*)ps_registry.Find(pipelines::deferred_composition);

			d3d12::desc::DescriptorHeapDesc heap_desc;
			heap_desc.m_shader_visible = true;
			heap_desc.m_num_descriptors = 4;
			heap_desc.m_type = DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.m_versions = d3d12::settings::num_back_buffers;
			data.out_srv_heap = d3d12::CreateDescriptorHeap(n_render_system.m_device, heap_desc);

			for (uint32_t i = 0; i < d3d12::settings::num_back_buffers; ++i)
			{

				auto cpu_handle = d3d12::GetCPUHandle(data.out_srv_heap, i);

				auto deferred_main_data = fg->GetData<DeferredMainTaskData>();
				auto deferred_main_rt = data.out_deferred_main_rt = static_cast<D3D12RenderTarget*>(deferred_main_data.m_render_target);
				d3d12::CreateSRVFromRTV(deferred_main_rt, cpu_handle, 2, deferred_main_data.m_rt_properties.m_rtv_formats.data());
				d3d12::CreateSRVFromDSV(deferred_main_rt, cpu_handle);

			}
		}

		inline void ExecuteDeferredTask(RenderSystem & render_system, DeferredCompositionRenderTask_t & task, SceneGraph & scene_graph, DeferredCompositionTaskData & data)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(render_system);

			if (n_render_system.m_render_window.has_value())
			{
				const auto cmd_list = task.GetCommandList<D3D12CommandList>().first;
				const auto viewport = n_render_system.m_viewport;
				const auto camera_cb = static_cast<D3D12ConstantBufferHandle*>(scene_graph.GetActiveCamera()->m_camera_cb);
				const auto frame_idx = n_render_system.GetFrameIdx();

				auto cpu_handle = d3d12::GetCPUHandle(data.out_srv_heap, frame_idx, 3);
				d3d12::CreateSRVFromStructuredBuffer(static_cast<D3D12StructuredBufferHandle*>(scene_graph.GetLightBuffer())->m_native, cpu_handle, frame_idx);

				if constexpr (d3d12::settings::use_bundles)
				{
					// Record all bundles again if required.
					if (data.out_requires_bundle_recording)
					{
						for (auto i = 0; i < data.out_bundle_cmd_lists.size(); i++)
						{
							d3d12::Begin(data.out_bundle_cmd_lists[i], 0);
							RecordDrawCommands(n_render_system, data.out_bundle_cmd_lists[i], static_cast<D3D12ConstantBufferHandle*>(camera_cb)->m_native, data, i);
							d3d12::End(data.out_bundle_cmd_lists[i]);
						}
						data.out_requires_bundle_recording = false;
					}
				}

				//Render deferred

				d3d12::TransitionDepth(cmd_list, data.out_deferred_main_rt, ResourceState::DEPTH_WRITE, ResourceState::PIXEL_SHADER_RESOURCE);

				d3d12::BindViewport(cmd_list, viewport);

				if constexpr (d3d12::settings::use_bundles)
				{
					d3d12::BindDescriptorHeaps(cmd_list, { data.out_srv_heap }, frame_idx);
					d3d12::ExecuteBundle(cmd_list, data.out_bundle_cmd_lists[frame_idx]);
				}
				else
				{
					RecordDrawCommands(n_render_system, cmd_list, static_cast<D3D12ConstantBufferHandle*>(camera_cb)->m_native, data, frame_idx);
				}

				d3d12::TransitionDepth(cmd_list, data.out_deferred_main_rt, ResourceState::PIXEL_SHADER_RESOURCE, ResourceState::DEPTH_WRITE);
			}
		}


		inline void ResizeDeferredTask(DeferredCompositionRenderTask_t & task, DeferredCompositionTaskData & data, std::uint32_t width, std::uint32_t height)
		{
			d3d12::Destroy(data.out_srv_heap);
		}

		inline void DestroyTestTask(DeferredCompositionRenderTask_t & task, DeferredCompositionTaskData& data)
		{
			d3d12::Destroy(data.out_srv_heap);
		}

	} /* internal */


	//! Used to create a new defferred task.
	[[nodiscard]] inline std::unique_ptr<DeferredCompositionRenderTask_t> GetDeferredCompositionTask()
	{
		auto ptr = std::make_unique<DeferredCompositionRenderTask_t>(nullptr, "Deferred Render Task", RenderTaskType::DIRECT, true,
			RenderTargetProperties{
				true,
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::nullopt,
				false,
				Format::UNKNOWN,
				{ Format::R8G8B8A8_UNORM },
				1,
				true,
				true
			},
			[](RenderSystem & render_system, DeferredCompositionRenderTask_t & task, DeferredCompositionTaskData & data, bool) { internal::SetupDeferredTask(render_system, task, data); },
			[](RenderSystem & render_system, DeferredCompositionRenderTask_t & task, SceneGraph & scene_graph, DeferredCompositionTaskData & data) { internal::ExecuteDeferredTask(render_system, task, scene_graph, data); },
			[](DeferredCompositionRenderTask_t & task, DeferredCompositionTaskData & data, bool) { internal::DestroyTestTask(task, data); }
		);

		return ptr;
	}

} /* wr */