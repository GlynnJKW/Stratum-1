#ifndef PI
#define PI (3.1415926535897932)
#endif
#ifndef INV_PI
#define INV_PI (1.0 / PI)
#endif

float3 qtRotate(float4 q, float3 v) {
	return 2 * dot(q.xyz, v) * q.xyz + (q.w * q.w - dot(q.xyz, q.xyz)) * v + 2 * q.w * cross(q.xyz, v);
}

float sqr(float x) { return x*x; }
float pow2(float x) { return x*x; }
float pow4(float x) { return pow2(x) * pow2(x); }
float pow5(float x) { return x * pow4(x); }
float2 sqr(float2 x) { return x*x; }
float2 pow2(float2 x) { return x*x; }
float2 pow4(float2 x) { return pow2(x) * pow2(x); }
float2 pow5(float2 x) { return x * pow4(x); }
float3 sqr(float3 x) { return x*x; }
float3 pow2(float3 x) { return x*x; }
float3 pow4(float3 x) { return pow2(x) * pow2(x); }
float3 pow5(float3 x) { return x * pow4(x); }
float4 sqr(float4 x) { return x*x; }
float4 pow2(float4 x) { return x*x; }
float4 pow4(float4 x) { return pow2(x) * pow2(x); }
float4 pow5(float4 x) { return x * pow4(x); }

float Luminance(float3 c) { return dot(c, float3(0.3, 0.6, 1.0)); }