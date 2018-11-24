#include <memory>
#include <algorithm>
#include "wisp.hpp"
#include "render_tasks/d3d12_test_render_task.hpp"
#include "render_tasks/d3d12_imgui_render_task.hpp"
#include "imgui_tools.hpp"

bool main_menu = true;
bool open0 = true;
bool open1 = true;
bool open2 = true;
bool open_console = false;
bool show_imgui = true;
char message_buffer[600];

std::unique_ptr<wr::D3D12RenderSystem> render_system;

static wr::imgui::special::DebugConsole debug_console;

void RenderEditor()
{
	debug_console.Draw("Console", &open_console);

	if (!show_imgui) return;

	if (main_menu && ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit", "ALT+F4")) std::exit(0);
			if (ImGui::MenuItem("Hide ImGui", "F1")) show_imgui = false;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window"))
		{
			wr::imgui::menu::Registries();
			ImGui::Separator();
			ImGui::MenuItem("Theme", nullptr, &open0);
			ImGui::MenuItem("ImGui Details", nullptr, &open1);
			ImGui::MenuItem("Logging Example", nullptr, &open2);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	ImGui::DockSpaceOverViewport(main_menu, nullptr, ImGuiDockNodeFlags_PassthruDockspace);

	auto& io = ImGui::GetIO();

	// Create dockable background
	if (open0)
	{
		ImGui::Begin("Theme", &open0);
		if (ImGui::Button("Cherry")) ImGui::StyleColorsCherry();
		if (ImGui::Button("Unreal Engine")) ImGui::StyleColorsUE();
		if (ImGui::Button("Light Green")) ImGui::StyleColorsLightGreen();
		if (ImGui::Button("Light")) ImGui::StyleColorsLight();
		if (ImGui::Button("Dark")) ImGui::StyleColorsDark();
		if (ImGui::Button("Dark2")) ImGui::StyleColorsDarkCodz1();
		ImGui::End();
	}

	if (open1)
	{
		ImGui::Begin("ImGui Details", &open1);
		ImGui::Text("Mouse Pos: (%0.f, %0.f)", io.MousePos.x, io.MousePos.y);
		ImGui::Text("Framerate: %.0f", io.Framerate);
		ImGui::Text("Delta: %f", io.DeltaTime);
		ImGui::Text("Display Size: (%.0f, %.0f)", io.DisplaySize.x, io.DisplaySize.y);
		ImGui::End();
	}

	if (open2)
	{
		ImGui::Begin("Logging Example", &open2);
		ImGui::InputText("Message", message_buffer, 600);
		if (ImGui::Button("LOG (Message)")) LOG(message_buffer);
		if (ImGui::Button("LOGW (Warning)")) LOGW(message_buffer);
		if (ImGui::Button("LOGE (Error)")) LOGE(message_buffer);
		if (ImGui::Button("LOGC (Critical)")) LOGC(message_buffer);
		ImGui::End();
	}

	wr::imgui::window::ShaderRegistry();
	wr::imgui::window::PipelineRegistry();
	wr::imgui::window::RootSignatureRegistry();
	wr::imgui::window::D3D12HardwareInfo(*render_system.get());
	wr::imgui::window::D3D12Settings();
}

void WispEntry()
{
	// ImGui Logging
	util::log_callback::impl = [&](std::string const & str)
	{
		debug_console.AddLog(str.c_str());
	};

	render_system = std::make_unique<wr::D3D12RenderSystem>();
	auto window = std::make_unique<wr::Window>(GetModuleHandleA(nullptr), "D3D12 Test App", 1280, 720);

	window->SetKeyCallback([](int key, int action, int mods) {
		if (action == WM_KEYUP && key == 0xC0)
		{
			open_console = !open_console;
			debug_console.EmptyInput();
		}
		if (action == WM_KEYUP && key == VK_F1)
		{
			show_imgui = !show_imgui;
		}
	});

	render_system->Init(window.get());

	// Load custom model
	auto model_pool = render_system->CreateModelPool(1);
	wr::Model* model;
	{
		wr::MeshData<wr::Vertex> mesh;
		static const constexpr float size = 0.5f;

		mesh.m_indices = {
			2, 1, 0, 3, 2, 0, 6, 5,
			4, 7, 6, 4, 10, 9, 8, 11,
			10, 8, 14, 13, 12, 15, 14, 12,
			18, 17, 16, 19, 18, 16, 22, 21,
			20, 23, 22, 20 
		};

		mesh.m_vertices = {
			{ 1, 1, -1, 1, 1, 0, 0, -1 },
			{ 1, -1, -1, 0, 1, 0, 0, -1 },
			{ -1, -1, -1, 0, 0, 0, 0, -1 },
			{ -1, 1, -1, 1, 0, 0, 0, -1 },
			{ 1, 1, 1, 1, 1, 0, 0, 1 },
			{ -1, 1, 1, 0, 1, 0, 0, 1 },
			{ -1, -1, 1, 0, 0, 0, 0, 1 },
			{ 1, -1, 1, 1, 0, 0, 0, 1 },
			{ 1, 1, -1, 1, 0, 1, 0, 0 },
			{ 1, 1, 1, 1, 1, 1, 0, 0 },
			{ 1, -1, 1, 0, 1, 1, 0, 0 },
			{ 1, -1, -1, 0, 0, 1, 0, 0 },
			{ 1, -1, -1, 1, 0, 0, -1, 0 },
			{ 1, -1, 1, 1, 1, 0, -1, 0 },
			{ -1, -1, 1, 0, 1, 0, -1, 0 },
			{ -1, -1, -1, 0, 0, 0, -1, 0 },
			{ -1, -1, -1, 0, 1, -1, 0, 0 },
			{ -1, -1, 1, 0, 0, -1, 0, 0 },
			{ -1, 1, 1, 1, 0, -1, 0, 0 },
			{ -1, 1, -1, 1, 1, -1, 0, 0 },
			{ 1, 1, 1, 1, 0, 0, 1, 0 },
			{ 1, 1, -1, 1, 1, 0, 1, 0 },
			{ -1, 1, -1, 0, 1, 0, 1, 0 },
			{ -1, 1, 1, 0, 0, 0, 1, 0 },
		};

		model = model_pool->LoadCustom<wr::Vertex>({ mesh });
	}

	auto scene_graph = std::make_shared<wr::SceneGraph>(render_system.get());

	auto mesh_node = scene_graph->CreateChild<wr::MeshNode>(nullptr, model);
	auto mesh_node_1 = scene_graph->CreateChild<wr::MeshNode>(nullptr, model);
	auto mesh_node_2 = scene_graph->CreateChild<wr::MeshNode>(nullptr, model);
	auto mesh_node_3 = scene_graph->CreateChild<wr::MeshNode>(nullptr, model);
	auto mesh_node_4 = scene_graph->CreateChild<wr::MeshNode>(nullptr, model);
	auto camera = scene_graph->CreateChild<wr::CameraNode>(nullptr, 1.74f, (float)window->GetWidth() / (float)window->GetHeight());

	{
		float dist = 4;
		mesh_node_1->SetPosition({ dist, dist, dist });
		mesh_node_2->SetPosition({ -dist, dist, dist });
		mesh_node_3->SetPosition({ dist, -dist, dist });
		mesh_node_4->SetPosition({ -dist, -dist, dist });
	}

	camera->SetPosition(0, 0, -5);

	render_system->InitSceneGraph(*scene_graph.get());

	wr::FrameGraph frame_graph;
	frame_graph.AddTask(wr::GetTestTask());
	frame_graph.AddTask(wr::GetImGuiTask(&RenderEditor));
	frame_graph.Setup(*render_system);

	float t = 0;

	while (window->IsRunning())
	{
		mesh_node->SetRotation({ sin(t/2.f) * 20.f, -t * 10, 0});

		mesh_node_1->SetRotation({ t, 0, sin(-t) * 20 });
		mesh_node_2->SetRotation({ t, 0, sin(t) * 20 });
		mesh_node_3->SetRotation({ -t, 0, sin(-t) * 20 });
		mesh_node_4->SetRotation({ -t, 0, sin(t) * 20 });

		t += 10.f * ImGui::GetIO().DeltaTime;

		window->PollEvents();
		auto texture = render_system->Render(scene_graph, frame_graph);
	}

	render_system->WaitForAllPreviousWork(); // Make sure GPU is finished before destruction.
	frame_graph.Destroy();
	render_system.reset();
}

WISP_ENTRY(WispEntry)
