#pragma once

#include <memory>

#include "d3d12_structs.hpp"

namespace wr::d3d12
{

	// Device
	[[nodiscard]] Device* CreateDevice();
	void Destroy(Device* device);

	// CommandQueue
	[[nodiscard]] CommandQueue* CreateCommandQueue(Device* device, CmdListType type);
	void Execute(CommandQueue* cmd_queue, std::vector<CommandList*> const & cmd_lists, Fence* fence);
	void Destroy(CommandQueue* cmd_queue);

	// CommandList
	[[nodiscard]] CommandList* CreateCommandList(Device* device, unsigned int num_allocators, CmdListType type);
	void SetName(CommandList* cmd_list, std::string const & name);
	void SetName(CommandList* cmd_list, std::wstring const & name);
	void Begin(CommandList* cmd_list, unsigned int frame_idx);
	void End(CommandList* cmd_list);
	void BindRenderTarget(CommandList* cmd_list, RenderTarget* render_target, bool clear = true, bool clear_depth = true);
	void BindRenderTargetVersioned(CommandList* cmd_list, RenderTarget* render_target, unsigned int frame_idx, bool clear = true, bool clear_depth = true);
	void BindRenderTargetOnlyDepth(CommandList* cmd_list, RenderTarget* render_target, unsigned int frame_idx, bool clear = true);
	void BindViewport(CommandList* cmd_list, Viewport const & viewport);
	void BindPipeline(CommandList* cmd_list, PipelineState* pipeline_state);
	void BindDescriptorHeaps(CommandList* cmd_list, std::vector<DescriptorHeap*> heaps);
	//void BindCompute(CommandList& cmd_list, PipelineState* pipeline_state);
	void SetPrimitiveTopology(CommandList* cmd_list, D3D12_PRIMITIVE_TOPOLOGY topology);
	void BindConstantBuffer(CommandList* cmd_list, HeapResource* buffer, unsigned int root_parameter_idx, unsigned int frame_idx);
	// void BindCompute(CommandList* cmd_list, ConstantBuffer* buffer, unsigned int root_parameter_idx, unsigned int frame_idx);
	// void Bind(CommandList& cmd_list, TextureArray& ta, unsigned int root_param_index);
	void BindDescriptorTable(CommandList* cmd_list, DescHeapGPUHandle& handle, unsigned int root_param_index);
	//void BindCompute(CommandList& cmd_list, DescHeapGPUHandle& handle, unsigned int root_param_index);
	void Bind(CommandList& cmd_list, std::vector<DescriptorHeap*> const & heaps);
	void BindVertexBuffer(CommandList* cmd_list, StagingBuffer* buffer, std::size_t offset, std::size_t size, std::size_t m_stride);
	void BindIndexBuffer(CommandList* cmd_list, StagingBuffer* buffer, unsigned int offset, unsigned int size);
	void Draw(CommandList* cmd_list, unsigned int vertex_count, unsigned int inst_count);
	void DrawIndexed(CommandList* cmd_list, unsigned int idx_count, unsigned int inst_count);
	void Transition(CommandList* cmd_list, RenderTarget* render_target, unsigned int frame_index, ResourceState from, ResourceState to);
	void Transition(CommandList* cmd_list, RenderTarget* render_target, ResourceState from, ResourceState to);
	void TransitionDepth(CommandList* cmd_list, RenderTarget* render_target, ResourceState from, ResourceState to);
	// void Transition(CommandList* cmd_list, Texture* texture, ResourceState from, ResourceState to);
	void Destroy(CommandList* cmd_list);

