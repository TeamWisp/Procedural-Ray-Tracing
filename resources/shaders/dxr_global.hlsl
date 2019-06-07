#ifndef __DXR_GLOBAL_HLSL__
#define __DXR_GLOBAL_HLSL__

#define MAX_RECURSION 3
//#define FOUR_X_A
//#define PATH_TRACING
//#define DEPTH_OF_FIELD
#define EPSILON 0.1
#define SOFT_SHADOWS
#define MAX_SHADOW_SAMPLES 1
#define RUSSIAN_ROULETTE
#define NO_PATH_TRACED_NORMALS
#define CALC_BITANGENT // calculate bitangent in the shader instead of using the bitangent uploaded

#ifdef FALLBACK
	#undef MAX_RECURSION
	#define MAX_RECURSION 1

	
	//#define NO_SHADOWS
#endif

RaytracingAccelerationStructure Scene : register(t0, space0);

#endif //__DXR_GLOBAL_HLSL__
