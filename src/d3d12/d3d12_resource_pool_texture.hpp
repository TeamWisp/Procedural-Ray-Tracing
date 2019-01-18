#pragma once

#include "../resource_pool_texture.hpp"
#include "d3d12_enums.hpp"
#include "d3d12_texture_resources.hpp"
#include <DirectXMath.h>


namespace wr
{
	struct MipMapping_CB
	{
		DirectX::XMFLOAT2 TexelSize;	// 1.0 / OutMip1.Dimensions
	};

	class D3D12RenderSystem;
	class DescriptorAllocator;

	class D3D12TexturePool : public TexturePool
	{
	public:
		explicit D3D12TexturePool(D3D12RenderSystem& render_system, std::size_t size_in_mb, std::size_t num_of_textures);
		~D3D12TexturePool() final;

		void Evict() final;
		void MakeResident() final;
		void Stage(CommandList* cmd_list) final;
		void PostStageClear() final;
		void EndOfFrame() final;

		d3d12::TextureResource* GetTexture(uint64_t texture_id) final;

		[[nodiscard]] TextureHandle CreateCubemap(std::string_view name, uint32_t width, uint32_t height, uint32_t mip_levels, Format format, bool allow_render_dest) final;

		DescriptorAllocator* GetAllocator(DescriptorHeapType type);

		void Unload(uint64_t texture_id) final;

	protected:

		d3d12::TextureResource* LoadPNG(std::string_view path, bool srgb, bool generate_mips) final;
		d3d12::TextureResource* LoadDDS(std::string_view path, bool srgb, bool generate_mips) final;
		d3d12::TextureResource* LoadHDR(std::string_view path, bool srgb, bool generate_mips) final;

		void MoveStagedTextures();
		void GenerateMips(std::vector<d3d12::TextureResource*>& const textures, CommandList* cmd_list);
		void GenerateMips_UAV(std::vector<d3d12::TextureResource*>& const textures, CommandList* cmd_list);
		void GenerateMips_BGR(std::vector<d3d12::TextureResource*>& const textures, CommandList* cmd_list);
		void GenerateMips_SRGB(std::vector<d3d12::TextureResource*>& const textures, CommandList* cmd_list);

		bool CheckUAVCompatibility(Format format);
		bool CheckBGRFormat(Format format);
		bool CheckSRGBFormat(Format format);

		D3D12RenderSystem& m_render_system;

		//CPU only visible heaps used for staging of descriptors.
		//Renderer will copy the descriptor it needs to the GPU visible heap used for rendering.
		DescriptorAllocator* m_allocators[static_cast<size_t>(DescriptorHeapType::DESC_HEAP_TYPE_NUM_TYPES)];


		//Descriptor heap used for compute pipeline when doing mipmapping
		d3d12::DescriptorHeap* m_mipmapping_heap;
		d3d12::DescHeapCPUHandle m_mipmapping_cpu_handle;
		d3d12::DescHeapGPUHandle m_mipmapping_gpu_handle;
	};




}