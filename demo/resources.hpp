#pragma once

#include "wisp.hpp"

namespace resources
{

	static std::shared_ptr<wr::ModelPool> model_pool;
	static std::shared_ptr<wr::TexturePool> texture_pool;
	static std::shared_ptr<wr::MaterialPool> material_pool;

	static wr::Model* cube_model;
	static wr::Model* plane_model;
	static wr::Model* test_model;
	static wr::MaterialHandle rusty_metal_material;
	static wr::MaterialHandle rock_material;

	void CreateResources(wr::RenderSystem* render_system)
	{
		texture_pool = render_system->CreateTexturePool(8, 20);
		material_pool = render_system->CreateMaterialPool(8);

		// Load Texture.
		wr::TextureHandle rusty_metal_albedo = texture_pool->Load("resources/materials/rusty_metal/rusty_metal_albedo.png", false, true);
		wr::TextureHandle rusty_metal_normal = texture_pool->Load("resources/materials/rusty_metal/rusty_metal_normal.png", false, true);
		wr::TextureHandle rusty_metal_roughness = texture_pool->Load("resources/materials/rusty_metal/rusty_metal_roughness.png", false, true);
		wr::TextureHandle rusty_metal_metallic = texture_pool->Load("resources/materials/rusty_metal/rusty_metal_metallic.png", false, true);

		// Create Material
		rusty_metal_material = material_pool->Create();

		rusty_metal_material.SetAlbedo(rusty_metal_albedo);
		rusty_metal_material.SetNormal(rusty_metal_normal);
		rusty_metal_material.SetRoughness(rusty_metal_roughness);
		rusty_metal_material.SetMetallic(rusty_metal_metallic);

		// Load Texture.
		wr::TextureHandle rock_albedo = texture_pool->Load("resources/materials/rock/rock_albedo.png", false, true);
		wr::TextureHandle rock_normal = texture_pool->Load("resources/materials/rock/rock_normal.png", false, true);
		wr::TextureHandle rock_roughness = texture_pool->Load("resources/materials/rock/rock_roughness.png", false, true);
		wr::TextureHandle rock_metallic = texture_pool->Load("resources/materials/rock/rock_metallic.png", false, true);

		// Create Material
		rock_material = material_pool->Create();

		rock_material.SetAlbedo(rock_albedo);
		rock_material.SetNormal(rock_normal);
		rock_material.SetRoughness(rock_roughness);
		rock_material.SetMetallic(rock_metallic);
	
		model_pool = render_system->CreateModelPool(16, 16);

		// Load Cube.
		{
			wr::MeshData<wr::Vertex> mesh;

			mesh.m_indices = {
				2, 1, 0, 3, 2, 0, 6, 5,
				4, 7, 6, 4, 10, 9, 8, 11,
				10, 8, 14, 13, 12, 15, 14, 12,
				18, 17, 16, 19, 18, 16, 22, 21,
				20, 23, 22, 20
			};

			mesh.m_vertices = {
				{ 1, 1, -1,		1, 1,		0, 0, -1,		0, 0, 0,	0, 0, 0 },
				{ 1, -1, -1,	0, 1,		0, 0, -1,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, -1,	0, 0,		0, 0, -1,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	1, 0,		0, 0, -1,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, 1,		1, 1,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		0, 1,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 0,		0, 0, 1,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		1, 0,		0, 0, 1,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, -1,		1, 0,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, 1, 1,		1, 1,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		0, 1,		1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, -1,	0, 0,		1, 0, 0,		0, 0, 0,	0, 0, 0  },

				{ 1, -1, -1,	1, 0,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, -1, 1,		1, 1,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 1,		0, -1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, -1,	0, 0,		0, -1, 0,		0, 0, 0,	0, 0, 0  },

				{ -1, -1, -1,	0, 1,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, -1, 1,	0, 0,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		1, 0,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	1, 1,		-1, 0, 0,		0, 0, 0,	0, 0, 0  },

				{ 1, 1, 1,		1, 0,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ 1, 1, -1,		1, 1,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, -1,	0, 1,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
				{ -1, 1, 1,		0, 0,		0, 1, 0,		0, 0, 0,	0, 0, 0  },
			};

			cube_model = model_pool->LoadCustom<wr::Vertex>({ mesh });
		}

		{
			wr::MeshData<wr::Vertex> mesh;

			mesh.m_indices = {
				2, 1, 0, 3, 2, 0
			};

			mesh.m_vertices = {
				//POS				UV			NORMAL				TANGENT			BINORMAL
				{  1,  1,  0,		1, 1,		0, 0, -1,			0, 0, 1,		0, 1, 0},
				{  1, -1,  0,		1, 0,		0, 0, -1,			0, 0, 1,		0, 1, 0},
				{ -1, -1,  0,		0, 0,		0, 0, -1,			0, 0, 1,		0, 1, 0},
				{ -1,  1,  0,		0, 1,		0, 0, -1,			0, 0, 1,		0, 1, 0},
			};

			plane_model = model_pool->LoadCustom<wr::Vertex>({ mesh });
		}


		{
			test_model = model_pool->Load<wr::Vertex>(material_pool.get(), texture_pool.get(), "resources/models/xbot.fbx");
		}
	}

}
