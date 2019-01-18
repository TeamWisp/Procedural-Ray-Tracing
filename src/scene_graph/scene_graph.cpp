#include "scene_graph.hpp"

#include <algorithm>

#include "../renderer.hpp"
#include "../util/log.hpp"

#include "camera_node.hpp"
#include "mesh_node.hpp"
#include "skybox_node.hpp"

//TODO: Make platform independent
#include "../d3d12/d3d12_defines.hpp"
#include "../d3d12/d3d12_functions.hpp"
#include "../d3d12/d3d12_renderer.hpp"
#include "../d3d12/d3d12_constant_buffer_pool.hpp"

namespace wr
{

	SceneGraph::SceneGraph(RenderSystem* render_system)
		: m_render_system(render_system), m_root(std::make_shared<Node>())
	{
		m_lights.resize(d3d12::settings::num_lights);
	}

	SceneGraph::~SceneGraph()
	{
		RemoveChildren(GetRootNode());
	}

	//! Used to obtain the root node.
	std::shared_ptr<Node> SceneGraph::GetRootNode() const
	{
		return m_root;
	}

	//! Used to obtain the children of a node.
	std::vector<std::shared_ptr<Node>> SceneGraph::GetChildren(std::shared_ptr<Node> const & parent)
	{
		return parent ? parent->m_children : m_root->m_children;
	}

	//! Used to remove the children of a node.
	void SceneGraph::RemoveChildren(std::shared_ptr<Node> const & parent)
	{
		parent->m_children.clear();
	}

	//! Returns the active camera.
	/*!
		If there are multiple active cameras it will return the first one.
	*/
	std::shared_ptr<CameraNode> SceneGraph::GetActiveCamera()
	{
		for (auto& camera_node : m_camera_nodes)
		{
			if (camera_node->m_active)
			{
				return camera_node;
			}
		}

		LOGW("Failed to obtain a active camera node.");
		return nullptr;
	}

	std::vector<std::shared_ptr<LightNode>>& SceneGraph::GetLightNodes()
	{
		return m_light_nodes;
	}

	std::vector<std::shared_ptr<MeshNode>>& SceneGraph::GetMeshNodes()
	{
		return m_mesh_nodes;
	}

	std::shared_ptr<SkyboxNode> SceneGraph::GetCurrentSkybox()
	{
		if (!m_skybox_nodes.empty())
		{
			return m_skybox_nodes.at(0);
		}
		else
		{
			return nullptr;
		}
	}

	//! Initialize the scene graph
	void SceneGraph::Init()
	{
		m_init_meshes_func_impl(m_render_system, m_mesh_nodes);

		// Create constant buffer pool

		constexpr auto model_size = sizeof(temp::ObjectData) * d3d12::settings::num_instances_per_batch;
		constexpr auto model_cbs_size = SizeAlign(model_size, 256) * d3d12::settings::num_back_buffers;

		m_constant_buffer_pool = m_render_system->CreateConstantBufferPool((uint32_t) std::ceil(model_cbs_size / (1024 * 1024.f)));

		// Initialize cameras

		m_init_cameras_func_impl(m_render_system, m_camera_nodes);

		// Create Light Buffer

		uint64_t light_count = (uint64_t) m_lights.size();
		uint64_t light_buffer_stride = sizeof(Light), light_buffer_size = light_buffer_stride * light_count;
		uint64_t light_buffer_aligned_size = SizeAlign(light_buffer_size, 65536) * d3d12::settings::num_back_buffers;

		m_structured_buffer = m_render_system->CreateStructuredBufferPool((size_t) std::ceil(light_buffer_aligned_size / (1024 * 1024.f)));
		m_light_buffer = m_structured_buffer->Create(light_buffer_size, light_buffer_stride, false);

		//Initialize lights

		m_init_lights_func_impl(m_render_system, m_light_nodes, m_lights);

	}

	//! Update the scene graph
	void SceneGraph::Update()
	{
		m_update_transforms_func_impl(m_render_system, *this, m_root);
		m_update_cameras_func_impl(m_render_system, m_camera_nodes);
		m_update_meshes_func_impl(m_render_system, m_mesh_nodes);
		m_update_lights_func_impl(m_render_system, *this);
	}

	//! Render the scene graph
	/*!
		The user is expected to call `Optimize`. If they don't this function will do it manually.
	*/
	void SceneGraph::Render(CommandList* cmd_list, CameraNode* camera)
	{
		m_render_meshes_func_impl(m_render_system, m_batches, camera, cmd_list);
	}

	temp::MeshBatches& SceneGraph::GetBatches() 
	{ 
		return m_batches;
	}

