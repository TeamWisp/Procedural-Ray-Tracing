/*
The mipmapping implementation used in this framework is a ported version of 
MiniEngine's implementation.
*/
/*
The MIT License(MIT)

Copyright(c) 2013 - 2015 Microsoft

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "d3d12_resource_pool_texture.hpp"

#include "d3d12_functions.hpp"
#include "d3d12_defines.hpp"
#include "d3d12_renderer.hpp"
#include "d3d12_defines.hpp"

#include "../renderer.hpp"
#include "../settings.hpp"
#include "d3d12_renderer.hpp"
#include "d3d12_structs.hpp"
#include "../pipeline_registry.hpp"
#include "d3d12_descriptors_allocations.hpp"

#include <DirectXTex.h>
#include <comdef.h>


namespace wr
{
	D3D12TexturePool::D3D12TexturePool(D3D12RenderSystem& render_system)
		: TexturePool()
		, m_render_system(render_system)
	{
		auto device = m_render_system.m_device;

		m_staging_textures.resize(d3d12::settings::num_back_buffers);

		//Staging heap
		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			m_allocators[i] = new DescriptorAllocator(render_system, static_cast<DescriptorHeapType>(i));
		}

		m_mipmapping_allocator = new DescriptorAllocator(render_system, DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV);

		// Default UAVs
		m_default_uav = m_mipmapping_allocator->Allocate(4);

		for (uint8_t i = 0; i < 4; ++i)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			uavDesc.Texture2D.MipSlice = i;
			uavDesc.Texture2D.PlaneSlice = 0;

			d3d12::DescHeapCPUHandle handle = m_default_uav.GetDescriptorHandle(i);
			device->m_native->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, handle.m_native);
		}
	}

	D3D12TexturePool::~D3D12TexturePool()
	{
		{
			//Let the allocation go out of scope to clear it before the texture pool and its allocators are destroyed
			DescriptorAllocation alloc = std::move(m_default_uav);
		}
		
		delete m_mipmapping_allocator;

		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			delete m_allocators[i];
		}

		while (m_unstaged_textures.size() > 0)
		{
			uint64_t texture_id = m_unstaged_textures.begin()->first;

			d3d12::TextureResource* texture = static_cast<d3d12::TextureResource*>(m_unstaged_textures.at(texture_id).first);
			m_unstaged_textures.erase(texture_id);

			SAFE_RELEASE(texture->m_resource);
			SAFE_RELEASE(texture->m_intermediate);

			delete texture;
		}
		
		while(m_staged_textures.size() > 0)
		{
			TextureHandle handle = { this, static_cast<uint32_t>(m_staged_textures.begin()->first) };
			D3D12TexturePool::MarkForUnload(handle, 0);
		}

		D3D12TexturePool::UnloadTextures(0);
	}

	void D3D12TexturePool::Evict()
	{
	}

	void D3D12TexturePool::MakeResident()
	{
	}

	void D3D12TexturePool::Stage(CommandList* cmd_list)
	{
		size_t unstaged_number = m_unstaged_textures.size();

		if (unstaged_number > 0)
		{
			d3d12::CommandList* cmdlist = static_cast<d3d12::CommandList*>(cmd_list);

			std::vector<d3d12::TextureResource*> unstaged_textures;
			std::vector<d3d12::TextureResource*> need_mipmapping;

			auto itr = m_unstaged_textures.begin();

			for (itr; itr != m_unstaged_textures.end(); ++itr)
			{
				d3d12::TextureResource* texture = static_cast<d3d12::TextureResource*>(itr->second.first);

				unstaged_textures.push_back(texture);

				decltype(d3d12::Device::m_native) n_device;
				texture->m_resource->GetDevice(IID_PPV_ARGS(&n_device));


				DirectX::ScratchImage* image = itr->second.second;

				texture->m_subresources.resize(image->GetImageCount());
				const DirectX::Image* pImages = image->GetImages();
				for (int i = 0; i < image->GetImageCount(); ++i)
				{
					auto& subresource = texture->m_subresources[i];
					subresource.RowPitch = pImages[i].rowPitch;
					subresource.SlicePitch = pImages[i].slicePitch;
					subresource.pData = pImages[i].pixels;
				}


				std::vector<D3D12_SUBRESOURCE_DATA> subresources = texture->m_subresources;
				UpdateSubresources(cmdlist->m_native, texture->m_resource, texture->m_intermediate, 0, 0, static_cast<uint32_t>(subresources.size()), subresources.data());

				texture->m_is_staged = true;

				if (texture->m_need_mips)
				{
					need_mipmapping.push_back(texture);
				}
			}

			d3d12::Transition(cmdlist, unstaged_textures, wr::ResourceState::COPY_DEST, wr::ResourceState::PIXEL_SHADER_RESOURCE);

			for (auto* t : need_mipmapping)
			{
				GenerateMips(t, cmd_list);
			}

			MoveStagedTextures(m_render_system.GetFrameIdx());
		}
	}

	void D3D12TexturePool::PostStageClear()
	{
		
	}

	void D3D12TexturePool::ReleaseTemporaryResources()
	{
		m_mipmapping_allocator->ReleaseStaleDescriptors();

		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			m_allocators[i]->ReleaseStaleDescriptors();
		}

		unsigned int frame_idx = m_render_system.GetFrameIdx();

		//Release temporary textures
		for (auto* t : m_temporary_textures[frame_idx])
		{
			d3d12::Destroy(t);
		}

		m_temporary_textures[frame_idx].clear();

		//Release temporary heaps
		for (auto* h : m_temporary_heaps[frame_idx])
		{
			d3d12::Destroy(h);
		}


		for (auto& map : m_staging_textures[frame_idx])
		{
			auto* texture = (d3d12::TextureResource*) map.second;

			if (texture->m_intermediate)
				SAFE_RELEASE(texture->m_intermediate);

			((d3d12::TextureResource*)m_staged_textures[map.first])->m_intermediate = nullptr;
		}

		m_staging_textures[frame_idx].clear();
		m_temporary_heaps[frame_idx].clear();
	}

	d3d12::TextureResource* D3D12TexturePool::GetTextureResource(TextureHandle handle)
	{
		return static_cast<d3d12::TextureResource*>(m_staged_textures.at(handle.m_id));
	}

	TextureHandle D3D12TexturePool::CreateCubemap(std::string_view name, uint32_t width, uint32_t height, uint32_t mip_levels, Format format, bool allow_render_dest)
	{
		auto device = m_render_system.m_device;

		d3d12::desc::TextureDesc desc;

		uint32_t mip_lvl_final = mip_levels;

		// 0 means generate max number of mip levels
		if (mip_lvl_final == 0)
		{
			mip_lvl_final = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
		}

		desc.m_width = width;
		desc.m_height = height;
		desc.m_is_cubemap = true;
		desc.m_depth = 1;
		desc.m_array_size = 6;
		desc.m_mip_levels = mip_lvl_final;
		desc.m_texture_format = format;
		desc.m_initial_state = allow_render_dest ? ResourceState::RENDER_TARGET : ResourceState::COPY_DEST;

		d3d12::TextureResource* texture = d3d12::CreateTexture(device, &desc, true);

		texture->m_allow_render_dest = allow_render_dest;
		texture->m_is_staged = true;
		texture->m_need_mips = false;

		std::wstring wide_string(name.begin(), name.end());

		d3d12::SetName(texture, wide_string);

		DescriptorAllocation srv_alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();
		DescriptorAllocation uav_alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();

		if (srv_alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}
		if (uav_alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}

		texture->m_srv_allocation = std::move(srv_alloc);
		texture->m_uav_allocation = std::move(uav_alloc);

		d3d12::CreateSRVFromTexture(texture);

		if (allow_render_dest)
		{
			DescriptorAllocation alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->Allocate(6);

			if (alloc.IsNull())
			{
				LOGC("Couldn't allocate descriptor for the texture resource");
			}

			texture->m_rtv_allocation = std::move(alloc);

			d3d12::CreateRTVFromCubemap(texture);
		}

		m_loaded_textures++;

		uint64_t texture_id = m_id_factory.GetUnusedID();

		TextureHandle texture_handle;
		texture_handle.m_pool = this;
		texture_handle.m_id = static_cast<std::uint32_t>(texture_id);

		m_staged_textures.insert(std::make_pair(texture_id, texture));

		return texture_handle;
	}

	TextureHandle D3D12TexturePool::CreateTexture(std::string_view name, uint32_t width, uint32_t height, uint32_t mip_levels, Format format, bool allow_render_dest)
	{
		auto device = m_render_system.m_device;

		d3d12::desc::TextureDesc desc;

		uint32_t mip_lvl_final = mip_levels;

		// 0 means generate max number of mip levels
		if (mip_lvl_final == 0)
		{
			mip_lvl_final = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
		}

		desc.m_width = width;
		desc.m_height = height;
		desc.m_is_cubemap = false;
		desc.m_depth = 1;
		desc.m_array_size = 1;
		desc.m_mip_levels = mip_lvl_final;
		desc.m_texture_format = format;
		desc.m_initial_state = allow_render_dest ? ResourceState::RENDER_TARGET : ResourceState::COPY_DEST;

		d3d12::TextureResource* texture = d3d12::CreateTexture(device, &desc, true);

		texture->m_allow_render_dest = allow_render_dest;
		texture->m_is_staged = true;
		texture->m_need_mips = false;

		std::wstring wide_string(name.begin(), name.end());

		d3d12::SetName(texture, wide_string);

		DescriptorAllocation srv_alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();
		DescriptorAllocation uav_alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();

		if (srv_alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}
		if (uav_alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}

		texture->m_srv_allocation = std::move(srv_alloc);
		texture->m_uav_allocation = std::move(uav_alloc);

		d3d12::CreateSRVFromTexture(texture);
		d3d12::CreateUAVFromTexture(texture, 0);

		if (allow_render_dest)
		{
			DescriptorAllocation alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->Allocate(6);

			if (alloc.IsNull())
			{
				LOGC("Couldn't allocate descriptor for the texture resource");
			}

			texture->m_rtv_allocation = std::move(alloc);

			d3d12::CreateRTVFromTexture2D(texture);
		}

		m_loaded_textures++;

		uint64_t texture_id = m_id_factory.GetUnusedID();

		TextureHandle texture_handle;
		texture_handle.m_pool = this;
		texture_handle.m_id = static_cast<std::uint32_t>(texture_id);

		m_staged_textures.insert(std::make_pair(texture_id, texture));

		return texture_handle;
	}

	DescriptorAllocator * D3D12TexturePool::GetAllocator(DescriptorHeapType type)
	{
		return m_allocators[static_cast<size_t>(type)];
	}

	void D3D12TexturePool::MarkForUnload(TextureHandle& handle, unsigned int frame_idx)
	{
		uint64_t texture_id = handle.m_id;

		d3d12::TextureResource* texture = static_cast<d3d12::TextureResource*>(m_staged_textures.at(texture_id));
		m_staged_textures.erase(texture_id);
		m_staging_textures.at(frame_idx).erase(texture_id);

		m_marked_for_unload.at(frame_idx).push_back(texture);

#ifdef _DEBUG
		LOGW("[DEBUG MESSAGE]: Handle {} from {} invalidated.", texture_id, m_name);
#endif

		handle.m_pool = nullptr;
		handle.m_id = -UINT_MAX;
	}

	void D3D12TexturePool::UnloadTextures(unsigned int frame_idx)
	{
		auto& vec = m_marked_for_unload.at(frame_idx);

		for (auto* texture : vec)
		{
			SAFE_RELEASE(texture->m_resource);

			if (texture->m_intermediate)
				SAFE_RELEASE(texture->m_intermediate);

			delete texture;
		}

		m_marked_for_unload.at(frame_idx).clear();
	}

	TextureHandle D3D12TexturePool::LoadFromFile(std::string_view path, bool srgb, bool generate_mips)
	{
		auto device = m_render_system.m_device;

		DirectX::TexMetadata metadata;
		DirectX::ScratchImage* image = new DirectX::ScratchImage;

		std::optional<std::string_view> extension = util::GetFileExtension(path);
		std::wstring wide_string(path.begin(), path.end());

		if (std::string_view ext_int = extension.value(); extension.has_value())
		{
			HRESULT hr = S_OK;

			if (ext_int.find("png") != std::string_view::npos
				|| ext_int.find("jpeg") != std::string_view::npos
				|| ext_int.find("jpg") != std::string_view::npos
				|| ext_int.find("bmp") != std::string_view::npos)
			{
				hr = LoadFromWICFile(wide_string.c_str(),
					DirectX::WIC_FLAGS_NONE, &metadata, *image);
			}
			else if (ext_int.find("dds") != std::string_view::npos)
			{
				hr = LoadFromDDSFile(wide_string.c_str(),
					DirectX::DDS_FLAGS_NONE, &metadata, *image);
			}
			else if (ext_int.find("hdr") != std::string_view::npos)
			{
				hr = LoadFromHDRFile(wide_string.c_str(), &metadata, *image);
			}
			else if (ext_int.find("tga") != std::string_view::npos)
			{
				hr = DirectX::LoadFromTGAFile(wide_string.c_str(), &metadata, *image);
			}
			else
			{
				LOGC("Texture {} not loaded. Format not supported.", path);
				return {};
			}

			if (FAILED(hr))
			{
				_com_error err(hr);
				LPCTSTR errMsg = err.ErrorMessage();

				LOGC("ERROR: DirectXTex error: {}", errMsg);
			}
		}

		uint32_t mip_lvls;

		bool mip_generation = (metadata.mipLevels > 1) ? false : generate_mips;

		if (mip_generation)
		{
			mip_lvls = static_cast<uint32_t>(std::floor(std::log2(std::max(metadata.width, metadata.height)))) + 1;
		}
		else
		{
			mip_lvls = static_cast<std::uint32_t>(metadata.mipLevels);
		}

		Format texture_format = static_cast<wr::Format>(metadata.format);

		if (srgb)
		{
			texture_format = static_cast<Format>(DirectX::MakeSRGB(static_cast<DXGI_FORMAT>(texture_format)));
		}

		d3d12::desc::TextureDesc desc;

		desc.m_width = static_cast<std::uint32_t>(metadata.width);
		desc.m_height = static_cast<std::uint32_t>(metadata.height);
		desc.m_is_cubemap = metadata.IsCubemap();
		desc.m_depth = static_cast<std::uint32_t>(metadata.depth);
		desc.m_array_size = static_cast<std::uint32_t>(metadata.arraySize);
		desc.m_mip_levels = mip_lvls;
		desc.m_texture_format = texture_format;
		desc.m_initial_state = ResourceState::COPY_DEST;

		d3d12::TextureResource* texture = d3d12::CreateTexture(device, &desc, mip_generation);

		DescriptorAllocation alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();

		if (alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}

		texture->m_need_mips = mip_generation;
		texture->m_srv_allocation = std::move(alloc);
		texture->m_resource->SetName(wide_string.c_str());

		d3d12::CreateSRVFromTexture(texture);

		m_loaded_textures++;

		LOG("[TEXTURE LOADED] {}", path);

		uint64_t texture_id = m_id_factory.GetUnusedID();

		TextureHandle texture_handle;
		texture_handle.m_pool = this;
		texture_handle.m_id = static_cast<std::uint32_t>(texture_id);

		m_unstaged_textures.insert(std::make_pair(texture_id, std::make_pair(texture, image)));

		return texture_handle;
	}
	
	TextureHandle D3D12TexturePool::LoadFromMemory(unsigned char* data, size_t width, size_t height, TextureFormat type, bool srgb, bool generate_mips)
	{
		auto device = m_render_system.m_device;

		DirectX::TexMetadata metadata;
		DirectX::ScratchImage* image = new DirectX::ScratchImage;

		HRESULT hr;

		switch (type)
		{
		case wr::TextureFormat::WIC:
		{
			//Assuming RGBA8_UNORM for the time being, need to find a solution
			hr = LoadFromWICMemory(data, width * height * 8/*bits*/ * 4/*channels*/,
				DirectX::WIC_FLAGS_NONE, &metadata, *image);

			break;
		}
		case wr::TextureFormat::DDS:
		{
			//Assuming RGBA8_UNORM for the time being, need to find a solution
			hr = LoadFromDDSMemory(data, width * height * 8/*bits*/ * 4/*channels*/,
				DirectX::DDS_FLAGS_NONE, &metadata, *image);

			break;
		}

		case wr::TextureFormat::HDR:
		{
			//Assuming RGBA16_FLOAT for the time being, need to find a solution
			hr = LoadFromHDRMemory(data, width * height * 16/*bits*/ * 4/*channels*/,
				&metadata, *image);

			break;
		}
		case wr::TextureFormat::RAW:
		{
			DirectX::Image temp_image = {};

			temp_image.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			temp_image.width = width;
			temp_image.height = height;
			temp_image.rowPitch = width * 4;
			temp_image.slicePitch = width * height * 4;
			temp_image.pixels = data;

			hr = image->InitializeFromImage(temp_image);

			metadata.width = width;
			metadata.height = height;
			metadata.arraySize = 1;
			metadata.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			metadata.mipLevels = 1;
			metadata.depth = 1;
			metadata.dimension = DirectX::TEX_DIMENSION::TEX_DIMENSION_TEXTURE2D;
			metadata.miscFlags = 0;
			metadata.miscFlags2 = 0;

			break;
		}

		default:
			LOGC("[ERROR]: How did we even get here?")
			break;
		}

		if (FAILED(hr))
		{
			_com_error err(hr);
			LPCTSTR errMsg = err.ErrorMessage();

			LOGC("ERROR: DirectXTex error: {}", errMsg);
		}

		uint32_t mip_lvls;

		bool mip_generation = (metadata.mipLevels > 1) ? false : generate_mips;

		if (mip_generation)
		{
			mip_lvls = static_cast<uint32_t>(std::floor(std::log2(std::max(metadata.width, metadata.height)))) + 1;
		}
		else
		{
			mip_lvls = static_cast<std::uint32_t>(metadata.mipLevels);
		}

		Format texture_format = static_cast<wr::Format>(metadata.format);

		d3d12::desc::TextureDesc desc;

		desc.m_width = static_cast<std::uint32_t>(metadata.width);
		desc.m_height = static_cast<std::uint32_t>(metadata.height);
		desc.m_is_cubemap = metadata.IsCubemap();
		desc.m_depth = static_cast<std::uint32_t>(metadata.depth);
		desc.m_array_size = static_cast<std::uint32_t>(metadata.arraySize);
		desc.m_mip_levels = mip_lvls;
		desc.m_texture_format = texture_format;
		desc.m_initial_state = ResourceState::COPY_DEST;

		d3d12::TextureResource* texture = d3d12::CreateTexture(device, &desc, mip_generation);

		DescriptorAllocation alloc = m_allocators[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate();

		if (alloc.IsNull())
		{
			LOGC("Couldn't allocate descriptor for the texture resource");
		}

		texture->m_need_mips = mip_generation;
		texture->m_srv_allocation = std::move(alloc);

		std::wstring name = L"TextureFromMemory" + std::to_wstring(m_loaded_textures);
		texture->m_resource->SetName(name.c_str());

		d3d12::CreateSRVFromTexture(texture);

		m_loaded_textures++;

		LOG("[TEXTURE LOADED]: Texture from Memory");

		uint64_t texture_id = m_id_factory.GetUnusedID();

		TextureHandle texture_handle;
		texture_handle.m_pool = this;
		texture_handle.m_id = static_cast<std::uint32_t>(texture_id);

		m_unstaged_textures.insert(std::make_pair(texture_id, std::make_pair(texture, image)));

		return texture_handle;

	}

	void D3D12TexturePool::MoveStagedTextures(unsigned int frame_idx)
	{
		for (auto itr = m_unstaged_textures.begin(); itr != m_unstaged_textures.end(); ++itr)
		{
			m_staged_textures.insert(std::make_pair(itr->first, itr->second.first));
			m_staging_textures[frame_idx].insert(std::make_pair(itr->first, itr->second.first));

			//Free the ScratchImage as it's not needed anymore after staging
			delete itr->second.second;
		}

		m_unstaged_textures.clear();
	}

	void D3D12TexturePool::GenerateMips(d3d12::TextureResource* texture, CommandList* cmd_list)
	{
		wr::d3d12::CommandList* d3d12_cmd_list = static_cast<wr::d3d12::CommandList*>(cmd_list);
		auto pipeline = static_cast<d3d12::PipelineState*>(PipelineRegistry::Get().Find(pipelines::mip_mapping));

		d3d12::BindComputePipeline(d3d12_cmd_list, pipeline);

		if (texture->m_need_mips)
		{
			if (d3d12::CheckUAVCompatibility(texture->m_format) || d3d12::CheckOptionalUAVFormat(texture->m_format))
			{
				GenerateMips_UAV(texture, cmd_list);
			}
			else if (d3d12::CheckBGRFormat(texture->m_format))
			{
				GenerateMips_BGR(texture, cmd_list);
			}
			else if (d3d12::CheckSRGBFormat(texture->m_format))
			{
				GenerateMips_SRGB(texture, cmd_list);
			}
			else
			{
				LOGC("[ERROR]: GenerateMips-> I don't know how we ended up here!");
			}
		}
	}

	void D3D12TexturePool::GenerateMips_UAV(d3d12::TextureResource* texture, CommandList* cmd_list)
	{
		wr::d3d12::CommandList* d3d12_cmd_list = static_cast<wr::d3d12::CommandList*>(cmd_list);
		
		//Create shader resource view for the source texture in the descriptor heap
		DescriptorAllocation srv_alloc = m_mipmapping_allocator->Allocate();
		d3d12::DescHeapCPUHandle srv_handle = srv_alloc.GetDescriptorHandle();

		d3d12::CreateSRVFromTexture(texture, srv_handle);

		MipMapping_CB generate_mips_cb;

		for (uint32_t src_mip = 0; src_mip < texture->m_mip_levels - 1u;)
		{
			uint32_t src_width = static_cast<std::uint32_t>(texture->m_width) >> src_mip;
			uint32_t src_height = static_cast<std::uint32_t>(texture->m_height) >> src_mip;
			uint32_t dst_width = src_width >> 1;
			uint32_t dst_height = src_height >> 1;

			// Determine the compute shader to use based on the dimension of the 
			// source texture.
			// 0b00(0): Both width and height are even.
			// 0b01(1): Width is odd, height is even.
			// 0b10(2): Width is even, height is odd.
			// 0b11(3): Both width and height are odd.
			generate_mips_cb.src_dimension = (src_height & 1) << 1 | (src_width & 1);

			// Max amount of mip levels to compute in a pass is 4.
			DWORD mip_count;

			// The number of times we can half the size of the texture and get
			// exactly a 50% reduction in size.
			// A 1 bit in the width or height indicates an odd dimension.
			// The case where either the width or the height is exactly 1 is handled
			// as a special case (as the dimension does not require reduction).
			_BitScanForward(&mip_count, (dst_width == 1 ? dst_height : dst_width) |
										(dst_height == 1 ? dst_width : dst_height));

			// Maximum number of mips to generate is 4.
			mip_count = std::min<DWORD>(4, mip_count + 1);
			// Clamp to total number of mips left over.
			mip_count = ((static_cast<DWORD>(src_mip) + mip_count) > texture->m_mip_levels) ? static_cast<DWORD>(texture->m_mip_levels - src_mip) : mip_count;

			// Dimensions should not reduce to 0.
			// This can happen if the width and height are not the same.
			dst_width = std::max<DWORD>(1, dst_width);
			dst_height = std::max<DWORD>(1, dst_height);

			generate_mips_cb.src_mip_level = src_mip;
			generate_mips_cb.num_mip_levels = mip_count;
			generate_mips_cb.texel_size.x = 1.0f / (float)dst_width;
			generate_mips_cb.texel_size.y = 1.0f / (float)dst_height;

			d3d12::BindCompute32BitConstants(d3d12_cmd_list, &generate_mips_cb, sizeof(MipMapping_CB) / sizeof(uint32_t), 0, 0);

			d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[src_mip], ResourceState::PIXEL_SHADER_RESOURCE, src_mip, 1);

			d3d12::SetShaderSRV(d3d12_cmd_list, 1, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::SOURCE)), srv_handle);

			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				std::uint32_t idx = src_mip + mip + 1u;

				d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[idx], ResourceState::UNORDERED_ACCESS, idx, 1);

				DescriptorAllocation uav_alloc = m_mipmapping_allocator->Allocate();
				d3d12::DescHeapCPUHandle uav_handle = uav_alloc.GetDescriptorHandle();

				d3d12::CreateUAVFromTexture(texture, uav_handle, src_mip + mip + 1);

				d3d12::SetShaderUAV(d3d12_cmd_list, 1, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::DEST)) + mip, uav_handle);
			}
			// Pad any unused mip levels with a default UAV to keeps the DX12 runtime happy.
			if (mip_count < 4)
			{
				d3d12_cmd_list->m_dynamic_descriptor_heaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
					COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::DEST)),
					mip_count + 1, 4 - mip_count, m_default_uav.GetDescriptorHandle());
			}

			d3d12::Dispatch(d3d12_cmd_list, ((dst_width + 8 - 1) / 8), ((dst_height + 8 - 1) / 8), 1u);

			//Wait for all accesses to the destination texture UAV to be finished before generating the next mipmap, as it will be the source texture for the next mipmap
			d3d12::UAVBarrier(d3d12_cmd_list, { texture });

			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				std::uint32_t idx = src_mip + mip + 1u;

				d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[idx], ResourceState::PIXEL_SHADER_RESOURCE, idx, 1);
			}

			src_mip += mip_count;
		}

		texture->m_need_mips = false;
	}

	void D3D12TexturePool::GenerateMips_BGR(d3d12::TextureResource* texture, CommandList* cmd_list)
	{
		auto device = m_render_system.m_device;
		wr::d3d12::CommandList* d3d12_cmd_list = static_cast<wr::d3d12::CommandList*>(cmd_list);
		unsigned int frame_idx = m_render_system.GetFrameIdx();

		//Create new resource with UAV compatible format
		d3d12::desc::TextureDesc copy_desc;
		copy_desc.m_width = static_cast<std::uint32_t>(texture->m_width);
		copy_desc.m_height = static_cast<std::uint32_t>(texture->m_height);
		copy_desc.m_depth = static_cast<std::uint32_t>(texture->m_depth);
		copy_desc.m_array_size = static_cast<std::uint32_t>(texture->m_array_size);
		copy_desc.m_initial_state = ResourceState::COMMON;
		copy_desc.m_is_cubemap = texture->m_is_cubemap;
		copy_desc.m_mip_levels = static_cast<std::uint32_t>(texture->m_mip_levels);
		copy_desc.m_texture_format = Format::R8G8B8A8_UNORM;

		// Create a heap to alias the resource. This is used to copy the resource without 
		// failing GPU validation.
		auto resource = texture->m_resource;
		auto resourceDesc = resource->GetDesc();

		auto allocation_info = device->m_native->GetResourceAllocationInfo(0, 1, &resourceDesc);

		d3d12::Heap<wr::HeapOptimization::BIG_STATIC_BUFFERS>* heap;
		heap = d3d12::CreateHeap_BSBO(device, allocation_info.SizeInBytes, ResourceType::TEXTURE, 1);

		m_temporary_heaps[frame_idx].push_back(heap);

		//Create the copy resource placed in the heap.
		d3d12::TextureResource* copy_texture = d3d12::CreatePlacedTexture(device, &copy_desc, true, heap);

		m_temporary_textures[frame_idx].push_back(copy_texture);

		// Create an alias for which to perform the copy operation.
		d3d12::desc::TextureDesc alias_desc = copy_desc;
		alias_desc.m_texture_format = (texture->m_format == Format::B8G8R8X8_UNORM ||
										texture->m_format == Format::B8G8R8X8_UNORM_SRGB) ?
											Format::B8G8R8X8_UNORM : Format::B8G8R8A8_UNORM;

		d3d12::TextureResource* alias_texture = d3d12::CreatePlacedTexture(device, &alias_desc, true, heap);

		m_temporary_textures[frame_idx].push_back(alias_texture);

		d3d12::Alias(d3d12_cmd_list, nullptr, alias_texture);

		//Copy resource
		d3d12::CopyResource(d3d12_cmd_list, texture, alias_texture);

		// Alias the UAV compatible texture back.
		d3d12::Alias(d3d12_cmd_list, alias_texture, copy_texture);

		//Now use the resource copy to generate the mips
		GenerateMips_UAV(copy_texture, cmd_list);

		// Copy back to the original (via the alias to ensure GPU validation)
		d3d12::Alias(d3d12_cmd_list, copy_texture, alias_texture);

		d3d12::CopyResource(d3d12_cmd_list, alias_texture, texture);
	}

	void D3D12TexturePool::GenerateMips_SRGB(d3d12::TextureResource* texture, CommandList* cmd_list)
	{
		auto device = m_render_system.m_device;
		wr::d3d12::CommandList* d3d12_cmd_list = static_cast<wr::d3d12::CommandList*>(cmd_list);

		//Create copy of the texture and store it for later deletion.
		d3d12::desc::TextureDesc copy_desc;

		copy_desc.m_width = static_cast<std::uint32_t>(texture->m_width);
		copy_desc.m_height = static_cast<std::uint32_t>(texture->m_height);
		copy_desc.m_is_cubemap = texture->m_is_cubemap;
		copy_desc.m_depth = static_cast<std::uint32_t>(texture->m_depth);
		copy_desc.m_array_size = static_cast<std::uint32_t>(texture->m_array_size);
		copy_desc.m_mip_levels = static_cast<std::uint32_t>(texture->m_mip_levels);
		copy_desc.m_texture_format = d3d12::RemoveSRGB(texture->m_format);
		copy_desc.m_initial_state = ResourceState::COPY_DEST;

		auto texture_copy = d3d12::CreateTexture(device, &copy_desc, true);

		unsigned int frame_idx = m_render_system.GetFrameIdx();

		m_temporary_textures[frame_idx].push_back(texture_copy);

		d3d12::CopyResource(d3d12_cmd_list, texture, texture_copy);

		GenerateMips(texture_copy, cmd_list);

		d3d12::CopyResource(d3d12_cmd_list, texture_copy, texture);
	}

	void D3D12TexturePool::GenerateMips_Cubemap(d3d12::TextureResource* texture, CommandList* cmd_list, unsigned int array_slice)
	{
		wr::d3d12::CommandList* d3d12_cmd_list = static_cast<wr::d3d12::CommandList*>(cmd_list);

		//Create shader resource view for the source texture in the descriptor heap
		DescriptorAllocation srv_alloc = m_mipmapping_allocator->Allocate();
		d3d12::DescHeapCPUHandle srv_handle = srv_alloc.GetDescriptorHandle();

		d3d12::CreateSRVFromCubemapFace(texture, srv_handle, static_cast<unsigned int>(texture->m_mip_levels), 0, array_slice);

		MipMapping_CB generate_mips_cb;

		for (uint32_t src_mip = 0; src_mip < texture->m_mip_levels - 1u;)
		{
			uint32_t src_width = static_cast<std::uint32_t>(texture->m_width) >> src_mip;
			uint32_t src_height = static_cast<std::uint32_t>(texture->m_height) >> src_mip;
			uint32_t dst_width = src_width >> 1;
			uint32_t dst_height = src_height >> 1;

			// Determine the compute shader to use based on the dimension of the 
			// source texture.
			// 0b00(0): Both width and height are even.
			// 0b01(1): Width is odd, height is even.
			// 0b10(2): Width is even, height is odd.
			// 0b11(3): Both width and height are odd.
			generate_mips_cb.src_dimension = (src_height & 1) << 1 | (src_width & 1);

			// Max amount of mip levels to compute in a pass is 4.
			DWORD mip_count;

			// The number of times we can half the size of the texture and get
			// exactly a 50% reduction in size.
			// A 1 bit in the width or height indicates an odd dimension.
			// The case where either the width or the height is exactly 1 is handled
			// as a special case (as the dimension does not require reduction).
			_BitScanForward(&mip_count, (dst_width == 1 ? dst_height : dst_width) |
				(dst_height == 1 ? dst_width : dst_height));

			// Maximum number of mips to generate is 4.
			mip_count = std::min<DWORD>(4, mip_count + 1);
			// Clamp to total number of mips left over.
			mip_count = ((static_cast<DWORD>(src_mip) + mip_count) > texture->m_mip_levels) ? static_cast<DWORD>(texture->m_mip_levels - src_mip) : mip_count;

			// Dimensions should not reduce to 0.
			// This can happen if the width and height are not the same.
			dst_width = std::max<DWORD>(1, dst_width);
			dst_height = std::max<DWORD>(1, dst_height);

			generate_mips_cb.src_mip_level = src_mip;
			generate_mips_cb.num_mip_levels = mip_count;
			generate_mips_cb.texel_size.x = 1.0f / (float)dst_width;
			generate_mips_cb.texel_size.y = 1.0f / (float)dst_height;

			d3d12::BindCompute32BitConstants(d3d12_cmd_list, &generate_mips_cb, sizeof(MipMapping_CB) / sizeof(uint32_t), 0, 0);

			d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[src_mip], ResourceState::PIXEL_SHADER_RESOURCE, src_mip, 1);

			d3d12::SetShaderSRV(d3d12_cmd_list, 1, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::SOURCE)), srv_handle);

			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				uint32_t idx = src_mip + mip + 1u;

				d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[idx], ResourceState::UNORDERED_ACCESS, idx, 1);

				DescriptorAllocation uav_alloc = m_mipmapping_allocator->Allocate();
				d3d12::DescHeapCPUHandle uav_handle = uav_alloc.GetDescriptorHandle();

				d3d12::CreateUAVFromCubemapFace(texture, uav_handle, src_mip + mip + 1, array_slice);

				d3d12::SetShaderUAV(d3d12_cmd_list, 1, COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::DEST)) + mip, uav_handle);
			}
			// Pad any unused mip levels with a default UAV to keeps the DX12 runtime happy.
			if (mip_count < 4)
			{
				d3d12_cmd_list->m_dynamic_descriptor_heaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
					COMPILATION_EVAL(rs_layout::GetHeapLoc(params::mip_mapping, params::MipMappingE::DEST)),
					mip_count + 1, 4 - mip_count, m_default_uav.GetDescriptorHandle());
			}

			d3d12::Dispatch(d3d12_cmd_list, ((dst_width + 8 - 1) / 8), ((dst_height + 8 - 1) / 8), 1u);

			//Wait for all accesses to the destination texture UAV to be finished before generating the next mipmap, as it will be the source texture for the next mipmap
			d3d12::UAVBarrier(d3d12_cmd_list, { texture });

			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				uint32_t idx = src_mip + mip + 1u;

				d3d12::Transition(d3d12_cmd_list, texture, texture->m_subresource_states[idx], ResourceState::PIXEL_SHADER_RESOURCE, idx, 1);
			}

			src_mip += mip_count;
		}

		texture->m_need_mips = false;
	}
}