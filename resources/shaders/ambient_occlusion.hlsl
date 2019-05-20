#include "rt_global.hlsl"
#include "util.hlsl"

RWTexture2D<float4> output : register(u0); // x: AO value
Texture2D gbuffer_normal : register(t1);
Texture2D gbuffer_depth : register(t2);

struct AOHitInfo
{
  float is_hit;
  float thisvariablesomehowmakeshybridrenderingwork_killme;
};

cbuffer CBData : register(b0)
{
	float4x4 inv_vp;
	float4x4 inv_view;

	float bias;
	float radius;
	float power;
	unsigned int sample_count;
};

struct Attributes { };

float3 unpack_position(float2 uv, float depth)
{
	// Get world space position
	const float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
	float4 wpos = mul(inv_vp, ndc);
	return (wpos.xyz / wpos.w).xyz;
}

bool TraceAORay(uint idx, float3 origin, float3 direction, float far, unsigned int depth)
{
	// Define a ray, consisting of origin, direction, and the min-max distance values
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = bias;
	ray.TMax = far;

	AOHitInfo payload = { false, 0 };

	// Trace the ray
	TraceRay(
		Scene,
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,// RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
		~0, // InstanceInclusionMask
		0, // RayContributionToHitGroupIndex
		0, // MultiplierForGeometryContributionToHitGroupIndex
		0, // miss shader index is set to idx but can probably be anything.
		ray,
		payload);

	return payload.is_hit;
}


[shader("raygeneration")]
void AORaygenEntry()
{
    uint rand_seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, 0);

	// Texture UV coordinates [0, 1]
	float2 uv = float2(DispatchRaysIndex().xy) / float2(DispatchRaysDimensions().xy - 1);

	// Screen coordinates [0, resolution] (inverted y)
	int2 screen_co = DispatchRaysIndex().xy;

    float3 normal = gbuffer_normal[screen_co].xyz;

	float depth = gbuffer_depth[screen_co].x;
	float3 wpos = unpack_position(float2(uv.x, 1.f - uv.y), depth);


	float3 camera_pos = float3(inv_view[0][3], inv_view[1][3], inv_view[2][3]);
	float cam_distance = length(wpos-camera_pos);

    float ao_value = 1.0f;
    for(uint i = 0; i< sample_count; i++)
    {
        ao_value -= (1.0f/float(sample_count)) * TraceAORay(0, wpos + normal * (cam_distance * bias), getCosHemisphereSample(rand_seed, normal), radius, 0);
    }
	
    output[DispatchRaysIndex().xy].x = ao_value / power;
	//output[DispatchRaysIndex().xy].x = cam_distance;
}

[shader("closesthit")]
void ClosestHitEntry(inout AOHitInfo hit, Attributes bary)
{
    hit.is_hit = 1.f;
}

[shader("miss")]
void MissEntry(inout AOHitInfo hit : SV_RayPayload)
{
    hit.is_hit = 0.0f;
}
