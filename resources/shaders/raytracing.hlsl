#define LIGHTS_REGISTER register(t2)
#include "util.hlsl"
#include "pbr_util.hlsl"
#include "rt_texture_lod.hlsl"
#include "lighting.hlsl"

static const float M_PI = 3.14159265f;

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

Texture2D skybox : register(t6);
TextureCube irradiance_map : register(t7);
Texture2D g_textures[200] : register(t8);
SamplerState s0 : register(s0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct HitInfo
{
	float3 color;
	unsigned int seed;
	float3 origin;
	unsigned int depth;
};

cbuffer CameraProperties : register(b0)
{
	float4x4 view;
	float4x4 inv_projection_view;
	float3 camera_position;
	float padding;

	float focal_radius;
	float focal_len;
	float frame_idx;
	float intensity;
};

struct Ray
{
	float3 origin;
	float3 direction;
};

float3 getPerpendicularVector(float3 u)
{
	float3 a = abs(u);
	uint xm = ((a.x - a.y)<0 && (a.x - a.z)<0) ? 1 : 0;
	uint ym = (a.y - a.z)<0 ? (1 ^ xm) : 0;
	uint zm = 1 ^ (xm | ym);
	return cross(u, float3(xm, ym, zm));
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 getCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
	// Get 2 random numbers to select our sample with
	float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = getPerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(max(0.0, 1.0f - randVal.x));
}

uint3 Load3x32BitIndices(uint offsetBytes)
{
	// Load first 2 indices
 	return g_indices.Load3(offsetBytes);
}

inline Ray GenerateCameraRay(uint2 index, in float3 cameraPosition, in float4x4 projectionToWorld, in float2 offset, unsigned int seed)
{
#ifdef DEPTH_OF_FIELD
	float2 pixelOff = float2(nextRand(seed), nextRand(seed));  // Random offset in pixel to reduce floating point error.

	float3 cameraU = float3(1, 0, 0);
	float3 cameraV = float3(0, 1, 0);
	float3 cameraW = float3(0, 0, 1);

	float2 xy = (index + offset + pixelOff) + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

	// Unproject the pixel coordinate into a world positon.
	float4 world = mul(float4(screenPos, 0, 1), projectionToWorld);
	world.xyz = world.x * cameraU + world.y * cameraV + cameraW;
	world.xyz /= 1;

	float2 pixelCenter = (index + offset) / DispatchRaysDimensions().xy;            // Pixel ID -> [0..1] over screen
	float2 ndc = float2(2, -2) * pixelCenter + float2(-1, 1);             // Convert to [-1..1]
	float3 rayDir = ndc.x * cameraU + ndc.y * cameraV + cameraW;  // Ray to point on near plane
	rayDir /= 1;

	float focallen = focal_len;
	float lensrad = focal_len / (2.0f * 16);
	float3 focalPt = cameraPosition + focallen * world;

	float2 rngLens = float2(6.2831853f * nextRand(seed), lensrad*nextRand(seed));
	float2 lensPos = float2(cos(rngLens.x) * rngLens.y, sin(rngLens.x) * rngLens.y);

	//lensPos = mul(float4(lensPos, 0, 0), projectionToWorld);

	Ray ray;
	ray.origin = cameraPosition + float3(lensPos, 0);
	ray.direction = normalize(focalPt.xyz - ray.origin);
#else
    float2 xy = (index + offset) + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a world positon.
    float4 world = mul(float4(screenPos, 0, 1), projectionToWorld);
    world.xyz /= world.w;

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = normalize(world.xyz - ray.origin);

    return ray;
#endif
}

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float4 TraceColorRay(float3 origin, float3 direction, unsigned int depth, unsigned int seed)
{
	if (depth >= MAX_RECURSION)
	{
		return skybox.SampleLevel(s0, SampleSphericalMap(direction), 0);
	}

	// Define a ray, consisting of origin, direction, and the min-max distance values
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = 10000.0;

	HitInfo payload = { float3(1, 1, 1), seed, origin, depth };

	// Trace the ray
	TraceRay(
		Scene,
		RAY_FLAG_CULL_FRONT_FACING_TRIANGLES,
		~0, // InstanceInclusionMask
		0, // RayContributionToHitGroupIndex
		0, // MultiplierForGeometryContributionToHitGroupIndex
		0, // miss shader index
		ray,
		payload);

	return float4(payload.color, 1);
}

[shader("raygeneration")]
void RaygenEntry()
{
	uint rand_seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frame_idx);

//#define FOUR_X_AA
#ifdef FOUR_X_AA
	Ray a = GenerateCameraRay(DispatchRaysIndex().xy, camera_position, inv_projection_view, float2(0.5, 0), rand_seed);
	Ray b = GenerateCameraRay(DispatchRaysIndex().xy, camera_position, inv_projection_view, float2(-0.5, 0), rand_seed);
	Ray c = GenerateCameraRay(DispatchRaysIndex().xy, camera_position, inv_projection_view, float2(0.0, 0.5), rand_seed);
	Ray d = GenerateCameraRay(DispatchRaysIndex().xy, camera_position, inv_projection_view, float2(0.0, -0.5), rand_seed);

	float3 result_a = TraceColorRay(a.origin, a.direction, 0, rand_seed);
	nextRand(rand_seed);
	float3 result_b = TraceColorRay(b.origin, b.direction, 0, rand_seed);
	nextRand(rand_seed);
	float3 result_c = TraceColorRay(c.origin, c.direction, 0, rand_seed);
	nextRand(rand_seed);
	float3 result_d = TraceColorRay(d.origin, d.direction, 0, rand_seed);

	float3 result = (result_a + result_b + result_c + result_d) / 4;
#else
	Ray ray = GenerateCameraRay(DispatchRaysIndex().xy, camera_position, inv_projection_view, float2(0, 0), rand_seed);
	float3 result = TraceColorRay(ray.origin, ray.direction, 0, rand_seed);
#endif
	
	gOutput[DispatchRaysIndex().xy] = float4(result, 1);
}

