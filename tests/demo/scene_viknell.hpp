
#pragma once

#include "wisp.hpp"
#include "window.hpp"
#include "scene_graph/scene_graph.hpp"
#include "imgui/imgui.hpp"
#include "physics_node.hpp"
#include "debug_camera.hpp"
#include "spline_node.hpp"

namespace viknell_scene
{
	namespace resources
	{
		//////////////////////////
		// RESOURCES
		//////////////////////////

		static std::shared_ptr<wr::ModelPool> model_pool;
		static std::shared_ptr<wr::TexturePool> texture_pool;
		static std::shared_ptr<wr::MaterialPool> material_pool;

		static wr::Model* sphere_model;
		static wr::Model* plane_model;
		static wr::Model* test_model;

		static wr::ModelData* plane_model_data;

		static wr::MaterialHandle bamboo_material;
		static wr::MaterialHandle mirror_material;

		static wr::TextureHandle equirectangular_environment_map;

		void CreateResources(wr::RenderSystem* render_system)
		{
			texture_pool = render_system->CreateTexturePool();
			material_pool = render_system->CreateMaterialPool(256);
			model_pool = render_system->CreateModelPool(64_mb, 64_mb);

			wr::TextureHandle bamboo_albedo = texture_pool->LoadFromFile("resources/materials/bamboo/bamboo-wood-semigloss-albedo.png", true, true);
			wr::TextureHandle bamboo_normal = texture_pool->LoadFromFile("resources/materials/bamboo/bamboo-wood-semigloss-normal.png", false, true);
			wr::TextureHandle bamboo_roughness = texture_pool->LoadFromFile("resources/materials/bamboo/bamboo-wood-semigloss-roughness.png", false, true);
			wr::TextureHandle bamboo_metallic = texture_pool->LoadFromFile("resources/materials/bamboo/bamboo-wood-semigloss-metal.png", false, true);

			equirectangular_environment_map = texture_pool->LoadFromFile("resources/materials/Circus_Backstage_3k.hdr", false, false);

			// Create Materials
			mirror_material = material_pool->Create(texture_pool.get());
			wr::Material* mirror_internal = material_pool->GetMaterial(mirror_material);

			mirror_internal->SetConstant<wr::MaterialConstant::ROUGHNESS>(0);
			mirror_internal->SetConstant<wr::MaterialConstant::METALLIC>(1);
			mirror_internal->SetConstant<wr::MaterialConstant::COLOR>({ 1, 1, 1 });

			bamboo_material = material_pool->Create(texture_pool.get());
			wr::Material* bamboo_material_internal = material_pool->GetMaterial(bamboo_material);

			bamboo_material_internal->SetTexture(wr::TextureType::ALBEDO, bamboo_albedo);
			bamboo_material_internal->SetTexture(wr::TextureType::NORMAL, bamboo_normal);
			bamboo_material_internal->SetTexture(wr::TextureType::ROUGHNESS, bamboo_roughness);
			bamboo_material_internal->SetTexture(wr::TextureType::METALLIC, bamboo_metallic);

			plane_model = model_pool->Load<wr::Vertex>(material_pool.get(), texture_pool.get(), "resources/models/plane.fbx", &plane_model_data);
			test_model = model_pool->LoadWithMaterials<wr::Vertex>(material_pool.get(), texture_pool.get(), "resources/models/xbot.fbx");
			sphere_model = model_pool->Load<wr::Vertex>(material_pool.get(), texture_pool.get(), "resources/models/sphere.fbx");
		}

		void ReleaseResources()
		{
			model_pool.reset();
			texture_pool.reset();
			material_pool.reset();
		};
	}


	static std::shared_ptr<DebugCamera> camera;
	static std::shared_ptr<SplineNode> camera_spline_node;
	static std::shared_ptr<wr::LightNode> directional_light_node;
	static std::shared_ptr<PhysicsMeshNode> test_model;
	static float t = 0;

