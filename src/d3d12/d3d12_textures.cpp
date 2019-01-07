#include "d3d12_functions.hpp"

#include "../util/log.hpp"
#include "d3d12_defines.hpp"
#include "d3dx12.hpp"
#include "d3d12_texture_resources.hpp"

namespace wr::d3d12
{
	TextureResource* CreateTexture(Device* device, desc::TextureDesc* description, bool allow_uav)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Width = description->m_width;
		desc.Height = description->m_height;
		desc.MipLevels = static_cast<UINT16>(description->m_mip_levels);
		desc.DepthOrArraySize = (description->m_depth > 1) ? description->m_array_size : description->m_depth;
		desc.Format = static_cast<DXGI_FORMAT>(description->m_texture_format);
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Flags = allow_uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

		auto native_device = device->m_native;

		ID3D12Resource* resource;

		HRESULT res = native_device->CreateCommittedResource(&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			static_cast<D3D12_RESOURCE_STATES>(description->m_initial_state),
			nullptr,
			IID_PPV_ARGS(&resource));

		if (FAILED(res))
		{
			LOGC("Error: Couldn't create texture");
		}

		// Create intermediate resource on upload heap for staging
		uint64_t textureUploadBufferSize;
		device->m_native->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

		ID3D12Resource* intermediate;

		CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);

		device->m_native->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&intermediate));


		TextureResource* texture = new TextureResource();

		texture->m_width = description->m_width;
		texture->m_height = description->m_height;
		texture->m_depth = description->m_depth;
		texture->m_array_size = description->m_array_size;
		texture->m_mip_levels = description->m_mip_levels;
		texture->m_format = description->m_texture_format;
		texture->m_current_state = ResourceState::COPY_DEST;
		texture->m_resource = resource;
		texture->m_intermediate = intermediate;
		texture->m_need_mips = (texture->m_mip_levels > 1);
		texture->m_is_cubemap = description->m_is_cubemap;
		texture->m_is_staged = false;

		return texture;
	}

	void SetName(TextureResource * tex, std::wstring name)
	{
		tex->m_resource->SetName(name.c_str());
	}

	void CreateSRVFromTexture(TextureResource* tex)
	{
		decltype(Device::m_native) n_device;

		tex->m_resource->GetDevice(IID_PPV_ARGS(&n_device));

		unsigned int increment_size = n_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = (DXGI_FORMAT)tex->m_format;
		srv_desc.Texture2D.MipLevels = tex->m_mip_levels;

		//Calculate dimension
		D3D12_SRV_DIMENSION dimension;

		if (tex->m_is_cubemap)
		{
			dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		}
		else
		{
			if (tex->m_depth > 1)
			{
				//Then it's a 3D texture
				dimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			}
			else
			{
				if (tex->m_height > 1)
				{
					if (tex->m_array_size > 1)
					{
						dimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					}
					else
					{
						dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					}
				}
				else
				{
					//Then it's a 1D texture
					if (tex->m_array_size > 1)
					{
						dimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
					}
					else
					{
						dimension = D3D12_SRV_DIMENSION_TEXTURE1D;
					}
				}
			}

		}

		srv_desc.ViewDimension = dimension;

		d3d12::DescHeapCPUHandle handle = tex->m_desc_allocation.GetDescriptorHandle();

		n_device->CreateShaderResourceView(tex->m_resource, &srv_desc, handle.m_native);
	}

	void SetShaderTexture(wr::d3d12::CommandList* cmd_list, uint32_t rootParameterIndex, uint32_t descriptorOffset, TextureResource* tex)
	{
		d3d12::DescHeapCPUHandle handle = tex->m_desc_allocation.GetDescriptorHandle();

		cmd_list->m_dynamic_descriptor_heaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(rootParameterIndex, descriptorOffset, 1, handle);
	}

	void Destroy(TextureResource* tex)
	{
		SAFE_RELEASE(tex->m_resource);
		delete tex;
	}


} /* wr::d3d12 */