[shader("miss")]
void MissEntry(inout HitInfo payload)
{
	payload.color = skybox.SampleLevel(s0, SampleSphericalMap(WorldRayDirection()), 0);
}

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
void ClosestHitEntry(inout HitInfo payload, in MyAttributes attr)
{
	// Calculate the essentials
	const Offset offset = g_offsets[InstanceID()];
	const Material material = g_materials[offset.material_idx];
	const float3 hit_pos = HitWorldPosition();
	const float index_offset = offset.idx_offset;
	const float vertex_offset = offset.vertex_offset;

	// Find first index location
	const uint index_size = 4;
    const uint indices_per_triangle = 3;
    const uint triangle_idx_stride = indices_per_triangle * index_size;

    uint base_idx = PrimitiveIndex() * triangle_idx_stride;
	base_idx += index_offset * 4; // offset the start

	uint3 indices = Load3x32BitIndices(base_idx);
	indices += float3(vertex_offset, vertex_offset, vertex_offset); // offset the start

	// Gather triangle vertices
	const Vertex v0 = g_vertices[indices.x];
	const Vertex v1 = g_vertices[indices.y];
	const Vertex v2 = g_vertices[indices.z];

	// Variables
	const float3 V = normalize(payload.origin - hit_pos);

	// Calculate actual "fragment" attributes.
	const float3 frag_pos = HitAttribute(v0.pos, v1.pos, v2.pos, attr);
	const float3 normal = normalize(HitAttribute(v0.normal, v1.normal, v2.normal, attr));
	const float3 tangent = HitAttribute(v0.tangent, v1.tangent, v2.tangent, attr);
	const float3 bitangent = HitAttribute(v0.bitangent, v1.bitangent, v2.bitangent, attr);

	float2 uv = HitAttribute(float3(v0.uv, 0), float3(v1.uv, 0), float3(v2.uv, 0), attr).xy;
	uv.y = 1.0f - uv.y;

	float mip_level = 0;

//#define COMPRESSED_PBR
#ifdef COMPRESSED_PBR
	const float3 albedo = g_textures[material.albedo_id].SampleLevel(s0, uv, mip_level).xyz;
	float roughness =  max(0.05, g_textures[material.metalicness_id].SampleLevel(s0, uv, mip_level).y);
	float metal = g_textures[material.metalicness_id].SampleLevel(s0, uv, mip_level).z;
	const float3 normal_t = (g_textures[material.normal_id].SampleLevel(s0, uv, mip_level).xyz) * 2.0 - float3(1.0, 1.0, 1.0);
#else
	const float3 albedo = g_textures[material.albedo_id].SampleLevel(s0, uv, mip_level).xyz;
	float roughness =  max(0.05, g_textures[material.roughness_id].SampleLevel(s0, uv, mip_level).r);
	float metal = g_textures[material.metalicness_id].SampleLevel(s0, uv, mip_level).r;
	const float3 normal_t = (g_textures[material.normal_id].SampleLevel(s0, uv, mip_level).xyz * 2.0) - float3(1.0, 1.0, 1.0);
#endif
	
	float3 N = normalize(mul(ObjectToWorld3x4(), float4(normal, 0)));
	float3 T = normalize(mul(ObjectToWorld3x4(), float4(tangent, 0)));
//#define CALC_B
#ifndef CALC_B
	const float3 B = normalize(mul(ObjectToWorld3x4(), float4(bitangent, 0)));
#else
	T = normalize(T - dot(T, N) * N);
	float3 B = cross(N, T);
#endif
	const float3x3 TBN = float3x3(T, B, N);

	float3 fN = normalize(mul(normal_t, TBN));
	if (dot(fN, V) <= 0.0f) fN = -fN;

	// Irradiance
	float3 flipped_N = fN;
	flipped_N.y *= -1;
	const float3 sampled_irradiance = irradiance_map.SampleLevel(s0, flipped_N, 0).xyz;

	// Direct
	float3 reflect_dir = reflect(-V, fN);
	float3 reflection = TraceColorRay(hit_pos + N * EPSILON, reflect_dir, payload.depth + 1, payload.seed);

	const float3 F = F_SchlickRoughness(max(dot(fN, V), 0.0), metal, albedo, roughness);
	float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metal;

	float3 lighting = shade_pixel(hit_pos, V, albedo, metal, roughness, fN, payload.seed, payload.depth);
	float3 specular = (reflection) * F;
	float3 diffuse = albedo * sampled_irradiance;
	float3 ambient = (kD * diffuse + specular);

	//float2 xx = WorldToScreen(frag_pos, inv_projection_view);

	payload.color = metal;
	payload.color = ambient + lighting;
	//payload.color = float3(xx, 0);
}
