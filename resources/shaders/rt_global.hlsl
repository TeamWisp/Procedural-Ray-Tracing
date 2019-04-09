#define MAX_RECURSION 3
//#define FOUR_X_A
//#define PATH_TRACING
//#define DEPTH_OF_FIELD
#define EPSILON 5.0
#define SOFT_SHADOWS
#define MAX_SHADOW_SAMPLES 1

#ifdef FALLBACK
	#undef MAX_RECURSION
	#define MAX_RECURSION 1

	
	//#define NO_SHADOWS
#endif

RaytracingAccelerationStructure Scene : register(t0, space0);

