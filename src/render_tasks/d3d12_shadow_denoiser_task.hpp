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
#pragma once
#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"
#include "../d3d12/d3d12_structured_buffer_pool.hpp"
#include "../frame_graph/frame_graph.hpp"
#include "../scene_graph/scene_graph.hpp"
#include "../scene_graph/camera_node.hpp"
#include "../engine_registry.hpp"

#include "../render_tasks/d3d12_deferred_main.hpp"
#include "../render_tasks/d3d12_rt_shadow_task.hpp"

#include <optional>

namespace wr
{
	struct ShadowDenoiserSettings
	{
		struct Runtime
		{
			float m_alpha = 0.05f;
			float m_moments_alpha = 0.2f;
			float m_l_phi = 4.f;
			float m_n_phi = 128.f;
			float m_z_phi = 1.0f;
		};
		Runtime m_runtime;
	};
	struct ShadowDenoiserData
	{		
		d3d12::PipelineState* m_reprojection_pipeline;
		d3d12::PipelineState* m_filter_moments_pipeline;
		d3d12::PipelineState* m_wavelet_filter_pipeline;

		DescriptorAllocator* out_allocator;
		DescriptorAllocation out_allocation;

		std::shared_ptr<ConstantBufferPool> m_constant_buffer_pool;

		temp::ShadowDenoiserSettings_CBData m_denoiser_settings;
		std::array<ConstantBufferHandle*, d3d12::settings::shadow_denoiser_wavelet_iterations> m_denoiser_settings_buffer;

		ConstantBufferHandle* m_denoiser_camera;

		d3d12::RenderTarget* m_input_render_target;
		d3d12::RenderTarget* m_gbuffer_render_target;

		d3d12::RenderTarget* m_in_hist_length;
		
		d3d12::RenderTarget* m_in_prev_color;
		d3d12::RenderTarget* m_in_prev_moments;
		d3d12::RenderTarget* m_in_prev_normals;
		d3d12::RenderTarget* m_in_prev_depth;

		d3d12::RenderTarget* m_out_color_render_target;
		d3d12::RenderTarget* m_out_moments_render_target;
		d3d12::RenderTarget* m_out_hist_length_render_target;

		d3d12::RenderTarget* m_ping_pong_render_target;
	};

	namespace internal
	{
		inline void SetupShadowDenoiserTask(RenderSystem& rs, FrameGraph&  fg, RenderTaskHandle handle, bool resize)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<ShadowDenoiserData>(handle);

			auto& ps_registry = PipelineRegistry::Get();
			data.m_reprojection_pipeline = (d3d12::PipelineState*)ps_registry.Find(pipelines::svgf_denoiser_reprojection);
			data.m_filter_moments_pipeline = (d3d12::PipelineState*)ps_registry.Find(pipelines::svgf_denoiser_filter_moments);
			data.m_wavelet_filter_pipeline = (d3d12::PipelineState*)ps_registry.Find(pipelines::svgf_denoiser_wavelet_filter);

			//Retrieve the texture pool from the render system. It will be used to allocate temporary cpu visible descriptors
			std::shared_ptr<D3D12TexturePool> texture_pool = std::static_pointer_cast<D3D12TexturePool>(n_render_system.m_texture_pools[0]);
			if (!texture_pool)
			{
				LOGC("Texture pool is nullptr. This shouldn't happen as the render system should always create the first texture pool");
			}

			data.out_allocator = texture_pool->GetAllocator(DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV);
			data.out_allocation = std::move(data.out_allocator->Allocate(15));

			if (!resize)
			{
				data.m_constant_buffer_pool = n_render_system.CreateConstantBufferPool(
					SizeAlignTwoPower(
						sizeof(temp::ShadowDenoiserSettings_CBData), 256) * d3d12::settings::num_back_buffers * data.m_denoiser_settings_buffer.size());
				for (int i = 0; i < data.m_denoiser_settings_buffer.size(); ++i)
				{
					data.m_denoiser_settings_buffer[i] = data.m_constant_buffer_pool->Create(sizeof(temp::ShadowDenoiserSettings_CBData));
				}			
			}

			data.m_input_render_target = static_cast<d3d12::RenderTarget*>(fg.GetPredecessorRenderTarget<RTShadowData>());