	void CreateScene(wr::SceneGraph* scene_graph, wr::Window* window, phys::PhysicsEngine& phys_engine)
	{
		camera = scene_graph->CreateChild<DebugCamera>(nullptr, 90.f, (float)window->GetWidth() / (float)window->GetHeight());
		camera->SetPosition({ 0, 0, 2 });
		camera->SetSpeed(10);

		camera_spline_node = scene_graph->CreateChild<SplineNode>(nullptr, "Camera Spline", false);

		auto skybox = scene_graph->CreateChild<wr::SkyboxNode>(nullptr, resources::equirectangular_environment_map);

		// Geometry
		auto floor = scene_graph->CreateChild<PhysicsMeshNode>(nullptr, resources::plane_model);
		auto roof = scene_graph->CreateChild<wr::MeshNode>(nullptr, resources::plane_model);
		auto back_wall = scene_graph->CreateChild<wr::MeshNode>(nullptr, resources::plane_model);
		auto left_wall = scene_graph->CreateChild<wr::MeshNode>(nullptr, resources::plane_model);
		auto right_wall = scene_graph->CreateChild<wr::MeshNode>(nullptr, resources::plane_model);
		test_model = scene_graph->CreateChild<PhysicsMeshNode>(nullptr, resources::test_model);
		auto sphere = scene_graph->CreateChild<wr::MeshNode>(nullptr, resources::sphere_model);

		floor->SetupConvex(phys_engine, resources::plane_model_data);
		floor->SetRestitution(1.f);
		floor->SetPosition({ 0, -1, 0 });
		floor->SetRotation({ 90_deg, 0, 0 });
		floor->AddMaterial(resources::bamboo_material);
		sphere->SetPosition({ 1, -1, -1 });
		sphere->SetScale({ 0.6f, 0.6f, 0.6f });
		sphere->AddMaterial(resources::mirror_material);
		roof->SetPosition({ 0, 1, 0 });
		roof->SetRotation({ -90_deg, 0, 0 });
		roof->AddMaterial(resources::bamboo_material);
		back_wall->SetPosition({ 0, 0, -1 });
		back_wall->SetRotation({ 0, 180_deg, 0 });
		back_wall->AddMaterial(resources::bamboo_material);
		left_wall->SetPosition({ -1, 0, 0 });
		left_wall->SetRotation({ 0, -90_deg, 0 });
		left_wall->AddMaterial(resources::bamboo_material);
		right_wall->SetPosition({ 1, 0, 0 });
		right_wall->SetRotation({ 0, 90_deg, 0 });
		right_wall->AddMaterial(resources::bamboo_material);

		test_model->SetMass(0.01f);
		test_model->SetupSimpleSphereColl(phys_engine, 0.5);
		test_model->m_rigid_body->setRestitution(0.4);
		test_model->m_rigid_body->setFriction(0);
		test_model->m_rigid_body->setRollingFriction(0);
		test_model->m_rigid_body->setSpinningFriction(0);
		test_model->SetPosition({ 0, -1, 0 });
		test_model->SetRotation({ 0, 180_deg, 0 });
		test_model->SetScale({ 0.01f,0.01f,0.01f });
		test_model->m_rigid_body->activate(true);

		// Lights
		auto point_light_0 = scene_graph->CreateChild<wr::LightNode>(nullptr, wr::LightType::DIRECTIONAL, DirectX::XMVECTOR{ 1, 1, 1 });
		point_light_0->SetRotation({ 20.950f, 0.98f, 0.f });
		point_light_0->SetPosition({ -0.002f, 0.080f, 1.404f });

		auto point_light_1 = scene_graph->CreateChild<wr::LightNode>(nullptr, wr::LightType::POINT, DirectX::XMVECTOR{ 1, 0, 0 });
		point_light_1->SetRadius(5.0f);
		point_light_1->SetPosition({ 0.5f, 0.f, -0.3f });

		auto point_light_2 = scene_graph->CreateChild<wr::LightNode>(nullptr, wr::LightType::POINT, DirectX::XMVECTOR{ 0, 0, 1 });
		point_light_2->SetRadius(5.0f);
		point_light_2->SetPosition({ -0.5f, 0.5f, -0.3f });

		//auto dir_light = scene_graph->CreateChild<wr::LightNode>(nullptr, wr::LightType::DIRECTIONAL, DirectX::XMVECTOR{ 1, 1, 1 });
	}

	void UpdateScene(wr::SceneGraph* sg)
	{
		t += 10.f * ImGui::GetIO().DeltaTime;

		test_model->m_rigid_body->activate(true);

		//auto pos = test_model->m_position;
		//pos.m128_f32[0] = sin(t * 0.1) * 0.5;
		//test_model->SetPosition(pos);

		//camera->Update(ImGui::GetIO().DeltaTime);
		camera_spline_node->UpdateSplineNode(ImGui::GetIO().DeltaTime, camera);
	}
} /* cube_scene */
