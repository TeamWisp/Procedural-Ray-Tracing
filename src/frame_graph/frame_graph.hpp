#pragma once

#include <functional>

#include "render_task.hpp"

namespace wr
{

	class FrameGraph
	{
	public:
		FrameGraph() {}
		virtual ~FrameGraph() = default;

		FrameGraph(const FrameGraph&) = delete;
		FrameGraph(FrameGraph&&) = default;
		FrameGraph& operator=(const FrameGraph&) = delete;
		FrameGraph& operator=(FrameGraph&&) = default;

		template<typename T>
		void AddTask(std::string const & name, decltype(RenderTask<T>::setup_func_type) setup, decltype(RenderTask<T>::execute_func_type) execute);
		void AddTask(std::unique_ptr<BaseRenderTask> task);
		void Setup(RenderSystem & render_system);
		void Execute(RenderSystem & render_system, SceneGraph & scene_graph);

		template<typename T>
		std::vector<T*> GetAllCommandLists() const
		{
			static_assert(std::is_base_of<CommandList, T>::value, "Type must be child of wr::CommandList");

			std::vector<T*> retval;
			for (auto& task : m_tasks)
			{
				retval.push_back(task->GetCommandList<T>().first);
			}

			return retval;
		}

		template<typename T>
		auto GetData();

	private:
		std::vector<std::unique_ptr<BaseRenderTask>> m_tasks;

	};

	//! Used to add a task by creating a new one.
	template<typename T>
	void FrameGraph::AddTask(std::string const & name, decltype(RenderTask<T>::setup_func_type) setup, decltype(RenderTask<T>::execute_func_type) execute)
	{
		m_tasks.push_back(std::make_unique<RenderTask<T>>(this, name, setup, execute));
	}

	//! Used to obtain data from a previously run render task.
	template<typename T>
	auto FrameGraph::GetData()
	{
		for (auto& task : m_tasks)
		{
			if (typeid(T) == task->data_type_info)
			{
				return static_cast<T*>(task->data);
			}
		}
	}

} /* wr */