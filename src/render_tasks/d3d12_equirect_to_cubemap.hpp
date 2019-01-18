#pragma once

#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../d3d12/d3d12_model_pool.hpp"
#include "../d3d12/d3d12_resource_pool_texture.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../d3d12/d3d12_pipeline_registry.hpp"
#include "../engine_registry.hpp"

#include "../platform_independend_structs.hpp"
#include "d3d12_imgui_render_task.hpp"
#include "../scene_graph/camera_node.hpp"

namespace wr
{

	struct EquirectToCubemapTaskData
	{
		D3D12Pipeline* in_pipeline;

		d3d12::TextureResource* in_equirectangular;
		d3d12::TextureResource* out_cubemap;
	};
	
	namespace internal
	{

		inline void SetupEquirectToCubemapTask(RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<EquirectToCubemapTaskData>(handle);

			auto& ps_registry = PipelineRegistry::Get();
			data.in_pipeline = (D3D12Pipeline*)ps_registry.Find(pipelines::basic_deferred); //TODO: pipelines::equirect_to_cubemap
		}

		inline void ExecuteEquirectToCubemapTask(RenderSystem& rs, FrameGraph& fg, SceneGraph& scene_graph, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<EquirectToCubemapTaskData>(handle);

			if (n_render_system.m_render_window.has_value())
			{
				auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
				auto render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);

				d3d12::TextureResource* cubemap = data.out_cubemap;
				
				const auto viewport = d3d12::CreateViewport(cubemap->m_width, cubemap->m_height);

				const auto frame_idx = n_render_system.GetRenderWindow()->m_frame_idx;

				d3d12::BindViewport(cmd_list, viewport);
				d3d12::BindPipeline(cmd_list, data.in_pipeline->m_native);
				d3d12::SetPrimitiveTopology(cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				struct ProjectionView_CB
				{
					DirectX::XMMATRIX m_view;
					DirectX::XMMATRIX m_projection;
				};

				auto d3d12_cb_handle = static_cast<D3D12ConstantBufferHandle*>(scene_graph.GetActiveCamera()->m_camera_cb);

				d3d12::BindConstantBuffer(cmd_list, d3d12_cb_handle->m_native, 0, frame_idx);

				scene_graph.Render(cmd_list, scene_graph.GetActiveCamera().get());
			}
		}
	} /* internal */
	
	inline void AddEquirectToCubemapTask(FrameGraph& fg, std::size_t width, std::size_t height)
	{
		RenderTargetProperties rt_properties
		{
			false,
			width,
			height,
			ResourceState::RENDER_TARGET,
			ResourceState::NON_PIXEL_SHADER_RESOURCE,
			true,
			Format::D32_FLOAT,
			{ Format::R32G32B32A32_FLOAT },
			1,
			true,
			true
		};

		RenderTaskDesc desc;
		desc.m_setup_func = [](RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool) {
			internal::SetupEquirectToCubemapTask(rs, fg, handle);
		};
		desc.m_execute_func = [](RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle) {
			internal::ExecuteEquirectToCubemapTask(rs, fg, sg, handle);
		};
		desc.m_destroy_func = [](FrameGraph&, RenderTaskHandle, bool) {
			// Nothing to destroy
		};
		desc.m_name = "Equirect To Cubemap";
		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::DIRECT;
		desc.m_allow_multithreading = true;

		fg.AddTask<EquirectToCubemapTaskData>(desc);
	}

} /* wr */