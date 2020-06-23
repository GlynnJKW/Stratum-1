#pragma vertex vsmain
#pragma fragment fsmain

#pragma renderqueue 0
#pragma cull false
#pragma zwrite false
#pragma ztest false

#pragma static_sampler Sampler maxAnisotropy=0 addressMode=clamp_edge

#pragma multi_compile ENVIRONMENT_TEXTURE ENVIRONMENT_TEXTURE_HDR

#include <include/shadercompat.h>
#include <include/math.hlsli>

// per-camera
[[vk::binding(CAMERA_BUFFER_BINDING, PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b1);
// per-material
[[vk::binding(BINDING_START + 5, PER_MATERIAL)]] Texture2D<float4> EnvironmentTexture : register(t7);

[[vk::binding(BINDING_START + 6, PER_MATERIAL)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b3) {
	uint StereoEye;
	float3 AmbientLight;
}

#include <include/util.hlsli>

void vsmain(
	[[vk::location(0)]] float3 vertex : POSITION,
	out float4 position : SV_Position,
	out float4 screenPos : TEXCOORD0,
	out float3 viewRay : TEXCOORD1) {
	if (Camera.OrthographicSize != 0) {
		position = float4(vertex.xy, 0, 1);
		viewRay = float3(STRATUM_MATRIX_V[0].z, STRATUM_MATRIX_V[1].z, STRATUM_MATRIX_V[2].z);
	} else {
		position = mul(STRATUM_MATRIX_P, float4(vertex, 1));
		viewRay = mul(vertex, (float3x3)STRATUM_MATRIX_V);
	}
	screenPos = ComputeScreenPos(position);
}

void fsmain(
	in float4 screenPos : TEXCOORD0,
	in float3 viewRay : TEXCOORD1,
	out float4 color : SVTarget0,
	out float4 depthNormal : SVTarget1 ) {
	float3 ray = normalize(viewRay);

#if defined(ENVIRONMENT_TEXTURE) || defined(ENVIRONMENT_TEXTURE_HDR)
	float2 envuv = float2(atan2(ray.z, ray.x) * INV_PI * .5 + .5, acos(ray.y) * INV_PI);
	color = float4(EnvironmentTexture.SampleLevel(Sampler, envuv, 0).rgb, 1);
#ifdef ENVIRONMENT_TEXTURE_HDR
	color = pow(color, 1 / 2.2);
#endif
#else
	color = float4(AmbientLight, 1);
#endif
	depthNormal = float4(normalize(float3(1)) * Camera.Far, 1);
}