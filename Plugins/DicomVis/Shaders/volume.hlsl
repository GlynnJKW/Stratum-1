#pragma kernel Render


#pragma multi_compile MASK_COLOR
#pragma multi_compile NON_BAKED_RGBA NON_BAKED_R NON_BAKED_R_COLORIZE
#pragma multi_compile GRADIENT_TEXTURE
#pragma multi_compile LIGHTING

#pragma static_sampler Sampler max_lod=0 addressMode=clamp_border borderColor=float_transparent_black
//#ifdef BIT_MASK
//#pragma static_sampler MaskSampler max_lod=0 addressMode=clamp_border borderColor=float_transparent_black filter=nearest
//#endif

#ifndef PI
#define PI (3.1415926535897932)
#endif
#ifndef INV_PI
#define INV_PI (1.0 / PI)
#endif

[[vk::binding(0, 0)]] RWTexture2D<float4> History : register(u0);
[[vk::binding(1, 0)]] RWTexture2D<float4> RenderTarget : register(u1);
[[vk::binding(2, 0)]] RWTexture2D<float4> DepthNormal : register(u2);

#if defined(NON_BAKED_R) || defined (NON_BAKED_R_COLORIZE)
[[vk::binding(3, 0)]] Texture3D<float> Volume : register(t0);
#else
[[vk::binding(3, 0)]] Texture3D<float4> Volume : register(t0);
#endif
[[vk::binding(4, 0)]] RWTexture3D<float4> Gradient : register(u3);
[[vk::binding(5, 0)]] Texture3D<uint> RawMask : register(t1);

[[vk::binding(6, 0)]] Texture2D<float4> EnvironmentTexture	: register(t2);

[[vk::binding(7, 0)]] Texture2D<float4> NoiseTex : register(t3);
[[vk::binding(8, 0)]] SamplerState Sampler : register(s0);

struct MaskColors {
	float3 BladderColor;
	float spacing1;
	float3 KidneyColor;
	float spacing2;
	float3 ColonColor;
	float spacing3;
	float3 SpleenColor;
	float spacing4;
	float3 IleumColor;
	float spacing5;
	float3 AortaColor;
	float spacing6;
};
[[vk::binding(9, 0)]] ConstantBuffer<MaskColors> MaskCols : register(b3);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	float4x4 InvViewProj;

	uint2 ScreenResolution;
	uint2 WriteOffset;

	uint3 VolumeResolution;

	float3 VolumePosition;
	float4 VolumeRotation;
	float3 VolumeScale;
	float4 InvVolumeRotation;
	float3 InvVolumeScale;

	float Density;

	uint MaskValue;
	float2 RemapRange;
	float2 HueRange;

	float StepSize;
	uint FrameIndex;
	int DisplayBody;
}

#include "common.hlsli"
#include <include/sampling.hlsli>

float2 RayBox(float3 rayOrigin, float3 rayDirection, float3 mn, float3 mx) {
	float3 id = 1 / rayDirection;
	float3 t0 = (mn - rayOrigin) * id;
	float3 t1 = (mx - rayOrigin) * id;
	float3 tmin = min(t0, t1);
	float3 tmax = max(t0, t1);
	return float2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

float3 qmul(float4 q, float3 vec) {
	return 2 * dot(q.xyz, vec) * q.xyz + (q.w * q.w - dot(q.xyz, q.xyz)) * vec + 2 * q.w * cross(q.xyz, vec);
}
float3 WorldToVolume(float3 pos) {
	return qmul(InvVolumeRotation, pos - VolumePosition) * InvVolumeScale;
}
float3 WorldToVolumeV(float3 vec) {
	return qmul(InvVolumeRotation, vec) * InvVolumeScale;
}
float3 VolumeToWorldV(float3 vec) {
	return qmul(VolumeRotation, vec) * VolumeScale;
}

[numthreads(8, 8, 1)]
void Render(uint3 index : SV_DispatchThreadID) {
	if (any(index.xy >= ScreenResolution)) return;

	// 4xMSAA sampling pattern (unused atm)
	static const float2 jitters[4] = {
		 float2(0.125, 0.375),
		-float2(0.125, 0.375) - float2(0.125, 0.375),
		 float2(0.125, 0.375) - float2(0.125, 0.375),
		-float2(0.125, 0.375) + float2(0.125, 0.375),
	};

	uint idx = index.y * ScreenResolution.x + index.x;
	uint rnd = 0xFFFF * NoiseTex.Load(uint3((idx / (4 * 256)) % 256, (idx / 4) % 256, 0))[idx % 4];
	RandomSampler rng;
	rng.index = FrameIndex % (CMJ_DIM * CMJ_DIM);
	rng.dimension = 1;
	rng.scramble = rnd * 0x1fe3434f * ((FrameIndex + 133 * rnd) / (CMJ_DIM * CMJ_DIM));

	float2 uv = index.xy + 0.5;//jitters[aaSampleIndex];	
	float4 unprojected = mul(InvViewProj, float4(2 * uv / float2(ScreenResolution) - 1, 0, 1));

	float3 rayOrigin = 0;
	float3 rd_w = normalize(unprojected.xyz / unprojected.w - rayOrigin);

	float depth = length(DepthNormal[WriteOffset + index.xy].xyz);
	float f = length(WorldToVolumeV(rd_w * depth));

	rayOrigin = WorldToVolume(rayOrigin);
	float3 rayDirection = WorldToVolumeV(rd_w);

	float2 isect = RayBox(rayOrigin, rayDirection, -.5, .5);
	isect.x = max(0, isect.x);
	isect.y = min(isect.y, f);

	if (isect.x >= isect.y) return;
	
	// jitter samples
	isect.x -= StepSize * SampleRNG(rng).x;

	// place rayOrigin at intersection start
	rayOrigin += 0.5;
	rayOrigin += rayDirection * isect.x;
	isect.y -= isect.x;

	rd_w = -rd_w;

	float3 sp = rayOrigin + rayDirection * StepSize;
	float3 dsp = rayDirection * StepSize;

	float4 sum = 0;
	for (float t = StepSize; t < isect.y;) {
		// SampleColor is defined in common.hlsli
		// It samples the volume and does any processing required, unless the volume has been baked already.
		float4 localSample = SampleColor(sp);

		#ifdef LIGHTING
		float3 gradient = VolumeToWorldV(SampleGradient(sp));
		float l = length(gradient);
		if (l > .001) {
			localSample.rgb *= saturate(1 - l) * max(0, dot(float3(.57735, .57735, .57735), gradient / l));
		}
		#endif
		
		localSample.a *= StepSize * Density;
		localSample.a = saturate(localSample.a);

		// front-to-back alpha blending
		localSample.rgb *= localSample.a;
		sum += (1 - sum.a) * localSample;

		t += StepSize;
		sp += dsp;
	}
	
	uint2 coord = WriteOffset + index.xy;

	
	if (FrameIndex > 0)
		sum += History[coord]; // accumulate history
	// TODO: use depth+normal buffer to attempt to accumulate history even if the camera/volume moves

	History[coord] = sum; // save history

	sum /= FrameIndex + 1; // average over history
	RenderTarget[coord] = float4(RenderTarget[coord].rgb * (1 - sum.a) + sum.rgb * sum.a, 1);
}