	StructuredBufferHandle* SceneGraph::GetLightBuffer()
	{
		return m_light_buffer;
	}

	uint32_t SceneGraph::GetCurrentLightSize()
	{
		return m_next_light_id;
	}

	Light* SceneGraph::GetLight(uint32_t offset)
	{
		return offset >= m_next_light_id ? nullptr : m_lights.data() + offset;
	}

	void SceneGraph::RegisterLight(std::shared_ptr<LightNode>& new_node)
	{
		//Allocate a light into the array

		if (m_next_light_id == (uint32_t)m_lights.size())
			LOGE("Couldn't allocate light node; out of memory");

		new_node->m_light = m_lights.data() + m_next_light_id;
		memcpy(new_node->m_light, &new_node->m_temp, sizeof(new_node->m_temp));
		++m_next_light_id;

		//Update light count

		if (m_lights.size() != 0)
		{
			m_lights[0].tid &= 0x3;											//Keep id
			m_lights[0].tid |= uint32_t(m_light_nodes.size() + 1) << 2;		//Set lights

			if (m_light_nodes.size() != 0)
			{
				m_light_nodes[0]->SignalChange();
			}
		}

		//Track the node

		m_light_nodes.push_back(new_node);
	}

	void SceneGraph::Optimize() 
	{
		//Update batches

		bool should_update = m_batches.size() == 0;

		for (auto& elem : m_batches)
		{
			if (elem.second.num_instances == 0)
			{
				should_update = true;
				break;
			}
		}

		if (should_update)
		{
			constexpr uint32_t max_size = d3d12::settings::num_instances_per_batch;

			constexpr auto model_size = sizeof(temp::ObjectData) * max_size;

			for (unsigned int i = 0; i < m_mesh_nodes.size(); ++i) {

				auto node = m_mesh_nodes[i];
				std::pair<std::unordered_multimap<Model*, temp::MeshBatch>::iterator, 
					std::unordered_multimap<Model*, temp::MeshBatch>::iterator>
					iterators = m_batches.equal_range(node->m_model);

				if (node->m_model == nullptr || (!GetActiveCamera()->InView(node) && d3d12::settings::enable_object_culling))
				{
					continue;
				}

				//Insert new if doesn't exist
				if (std::distance(iterators.first,iterators.second) == 0)
				{

					ConstantBufferHandle* object_buffer = m_constant_buffer_pool->Create(model_size);

					temp::MeshBatch temp_batch = {};
					std::unordered_multimap<Model*, temp::MeshBatch>::iterator it = 
						m_batches.emplace(std::make_pair(node->m_model, temp_batch));

					temp::MeshBatch& batch = it->second;

					batch.batch_buffer = object_buffer;
					batch.data.objects.resize(d3d12::settings::num_instances_per_batch);
					batch.materials = node->GetModelMaterials();
					
					iterators = m_batches.equal_range(node->m_model);
				}

				bool found_batch = false;

				for (std::unordered_multimap<Model*, temp::MeshBatch>::iterator it = iterators.first; it != iterators.second; ++it)
				{
					if (it->second.num_instances < max_size)
					{
						bool materials_equal = true;
						std::vector<MaterialHandle*> model_materials = node->GetModelMaterials();
						std::vector<MaterialHandle*> batch_materials = it->second.materials;
						for (int j = 0; j < batch_materials.size(); ++j)
						{
							if ((*model_materials[j]) != (*batch_materials[j]))
							{
								materials_equal = false;
								break;
							}
						}
						if (materials_equal == false)
						{
							continue;
						}

						temp::MeshBatch& batch = it->second;
						unsigned int& offset = batch.num_instances;
						batch.data.objects[offset] = { node->m_transform };
						++offset;

						found_batch = true;
						break;
					}
				}

				if (!found_batch)
				{
					ConstantBufferHandle* object_buffer = m_constant_buffer_pool->Create(model_size);

					temp::MeshBatch temp_batch = {};
					std::unordered_multimap<Model*, temp::MeshBatch>::iterator it =
						m_batches.emplace(std::make_pair(node->m_model, temp_batch));

					temp::MeshBatch& batch = it->second;

					batch.batch_buffer = object_buffer;
					batch.data.objects.resize(d3d12::settings::num_instances_per_batch);
					batch.materials = node->GetModelMaterials();
					unsigned int& offset = batch.num_instances;
					batch.data.objects[offset] = { node->m_transform };
					++offset;					
				}

			}

			//Update object data
			for (auto& elem : m_batches)
			{
				temp::MeshBatch& batch = elem.second;
				m_constant_buffer_pool->Update(batch.batch_buffer, model_size, 0, (uint8_t*)batch.data.objects.data());
			}

		}
		
	}

} /* wr */
