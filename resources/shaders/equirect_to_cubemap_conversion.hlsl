struct VS_INPUT
{
	float3 pos : POSITION;
};

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float3 local_pos : LOCPOS;
};

cbuffer PassIndex : register (b1)
{
	int idx;
}

cbuffer CameraProperties : register(b0)
{
	float4x4 view;
	float4x4 projection;
};

// For textures arrays
// 0 is +X -> matrix to rotate 90� cw around Y
// 1 is -X -> matrix to rotate 90� ccw around Y
// 2 is +Y -> matrix to rotate 90� ccw around X
// 3 is -Y -> matrix to rotate 90� cw around X
// 4 is +Z -> identity matrix
// 5 is -Z -> matrix to rotate 180� cw around Y

VS_OUTPUT main_vs(VS_INPUT input)
{
	VS_OUTPUT output;

	output.local_pos = input.pos.xyz;

	float4x4 vp = mul(projection, view);
	output.pos =  mul(vp, float4(output.local_pos, 1.0f));
	
	return output;
}

struct PS_OUTPUT
{
	float4 color;
};

Texture2D equirectangular_texture : register(t0);
SamplerState s0 : register(s0);

float2 SampleSphericalMap(float3 v)
{
	float2 inv_atan = float2(0.1591f, 0.3183f);

	float2 uv = float2(atan2(v.z, v.x), asin(v.y));
	uv *= inv_atan;
	uv += 0.5f;

	return uv;
}

PS_OUTPUT main_ps(VS_OUTPUT input) : SV_TARGET
{
	PS_OUTPUT output;
	
	float2 uv = SampleSphericalMap(normalize(input.local_pos));

	float3 color = equirectangular_texture.Sample(s0, uv).rgb;

	output.color = float4(color, 1.0f);

	return output;
}
