/*!
 * Copyright 2019 Breda University of Applied Sciences and Team Wisp (Viktor Zoutman, Emilio Laiso, Jens Hagen, Meine Zeinstra, Tahar Meijs, Koen Buitenhuis, Niels Brunekreef, Darius Bouma, Florian Schut)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "d3d12_renderer.hpp"

#include "../util/defines.hpp"
#include "../util/log.hpp"
#include "../scene_graph/scene_graph.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../window.hpp"

#include "d3d12_defines.hpp"
#include "d3d12_material_pool.hpp"
#include "d3d12_resource_pool_texture.hpp"
#include "d3d12_model_pool.hpp"
#include "d3d12_constant_buffer_pool.hpp"
#include "d3d12_structured_buffer_pool.hpp"
#include "d3d12_functions.hpp"
#include "../pipeline_registry.hpp"
#include "../rt_pipeline_registry.hpp"
#include "../shader_registry.hpp"
#include "../root_signature_registry.hpp"
#include "d3d12_resource_pool_texture.hpp"
#include "d3d12_dynamic_descriptor_heap.hpp"

#include "../scene_graph/mesh_node.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../scene_graph/light_node.hpp"
#include "../scene_graph/skybox_node.hpp"

#include "../render_tasks/d3d12_equirect_to_cubemap.hpp"
#include "../render_tasks/d3d12_cubemap_convolution.hpp"

#include <iostream>
#include <string>

namespace wr
{
	LINK_SG_RENDER_MESHES(D3D12RenderSystem, Render_MeshNodes)
	LINK_SG_INIT_MESHES(D3D12RenderSystem, Init_MeshNodes)
	LINK_SG_INIT_CAMERAS(D3D12RenderSystem, Init_CameraNodes)
	LINK_SG_INIT_LIGHTS(D3D12RenderSystem, Init_LightNodes)
	LINK_SG_UPDATE_MESHES(D3D12RenderSystem, Update_MeshNodes)
	LINK_SG_UPDATE_CAMERAS(D3D12RenderSystem, Update_CameraNodes)
	LINK_SG_UPDATE_LIGHTS(D3D12RenderSystem, Update_LightNodes)
	LINK_SG_UPDATE_TRANSFORMS(D3D12RenderSystem, Update_Transforms)
	LINK_SG_DELETE_SKYBOX(D3D12RenderSystem, Delete_Skybox)

	D3D12RenderSystem::~D3D12RenderSystem()
	{
		for (int i = 0; i < m_structured_buffer_pools.size(); ++i)
		{
			m_structured_buffer_pools[i].reset();
		}

		for (auto* shape : m_simple_shapes)
		{
			m_shapes_pool->Destroy(shape);
		}

		for (int i = 0; i < m_model_pools.size(); ++i)
		{
			m_model_pools[i].reset();
		}

		for (int i = 0; i < m_texture_pools.size(); ++i)
		{
			m_texture_pools[i].reset();
		}

		for (auto* fence : m_fences)
		{
			SAFE_RELEASE(fence->m_native);
			delete fence;
		}

		DestroyShaderRegistry();
		DestroyRootSignatureRegistry();
		DestroyPipelineRegistry();
		DestroyRTPipelineRegistry();

		d3d12::Destroy(m_fullscreen_quad_vb);
		d3d12::Destroy(m_direct_cmd_list);
		d3d12::Destroy(m_device);
		d3d12::Destroy(m_direct_queue);
		d3d12::Destroy(m_copy_queue);
		d3d12::Destroy(m_compute_queue);
		if (m_render_window.has_value())
		{
			d3d12::Destroy(m_render_window.value());
		}
	}

	void D3D12RenderSystem::Init(std::optional<Window*> window)
	{
		m_window = window;
		m_device = d3d12::CreateDevice();
		SetName(m_device, L"Default D3D12 Device");
		m_direct_queue = d3d12::CreateCommandQueue(m_device, CmdListType::CMD_LIST_DIRECT);
		m_compute_queue = d3d12::CreateCommandQueue(m_device, CmdListType::CMD_LIST_COMPUTE);
		m_copy_queue = d3d12::CreateCommandQueue(m_device, CmdListType::CMD_LIST_COPY);
		SetName(m_direct_queue, L"Default D3D12 Direct Command Queue");
		SetName(m_compute_queue, L"Default D3D12 Compute Command Queue");
		SetName(m_copy_queue, L"Default D3D12 Copy Command Queue");

		if (window.has_value() && window.value()->HasPhysicalWindow())
		{
			m_render_window = d3d12::CreateRenderWindow(m_device, window.value()->GetWindowHandle(), m_direct_queue, d3d12::settings::num_back_buffers);
		}

		PrepareShaderRegistry();
		PrepareRootSignatureRegistry();
		PreparePipelineRegistry();
		PrepareRTPipelineRegistry();

		// Create fences
		for (auto i = 0; i < m_fences.size(); i++)
		{
			m_fences[i] = d3d12::CreateFence(m_device);
			SetName(m_fences[i], (L"Fence " + std::to_wstring(i)));
		}

		// Create viewport
		m_viewport = d3d12::CreateViewport(window.has_value() ? window.value()->GetWidth() : 400, window.has_value() ? window.value()->GetHeight() : 400);

		// Create screen quad
		m_fullscreen_quad_vb = d3d12::CreateStagingBuffer(m_device, (void*)temp::quad_vertices, 4 * sizeof(Vertex2D), sizeof(Vertex2D), ResourceState::VERTEX_AND_CONSTANT_BUFFER);
		SetName(m_fullscreen_quad_vb, L"Fullscreen quad vertex buffer");

		// Create Command List
		m_direct_cmd_list = d3d12::CreateCommandList(m_device, d3d12::settings::num_back_buffers, CmdListType::CMD_LIST_DIRECT);
		SetName(m_direct_cmd_list, L"Defauld DX12 Command List");

		// Raytracing cb pool
		m_raytracing_cb_pool = CreateConstantBufferPool(1_mb);

		// Simple Shapes Model Pool
		m_shapes_pool = CreateModelPool(8_mb, 8_mb);
		LoadPrimitiveShapes();

		// Material raytracing sb pool
		size_t rt_mat_align_size = SizeAlignTwoPower((sizeof(temp::RayTracingMaterial_CBData) * d3d12::settings::num_max_rt_materials), 65536) * d3d12::settings::num_back_buffers;
		m_raytracing_material_sb_pool = CreateStructuredBufferPool(rt_mat_align_size);

		// Offset raytracing sb pool
		size_t rt_offset_align_size = SizeAlignTwoPower((sizeof(temp::RayTracingOffset_CBData) * d3d12::settings::num_max_rt_materials), 65536) * d3d12::settings::num_back_buffers;
		m_raytracing_offset_sb_pool = CreateStructuredBufferPool(rt_offset_align_size);

		// Begin Recording
		auto frame_idx = GetFrameIdx();
		d3d12::Begin(m_direct_cmd_list, frame_idx);

		// Stage fullscreen quad
		d3d12::StageBuffer(m_fullscreen_quad_vb, m_direct_cmd_list);
		
		// Execute
		d3d12::End(m_direct_cmd_list);
		d3d12::Execute(m_direct_queue, { m_direct_cmd_list }, m_fences[frame_idx]);

		m_buffer_frame_graph_uids.resize(d3d12::settings::num_back_buffers);

		//Rendering engine creates a texture pool that will be used by the render tasks.
		m_texture_pools.push_back(CreateTexturePool());
		CreateDefaultResources();
	}

	CPUTextures D3D12RenderSystem::Render(SceneGraph& scene_graph, FrameGraph& frame_graph)
	{
		if (m_skybox_changed)
		{
			frame_graph.SetShouldExecute<wr::EquirectToCubemapTaskData>(true);
			frame_graph.SetShouldExecute<wr::CubemapConvolutionTaskData>(true);

			m_skybox_changed = false;
		}

		// Perform render target save requests
		while (!m_requested_rt_saves.empty())
		{
			WaitForAllPreviousWork();
			auto back = m_requested_rt_saves.back();
			SaveRenderTargetToDisc(back.m_path, back.m_render_target, back.m_index);
			m_requested_rt_saves.pop();
		}

		if (m_requested_fullscreen_state.has_value())
		{
			WaitForAllPreviousWork();
			m_render_window.value()->m_swap_chain->SetFullscreenState(m_requested_fullscreen_state.value(), nullptr);
			Resize(m_window.value()->GetWidth(), m_window.value()->GetHeight());
			m_requested_fullscreen_state = std::nullopt;
		}

		auto frame_idx = GetFrameIdx();
		d3d12::WaitFor(m_fences[frame_idx]);

		//Signal to the texture pool that we waited for the previous frame 
		//so that stale descriptors and temporary textures can be freed.
		for (auto pool : m_texture_pools)
		{
			pool->ReleaseTemporaryResources();
			pool->UnloadTextures(frame_idx);
		}

		// Perform reload requests
		{
			// Root Signatures
			auto& rs_registry = RootSignatureRegistry::Get();
			rs_registry.Lock();
			for (auto request : rs_registry.GetReloadRequests())
			{
				ReloadRootSignatureRegistryEntry(request);
			}
			rs_registry.ClearReloadRequests();
			rs_registry.Unlock();

			// Shaders
			auto& shader_registry = ShaderRegistry::Get();
			shader_registry.Lock();
			for (auto request : shader_registry.GetReloadRequests())
			{
				ReloadShaderRegistryEntry(request);
			}
			shader_registry.ClearReloadRequests();
			shader_registry.Unlock();

			// Pipelines
			auto& pipeline_registry = PipelineRegistry::Get();
			pipeline_registry.Lock();
			for (auto request : pipeline_registry.GetReloadRequests())
			{
				ReloadPipelineRegistryEntry(request);
			}
			pipeline_registry.ClearReloadRequests();
			pipeline_registry.Unlock();

			// RT Pipelines
			auto& rt_pipeline_registry = RTPipelineRegistry::Get();
			rt_pipeline_registry.Lock();
			for (auto request : rt_pipeline_registry.GetReloadRequests())
			{
				ReloadRTPipelineRegistryEntry(request);
			}
			rt_pipeline_registry.ClearReloadRequests();
			rt_pipeline_registry.Unlock();
		}


		bool clear_frame_buffer = false;

		if (frame_graph.GetUID() != m_buffer_frame_graph_uids[frame_idx])
		{
			m_buffer_frame_graph_uids[frame_idx] = frame_graph.GetUID();
			clear_frame_buffer = true;
		}

		PreparePreRenderCommands(clear_frame_buffer, frame_idx);

		scene_graph.Update();
		scene_graph.Optimize();

		frame_graph.Execute(scene_graph);

		auto cmd_lists = frame_graph.GetAllCommandLists<d3d12::CommandList>();
		std::vector<d3d12::CommandList*> n_cmd_lists;
		n_cmd_lists.reserve(cmd_lists.size());

		n_cmd_lists.push_back(m_direct_cmd_list);

		for (auto& list : cmd_lists)
		{
			n_cmd_lists.push_back(list);
		}

		// Reset the batches.
		ResetBatches(scene_graph);

		d3d12::Execute(m_direct_queue, n_cmd_lists, m_fences[frame_idx]);

		if (m_render_window.has_value())
		{
			d3d12::Present(m_render_window.value());
		}

		m_bound_model_pool = nullptr;

		for (int i = 0; i < m_model_pools.size(); ++i)
		{
			m_model_pools[i]->SetUpdated(false);
		}

		// Optional CPU-visible copy of the render target pixel data
		const auto cpu_output_texture = frame_graph.GetOutputTexture();

		++frame_counter;

		// Optional CPU-visible copy of the render target pixel and/or depth data
		return frame_graph.GetOutputTexture();
	}

	void D3D12RenderSystem::Resize(std::uint32_t width, std::uint32_t height)
	{
		d3d12::ResizeViewport(m_viewport, (int)width, (int)height);
		if (m_render_window.has_value())
		{
			d3d12::Resize(m_render_window.value(), m_device, width, height);
		}
	}

	std::shared_ptr<TexturePool> D3D12RenderSystem::CreateTexturePool()
	{
		std::shared_ptr<D3D12TexturePool> pool = std::make_shared<D3D12TexturePool>(*this);
		m_texture_pools.push_back(pool);
		return pool;
	}

	std::shared_ptr<MaterialPool> D3D12RenderSystem::CreateMaterialPool(std::size_t size_in_bytes)
	{
		return std::make_shared<D3D12MaterialPool>(*this);
	}

	std::shared_ptr<ModelPool> D3D12RenderSystem::CreateModelPool(std::size_t vertex_buffer_pool_size_in_bytes, std::size_t index_buffer_pool_size_in_bytes)
	{
		std::shared_ptr<D3D12ModelPool> pool = std::make_shared<D3D12ModelPool>(*this, vertex_buffer_pool_size_in_bytes, index_buffer_pool_size_in_bytes);
		m_model_pools.push_back(pool);
		return pool;
	}

	std::shared_ptr<ConstantBufferPool> D3D12RenderSystem::CreateConstantBufferPool(std::size_t size_in_bytes)
	{
		return std::make_shared<D3D12ConstantBufferPool>(*this, size_in_bytes);
	}

	std::shared_ptr<StructuredBufferPool> D3D12RenderSystem::CreateStructuredBufferPool(std::size_t size_in_bytes)
	{
		std::shared_ptr<D3D12StructuredBufferPool> pool = std::make_shared<D3D12StructuredBufferPool>(*this, size_in_bytes);
		m_structured_buffer_pools.push_back(pool);
		return pool;
	}

	std::shared_ptr<TexturePool> D3D12RenderSystem::GetDefaultTexturePool()
	{
		if (m_texture_pools.size() > 0)
		{
			return m_texture_pools[0];
		}

		return std::shared_ptr<TexturePool>();
	}

	void D3D12RenderSystem::WaitForAllPreviousWork()
	{
		for (auto& fence : m_fences)
		{
			d3d12::WaitFor(fence);
			Signal(fence, m_direct_queue);
		}
	}

	CommandList* D3D12RenderSystem::GetDirectCommandList(unsigned int num_allocators)
	{
		return d3d12::CreateCommandList(m_device, num_allocators, CmdListType::CMD_LIST_DIRECT);
	}

	CommandList* D3D12RenderSystem::GetBundleCommandList(unsigned int num_allocators)
	{
		return d3d12::CreateCommandList(m_device, num_allocators, CmdListType::CMD_LIST_BUNDLE);
	}

	CommandList* D3D12RenderSystem::GetComputeCommandList(unsigned int num_allocators)
	{
		return d3d12::CreateCommandList(m_device, num_allocators, CmdListType::CMD_LIST_DIRECT);
	}

	CommandList* D3D12RenderSystem::GetCopyCommandList(unsigned int num_allocators)
	{
		return d3d12::CreateCommandList(m_device, num_allocators, CmdListType::CMD_LIST_DIRECT);
	}

	void D3D12RenderSystem::SetCommandListName(CommandList* cmd_list, std::wstring const& name)
	{
		d3d12::SetName(static_cast<d3d12::CommandList*>(cmd_list), name);
	}

	void D3D12RenderSystem::DestroyCommandList(CommandList* cmd_list)
	{
		Destroy(static_cast<wr::d3d12::CommandList*>(cmd_list));
	}

	RenderTarget* D3D12RenderSystem::GetRenderTarget(RenderTargetProperties properties)
	{
		if (properties.m_is_render_window)
		{
			if (!m_render_window.has_value())
			{
				LOGC("Tried using a render task which depends on the render window.");
				return nullptr;
			}
			return m_render_window.value();
		}
		else
		{
			d3d12::desc::RenderTargetDesc desc;
			desc.m_initial_state = properties.m_state_finished.Get().value_or(ResourceState::RENDER_TARGET);
			desc.m_create_dsv_buffer = properties.m_create_dsv_buffer;
			desc.m_num_rtv_formats = properties.m_num_rtv_formats;
			desc.m_rtv_formats = properties.m_rtv_formats;
			desc.m_dsv_format = properties.m_dsv_format;

			if (properties.m_width.Get().has_value() || properties.m_height.Get().has_value())
			{
				auto retval = d3d12::CreateRenderTarget(m_device, 
					static_cast<std::uint32_t>(properties.m_width.Get().value() * properties.m_resolution_scale.Get()),
					static_cast<std::uint32_t>(properties.m_height.Get().value() * properties.m_resolution_scale.Get()),
					desc);

				return retval;
			}
			else if (m_window.has_value())
			{
				auto retval = d3d12::CreateRenderTarget(m_device, 
					static_cast<std::uint32_t>(m_window.value()->GetWidth() * properties.m_resolution_scale.Get()),
					static_cast<std::uint32_t>(m_window.value()->GetHeight() * properties.m_resolution_scale.Get()),
					desc);

				return retval;
			}
			else
			{
				LOGC("Render target doesn't have a width or height specified. And there is no window to take the window size from. Hence can't create a proper render target.");
				return nullptr;
			}
		}
	}

	void D3D12RenderSystem::SetRenderTargetName(RenderTarget* render_target, std::wstring const& name)
	{
		d3d12::SetName(static_cast<d3d12::RenderTarget*>(render_target), name);
	}

	void D3D12RenderSystem::ResizeRenderTarget(RenderTarget** render_target, std::uint32_t width, std::uint32_t height)
	{
		auto n_render_target = static_cast<d3d12::RenderTarget*>(*render_target);
		d3d12::Resize((d3d12::RenderTarget**)&n_render_target, m_device, width, height);

		(*render_target) = n_render_target;
	}
	void D3D12RenderSystem::DestroyRenderTarget(RenderTarget** render_target)
	{
		Destroy((d3d12::RenderTarget*)*render_target);
		*render_target = nullptr;
	}

	void D3D12RenderSystem::RequestFullscreenChange(bool fullscreen_state)
	{
		m_requested_fullscreen_state = fullscreen_state;
	}

	void D3D12RenderSystem::ResetCommandList(CommandList* cmd_list)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto frame_idx = GetFrameIdx();
		d3d12::Begin(n_cmd_list, frame_idx);
	}

	void D3D12RenderSystem::CloseCommandList(CommandList* cmd_list)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		d3d12::End(n_cmd_list);
	}

	void D3D12RenderSystem::StartRenderTask(CommandList* cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto n_render_target = static_cast<d3d12::RenderTarget*>(render_target.first);
		auto frame_idx = GetFrameIdx();

		if (render_target.second.m_is_render_window) // TODO: do once at the beginning of the frame.
		{
			d3d12::Transition(n_cmd_list, n_render_target, frame_idx, ResourceState::PRESENT, ResourceState::RENDER_TARGET);
		}
		else if (render_target.second.m_state_finished.Get().has_value() && render_target.second.m_state_execute.Get().has_value())
		{
			d3d12::Transition(n_cmd_list, n_render_target, render_target.second.m_state_finished.Get().value(), render_target.second.m_state_execute.Get().value());
		}
		else
		{
			LOGW("A render target has no transitions specified. Is this correct?");
		}

		if (render_target.second.m_is_render_window)
		{
			d3d12::BindRenderTargetVersioned(n_cmd_list, n_render_target, frame_idx, render_target.second.m_clear, render_target.second.m_clear_depth);
		}
		else
		{
			d3d12::BindRenderTarget(n_cmd_list, n_render_target, render_target.second.m_clear, render_target.second.m_clear_depth);
		}
	}

	void D3D12RenderSystem::StopRenderTask(CommandList* cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto n_render_target = static_cast<d3d12::RenderTarget*>(render_target.first);
		unsigned int frame_idx = GetFrameIdx();

		if (render_target.second.m_is_render_window)
		{
			d3d12::Transition(n_cmd_list, n_render_target, frame_idx, ResourceState::RENDER_TARGET, ResourceState::PRESENT);
		}
		else if (render_target.second.m_state_finished.Get().has_value() && render_target.second.m_state_execute.Get().has_value())
		{
			d3d12::Transition(n_cmd_list, n_render_target, render_target.second.m_state_execute.Get().value(), render_target.second.m_state_finished.Get().value());
		}
		else
		{
			LOGW("A render target has no transitions specified. Is this correct?");
		}
	}

	void D3D12RenderSystem::StartComputeTask(CommandList * cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
	}

	void D3D12RenderSystem::StopComputeTask(CommandList * cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
	}

	void D3D12RenderSystem::StartCopyTask(CommandList * cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto n_render_target = static_cast<d3d12::RenderTarget*>(render_target.first);
		auto frame_idx = GetFrameIdx();

		if (render_target.second.m_is_render_window) // TODO: do once at the beginning of the frame.
		{
			d3d12::Transition(n_cmd_list, n_render_target, frame_idx, ResourceState::PRESENT, render_target.second.m_state_execute.Get().value());
		}
		else if (render_target.second.m_state_finished.Get().has_value() && render_target.second.m_state_execute.Get().has_value())
		{
			d3d12::Transition(n_cmd_list, n_render_target, render_target.second.m_state_finished.Get().value(), render_target.second.m_state_execute.Get().value());
		}
	}

	void D3D12RenderSystem::StopCopyTask(CommandList * cmd_list, std::pair<RenderTarget*, RenderTargetProperties> render_target)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto n_render_target = static_cast<d3d12::RenderTarget*>(render_target.first);
		auto frame_idx = GetFrameIdx();

		if (render_target.second.m_is_render_window)
		{
			d3d12::Transition(n_cmd_list, n_render_target, frame_idx, render_target.second.m_state_execute.Get().value(), ResourceState::PRESENT);
		}
		else if (render_target.second.m_state_finished.Get().has_value() && render_target.second.m_state_execute.Get().has_value())
		{
			d3d12::Transition(n_cmd_list, n_render_target, render_target.second.m_state_execute.Get().value(), render_target.second.m_state_finished.Get().value());
		}
	}

	void D3D12RenderSystem::SaveRenderTargetToDisc(std::string const& path, RenderTarget* render_target, unsigned int index)
	{
		auto n_render_target = static_cast<d3d12::RenderTarget*>(render_target);
		auto format = static_cast<Format>(n_render_target->m_render_targets[index]->GetDesc().Format);
		
		auto rt_resource = n_render_target->m_render_targets[index];
		auto n_device = m_device->m_native;
		auto width = d3d12::GetRenderTargetWidth(n_render_target);
		auto height = d3d12::GetRenderTargetHeight(n_render_target);
		auto bytes_per_pixel = BytesPerPixel(format);
		std::uint64_t bytes_per_row = SizeAlignTwoPower(static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(bytes_per_pixel), 256);
		std::uint64_t texture_size = bytes_per_row * static_cast<std::uint64_t>(height);

		auto queue = d3d12::CreateCommandQueue(m_device, CmdListType::CMD_LIST_DIRECT);
		SetName(queue, L"Screenshot Command Queue");

		auto cmd_list = d3d12::CreateCommandList(m_device, 1, CmdListType::CMD_LIST_DIRECT);
		SetName(cmd_list, L"Screenshot Command List");

		auto fence = d3d12::CreateFence(m_device);

		d3d12::Begin(cmd_list, 0);

		// Create the actual read back buffer
		auto readback_buffer = d3d12::CreateReadbackBuffer(m_device, texture_size);
		d3d12::SetName(readback_buffer, L"Texture ReadBack Buffer (Used for saving to disc)");

		// Copy data to cpu
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		auto rt_desc = rt_resource->GetDesc();
		n_device->GetCopyableFootprints(&rt_desc, 0, 1, 0, &footprint, nullptr, nullptr, (std::uint64_t*)&texture_size);

		CD3DX12_TEXTURE_COPY_LOCATION dest_loc(readback_buffer->m_resource, footprint);
		CD3DX12_TEXTURE_COPY_LOCATION src_loc(rt_resource, 0);
		cmd_list->m_native->CopyTextureRegion(&dest_loc, 0, 0, 0, &src_loc, nullptr);

		d3d12::End(cmd_list);

		// Execute
		Execute(queue, { cmd_list }, fence);
		fence->m_fence_value++;
		Signal(fence, queue);
		WaitFor(fence);

		// Store data
		auto pixels = static_cast<std::uint8_t*>(d3d12::MapReadbackBuffer(readback_buffer, texture_size));
		d3d12::UnmapReadbackBuffer(readback_buffer);

		DirectX::Image img;
		img.format = static_cast<DXGI_FORMAT>(format);
		img.width = width;
		img.height = height;
		img.rowPitch = bytes_per_row;
		img.slicePitch = texture_size;
		img.pixels = pixels;

		auto wpath = std::wstring(path.begin(), path.end());
		TRY_M(DirectX::SaveToTGAFile(img, wpath.c_str()), "Failed to save image to disc");

		d3d12::Destroy(readback_buffer);
		d3d12::Destroy(cmd_list);
		d3d12::Destroy(fence);
		d3d12::Destroy(queue);
	}

	void D3D12RenderSystem::PrepareRootSignatureRegistry()
	{
		auto& registry = RootSignatureRegistry::Get();

		for (auto desc : registry.m_descriptions)
		{
			d3d12::desc::RootSignatureDesc n_desc;
			n_desc.m_parameters = desc.second.m_parameters;
			n_desc.m_samplers = desc.second.m_samplers;
			n_desc.m_rtx = desc.second.m_rtx;
			n_desc.m_rt_local = desc.second.m_rtx_local;

			auto n_rs = d3d12::CreateRootSignature(n_desc);
			d3d12::FinalizeRootSignature(n_rs, m_device);
			SetName(n_rs, (L"Root Signature " + desc.second.name));

			registry.m_objects.insert({ desc.first, n_rs });
		}
	}

	void D3D12RenderSystem::PrepareShaderRegistry()
	{
		auto& registry = ShaderRegistry::Get();

		for (auto desc : registry.m_descriptions)
		{
			auto shader_error = d3d12::LoadShader(m_device, desc.second.type, desc.second.path, desc.second.entry, desc.second.defines);

			if (std::holds_alternative<d3d12::Shader*>(shader_error))
			{
				auto shader = std::get<d3d12::Shader*>(shader_error);
				registry.m_objects.insert({ desc.first, shader });
			}
			else
			{
				try
				{
					LOGC(std::get<std::string>(shader_error));
				}
				catch(std::exception e)
				{
					LOGW("Seems like FMT failed to format the error message. Using cout instead.");
					std::cerr << std::get<std::string>(shader_error) << std::endl;
				}
			}
		}
	}

	void D3D12RenderSystem::PreparePipelineRegistry()
	{
		auto& registry = PipelineRegistry::Get();

		for (auto desc : registry.m_descriptions)
		{
			d3d12::desc::PipelineStateDesc n_desc;
			n_desc.m_counter_clockwise = desc.second.m_counter_clockwise;
			n_desc.m_cull_mode = desc.second.m_cull_mode;
			n_desc.m_depth_enabled = desc.second.m_depth_enabled;
			n_desc.m_dsv_format = desc.second.m_dsv_format;
			n_desc.m_input_layout = desc.second.m_input_layout;
			n_desc.m_num_rtv_formats = desc.second.m_num_rtv_formats;
			n_desc.m_rtv_formats = desc.second.m_rtv_formats;
			n_desc.m_topology_type = desc.second.m_topology_type;
			n_desc.m_type = desc.second.m_type;

			auto n_pipeline = d3d12::CreatePipelineState();

			if (desc.second.m_vertex_shader_handle.has_value())
			{
				auto obj = ShaderRegistry::Get().Find(desc.second.m_vertex_shader_handle.value());
				auto shader = static_cast<d3d12::Shader*>(obj);
				d3d12::SetVertexShader(n_pipeline, shader);
			}
			if (desc.second.m_pixel_shader_handle.has_value())
			{
				auto obj = ShaderRegistry::Get().Find(desc.second.m_pixel_shader_handle.value());
				auto shader = static_cast<d3d12::Shader*>(obj);
				d3d12::SetFragmentShader(n_pipeline, shader);
			}
			if (desc.second.m_compute_shader_handle.has_value())
			{
				auto obj = ShaderRegistry::Get().Find(desc.second.m_compute_shader_handle.value());
				auto shader = static_cast<d3d12::Shader*>(obj);
				d3d12::SetComputeShader(n_pipeline, shader);
			}
			{
				auto obj = RootSignatureRegistry::Get().Find(desc.second.m_root_signature_handle);
				d3d12::SetRootSignature(n_pipeline, static_cast<d3d12::RootSignature*>(obj));
			}

			d3d12::FinalizePipeline(n_pipeline, m_device, n_desc);
			SetName(n_pipeline, L"Default pipeline state");

			registry.m_objects.insert({ desc.first, n_pipeline });
		}
	}

	void D3D12RenderSystem::ReloadPipelineRegistryEntry(RegistryHandle handle)
	{
		auto& registry = PipelineRegistry::Get();
		std::optional<std::string> error_msg = std::nullopt;
		auto n_pipeline = static_cast<d3d12::PipelineState*>(registry.Find(handle));

		auto recompile_shader = [&error_msg, this](auto& pipeline_shader)
		{
			if (!pipeline_shader) return;

			auto new_shader_variant = d3d12::LoadShader(m_device, pipeline_shader->m_type,
				pipeline_shader->m_path,
				pipeline_shader->m_entry,
				pipeline_shader->m_defines);

			if (std::holds_alternative<d3d12::Shader*>(new_shader_variant))
			{
				pipeline_shader = std::get<d3d12::Shader*>(new_shader_variant);
			}
			else
			{
				error_msg = std::get<std::string>(new_shader_variant);
			}
		};

		// Vertex Shader
		{
			recompile_shader(n_pipeline->m_vertex_shader);
		}
		// Pixel Shader
		if (!error_msg.has_value()) {
			recompile_shader(n_pipeline->m_pixel_shader);
		}
		// Compute Shader
		if (!error_msg.has_value()) {
			recompile_shader(n_pipeline->m_compute_shader);
		}

		if (error_msg.has_value())
		{
			LOGW(error_msg.value());
			//open_shader_compiler_popup = true;
			//shader_compiler_error = error_msg.value();
		}
		else
		{
			d3d12::RefinalizePipeline(n_pipeline);
		}
	}

	void D3D12RenderSystem::ReloadRTPipelineRegistryEntry(RegistryHandle handle)
	{
		auto& registry = RTPipelineRegistry::Get();
		std::optional<std::string> error_msg = std::nullopt;
		auto n_pipeline = static_cast<d3d12::StateObject*>(registry.Find(handle));

		auto recompile_shader = [&error_msg, this](auto& pipeline_shader)
		{
			auto new_shader_variant = d3d12::LoadShader(m_device, pipeline_shader->m_type,
				pipeline_shader->m_path,
				pipeline_shader->m_entry,
				pipeline_shader->m_defines);

			if (std::holds_alternative<d3d12::Shader*>(new_shader_variant))
			{
				pipeline_shader = std::get<d3d12::Shader*>(new_shader_variant);
			}
			else
			{
				error_msg = std::get<std::string>(new_shader_variant);
			}
		};

		// Library Shader
		{
			recompile_shader(n_pipeline->m_desc.m_library);
		}

		if (error_msg.has_value())
		{
			LOGW(error_msg.value());
			//open_shader_compiler_popup = true;
			//shader_compiler_error = error_msg.value();
		}
		else
		{
			d3d12::RecreateStateObject(n_pipeline);
		}
	}

	void D3D12RenderSystem::ReloadShaderRegistryEntry(RegistryHandle handle)
	{
		auto& registry = ShaderRegistry::Get();
		std::optional<std::string> error_msg = std::nullopt;
		auto n_shader = static_cast<d3d12::Shader*>(registry.Find(handle));

		auto new_shader_variant = d3d12::LoadShader(m_device, n_shader->m_type,
			n_shader->m_path,
			n_shader->m_entry);

		if (std::holds_alternative<d3d12::Shader*>(new_shader_variant))
		{
			d3d12::Destroy(n_shader);
			n_shader = std::get<d3d12::Shader*>(new_shader_variant);
		}
		else
		{
			LOGW(std::get<std::string>(new_shader_variant));
		}
	}

	void D3D12RenderSystem::ReloadRootSignatureRegistryEntry(RegistryHandle handle)
	{
		auto& registry = RootSignatureRegistry::Get();
		auto n_root_signature = static_cast<d3d12::RootSignature*>(registry.Find(handle));

		d3d12::RefinalizeRootSignature(n_root_signature, m_device);
	}

	void D3D12RenderSystem::PrepareRTPipelineRegistry()
	{
		auto& registry = RTPipelineRegistry::Get();

		for (auto it : registry.m_descriptions)
		{
			auto desc = it.second;
			auto library = static_cast<d3d12::Shader*>(ShaderRegistry::Get().Find(desc.library_desc.shader_handle));

			d3d12::desc::StateObjectDesc n_desc;
			n_desc.m_library = library;
			n_desc.m_library_exports = desc.library_desc.exports;
			n_desc.max_attributes_size = static_cast<std::uint32_t>(desc.max_attributes_size);
			n_desc.max_payload_size = static_cast<std::uint32_t>(desc.max_payload_size);
			n_desc.max_recursion_depth = static_cast<std::uint32_t>(desc.max_recursion_depth);
			n_desc.m_hit_groups = desc.library_desc.m_hit_groups;

			if (auto rt_handle = desc.global_root_signature.value(); desc.global_root_signature.has_value())
			{
				n_desc.global_root_signature = static_cast<d3d12::RootSignature*>(RootSignatureRegistry::Get().Find(rt_handle));
			}

			n_desc.local_root_signatures = std::vector<d3d12::RootSignature*>();
			for (auto rt_handle : desc.local_root_signatures)
			{
				auto rs = static_cast<d3d12::RootSignature*>(RootSignatureRegistry::Get().Find(rt_handle));
				n_desc.local_root_signatures.value().push_back(rs);
			}

			auto n_state_object = d3d12::CreateStateObject(m_device, n_desc);

			registry.m_objects.insert({ it.first, n_state_object });
		}
	}

	namespace internal
	{

		template<typename R, typename T>
		void DestroyGenericRegistry()
		{
			auto& registry = R::Get();

			for (auto it : registry.m_objects)
			{
				auto native = static_cast<T*>(it.second);
				if (native)
				{
					d3d12::Destroy(native);
				}
			}
		}

	} /* internal */

	void D3D12RenderSystem::DestroyRootSignatureRegistry()
	{
		internal::DestroyGenericRegistry<RootSignatureRegistry, d3d12::RootSignature>();
	}

	void D3D12RenderSystem::DestroyShaderRegistry()
	{
		internal::DestroyGenericRegistry<ShaderRegistry, d3d12::Shader>();
	}

	void D3D12RenderSystem::DestroyPipelineRegistry()
	{
		internal::DestroyGenericRegistry<PipelineRegistry, d3d12::PipelineState>();
	}

	void D3D12RenderSystem::DestroyRTPipelineRegistry()
	{
		internal::DestroyGenericRegistry<RTPipelineRegistry, d3d12::StateObject>();
	}

	void D3D12RenderSystem::InitSceneGraph(SceneGraph& scene_graph)
	{
		auto frame_idx = GetFrameIdx();
		d3d12::WaitFor(m_fences[frame_idx]);
		d3d12::Begin(m_direct_cmd_list, frame_idx);

		scene_graph.Init();

		d3d12::End(m_direct_cmd_list);

		// Execute
		d3d12::Execute(m_direct_queue, { m_direct_cmd_list }, m_fences[frame_idx]);
	}

	void D3D12RenderSystem::Init_MeshNodes(std::vector<std::shared_ptr<MeshNode>>& nodes)
	{
	}

	void D3D12RenderSystem::Init_CameraNodes(std::vector<std::shared_ptr<CameraNode>>& nodes)
	{
		if (nodes.empty()) return;

		size_t cam_align_size = SizeAlignTwoPower(nodes.size() * sizeof(temp::ProjectionView_CBData), 256) * d3d12::settings::num_back_buffers;
		m_camera_pool = CreateConstantBufferPool((size_t)std::ceil(cam_align_size));

		for (auto& node : nodes)
		{
			node->m_camera_cb = m_camera_pool->Create(sizeof(temp::ProjectionView_CBData));
		}
	}

	void D3D12RenderSystem::Init_LightNodes(std::vector<std::shared_ptr<LightNode>>& nodes, std::vector<Light>& lights)
	{
	}

	void D3D12RenderSystem::Update_Transforms(SceneGraph& scene_graph, std::shared_ptr<Node>& node)
	{

		if (node->RequiresTransformUpdate(GetFrameIdx()))
		{
			node->UpdateTransform();
			node->SignalTransformUpdate(GetFrameIdx());
		}

		auto& children = node->m_children;
		auto it = children.begin();

		while (it != children.end())
		{
			Update_Transforms(scene_graph, *it);
			++it;
		}

	}

	void D3D12RenderSystem::Delete_Skybox(SceneGraph& scene_graph, std::shared_ptr<SkyboxNode>& skybox_node)
	{
		unsigned int frame_idx = GetFrameIdx();

		skybox_node->m_irradiance.value().m_pool->MarkForUnload(skybox_node->m_irradiance.value(), frame_idx);
		skybox_node->m_skybox.value().m_pool->MarkForUnload(skybox_node->m_skybox.value(), frame_idx);
		skybox_node->m_prefiltered_env_map.value().m_pool->MarkForUnload(skybox_node->m_prefiltered_env_map.value(), frame_idx);

		if (skybox_node->m_hdr.m_pool)
		{
			skybox_node->m_hdr.m_pool->MarkForUnload(skybox_node->m_hdr, frame_idx);
		}
	}

	void D3D12RenderSystem::PreparePreRenderCommands(bool clear_frame_buffer, int frame_idx)
	{
		d3d12::Begin(m_direct_cmd_list, frame_idx);

		for (int i = 0; i < m_structured_buffer_pools.size(); ++i)
		{
			m_structured_buffer_pools[i]->UpdateBuffers(m_direct_cmd_list, frame_idx);
		}

		for (int i = 0; i < m_model_pools.size(); ++i)
		{
			m_model_pools[i]->StageMeshes(m_direct_cmd_list);
		}

		for (auto pool : m_texture_pools)
		{
			pool->Stage(m_direct_cmd_list);
		}

		d3d12::End(m_direct_cmd_list);
	}

	void D3D12RenderSystem::Update_MeshNodes(std::vector<std::shared_ptr<MeshNode>>& nodes)
	{
		for (auto& node : nodes)
		{
			if (!node->RequiresUpdate(GetFrameIdx()))
			{
				continue;
			}

			node->Update(GetFrameIdx());
		}
	}

	void D3D12RenderSystem::Update_CameraNodes(std::vector<std::shared_ptr<CameraNode>>& nodes)
	{
		for (auto& node : nodes)
		{
			if (!node->RequiresUpdate(GetFrameIdx()))
			{
				continue;
			}

			node->UpdateTemp(GetFrameIdx());

			temp::ProjectionView_CBData data;
			data.m_projection = node->m_projection;
			data.m_inverse_projection = node->m_inverse_projection;
			data.m_prev_projection = node->m_prev_projection;
			data.m_view = node->m_view;
			data.m_inverse_view = node->m_inverse_view;
			data.m_prev_view = node->m_prev_view;
			
			data.m_is_hybrid = 0;

			node->m_camera_cb->m_pool->Update(node->m_camera_cb, sizeof(temp::ProjectionView_CBData), 0, (uint8_t*)&data);
		}
	}

	void D3D12RenderSystem::Update_LightNodes(SceneGraph& scene_graph)
	{
		bool should_update = false;
		uint32_t offset_start = 0, offset_end = 0;

		std::vector<std::shared_ptr<LightNode>>& light_nodes = scene_graph.GetLightNodes();

		for (uint32_t i = 0, j = (uint32_t)light_nodes.size(); i < j; ++i)
		{
			std::shared_ptr<LightNode>& node = light_nodes[i];

			if (!node->RequiresUpdate(GetFrameIdx()))
			{
				continue;
			}

			if (!should_update)
			{
				should_update = true;
				offset_start = i;
			}

			node->Update(GetFrameIdx());

			offset_end = i;
		}

		if (!should_update && !(offset_end == offset_start && offset_start == 0))
			return;

		//Update light count

		scene_graph.GetLight(0)->tid &= 3;
		scene_graph.GetLight(0)->tid |= scene_graph.GetCurrentLightSize() << 2;

		//Update structured buffer

		StructuredBufferHandle* structured_buffer = scene_graph.GetLightBuffer();

		structured_buffer->m_pool->Update(structured_buffer, scene_graph.GetLight(offset_start), sizeof(Light) * (offset_end - offset_start + 1), sizeof(Light) * offset_start);

	}

	void D3D12RenderSystem::Render_MeshNodes(temp::MeshBatches& batches, CameraNode* camera, CommandList* cmd_list)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);
		auto d3d12_camera_cb = static_cast<D3D12ConstantBufferHandle*>(camera->m_camera_cb);
	
		d3d12::BindConstantBuffer(n_cmd_list, d3d12_camera_cb->m_native, 0, GetFrameIdx());

		//Render batches
		for (auto& elem : batches)
		{
			auto model = elem.first.first;
			auto materials = elem.first.second;
			temp::MeshBatch& batch = elem.second;

			//Bind object data
			auto d3d12_cb_handle = static_cast<D3D12ConstantBufferHandle*>(batch.batch_buffer);
			d3d12::BindConstantBuffer(n_cmd_list, d3d12_cb_handle->m_native, 1, GetFrameIdx());

			//Render meshes
			for (std::size_t mesh_i = 0; mesh_i < model->m_meshes.size(); mesh_i++)
			{
				auto mesh = model->m_meshes[mesh_i];
				auto n_mesh = static_cast<D3D12ModelPool*>(model->m_model_pool)->GetMeshData(mesh.first->id);
				if (model->m_model_pool != m_bound_model_pool || n_mesh->m_vertex_staging_buffer_stride != m_bound_model_pool_stride)
				{
					D3D12ModelPool* model_pool = static_cast<D3D12ModelPool*>(model->m_model_pool);

					d3d12::BindVertexBuffer(n_cmd_list,
						model_pool->GetVertexStagingBuffer(),
						0,
						model_pool->GetVertexStagingBuffer()->m_size,
						n_mesh->m_vertex_staging_buffer_stride);

					d3d12::BindIndexBuffer(n_cmd_list,
						model_pool->GetIndexStagingBuffer(),
						0,
						static_cast<std::uint32_t>(model_pool->GetIndexStagingBuffer()->m_size));

					m_bound_model_pool = static_cast<D3D12ModelPool*>(model->m_model_pool);
					m_bound_model_pool_stride = n_mesh->m_vertex_staging_buffer_stride;
				}

				d3d12::BindDescriptorHeaps(n_cmd_list);

				// Pick the standard material or if available a user defined material.
				auto material_handle = mesh.second;
				if (materials.size() > mesh_i)
				{
					material_handle = materials[mesh_i];
				}

				if (material_handle != m_last_material)
				{
					m_last_material = material_handle;

					BindMaterial(material_handle, cmd_list);
				}

				if (n_mesh->m_index_count != 0)
				{
					d3d12::DrawIndexed(n_cmd_list,
						static_cast<std::uint32_t>(n_mesh->m_index_count),
						batch.num_instances,
						static_cast<std::uint32_t>(n_mesh->m_index_staging_buffer_offset),
						static_cast<std::uint32_t>(n_mesh->m_vertex_staging_buffer_offset));
				}
				else
				{
					d3d12::Draw(n_cmd_list, 
						static_cast<std::uint32_t>(n_mesh->m_vertex_count), 
						batch.num_instances, 
						static_cast<std::uint32_t>(n_mesh->m_vertex_staging_buffer_offset));
				}
			}
		}

		// Reset frame specific variables
		m_last_material.m_id = 0;
		m_last_material.m_pool = nullptr;

	}

	void D3D12RenderSystem::BindMaterial(MaterialHandle material_handle, CommandList* cmd_list)
	{
		auto n_cmd_list = static_cast<d3d12::CommandList*>(cmd_list);

		auto* material_internal = material_handle.m_pool->GetMaterial(material_handle);

		material_internal->UpdateConstantBuffer();

		D3D12ConstantBufferHandle* handle = static_cast<D3D12ConstantBufferHandle*>(material_internal->GetConstantBufferHandle());

		auto albedo_handle = material_internal->GetTexture(TextureType::ALBEDO);
		wr::d3d12::TextureResource* albedo_internal;
		if (albedo_handle.m_pool == nullptr)
		{
			auto default_albedo_handle = GetDefaultAlbedo();
			albedo_internal = static_cast<wr::d3d12::TextureResource*>(default_albedo_handle.m_pool->GetTextureResource(default_albedo_handle));
		}
		else
		{
			albedo_internal = static_cast<wr::d3d12::TextureResource*>(albedo_handle.m_pool->GetTextureResource(albedo_handle));
		}

		auto normal_handle = material_internal->GetTexture(TextureType::NORMAL);
		wr::d3d12::TextureResource* normal_internal;
		if (normal_handle.m_pool == nullptr)
		{
			auto default_normal_handle = GetDefaultNormal();
			normal_internal = static_cast<wr::d3d12::TextureResource*>(default_normal_handle.m_pool->GetTextureResource(default_normal_handle));
		}
		else
		{
			normal_internal = static_cast<wr::d3d12::TextureResource*>(normal_handle.m_pool->GetTextureResource(normal_handle));
		}

		auto roughness_handle = material_internal->GetTexture(TextureType::ROUGHNESS);
		wr::d3d12::TextureResource* roughness_internal;
		if (roughness_handle.m_pool == nullptr)
		{
			auto default_roughness_handle = GetDefaultRoughness();
			roughness_internal = static_cast<wr::d3d12::TextureResource*>(default_roughness_handle.m_pool->GetTextureResource(default_roughness_handle));
		}
		else
		{
			roughness_internal = static_cast<wr::d3d12::TextureResource*>(roughness_handle.m_pool->GetTextureResource(roughness_handle));
		}

		auto metallic_handle = material_internal->GetTexture(TextureType::METALLIC);
		wr::d3d12::TextureResource* metallic_internal;
		if (metallic_handle.m_pool == nullptr)
		{
			auto default_metallic_handle = GetDefaultMetalic();
			metallic_internal = static_cast<wr::d3d12::TextureResource*>(default_metallic_handle.m_pool->GetTextureResource(default_metallic_handle));
		}
		else
		{
			metallic_internal = static_cast<wr::d3d12::TextureResource*>(metallic_handle.m_pool->GetTextureResource(metallic_handle));
		}

		auto emissive_handle = material_internal->GetTexture(TextureType::EMISSIVE);
		wr::d3d12::TextureResource* emissive_internal;
		if (emissive_handle.m_pool == nullptr)
		{
			auto default_emissive_handle = GetDefaultEmissive();
			emissive_internal = static_cast<wr::d3d12::TextureResource*>(default_emissive_handle.m_pool->GetTextureResource(default_emissive_handle));
		}
		else
		{
			emissive_internal = static_cast<wr::d3d12::TextureResource*>(emissive_handle.m_pool->GetTextureResource(emissive_handle));
		}

		auto ao_handle = material_internal->GetTexture(TextureType::AO);
		wr::d3d12::TextureResource* ao_internal;
		if (ao_handle.m_pool == nullptr)
		{
			auto default_ao_handle = GetDefaultAO();
			ao_internal = static_cast<wr::d3d12::TextureResource*>(default_ao_handle.m_pool->GetTextureResource(default_ao_handle));
		}
		else
		{
			ao_internal = static_cast<wr::d3d12::TextureResource*>(ao_handle.m_pool->GetTextureResource(ao_handle));
		}

		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::ALBEDO)), albedo_internal);
		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::NORMAL)), normal_internal);
		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::ROUGHNESS)), roughness_internal);
		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::METALLIC)), metallic_internal);
		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::EMISSIVE)), emissive_internal);
		d3d12::SetShaderSRV(n_cmd_list, 2, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::deferred_main, params::DeferredMainE::AMBIENT_OCCLUSION)), ao_internal);

		d3d12::BindConstantBuffer(n_cmd_list, handle->m_native, 3, GetFrameIdx());
	}

	unsigned int D3D12RenderSystem::GetFrameIdx()
	{
		if (m_render_window.has_value())
		{
			return m_render_window.value()->m_frame_idx;
		}
		else
		{
			return frame_counter % d3d12::settings::num_back_buffers;
		}
	}

	d3d12::RenderWindow* D3D12RenderSystem::GetRenderWindow()
	{
		if (m_render_window.has_value())
		{
			return m_render_window.value();
		}
		else
		{
			LOGW("Called `D3D12RenderSystem::GetRenderWindow` without a window!");
			return nullptr;
		}
	}

	void D3D12RenderSystem::RequestSkyboxReload()
	{
		m_skybox_changed = true;
	}

	wr::Model* D3D12RenderSystem::GetSimpleShape(SimpleShapes type)
	{
		if (type == SimpleShapes::COUNT)
		{
			LOGC("Nice try boiii! That's not a shape.");
		}

		return m_simple_shapes[static_cast<std::size_t>(type)];
	}

	void D3D12RenderSystem::ResetBatches(SceneGraph& sg)
	{
		for (auto& batch : sg.GetBatches())
		{
			batch.second.num_instances = 0;
			batch.second.num_global_instances = 0;
		}
	}

	void D3D12RenderSystem::LoadPrimitiveShapes()
	{
		// Load Cube.
		{
			wr::MeshData<wr::Vertex> mesh;

			mesh.m_indices = {
				2, 1, 0, 3, 2, 0, 6, 5,
				4, 7, 6, 4, 10, 9, 8, 11,
				10, 8, 14, 13, 12, 15, 14, 12,
				18, 17, 16, 19, 18, 16, 22, 21,
				20, 23, 22, 20
			};

			mesh.m_vertices = {
				{ 1, 1, -1,		1, 1,		0, 0, -1,		0, 0, 0,	0, 0, 0 },
				{ 1, -1, -1,	0, 1,		0, 0, -1,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, -1,	0, 0,		0, 0, -1,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	1, 0,		0, 0, -1,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, 1,		1, 1,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		0, 1,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 0,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		1, 0,		0, 0, 1,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, -1,		1, 0,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, 1, 1,		1, 1,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		0, 1,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, -1,	0, 0,		1, 0, 0,		0, 0, 0,	0, 0, 0  },

				{ 1, -1, -1,	1, 0,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		1, 1,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 1,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, -1,	0, 0,		0, -1, 0,		0, 0, 0,	0, 0, 0  },

				{ -1, -1, -1,	0, 1,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 0,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		1, 0,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	1, 1,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, 1,		1, 0,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, 1, -1,		1, 1,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	0, 1,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		0, 0,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
			};

			m_simple_shapes[static_cast<std::size_t>(SimpleShapes::CUBE)] = m_shapes_pool->LoadCustom<wr::Vertex>({ mesh });
		}

		{
			wr::MeshData<wr::Vertex> mesh;

			mesh.m_indices = {
				2, 1, 0, 3, 2, 0
			};

			mesh.m_vertices = {
				//POS				UV			NORMAL				TANGENT			BINORMAL		COLOR
				{  1,  1,  0,		1, 1,		0, 0, -1,			0, 0, 1,		0, 1, 0 },
				{  1, -1,  0,		1, 0,		0, 0, -1,			0, 0, 1,		0, 1, 0 },
				{ -1, -1,  0,		0, 0,		0, 0, -1,			0, 0, 1,		0, 1, 0 },
				{ -1,  1,  0,		0, 1,		0, 0, -1,			0, 0, 1,		0, 1, 0 },
			};

			m_simple_shapes[static_cast<std::size_t>(SimpleShapes::PLANE)] = m_shapes_pool->LoadCustom<wr::Vertex>({ mesh });
		}
	}

	void D3D12RenderSystem::CreateDefaultResources()
	{
		auto default_texture_pool = m_texture_pools.at(0);

		m_default_cubemap = default_texture_pool->CreateCubemap("DefaultResource_Cubemap", 2, 2, 1, wr::Format::R8G8B8A8_UNORM, false);

		m_default_albedo = default_texture_pool->LoadFromFile(settings::default_albedo_path, false, false);
		m_default_normal = default_texture_pool->LoadFromFile(settings::default_normal_path, false, false);
		m_default_white = default_texture_pool->LoadFromFile(settings::default_white_texture, false, false);
		m_default_black = default_texture_pool->LoadFromFile(settings::default_black_texture, false, false);
	}

} /*  */
