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

#include "d3d12_dynamic_descriptor_heap.hpp"
#include "d3d12_renderer.hpp"
#include "d3d12_functions.hpp"
#include "../util/log.hpp"

namespace wr
{
	DynamicDescriptorHeap::DynamicDescriptorHeap(wr::d3d12::Device* device, DescriptorHeapType type, uint32_t num_descriptors_per_heap)
		: m_desc_heap_type(type)
		, m_num_descr_per_heap(num_descriptors_per_heap)
		, m_descriptor_table_bit_mask(0)
		, m_stale_descriptor_table_bit_mask(0)
		, m_num_free_handles(0)
		, m_device(device)
	{
		m_current_cpu_desc_handle.m_native.ptr = 0;
		m_current_gpu_desc_handle.m_native.ptr = 0;

		m_descriptor_handle_increment_size = m_device->m_native->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)type);

		// Allocate space for staging CPU visible descriptors.
		m_descriptor_handle_cache = std::make_unique<D3D12_CPU_DESCRIPTOR_HANDLE[]>(m_num_descr_per_heap);
	}

	DynamicDescriptorHeap::~DynamicDescriptorHeap()
	{}

	void DynamicDescriptorHeap::ParseRootSignature(const d3d12::RootSignature& root_signature)
	{
		// If the root signature changes, all descriptors must be (re)bound to the
		// command list.
		m_stale_descriptor_table_bit_mask = 0;

		const auto& root_signature_desc = root_signature.m_create_info;

		// Get a bit mask that represents the root parameter indices that match the 
		// descriptor heap type for this dynamic descriptor heap.
		switch (m_desc_heap_type)
		{
		case wr::DescriptorHeapType::DESC_HEAP_TYPE_CBV_SRV_UAV:
			m_descriptor_table_bit_mask = root_signature.m_descriptor_table_bit_mask;
			break;
		case wr::DescriptorHeapType::DESC_HEAP_TYPE_SAMPLER:
			m_descriptor_table_bit_mask = root_signature.m_sampler_table_bit_mask;
			break;
		default:
			//LOGC("You can't use a different type other than CBC_SRV_UAV or SAMPLER for dynamic descriptor heaps");
			break;
		}

		uint32_t descriptor_table_bitmask = m_descriptor_table_bit_mask;

		uint32_t current_offset = 0;
		DWORD root_idx;
		size_t num_parameters = root_signature_desc.m_parameters.size();

		while (_BitScanForward(&root_idx, descriptor_table_bitmask) && root_idx < num_parameters)
		{
			//Since a 32 bit mask is used to represent the descriptor tables in the root signature
			//I want to break if the root_idx is bigger than 32. If that ever happens, we might consider
			//switching to a 64bit mask.
			if (root_idx > 32) { LOGC("A maximum of 32 descriptor tables are supported"); }

			uint32_t num_descriptors = root_signature.m_num_descriptors_per_table[root_idx];

			DescriptorTableCache& descriptorTableCache = m_descriptor_table_cache[root_idx];
			descriptorTableCache.m_num_descriptors = num_descriptors;
			descriptorTableCache.m_base_descriptor = m_descriptor_handle_cache.get() + current_offset;

			current_offset += num_descriptors;

			// Flip the descriptor table bit so it's not scanned again for the current index.
			descriptor_table_bitmask ^= (1 << root_idx);
		}

		// Make sure the maximum number of descriptors per descriptor heap has not been exceeded.
		if (current_offset > m_num_descr_per_heap) { LOGC("The root signature requires more than the maximum number of descriptors per descriptor heap. Consider increasing the maximum number of descriptors per descriptor heap."); }
	}

	void DynamicDescriptorHeap::StageDescriptors(uint32_t root_param_idx, uint32_t offset, uint32_t num_descriptors, const d3d12::DescHeapCPUHandle src_desc)
	{
		if (num_descriptors > m_num_descr_per_heap || root_param_idx >= MaxDescriptorTables)
		{
			LOGC("Cannot stage more than the maximum number of descriptors per heap. Cannot stage more than MaxDescriptorTables root parameters");
		}

		DescriptorTableCache& descriptor_table_cache = m_descriptor_table_cache[root_param_idx];

		// Check that the number of descriptors to copy does not exceed the number
		// of descriptors expected in the descriptor table.
		if ((offset + num_descriptors) > descriptor_table_cache.m_num_descriptors)
		{
			LOGC("Number of descriptors exceeds the number of descriptors in the descriptor table.");
		}

		D3D12_CPU_DESCRIPTOR_HANDLE* dst_descriptor = (descriptor_table_cache.m_base_descriptor + offset);
		for (uint32_t i = 0; i < num_descriptors; ++i)
		{
			dst_descriptor[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(src_desc.m_native, i, m_descriptor_handle_increment_size);
		}

		// Set the root parameter index bit to make sure the descriptor table 
		// at that index is bound to the command list.
		m_stale_descriptor_table_bit_mask |= (1 << root_param_idx);
	}

	uint32_t DynamicDescriptorHeap::ComputeStaleDescriptorCount() const
	{
		uint32_t num_stale_desc = 0;
		DWORD i;
		DWORD stale_desc_bitmask = m_stale_descriptor_table_bit_mask;

		while (_BitScanForward(&i, stale_desc_bitmask))
		{
			num_stale_desc += m_descriptor_table_cache[i].m_num_descriptors;
			stale_desc_bitmask ^= (1 << i);
		}

		return num_stale_desc;
	}

	d3d12::DescriptorHeap* DynamicDescriptorHeap::RequestDescriptorHeap()
	{
		d3d12::DescriptorHeap* descriptor_heap;

		if (!m_available_descriptor_heaps.empty())
		{
			descriptor_heap = m_available_descriptor_heaps.front();
			m_available_descriptor_heaps.pop();
		}
		else
		{
			descriptor_heap = CreateDescriptorHeap();
			m_descriptor_heap_pool.push(descriptor_heap);
		}

		return descriptor_heap;
	}

	d3d12::DescriptorHeap* DynamicDescriptorHeap::CreateDescriptorHeap()
	{
		d3d12::desc::DescriptorHeapDesc desc;
		desc.m_type = m_desc_heap_type;
		desc.m_num_descriptors = m_num_descr_per_heap;
		desc.m_shader_visible = true;
		desc.m_versions = 1;

		d3d12::DescriptorHeap* descriptor_heap = d3d12::CreateDescriptorHeap(m_device, desc);

		return descriptor_heap;
	}

	void DynamicDescriptorHeap::CommitStagedDescriptorsForDraw(d3d12::CommandList& cmd_list)
	{
		// Compute the number of descriptors that need to be copied 
		uint32_t num_descriptors_to_commit = ComputeStaleDescriptorCount();
		
		if (num_descriptors_to_commit > 0)
		{
			if (!m_current_descriptor_heap || m_num_free_handles < num_descriptors_to_commit)
			{
				m_current_descriptor_heap = RequestDescriptorHeap();
				m_current_cpu_desc_handle = d3d12::GetCPUHandle(m_current_descriptor_heap, 0);
				m_current_gpu_desc_handle = d3d12::GetGPUHandle(m_current_descriptor_heap, 0);
				m_num_free_handles = m_num_descr_per_heap;

				d3d12::BindDescriptorHeap(&cmd_list, m_current_descriptor_heap, m_desc_heap_type, 0);

				// When updating the descriptor heap on the command list, all descriptor
				// tables must be (re)recopied to the new descriptor heap (not just
				// the stale descriptor tables).
				m_stale_descriptor_table_bit_mask = m_descriptor_table_bit_mask;
			}

			DWORD root_idx;
			// Scan from LSB to MSB for a bit set in staleDescriptorsBitMask
			while (_BitScanForward(&root_idx, m_stale_descriptor_table_bit_mask))
			{
				std::uint32_t num_src_desc = m_descriptor_table_cache[root_idx].m_num_descriptors;
				D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorHandles = m_descriptor_table_cache[root_idx].m_base_descriptor;

				D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[] =
				{
					m_current_cpu_desc_handle.m_native
				};
				std::uint32_t pDestDescriptorRangeSizes[] =
				{
					num_src_desc
				};

				// Copy the staged CPU visible descriptors to the GPU visible descriptor heap.
				m_device->m_native->CopyDescriptors(1, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					num_src_desc, pSrcDescriptorHandles, nullptr, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(m_desc_heap_type));

				// Set the descriptors on the command list using the passed-in setter function.
				d3d12::BindDescriptorTable(&cmd_list, m_current_gpu_desc_handle, root_idx);

				// Offset current CPU and GPU descriptor handles.
				d3d12::Offset(m_current_cpu_desc_handle, num_src_desc, m_descriptor_handle_increment_size);
				d3d12::Offset(m_current_gpu_desc_handle, num_src_desc, m_descriptor_handle_increment_size);

				m_num_free_handles -= num_src_desc;

				// Flip the stale bit so the descriptor table is not recopied again unless it is updated with a new descriptor.
				m_stale_descriptor_table_bit_mask ^= (1 << root_idx);
			}
		}
	}

	void DynamicDescriptorHeap::CommitStagedDescriptorsForDispatch(d3d12::CommandList& cmd_list)
	{
		// Compute the number of descriptors that need to be copied 
		std::uint32_t num_descriptors_to_commit = ComputeStaleDescriptorCount();

		if (num_descriptors_to_commit > 0)
		{
			if (!m_current_descriptor_heap || m_num_free_handles < num_descriptors_to_commit)
			{
				m_current_descriptor_heap = RequestDescriptorHeap();
				m_current_cpu_desc_handle = d3d12::GetCPUHandle(m_current_descriptor_heap, 0);
				m_current_gpu_desc_handle = d3d12::GetGPUHandle(m_current_descriptor_heap, 0);
				m_num_free_handles = m_num_descr_per_heap;

				d3d12::BindDescriptorHeap(&cmd_list, m_current_descriptor_heap, m_desc_heap_type, 0, d3d12::GetRaytracingType(m_device) == RaytracingType::FALLBACK);

				// When updating the descriptor heap on the command list, all descriptor
				// tables must be (re)recopied to the new descriptor heap (not just
				// the stale descriptor tables).
				m_stale_descriptor_table_bit_mask = m_descriptor_table_bit_mask;
			}

			DWORD root_idx;
			// Scan from LSB to MSB for a bit set in staleDescriptorsBitMask
			while (_BitScanForward(&root_idx, m_stale_descriptor_table_bit_mask))
			{
				std::uint32_t num_src_desc = m_descriptor_table_cache[root_idx].m_num_descriptors;
				D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorHandles = m_descriptor_table_cache[root_idx].m_base_descriptor;

				D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[] =
				{
					m_current_cpu_desc_handle.m_native
				};
				std::uint32_t pDestDescriptorRangeSizes[] =
				{
					num_src_desc
				};

				// Copy the staged CPU visible descriptors to the GPU visible descriptor heap.
				m_device->m_native->CopyDescriptors(1, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					num_src_desc, pSrcDescriptorHandles, nullptr, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(m_desc_heap_type));

				// Set the descriptors on the command list using the passed-in setter function.
				d3d12::BindComputeDescriptorTable(&cmd_list, m_current_gpu_desc_handle, root_idx);

				// Offset current CPU and GPU descriptor handles.
				d3d12::Offset(m_current_cpu_desc_handle, num_src_desc, m_descriptor_handle_increment_size);
				d3d12::Offset(m_current_gpu_desc_handle, num_src_desc, m_descriptor_handle_increment_size);

				m_num_free_handles -= num_src_desc;

				// Flip the stale bit so the descriptor table is not recopied again unless it is updated with a new descriptor.
				m_stale_descriptor_table_bit_mask ^= (1 << root_idx);
			}
		}
	}

	d3d12::DescHeapGPUHandle DynamicDescriptorHeap::CopyDescriptor(d3d12::CommandList& cmd_list, d3d12::DescHeapCPUHandle cpu_desc)
	{
		if (!m_current_descriptor_heap || m_num_free_handles < 1)
		{
			m_current_descriptor_heap = RequestDescriptorHeap();
			m_current_cpu_desc_handle = d3d12::GetCPUHandle(m_current_descriptor_heap, 0);
			m_current_gpu_desc_handle = d3d12::GetGPUHandle(m_current_descriptor_heap, 0);

			m_num_free_handles = m_num_descr_per_heap;

			d3d12::BindDescriptorHeap(&cmd_list, m_current_descriptor_heap, m_desc_heap_type, 0);

			// When updating the descriptor heap on the command list, all descriptor
			// tables must be (re)recopied to the new descriptor heap (not just
			// the stale descriptor tables).
			m_stale_descriptor_table_bit_mask = m_descriptor_table_bit_mask;
		}

		d3d12::DescHeapGPUHandle h_gpu = m_current_gpu_desc_handle;

		m_device->m_native->CopyDescriptorsSimple(1, m_current_cpu_desc_handle.m_native, cpu_desc.m_native, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(m_desc_heap_type));

		d3d12::Offset(m_current_cpu_desc_handle, 1, m_descriptor_handle_increment_size);
		d3d12::Offset(m_current_gpu_desc_handle, 1, m_descriptor_handle_increment_size);

		m_num_free_handles -= 1;

		return h_gpu;
	}

	void DynamicDescriptorHeap::Reset()
	{
		m_available_descriptor_heaps = m_descriptor_heap_pool;
		
		m_current_descriptor_heap = nullptr;

		/*if (m_current_descriptor_heap)
		{
			ID3D12DescriptorHeap* temp = m_current_descriptor_heap->m_native[0];
			if (temp)
			{
				m_current_descriptor_heap->m_native[0] = nullptr;
				temp->Release();
			}

			m_current_descriptor_heap = nullptr;
		}*/

		m_current_cpu_desc_handle.m_native = CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
		m_current_gpu_desc_handle.m_native = CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
		m_num_free_handles = 0;
		m_descriptor_table_bit_mask = 0;
		m_stale_descriptor_table_bit_mask = 0;

		// Reset the table cache
		for (int i = 0; i < MaxDescriptorTables; ++i)
		{
			m_descriptor_table_cache[i].Reset();
		}
	}


}