			data.m_gbuffer_render_target = static_cast<d3d12::RenderTarget*>(fg.GetPredecessorRenderTarget<DeferredMainTaskData>());

			d3d12::desc::RenderTargetDesc render_target_desc = {};
			render_target_desc.m_clear_color[0] = 0.f;
			render_target_desc.m_clear_color[1] = 0.f;
			render_target_desc.m_clear_color[2] = 0.f;
			render_target_desc.m_clear_color[3] = 0.f;
			render_target_desc.m_create_dsv_buffer = false;
			render_target_desc.m_dsv_format = Format::UNKNOWN;
			render_target_desc.m_initial_state = ResourceState::NON_PIXEL_SHADER_RESOURCE;
			render_target_desc.m_num_rtv_formats = 1;
			render_target_desc.m_rtv_formats[0] = Format::R16_FLOAT;

			data.m_in_hist_length = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = data.m_gbuffer_render_target->m_create_info.m_rtv_formats[0];

			data.m_in_prev_color = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = Format::R32G32_FLOAT;
			render_target_desc.m_create_dsv_buffer = false;

			data.m_in_prev_moments = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = data.m_gbuffer_render_target->m_create_info.m_rtv_formats[1];

			data.m_in_prev_normals = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = data.m_gbuffer_render_target->m_create_info.m_rtv_formats[4];

			data.m_in_prev_depth = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			data.m_out_color_render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);

			render_target_desc.m_rtv_formats[0] = Format::R32G32_FLOAT;
			render_target_desc.m_initial_state = ResourceState::UNORDERED_ACCESS;

			data.m_out_moments_render_target = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = data.m_in_hist_length->m_create_info.m_rtv_formats[0];

			data.m_out_hist_length_render_target = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			render_target_desc.m_rtv_formats[0] = data.m_out_color_render_target->m_create_info.m_rtv_formats[0];

			data.m_ping_pong_render_target = d3d12::CreateRenderTarget(
				n_render_system.m_device,
				n_render_system.m_viewport.m_viewport.Width,
				n_render_system.m_viewport.m_viewport.Height,
				render_target_desc
			);

