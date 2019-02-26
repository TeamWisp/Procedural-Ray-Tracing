#define LIGHTS_REGISTER register(t2)
#include "util.hlsl"
#include "pbr_util.hlsl"
#include "lighting.hlsl"
#include "rt_texture_lod.hlsl"

struct Vertex
{
	float3 pos;
	float2 uv;
	float3 normal;
	float3 tangent;
	float3 bitangent;
	float3 color;
};

struct Material
{
	float albedo_id;
	float normal_id;
	float roughness_id;
	float metalicness_id;
};

struct Offset
{
    float material_idx;
    float idx_offset;
    float vertex_offset;
};

RWTexture2D<float4> gOutput : register(u0);
ByteAddressBuffer g_indices : register(t1);
StructuredBuffer<Vertex> g_vertices : register(t3);
StructuredBuffer<Material> g_materials : register(t4);
StructuredBuffer<Offset> g_offsets : register(t5);

Texture2D g_textures[200] : register(t8);
Texture2D gbuffer_albedo : register(t208);
Texture2D gbuffer_normal : register(t209);
Texture2D gbuffer_depth : register(t210);
Texture2D skybox : register(t6);
TextureCube irradiance_map : register(t7);
SamplerState s0 : register(s0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct ReflectionHitInfo
{
	float3 origin;
	float3 color;
	uint seed;
	RayCone cone;
};

cbuffer CameraProperties : register(b0)
{
	float4x4 inv_view;
	float4x4 inv_projection;
	float4x4 inv_vp;

	float2 padding;
	float frame_idx;
	float intensity;
};

struct Ray
{
	float3 origin;
	float3 direction;
};

uint3 Load3x32BitIndices(uint offsetBytes)
{
	// Load first 2 indices
	return g_indices.Load3(offsetBytes);
}

// Retrieve hit world position.
float3 HitWorldPosition()
{
	return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float3 TraceReflectionRay(float3 origin, float3 norm, float3 direction, uint rand_seed, RayCone cone)
{
	origin += norm * EPSILON;

	ReflectionHitInfo payload = {origin, float3(0, 0, 1), rand_seed, cone};

	// Define a ray, consisting of origin, direction, and the min-max distance values
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = 10000.0;

	// Trace the ray
	TraceRay(
		Scene,
		RAY_FLAG_NONE,
		//RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
		// RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
		~0, // InstanceInclusionMask
		0, // RayContributionToHitGroupIndex
		1, // MultiplierForGeometryContributionToHitGroupIndex
		0, // miss shader index
		ray,
		payload);

	return payload.color;
}

float3 unpack_position(float2 uv, float depth)
{
	// Get world space position
	const float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
	float4 wpos = mul(inv_vp, ndc);
	return (wpos.xyz / wpos.w).xyz;
}

float3 DoReflection(float3 wpos, float3 V, float3 normal, uint rand_seed, RayCone cone)
{
	// Calculate ray info
	float3 reflected = reflect(-V, normal);

	// Shoot reflection ray
	float3 reflection = TraceReflectionRay(wpos, normal, reflected, rand_seed, cone);
	return reflection;
}

#define M_PI 3.14159265358979

[shader("raygeneration")]
void RaygenEntry()
{
	uint rand_seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frame_idx);

	// Texture UV coordinates [0, 1]
	float2 uv = float2(DispatchRaysIndex().xy) / float2(DispatchRaysDimensions().xy - 1);

	// Screen coordinates [0, resolution] (inverted y)
	int2 screen_co = DispatchRaysIndex().xy;

	// Get g-buffer information
	float4 albedo_roughness = gbuffer_albedo[screen_co];
	float4 normal_metallic = gbuffer_normal[screen_co];

	// Unpack G-Buffer
	float depth = gbuffer_depth[screen_co].x;
	float3 wpos = unpack_position(float2(uv.x, 1.f - uv.y), depth);
	float3 albedo = albedo_roughness.rgb;
	float roughness = albedo_roughness.w;
	float3 normal = normal_metallic.xyz;
	float metallic = normal_metallic.w;

	// Do lighting
	float3 cpos = float3(inv_view[0][3], inv_view[1][3], inv_view[2][3]);
	float3 V = normalize(cpos - wpos);

	if (length(normal) == 0)		//TODO: Could be optimized by only marking pixels that need lighting, but that would require execute rays indirect
	{
		gOutput[DispatchRaysIndex().xy] = float4(skybox.SampleLevel(s0, SampleSphericalMap(-V), 0));
		return;
	}

	// Setup the ray cone.
	SurfaceHit sfhit;
	sfhit.pos = wpos;
	sfhit.normal = normal;
	sfhit.dist = length(cpos - wpos);
	sfhit.surface_spread_angle = ComputeSurfaceSpreadAngle(gbuffer_depth, gbuffer_normal, inv_vp, wpos, normal);

	RayCone cone = ComputeRayConeFromGBuffer(sfhit);
	RayCone copy = cone;

	float3 lighting = shade_pixel(wpos, V, albedo, metallic, roughness, normal, rand_seed, 0);
	float3 reflection = DoReflection(wpos, V, normal, rand_seed, cone);

	float3 flipped_N = normal;
	flipped_N.y *= -1;
	const float3 sampled_irradiance = irradiance_map.SampleLevel(s0, flipped_N, 0).xyz;

	const float3 F = F_SchlickRoughness(max(dot(normal, V), 0.0), metallic, albedo, roughness);
	float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

	float3 specular = (reflection.xyz) * F;
	float3 diffuse = albedo * sampled_irradiance;
	float3 ambient = (kD * diffuse + specular);

	gOutput[DispatchRaysIndex().xy] = sfhit.surface_spread_angle;
	gOutput[DispatchRaysIndex().xy] = float4(ambient + lighting, 1);
}

//Reflections

float3 HitAttribute(float3 a, float3 b, float3 c, BuiltInTriangleIntersectionAttributes attr)
{
	float3 vertexAttribute[3];
	vertexAttribute[0] = a;
	vertexAttribute[1] = b;
	vertexAttribute[2] = c;

	return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

[shader("closesthit")]
void ReflectionHit(inout ReflectionHitInfo payload, in MyAttributes attr)
{
	// Calculate the essentials
	const Offset offset = g_offsets[InstanceID()];
	const Material material = g_materials[offset.material_idx];
	const float index_offset = offset.idx_offset;
	const float vertex_offset = offset.vertex_offset;
	const float3x4 model_matrix = ObjectToWorld3x4();

	// Find first index location
	const uint index_size = 4;
	const uint indices_per_triangle = 3;
	const uint triangle_idx_stride = indices_per_triangle * index_size;

	uint base_idx = PrimitiveIndex() * triangle_idx_stride;
	base_idx += index_offset * 4; // offset the start

	uint3 indices = Load3x32BitIndices(base_idx);
	indices += float3(vertex_offset, vertex_offset, vertex_offset); // offset the start

	// Gather triangle vertices
	 Vertex v0 = g_vertices[indices.x];
	 Vertex v1 = g_vertices[indices.y];
	 Vertex v2 = g_vertices[indices.z];

	//Get data from VBO
	float2 uv = HitAttribute(float3(v0.uv, 0), float3(v1.uv, 0), float3(v2.uv, 0), attr).xy;
	uv.y = 1.0f - uv.y;
	float3 normal = normalize(HitAttribute(v0.normal, v1.normal, v2.normal, attr));
	float3 tangent = HitAttribute(v0.tangent, v1.tangent, v2.tangent, attr);
	float3 bitangent = HitAttribute(v0.bitangent, v1.bitangent, v2.bitangent, attr);

	const float3 hit_pos = HitWorldPosition();
	float3 V = normalize(payload.origin - hit_pos);

	v0.uv.y = 1.0f - v0.uv.y;
	v1.uv.y = 1.0f - v1.uv.y;
	v2.uv.y = 1.0f - v2.uv.y;

	payload.cone = Propagate(payload.cone, 0, length(payload.origin - hit_pos));

	float mip = ComputeTextureLOD(
		payload.cone,
		V,
		normal,
		mul(model_matrix, float4(v0.pos, 1)),
		mul(model_matrix, float4(v1.pos, 1)),
		mul(model_matrix, float4(v2.pos, 1)),
		v0.uv,
		v1.uv,
		v2.uv,
		g_textures[material.albedo_id]
	);

	/*mip = ComputeTriangleArea(
		mul(model_matrix, float4(v0.pos, 1)),
		mul(model_matrix, float4(v1.pos, 1)),
		mul(model_matrix, float4(v2.pos, 1))
	);*/


	mip = ComputeTextureCoordsArea(
		v0.uv,
		v1.uv,
		v2.uv,
		g_textures[material.albedo_id]
	);


//#define COMPRESSED_PBR
#ifdef COMPRESSED_PBR
	float3 albedo = g_textures[material.albedo_id].SampleLevel(s0, uv, mip).xyz;
	float roughness =  max(0.05, g_textures[material.metalicness_id].SampleLevel(s0, uv, mip).y);
	float metal = g_textures[material.metalicness_id].SampleLevel(s0, uv, mip).z;
#else
	float3 albedo = g_textures[material.albedo_id].SampleLevel(s0, uv, mip).xyz;
	float roughness = max(0.05, g_textures[material.roughness_id].SampleLevel(s0, uv, mip).x);
	float metal = g_textures[material.metalicness_id].SampleLevel(s0, uv, mip).x;
#endif

	//Direction & position

	float3 N = normalize(mul(model_matrix, float4(normal, 0)));
	float3 T = normalize(mul(model_matrix, float4(tangent, 0)));
	float3 B = normalize(mul(model_matrix, float4(bitangent, 0)));
	float3x3 TBN = float3x3(T, B, N);

	float3 normal_t = (g_textures[material.normal_id].SampleLevel(s0, uv, mip).xyz) * 2.0 - float3(1.0, 1.0, 1.0);

	float3 fN = normalize(mul(normal_t, TBN));
	if (dot(fN, V) <= 0.0f) fN = -fN;

	//TODO: Reflections

	//Shading
	float3 flipped_N = fN;
	flipped_N.y *= -1;
	const float3 sampled_irradiance = irradiance_map.SampleLevel(s0, flipped_N, 0).xyz;

	const float3 F = F_SchlickRoughness(max(dot(fN, V), 0.0), metal, albedo, roughness);
	float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metal;

	float3 specular = (float3(0, 0, 0)) * F;
	float3 diffuse = albedo * sampled_irradiance;
	float3 ambient = (kD * diffuse + specular);

	float3 lighting = shade_pixel(hit_pos, V, albedo, metal, roughness, fN, payload.seed, 1);

	payload.color = ambient + lighting;
	payload.color = mip;
}

//Reflection skybox

[shader("miss")]
void ReflectionMiss(inout ReflectionHitInfo payload)
{
	payload.color = skybox.SampleLevel(s0, SampleSphericalMap(WorldRayDirection()), 0);
}
