#pragma kernel BakeVolume
#pragma kernel BakeGradient
#pragma kernel BakeTransferFunctionRGB
#pragma kernel BakeTransferFunctionA
#pragma kernel ClearTransferFunction

#pragma multi_compile MASK_COLOR
#pragma multi_compile NON_BAKED_RGBA NON_BAKED_R NON_BAKED_R_COLORIZE NON_BAKED_R_LUT

#define BLADDERBIT 1
#define KIDNEYBIT 2
#define COLONBIT 4
#define SPLEENBIT 8

#pragma static_sampler Sampler max_lod=0 addressMode=clamp_border borderColor=float_transparent_black

#if defined(NON_BAKED_R) || defined(NON_BAKED_R_COLORIZE) || defined(NON_BAKED_R_LUT)
[[vk::binding(0, 0)]] RWTexture3D<float> Volume : register(u0);
#else
[[vk::binding(0, 0)]] RWTexture3D<float4> Volume : register(u0);
#endif
[[vk::binding(1, 0)]] RWTexture3D<uint> RawMask : register(u1);

[[vk::binding(3, 0)]] RWTexture3D<float4> Output : register(u3);
[[vk::binding(4, 0)]] SamplerState Sampler : register(s0);

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


struct TransferTriangle {
	float bottomWidth;
	float topWidth;
	float center;
	float height;
};

struct TransferGradient {
	float center;
	float3 color;
};

#define PRECISION 0.000001

//#if defined NON_BAKED_R_LUT
[[vk::binding(10, 0)]] RWTexture2D<float4> TransferLUT : register(u4);
[[vk::binding(11, 0)]] Texture2D<float4> TransferLUTTex : register(t4);
[[vk::binding(12, 0)]] StructuredBuffer<TransferGradient> GradientRGB : register(t32);
[[vk::binding(13, 0)]] StructuredBuffer<TransferTriangle> GradientA : register(t33);
//#endif

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	uint3 VolumeResolution;
	uint MaskValue;
	float2 RemapRange;
	float2 HueRange;
	int DisplayBody;
}

#include "common.hlsli"

[numthreads(8, 1, 1)]
void ClearTransferFunction(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	TransferLUT[float3(index.x, 0, 0)] = float4(0, 0, 0, 0);
	TransferLUT[float3(index.x, 1, 0)] = float4(0, 0, 0, 0);
}

//Dispatch as (VolumeResolution.x + 3) / 8, Gradients.length - 1, 1
[numthreads(8, 1, 1)]
void BakeTransferFunctionRGB(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;

	float4 prev = TransferLUT[float3(index.x, 0, 0)];
	//index x: position (0 to 1) of LUT
	//index y: which gradient to use
	float t = float(index.x) / float(VolumeResolution.x);
	float localt = (t - GradientRGB[index.y].center) / (GradientRGB[index.y + 1].center - GradientRGB[index.y].center);
	if (localt < 0 || localt >= 1) return;
	TransferLUT[float3(index.x, 0, 0)] = float4(lerp(GradientRGB[index.y].color, GradientRGB[index.y + 1].color, localt), prev.a);
	TransferLUT[float3(index.x, 1, 0)] = float4(lerp(GradientRGB[index.y].color, GradientRGB[index.y + 1].color, localt), prev.a);
}

//Dispatch as (VolumeResolution.x + 3) / 8, Triangles.length, 1
[numthreads(8, 1, 1)]
void BakeTransferFunctionA(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;

	float4 prev = TransferLUT[float3(index.x, 0, 0)];
	//index x: position (0 to 1) of LUT
	//index y: which gradient to use
	float t = float(index.x) / float(VolumeResolution.x);
	TransferTriangle tri = GradientA[index.y];
	float dist = abs(tri.center - t);
	float a = 0;

	if (dist > tri.bottomWidth) return;
	else {
		if (dist <= tri.topWidth) {
			a = tri.height;
		}
		else {
			a = tri.height * (tri.bottomWidth - dist) / (tri.bottomWidth - tri.topWidth);
		}
	}
	TransferLUT[float3(index.x, 0, 0)] = float4(prev.rgb, max(prev.a, a));
	TransferLUT[float3(index.x, 1, 0)] = float4(prev.rgb, max(prev.a, a));
}


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
