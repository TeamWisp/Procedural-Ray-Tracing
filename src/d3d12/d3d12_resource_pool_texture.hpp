#pragma once

#include "../resource_pool_texture.hpp"
#include "d3d12_texture_resources.hpp"
#include "../util/thread_pool.hpp"
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
		void Stage() final;
		void WaitForStaging() final;
		void PostStageClear() final;
		void EndOfFrame() final;

		d3d12::TextureResource* GetTexture(uint64_t texture_id) final;
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

		util::ThreadPool* m_thread_pool;

		std::vector<d3d12::CommandList*> m_command_lists;
		std::vector<std::future<void>> m_command_recording_futures;

		d3d12::Fence* m_staging_fence;

		bool m_is_staging;

		//CPU only visible heap used for staging of descriptors.
		//Renderer will copy the descriptor it needs to the GPU visible heap used for rendering.
		DescriptorAllocator* m_texture_heap_allocator;

		//Descriptor heap used for compute pipeline when doing mipmapping
		d3d12::DescriptorHeap* m_mipmapping_heap;
		d3d12::DescHeapCPUHandle m_mipmapping_cpu_handle;
		d3d12::DescHeapGPUHandle m_mipmapping_gpu_handle;
	};




}