	// RenderTarget
	[[nodiscard]] RenderTarget* CreateRenderTarget(Device* device, unsigned int width, unsigned int height, desc::RenderTargetDesc descriptor);
	void SetName(RenderTarget* render_target, std::wstring name);
	void SetName(RenderTarget* render_target, std::string name);
	void CreateRenderTargetViews(RenderTarget* render_target, Device* device, unsigned int width, unsigned int height);
	void CreateDepthStencilBuffer(RenderTarget* render_target, Device* device, unsigned int width, unsigned int height);
	void CreateSRVFromDSV(RenderTarget* render_target, DescHeapCPUHandle& handle);
	void CreateSRVFromRTV(RenderTarget* render_target, DescHeapCPUHandle& handle, unsigned int num, Format formats[8]);
	void CreateSRVFromSpecificRTV(RenderTarget* render_target, DescHeapCPUHandle& handle, unsigned int id, Format format);
	// void CreateUAVFromTexture(Texture* tex, DescHeapCPUHandle& handle, unsigned int mip_slice = 0, unsigned int array_slice = 0);
	// void CreateSRVFromTexture(Texture* tex, DescHeapCPUHandle& handle);
	void Resize(RenderTarget** render_target, Device* device, unsigned int width, unsigned int height);
	void IncrementFrameIdx(RenderTarget* render_target);
	void DestroyDepthStencilBuffer(RenderTarget* render_target);
	void DestroyRenderTargetViews(RenderTarget* render_target);
	void Destroy(RenderTarget* render_target);

	// RenderWindow
	[[nodiscard]] RenderWindow* CreateRenderWindow(Device* device, HWND window, CommandQueue* cmd_queue, unsigned int num_back_buffers);
	[[nodiscard]] RenderWindow* CreateRenderWindow(Device* device, IUnknown* window, CommandQueue* cmd_queue, unsigned int num_back_buffers);
	void Resize(RenderWindow* render_window, Device* device, unsigned int width, unsigned int height, bool fullscreen);
	void Present(RenderWindow* render_window, Device* device);
	void Destroy(RenderWindow* render_window);

	// Descriptor Heap
	[[nodiscard]] DescriptorHeap* CreateDescriptorHeap(Device* device, desc::DescriptorHeapDesc const & descriptor);
	[[nodiscard]] DescHeapGPUHandle GetGPUHandle(DescriptorHeap* desc_heap, unsigned int index = 0);
	[[nodiscard]] DescHeapCPUHandle GetCPUHandle(DescriptorHeap* desc_heap, unsigned int index = 0);
	void Offset(DescHeapGPUHandle& handle, unsigned int index, unsigned int increment_size);
	void Offset(DescHeapCPUHandle& handle, unsigned int index, unsigned int increment_size);
	void Destroy(DescriptorHeap* desc_heap);

	// Fence
	[[nodiscard]] Fence* CreateFence(Device* device);
	void Signal(Fence* fence, CommandQueue* cmd_queue);
	void WaitFor(Fence* fence);
	void Destroy(Fence* fence);

	// Viewport
	[[nodiscard]] Viewport CreateViewport(int width, int height);
	void ResizeViewport(Viewport& viewport, int width, int height);

	// Shader
	[[nodiscard]] Shader* LoadShader(ShaderType type, std::string const & path, std::string const & entry = "main");
	bool ReloadShader(Shader* shader);
	void Destroy(Shader* shader);

	// Root Signature
	[[nodiscard]] RootSignature* CreateRootSignature(desc::RootSignatureDesc create_info);
	void FinalizeRootSignature(RootSignature* root_signature, Device* device);
	void Destroy(RootSignature* root_signature);

	// Pipeline State
	[[nodiscard]] PipelineState* CreatePipelineState(); // TODO: Creation of root signature and pipeline are not the same related to the descriptor.
	void SetVertexShader(PipelineState* pipeline_state, Shader* shader);
	void SetFragmentShader(PipelineState* pipeline_state, Shader* shader);
	void SetComputeShader(PipelineState* pipeline_state, Shader* shader);
	void SetRootSignature(PipelineState* pipeline_state, RootSignature* root_signature);
	void FinalizePipeline(PipelineState* pipeline_state, Device* device, desc::PipelineStateDesc desc);
	void RefinalizePipeline(PipelineState* pipeline_state, Device* device, desc::PipelineStateDesc desc);
	void Destroy(PipelineState* pipeline_state);

	// Staging Buffer
	[[nodiscard]] StagingBuffer* CreateStagingBuffer(Device* device, void* data, std::uint64_t size, std::uint64_t m_stride, ResourceState resource_state);
	void UpdateStagingBuffer(StagingBuffer* buffer, void* data, std::uint64_t size, std::uint64_t offset);
	void StageBuffer(StagingBuffer* buffer, CommandList* cmd_list);
	void StageBufferRegion(StagingBuffer* buffer, std::uint64_t size, std::uint64_t offset, CommandList* cmd_list);
	void FreeStagingBuffer(StagingBuffer* buffer);
	void Evict(StagingBuffer* buffer);
	void MakeResident(StagingBuffer* buffer);
	void Destroy(StagingBuffer* buffer);