			{
				constexpr unsigned int input = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::INPUT);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(input);
				d3d12::CreateSRVFromSpecificRTV(data.m_input_render_target, cpu_handle, 0, data.m_input_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int motion = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::MOTION);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(motion);
				d3d12::CreateSRVFromSpecificRTV(data.m_gbuffer_render_target, cpu_handle, 3, data.m_gbuffer_render_target->m_create_info.m_rtv_formats[3]);
			}

			{
				constexpr unsigned int normals = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::NORMAL);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(normals);
				d3d12::CreateSRVFromSpecificRTV(data.m_gbuffer_render_target, cpu_handle, 1, data.m_gbuffer_render_target->m_create_info.m_rtv_formats[1]);
			}

			{
				constexpr unsigned int depth = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::DEPTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(depth);
				d3d12::CreateSRVFromSpecificRTV(data.m_gbuffer_render_target, cpu_handle, 4, data.m_gbuffer_render_target->m_create_info.m_rtv_formats[4]);
			}

			{
				constexpr unsigned int hist_length = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::IN_HIST_LENGTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(hist_length);
				d3d12::CreateSRVFromSpecificRTV(data.m_in_hist_length, cpu_handle, 0, data.m_in_hist_length->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int prev_color = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_INPUT);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_color);
				d3d12::CreateSRVFromSpecificRTV(data.m_in_prev_color, cpu_handle, 0, data.m_in_prev_color->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int prev_moments = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_MOMENTS);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_moments);
				d3d12::CreateSRVFromSpecificRTV(data.m_in_prev_moments, cpu_handle, 0, data.m_in_prev_moments->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int prev_normals = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_NORMAL);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_normals);
				d3d12::CreateSRVFromSpecificRTV(data.m_in_prev_normals, cpu_handle, 0, data.m_in_prev_normals->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int prev_depth = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_DEPTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_depth);
				d3d12::CreateSRVFromSpecificRTV(data.m_in_prev_depth, cpu_handle, 0, data.m_in_prev_depth->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int out_color = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_COLOR);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_color);
				d3d12::CreateUAVFromSpecificRTV(data.m_out_color_render_target, cpu_handle, 0, data.m_out_color_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int out_moments = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_MOMENTS);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_moments);
				d3d12::CreateUAVFromSpecificRTV(data.m_out_moments_render_target, cpu_handle, 0, data.m_out_moments_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int out_hist_length = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_HIST_LENGTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_hist_length);
				d3d12::CreateUAVFromSpecificRTV(data.m_out_hist_length_render_target, cpu_handle, 0, data.m_out_hist_length_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int input_uav = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PING_PONG_UAV);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(input_uav);
				d3d12::CreateUAVFromSpecificRTV(data.m_ping_pong_render_target, cpu_handle, 0, data.m_ping_pong_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int input_srv = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PING_PONG_SRV);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(input_srv);
				d3d12::CreateSRVFromSpecificRTV(data.m_ping_pong_render_target, cpu_handle, 0, data.m_ping_pong_render_target->m_create_info.m_rtv_formats[0]);
			}

			{
				constexpr unsigned int output_srv = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUTPUT_SRV);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(output_srv);
				d3d12::CreateSRVFromSpecificRTV(data.m_out_color_render_target, cpu_handle, 0, data.m_out_color_render_target->m_create_info.m_rtv_formats[0]);
			}
		}

		inline void BindResources(D3D12RenderSystem& n_render_system, d3d12::CommandList* cmd_list, ShadowDenoiserData& data, bool is_fallback)
		{
			d3d12::BindDescriptorHeaps(cmd_list, is_fallback);

			{
				constexpr unsigned int input = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::INPUT);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(input);
				d3d12::SetShaderSRV(cmd_list, 0, input, cpu_handle);
			}

			{
				constexpr unsigned int motion = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::MOTION);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(motion);
				d3d12::SetShaderSRV(cmd_list, 0, motion, cpu_handle);
			}

			{
				constexpr unsigned int normals = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::NORMAL);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(normals);
				d3d12::SetShaderSRV(cmd_list, 0, normals, cpu_handle);
			}

			{
				constexpr unsigned int depth = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::DEPTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(depth);
				d3d12::SetShaderSRV(cmd_list, 0, depth, cpu_handle);
			}

			{
				constexpr unsigned int hist_length = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::IN_HIST_LENGTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(hist_length);
				d3d12::SetShaderSRV(cmd_list, 0, hist_length, cpu_handle);
			}
			
			{
				constexpr unsigned int prev_color = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_INPUT);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_color);
				d3d12::SetShaderSRV(cmd_list, 0, prev_color, cpu_handle);
			}

			{
				constexpr unsigned int prev_moments = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_MOMENTS);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_moments);
				d3d12::SetShaderSRV(cmd_list, 0, prev_moments, cpu_handle);
			}

			{
				constexpr unsigned int prev_normals = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_NORMAL);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_normals);
				d3d12::SetShaderSRV(cmd_list, 0, prev_normals, cpu_handle);
			}

			{
				constexpr unsigned int prev_depth = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PREV_DEPTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(prev_depth);
				d3d12::SetShaderSRV(cmd_list, 0, prev_depth, cpu_handle);
			}

			{
				constexpr unsigned int out_color = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_COLOR);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_color);
				d3d12::SetShaderUAV(cmd_list, 0, out_color, cpu_handle);
			}

			{
				constexpr unsigned int out_moments = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_MOMENTS);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_moments);
				d3d12::SetShaderUAV(cmd_list, 0, out_moments, cpu_handle);
			}

			{
				constexpr unsigned int out_hist = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_HIST_LENGTH);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(out_hist);
				d3d12::SetShaderUAV(cmd_list, 0, out_hist, cpu_handle);
			}
		}

		inline void Reproject(D3D12RenderSystem& n_render_system, SceneGraph& sg, d3d12::CommandList* cmd_list, ShadowDenoiserData& data, bool is_fallback)
		{
			unsigned int frame_idx = n_render_system.GetFrameIdx();
				
			const auto camera_cb = static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_camera);

			d3d12::BindComputePipeline(cmd_list, data.m_reprojection_pipeline);

			BindResources(n_render_system, cmd_list, data, is_fallback);

			d3d12::BindComputeConstantBuffer(cmd_list, static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_settings_buffer[0])->m_native, 1, frame_idx);

			d3d12::Dispatch(cmd_list,
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Width / 16.f)),
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Height / 16.f)),
				1);

		}

		inline void StoreBuffers(D3D12RenderSystem& n_render_system, SceneGraph& sg, d3d12::CommandList* cmd_list, ShadowDenoiserData& data, bool is_fallback)
		{
			d3d12::Transition(cmd_list, data.m_gbuffer_render_target, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_SOURCE);

			d3d12::Transition(cmd_list, data.m_out_moments_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);
			d3d12::Transition(cmd_list, data.m_out_hist_length_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);

			d3d12::Transition(cmd_list, data.m_in_prev_moments, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);
			d3d12::Transition(cmd_list, data.m_in_prev_normals, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);
			d3d12::Transition(cmd_list, data.m_in_prev_depth, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);

			d3d12::Transition(cmd_list, data.m_in_hist_length, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);

			cmd_list->m_native->CopyResource(data.m_in_prev_moments->m_render_targets[0], data.m_out_moments_render_target->m_render_targets[0]);
			cmd_list->m_native->CopyResource(data.m_in_prev_normals->m_render_targets[0], data.m_gbuffer_render_target->m_render_targets[1]);
			cmd_list->m_native->CopyResource(data.m_in_prev_depth->m_render_targets[0], data.m_gbuffer_render_target->m_render_targets[4]);
			cmd_list->m_native->CopyResource(data.m_in_hist_length->m_render_targets[0], data.m_out_hist_length_render_target->m_render_targets[0]);

			d3d12::Transition(cmd_list, data.m_gbuffer_render_target, ResourceState::COPY_SOURCE, ResourceState::NON_PIXEL_SHADER_RESOURCE);

			d3d12::Transition(cmd_list, data.m_out_moments_render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);
			d3d12::Transition(cmd_list, data.m_out_hist_length_render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);

			d3d12::Transition(cmd_list, data.m_in_prev_moments, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);
			d3d12::Transition(cmd_list, data.m_in_prev_normals, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);
			d3d12::Transition(cmd_list, data.m_in_prev_depth, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);

			d3d12::Transition(cmd_list, data.m_in_hist_length, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);

		}

		inline void FilterMoments(D3D12RenderSystem& n_render_system, SceneGraph& sg, d3d12::CommandList* cmd_list, ShadowDenoiserData& data, bool is_fallback)
		{
			unsigned int frame_idx = n_render_system.GetFrameIdx();

			const auto camera_cb = static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_camera);

			d3d12::BindComputePipeline(cmd_list, data.m_filter_moments_pipeline);

			BindResources(n_render_system, cmd_list, data, is_fallback);

			d3d12::BindComputeConstantBuffer(cmd_list, static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_settings_buffer[0])->m_native, 1, frame_idx);

			d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::NON_PIXEL_SHADER_RESOURCE);

			{
				constexpr unsigned int input_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::INPUT);
				constexpr unsigned int output_srv = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUTPUT_SRV);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(output_srv);
				d3d12::SetShaderSRV(cmd_list, 0, input_id, cpu_handle);
			}

			{
				constexpr unsigned int output_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_COLOR);
				constexpr unsigned int ping_ping_uav = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PING_PONG_UAV);
				auto cpu_handle = data.out_allocation.GetDescriptorHandle(ping_ping_uav);
				d3d12::SetShaderUAV(cmd_list, 0, output_id, cpu_handle);
			}

			d3d12::Dispatch(cmd_list,
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Width / 16.f)),
				static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Height / 16.f)),
				1);
		}

		inline void WaveletFilter(D3D12RenderSystem& n_render_system, SceneGraph& sg, d3d12::CommandList* cmd_list, ShadowDenoiserData& data, bool is_fallback)
		{
			unsigned int frame_idx = n_render_system.GetFrameIdx();

			const auto camera_cb = static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_camera);

			d3d12::BindComputePipeline(cmd_list, data.m_wavelet_filter_pipeline);

			BindResources(n_render_system, cmd_list, data, is_fallback);

			for (int i = 0; i < data.m_denoiser_settings_buffer.size(); ++i)
			{
				d3d12::BindComputeConstantBuffer(cmd_list, static_cast<D3D12ConstantBufferHandle*>(data.m_denoiser_settings_buffer[i])->m_native, 1, frame_idx);

				if (i % 2 == 0)
				{
					d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::NON_PIXEL_SHADER_RESOURCE);
					d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS);

					{
						constexpr unsigned int input_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::INPUT);
						constexpr unsigned int ping_pong_srv = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PING_PONG_SRV);
						auto cpu_handle = data.out_allocation.GetDescriptorHandle(ping_pong_srv);
						d3d12::SetShaderSRV(cmd_list, 0, input_id, cpu_handle);
					}

					{
						constexpr unsigned int output_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_COLOR);
						auto cpu_handle = data.out_allocation.GetDescriptorHandle(output_id);
						d3d12::SetShaderUAV(cmd_list, 0, output_id, cpu_handle);
					}
				}
				else
				{
					d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS);
					d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::NON_PIXEL_SHADER_RESOURCE);

					{
						constexpr unsigned int input_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::INPUT);
						constexpr unsigned int output_srv = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUTPUT_SRV);
						auto cpu_handle = data.out_allocation.GetDescriptorHandle(output_srv);
						d3d12::SetShaderSRV(cmd_list, 0, input_id, cpu_handle);
					}

					{
						constexpr unsigned int output_id = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::OUT_COLOR);
						constexpr unsigned int ping_ping_uav = rs_layout::GetHeapLoc(params::svgf_denoiser, params::SVGFDenoiserE::PING_PONG_UAV);
						auto cpu_handle = data.out_allocation.GetDescriptorHandle(ping_ping_uav);
						d3d12::SetShaderUAV(cmd_list, 0, output_id, cpu_handle);
					}
				}

				d3d12::Dispatch(cmd_list,
					static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Width / 16.f)),
					static_cast<int>(std::ceil(n_render_system.m_viewport.m_viewport.Height / 16.f)),
					1);

				if (i == d3d12::settings::shadow_denoiser_feedback_tap)
				{
					if (i % 2 == 0)
					{
						d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);
						d3d12::Transition(cmd_list, data.m_in_prev_color, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);

						cmd_list->m_native->CopyResource(data.m_in_prev_color->m_render_targets[0], data.m_out_color_render_target->m_render_targets[0]);

						d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);
						d3d12::Transition(cmd_list, data.m_in_prev_color, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);
					}
					else
					{

						d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);
						d3d12::Transition(cmd_list, data.m_in_prev_color, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);

						cmd_list->m_native->CopyResource(data.m_in_prev_color->m_render_targets[0], data.m_ping_pong_render_target->m_render_targets[0]);

						d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);
						d3d12::Transition(cmd_list, data.m_in_prev_color, ResourceState::COPY_DEST, ResourceState::NON_PIXEL_SHADER_RESOURCE);
					}
				}
			}

			if (d3d12::settings::shadow_denoiser_wavelet_iterations % 2 == 0)
			{
				d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);
				d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::COPY_DEST);

				cmd_list->m_native->CopyResource(data.m_out_color_render_target->m_render_targets[0], data.m_ping_pong_render_target->m_render_targets[0]);

				d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);
				d3d12::Transition(cmd_list, data.m_out_color_render_target, ResourceState::COPY_DEST, ResourceState::UNORDERED_ACCESS);
			}
			else
			{
				d3d12::Transition(cmd_list, data.m_ping_pong_render_target, ResourceState::NON_PIXEL_SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS);
			}
		}

		inline void ExecuteShadowDenoiserTask(RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle)
		{
			auto& n_render_system = static_cast<D3D12RenderSystem&>(rs);
			auto& data = fg.GetData<ShadowDenoiserData>(handle);
			auto cmd_list = fg.GetCommandList<d3d12::CommandList>(handle);
			auto render_target = fg.GetRenderTarget<d3d12::RenderTarget>(handle);
			bool is_fallback = d3d12::GetRaytracingType(n_render_system.m_device) == RaytracingType::FALLBACK;
			auto settings = fg.GetSettings<ShadowDenoiserData, ShadowDenoiserSettings>();

			//Populating setting cb
			data.m_denoiser_settings.m_alpha			= settings.m_runtime.m_alpha;
			data.m_denoiser_settings.m_moments_alpha	= settings.m_runtime.m_moments_alpha;
			data.m_denoiser_settings.m_l_phi			= settings.m_runtime.m_l_phi;
			data.m_denoiser_settings.m_z_phi			= settings.m_runtime.m_z_phi;
			data.m_denoiser_settings.m_n_phi			= settings.m_runtime.m_n_phi;

			for (int i = 0; i < data.m_denoiser_settings_buffer.size(); ++i)
			{
				data.m_denoiser_settings.m_step_distance = (float)(1 << i);
				
				data.m_constant_buffer_pool->Update(data.m_denoiser_settings_buffer[i], sizeof(temp::ShadowDenoiserSettings_CBData), 0, n_render_system.GetFrameIdx(), (uint8_t*)& data.m_denoiser_settings);
			}

			{
				const auto viewport = n_render_system.m_viewport;
				const auto frame_idx = n_render_system.GetFrameIdx();
				
				d3d12::Transition(cmd_list, render_target, ResourceState::COPY_SOURCE, ResourceState::UNORDERED_ACCESS);

				Reproject(n_render_system, sg, cmd_list, data, is_fallback);

				StoreBuffers(n_render_system, sg, cmd_list, data, is_fallback);

				FilterMoments(n_render_system, sg, cmd_list, data, is_fallback);

				WaveletFilter(n_render_system, sg, cmd_list, data, is_fallback);
				
				d3d12::Transition(cmd_list, render_target, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SOURCE);
			}
		}

		inline void DestroyShadowDenoiserTask(FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			auto& data = fg.GetData<ShadowDenoiserData>(handle);

			d3d12::Destroy(data.m_in_hist_length);
			d3d12::Destroy(data.m_in_prev_color);
			d3d12::Destroy(data.m_in_prev_moments);
			d3d12::Destroy(data.m_in_prev_normals);
			d3d12::Destroy(data.m_in_prev_depth);
			d3d12::Destroy(data.m_out_hist_length_render_target);
			d3d12::Destroy(data.m_out_moments_render_target);
			d3d12::Destroy(data.m_ping_pong_render_target);

			if (!resize) {
				for (auto buffer : data.m_denoiser_settings_buffer)
				{
					data.m_constant_buffer_pool->Destroy(buffer);
				}
			}
		}

	} /* internal */

	// pass a path to a texture location to load a custom denoiser kernel
	inline void AddShadowDenoiserTask(FrameGraph& fg)
	{
		std::wstring name(L"Shadow Denoiser");

		RenderTargetProperties rt_properties
		{
			RenderTargetProperties::IsRenderWindow(false),
			RenderTargetProperties::Width(std::nullopt),
			RenderTargetProperties::Height(std::nullopt),
			RenderTargetProperties::ExecuteResourceState(ResourceState::UNORDERED_ACCESS),
			RenderTargetProperties::FinishedResourceState(ResourceState::COPY_SOURCE),
			RenderTargetProperties::CreateDSVBuffer(false),
			RenderTargetProperties::DSVFormat(Format::UNKNOWN),
			RenderTargetProperties::RTVFormats({ Format::R16G16B16A16_FLOAT }),
			RenderTargetProperties::NumRTVFormats(1),
			RenderTargetProperties::Clear(true),
			RenderTargetProperties::ClearDepth(true),
			RenderTargetProperties::ResolutionScalar(1.0f)
		};

		RenderTaskDesc desc;
		desc.m_setup_func = [](RenderSystem& rs, FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			internal::SetupShadowDenoiserTask(rs, fg, handle, resize);
		};
		desc.m_execute_func = [](RenderSystem& rs, FrameGraph& fg, SceneGraph& sg, RenderTaskHandle handle)
		{
			internal::ExecuteShadowDenoiserTask(rs, fg, sg, handle);
		};
		desc.m_destroy_func = [](FrameGraph& fg, RenderTaskHandle handle, bool resize)
		{
			internal::DestroyShadowDenoiserTask(fg, handle, resize);
		};

		desc.m_properties = rt_properties;
		desc.m_type = RenderTaskType::COMPUTE;
		desc.m_allow_multithreading = true;

		fg.AddTask<ShadowDenoiserData>(desc, name, FG_DEPS<RTShadowData>());
		fg.UpdateSettings<ShadowDenoiserData>(ShadowDenoiserSettings());
	}

}/* wr */