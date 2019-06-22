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
#ifndef __DXR_STRUCTS_HLSL__
#define __DXR_STRUCTS_HLSL__

struct Vertex
{
	float3 pos;
	float2 uv;
	float3 normal;
	float3 tangent;
	float3 bitangent;
};

struct MaterialData
{
	float3 color;
	float metallic;

	float roughness;
	float emissive_multiplier;
	float is_double_sided;
	float use_alpha_masking;

	float albedo_uv_scale;
	float normal_uv_scale;
	float roughness_uv_scale;
	float metallic_uv_scale;

	float emissive_uv_scale;
	float ao_uv_scale;
	float padding;
	uint flags;
};

struct Material
{
	uint albedo_id;
	uint normal_id;
	uint roughness_id;
	uint metalicness_id;
	uint emissive_id;
	uint ao_id;
	float2 padding;

	MaterialData data;
};

struct Offset
{
	uint material_idx;
	uint idx_offset;
	uint vertex_offset;
};

struct Ray
{
	float3 origin;
	float3 direction;
};

struct RayCone
{
	float width;
	float spread_angle;
};

struct ReflectionHitInfo
{
	float3 origin;
	float3 color;
	unsigned int seed;
	unsigned int depth;
	float hit_t;
	RayCone cone;
};

struct ShadowHitInfo
{
	float is_hit;
	float padding;
};

struct PathTracingHitInfo
{
	float3 color;
	unsigned int seed;
	float3 origin;
	unsigned int depth;
};

struct PathTracingHitInfoCone
{
	float3 color;
	unsigned int seed;
	float3 origin;
	unsigned int depth;
	RayCone cone;
};

struct FullRTHitInfo
{
	float3 color;
	unsigned int seed;
	float3 origin;
	unsigned int depth;
};

struct SurfaceHit
{
	float3 pos;
	float3 normal;
	float surface_spread_angle;
	float dist;
};


#endif //__DXR_STRUCTS_HLSL__