	// Heap
	[[nodiscard]] Heap<HeapOptimization::SMALL_BUFFERS>* CreateHeap_SBO(Device* device, std::uint64_t size_in_bytes, ResourceType resource_type, unsigned int versioning_count);
	[[nodiscard]] Heap<HeapOptimization::BIG_BUFFERS>* CreateHeap_BBO(Device* device, std::uint64_t size_in_bytes, ResourceType resource_type, unsigned int versioning_count);
	[[nodiscard]] Heap<HeapOptimization::SMALL_STATIC_BUFFERS>* CreateHeap_SSBO(Device* device, std::uint64_t size_in_bytes, ResourceType resource_type, unsigned int versioning_count);
	[[nodiscard]] Heap<HeapOptimization::BIG_STATIC_BUFFERS>* CreateHeap_BSBO(Device* device, std::uint64_t size_in_bytes, ResourceType resource_type, unsigned int versioning_count);
	[[nodiscard]] HeapResource* AllocConstantBuffer(Heap<HeapOptimization::SMALL_BUFFERS>* heap, std::uint64_t size_in_bytes);
	[[nodiscard]] HeapResource* AllocConstantBuffer(Heap<HeapOptimization::BIG_BUFFERS>* heap, std::uint64_t size_in_bytes);
	[[nodiscard]] HeapResource* AllocStructuredBuffer(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap, std::uint64_t size_in_bytes, std::uint64_t stride);
	[[nodiscard]] HeapResource* AllocGenericBuffer(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap, std::uint64_t size_in_bytes);
	void DeallocConstantBuffer(Heap<HeapOptimization::SMALL_BUFFERS>* heap, HeapResource* heapResource);
	void DeallocConstantBuffer(Heap<HeapOptimization::BIG_BUFFERS>* heap, HeapResource* heapResource);
	void DeallocBuffer(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap, HeapResource* heapResource);
	void MapHeap(Heap<HeapOptimization::SMALL_BUFFERS>* heap);
	void MapHeap(Heap<HeapOptimization::BIG_BUFFERS>* heap);
	void UnmapHeap(Heap<HeapOptimization::SMALL_BUFFERS>* heap);
	void UnmapHeap(Heap<HeapOptimization::BIG_BUFFERS>* heap);
	void MakeResident(Heap<HeapOptimization::SMALL_BUFFERS>* heap);
	void MakeResident(Heap<HeapOptimization::BIG_BUFFERS>* heap);
	void MakeResident(Heap<HeapOptimization::SMALL_STATIC_BUFFERS>* heap);
	void MakeResident(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap);
	void EnqueueMakeResident(Heap<HeapOptimization::SMALL_BUFFERS>* heap, Fence* fence); // Untested
	void EnqueueMakeResident(Heap<HeapOptimization::BIG_BUFFERS>* heap, Fence* fence);  // Untested
	void EnqueueMakeResident(Heap<HeapOptimization::SMALL_STATIC_BUFFERS>* heap, Fence* fence); // Untested
	void EnqueueMakeResident(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap, Fence* fence);  // Untested
	void Evict(Heap<HeapOptimization::SMALL_BUFFERS>* heap);
	void Evict(Heap<HeapOptimization::BIG_BUFFERS>* heap);
	void Evict(Heap<HeapOptimization::SMALL_STATIC_BUFFERS>* heap);
	void Evict(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap);
	void Destroy(Heap<HeapOptimization::SMALL_BUFFERS>* heap);
	void Destroy(Heap<HeapOptimization::BIG_BUFFERS>* heap);
	void Destroy(Heap<HeapOptimization::SMALL_STATIC_BUFFERS>* heap);
	void Destroy(Heap<HeapOptimization::BIG_STATIC_BUFFERS>* heap);

	// Resources
	void UpdateConstantBuffer(HeapResource* buffer, unsigned int frame_idx, void* data, std::uint64_t size_in_bytes);
	void UpdateStructuredBuffer(HeapResource* buffer,
		unsigned int frame_idx,
		void* data,
		std::uint64_t size_in_bytes,
		std::uint64_t offset,
		std::uint64_t stride,
		CommandList* cmd_list);

} /* wr::d3d12 */