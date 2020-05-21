#pragma kernel BakeVolume
#pragma kernel BakeGradient

#pragma multi_compile MASK_COLOR
#pragma multi_compile NON_BAKED_RGBA NON_BAKED_R NON_BAKED_R_COLORIZE

#define BLADDERBIT 1
#define KIDNEYBIT 2
#define COLONBIT 4
#define SPLEENBIT 8

#pragma static_sampler Sampler max_lod=0 addressMode=clamp_border borderColor=float_transparent_black

#if defined(NON_BAKED_R) || defined(NON_BAKED_R_COLORIZE)
[[vk::binding(0, 0)]] RWTexture3D<float> Volume : register(u0);
#else
[[vk::binding(0, 0)]] RWTexture3D<float4> Volume : register(u0);
#endif
[[vk::binding(1, 0)]] RWTexture3D<uint> RawMask : register(u1);
[[vk::binding(3, 0)]] RWTexture3D<float4> Output : register(u3);
[[vk::binding(4, 0)]] SamplerState Sampler : register(s0);

/*
struct Triangle {
	float bottomWidth;
	float topWidth;
	float center;
	float height;
};

struct Gradient {
	float center;
	float3 color;
};
*/

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	uint3 VolumeResolution;
	uint MaskValue;
	float2 RemapRange;
	float2 HueRange;
}

#include "common.hlsli"

[numthreads(4, 4, 4)]
void BakeVolume(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	Output[index] = SampleColor(index); // Same SampleColor function that is used by the volume shader
}

[numthreads(4, 4, 4)]
void BakeGradient(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	Output[index] = float4(
		SampleColor(min(index + uint3(1, 0, 0), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(1, 0, 0), 0)).a,
		SampleColor(min(index + uint3(0, 1, 0), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(0, 1, 0), 0)).a,
		SampleColor(min(index + uint3(0, 0, 1), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(0, 0, 1), 0)).a, 0);
}
