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
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <ctime>
#include <iostream>

#include "wisp.hpp"
#include "version.hpp"
#include "demo_frame_graphs.hpp"
#include "util/file_watcher.hpp"

//Crashpad includes
#include "client/crashpad_client.h"
#include "client/settings.h"
#include "client/crash_report_database.h"

#include "engine_interface.hpp"
#include "physics_engine.hpp"
#include "scene_viknell.hpp"
#include "scene_alien.hpp"
#include "scene_emibl.hpp"
#include "scene_sponza.hpp"

#include "model_loader_assimp.hpp"
#include "model_loader_tinygltf.hpp"
#include "d3d12/d3d12_dynamic_descriptor_heap.hpp"

using DefaultScene = ViknellScene;
//#define ENABLE_PHYSICS

std::unique_ptr<wr::D3D12RenderSystem> render_system;
Scene* current_scene = nullptr;
Scene* new_scene = nullptr;

void RenderEditor(ImTextureID output)
{
	engine::RenderEngine(output, render_system.get(), current_scene, &new_scene);
}

void ShaderDirChangeDetected(std::string const & path, util::FileWatcher::FileStatus status)
{
	auto& registry = wr::PipelineRegistry::Get();
	auto& rt_registry = wr::RTPipelineRegistry::Get();

	if (status == util::FileWatcher::FileStatus::MODIFIED)
	{
		LOG("Change detected in the shader directory. Reloading pipelines and shaders.");

		for (auto it : registry.m_objects)
		{
			registry.RequestReload(it.first);
		}

		for (auto it : rt_registry.m_objects)
		{
			rt_registry.RequestReload(it.first);
		}
	}
}

int WispEntry()
{
	constexpr auto version = wr::GetVersion();
	LOG("Wisp Version {}.{}.{}", version.m_major, version.m_minor, version.m_patch);

	// ImGui Logging
	util::log_callback::impl = [&](std::string const & str)
	{
		engine::debug_console.AddLog(str.c_str());
	};

	render_system = std::make_unique<wr::D3D12RenderSystem>();

	phys::PhysicsEngine phys_engine;

	auto window = std::make_unique<wr::Window>(GetModuleHandleA(nullptr), "D3D12 Test App", 1280, 720);

	window->SetKeyCallback([](int key, int action, int mods)
	{
		current_scene->GetCamera<DebugCamera>()->KeyAction(key, action);

		if (action == WM_KEYUP && key == 0xC0)
		{
			engine::open_console = !engine::open_console;
			engine::debug_console.EmptyInput();
		}
		if (action == WM_KEYUP && key == VK_F1)
		{
			engine::show_imgui = !engine::show_imgui;
		}
		if (action == WM_KEYUP && key == VK_F2)
		{
			fg_manager::Next();
		}
		if (action == WM_KEYUP && key == VK_F3)
		{
			fg_manager::Prev();
		}
	});

	window->SetMouseCallback([](int key, int action, int mods)
	{
		current_scene->GetCamera<DebugCamera>()->MouseAction(key, action);
	});

	window->SetMouseWheelCallback([](int amount, int action, int mods)
	{
		current_scene->GetCamera<DebugCamera>()->MouseWheel(amount);
	});

	wr::ModelLoader* assimp_model_loader = new wr::AssimpModelLoader();
	wr::ModelLoader* gltf_model_loader = new wr::TinyGLTFModelLoader();

	TRY_M(CoInitialize(nullptr), "Couldn't CoInitialize");
	render_system->Init(window.get());	

	phys_engine.CreatePhysicsWorld();

	current_scene = new DefaultScene();
	current_scene->Init(render_system.get(), window->GetWidth(), window->GetHeight(), &phys_engine);

	fg_manager::Setup(*render_system, &RenderEditor);

	window->SetResizeCallback([&](std::uint32_t width, std::uint32_t height)
	{
		render_system->WaitForAllPreviousWork();
		render_system->Resize(width, height);
		current_scene->GetCamera<wr::CameraNode>()->SetAspectRatio((float)width / (float)height);
		current_scene->GetCamera<wr::CameraNode>()->SetOrthographicResolution(width, height);
		fg_manager::Resize(*render_system, width, height);
	});

	auto file_watcher = new util::FileWatcher("resources/shaders", std::chrono::milliseconds(100));
	file_watcher->StartAsync(&ShaderDirChangeDetected);

	window->SetRenderLoop([&](float dt) {

		bool capture_frame = engine::recorder.ShouldCaptureAndIncrement(dt);
		if (capture_frame) {
			fg_manager::Get()->SaveTaskToDisc<wr::PostProcessingData>(engine::recorder.GetNextFilename(".tga"), 0);
		}

		if (new_scene && new_scene != current_scene)
		{
			render_system->WaitForAllPreviousWork();
			delete current_scene;
			current_scene = new_scene;
			current_scene->Init(render_system.get(), window->GetWidth(), window->GetHeight(), &phys_engine);
			fg_manager::Get()->SetShouldExecute<wr::EquirectToCubemapTaskData>(true);
			fg_manager::Get()->SetShouldExecute<wr::CubemapConvolutionTaskData>(true);
		}

		current_scene->Update(dt);

#ifdef ENABLE_PHYSICS
		phys_engine.UpdateSim(delta, *current_scene->GetSceneGraph());
#endif

		auto texture = render_system->Render(*current_scene->GetSceneGraph(), *fg_manager::Get());

	});

	window->StartRenderLoop();

	delete assimp_model_loader;
	delete gltf_model_loader;

	render_system->WaitForAllPreviousWork(); // Make sure GPU is finished before destruction.

	delete current_scene;

	fg_manager::Destroy();
	render_system.reset();
	delete file_watcher;

	return 0;
}

WISP_ENTRY(WispEntry)
