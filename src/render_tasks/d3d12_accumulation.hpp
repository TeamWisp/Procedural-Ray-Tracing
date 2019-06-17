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

#include "../frame_graph/frame_graph.hpp"
#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "d3d12_raytracing_task.hpp"
#include "d3d12_path_tracer.hpp"

namespace wr
{
	struct AccumulationData
	{
		d3d12::RenderTarget* out_source_rt = nullptr;
		d3d12::PipelineState* out_pipeline = nullptr;

		DescriptorAllocator* out_allocator = nullptr;
		DescriptorAllocation out_allocation;
	};

	namespace internal
	{

		template<typename T>
		inline void SetupAccumulationTask(RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<AccumulationData>(handle);
			auto n_render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);

			//Retrieve the texture pool from the render system. It will be used to allocate temporary cpu visible descriptors
			std::shared_ptr<D3D12TexturePool> texture_pool = std::static_pointer_cast<D3D12TexturePool>(n_render_system.m_texture_pools[0]);
			if (!texture_pool)
			{
				LOGC("Texture pool is nullptr. This shouldn't happen as the render system should always create the first texture pool");
			}

			data.out_allocator = texture_pool->GetAllocator(DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV);
			data.out_allocation = data.out_allocator->Allocate(2);

			auto& ps_registry = PipelineRegistry::Get();
			data.out_pipeline = ((d3d12::PipelineState*)ps_registry.Find(pipelines::accumulation));

			auto source_rt = data.out_source_rt = static_cast<d3d12::RenderTarget*>(fg.GetPredecessorRenderTarget<T>());

			for (auto frame_idx = 0; frame_idx < 1; frame_idx++)
			{
				// Destination
				{
					constexpr unsigned int dest_idx = rs_layout::GetHeapLoc(params::accumulation, params::AccumulationE::DEST);
					auto cpu_handle = data.out_allocation.GetDescriptorHandle(dest_idx);
					d3d12::CreateUAVFromSpecificRTV(n_render_target, cpu_handle, frame_idx, n_render_target->m_create_info.m_rtv_formats[frame_idx]);
				}
				// Source
				{
					constexpr unsigned int source_idx = rs_layout::GetHeapLoc(params::accumulation, params::AccumulationE::SOURCE);
					auto cpu_handle = data.out_allocation.GetDescriptorHandle(source_idx);
					d3d12::CreateSRVFromSpecificRTV(source_rt, cpu_handle, frame_idx, source_rt->m_create_info.m_rtv_formats[frame_idx]);
				}
			}
		}

		inline void ExecuteAccumulationTask(RenderSystem& rs, FrameGraph& fg, SceneGraph&, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<AccumulationData>(handle);
			auto frame_idx = n_render_system.GetFrameIdx();
			auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);

			d3d12::BindComputePipeline(cmd_list, data.out_pipeline);

			constexpr unsigned int uav_idx = rs_layout::GetHeapLoc(params::accumulation, params::AccumulationE::DEST);
			d3d12::DescHeapCPUHandle uav_handle = data.out_allocation.GetDescriptorHandle(uav_idx);
			d3d12::SetShaderUAV(cmd_list, 0, uav_idx, uav_handle);

			constexpr unsigned int srv_idx = rs_layout::GetHeapLoc(params::accumulation, params::AccumulationE::SOURCE);
			d3d12::DescHeapCPUHandle srv_handle = data.out_allocation.GetDescriptorHandle(srv_idx);
			d3d12::SetShaderSRV(cmd_list, 0, srv_idx, srv_handle);

			d3d12::BindDescriptorHeaps(cmd_list);

			{
				auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(data.out_source_rt->m_render_targets[frame_idx % 1 /*versions*/]);
				cmd_list->m_native->ResourceBarrier(1, &barrier);
			}

			fg.WaitForPredecessorTask<PathTracerData>();

			float samples = n_render_system.temp_rough;
			d3d12::BindCompute32BitConstants(cmd_list, &samples, 1, 0, 1);

			d3d12::Dispatch(cmd_list,
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Width / 16.f)),
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Height / 16.f)),
				1);
		}

		inline void DestroyAccumulation(FrameGraph& fg, RenderTaskHandle handle, bool)
		{
			auto& data = fg.GetData<AccumulationData>(handle);

			// Small hack to force the allocations to go out of scope, which will tell the texture pool to free them
			DescriptorAllocation temp = std::move(data.out_allocation);
		}

	} /* internal */

	template<typename T>
	inline void AddAccumulationTask(FrameGraph& frame_graph)
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
		};

		RenderTaskDesc desc;
		desc.m_setup_func = [](RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize) {
			internal::SetupAccumulationTask<T>(rs, fg, handle, resize);
		};
		desc.m_execute_func = [](RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle) {
			internal::ExecuteAccumulationTask(rs, fg, sg, handle);
		};
		desc.m_destroy_func = [](FrameGraph& fg, RenderTaskHandle handle, bool resize) {
			internal::DestroyAccumulation(fg, handle, resize);
		};
		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::COMPUTE;
		desc.m_allow_multithreading = true;

		frame_graph.AddTask<AccumulationData>(desc, L"Accumulation Task", FG_DEPS<T>());
	}

} /* wr */
