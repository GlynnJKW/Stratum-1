#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 5000
#pragma cull false
#pragma blend alpha

#pragma static_sampler Sampler

#pragma multi_compile HUE
#pragma multi_compile SCREEN_SPACE

#pragma array Textures 32

#include <include/shadercompat.h>

struct GuiRect {
	float4x4 ObjectToWorld;
	float4 Color;
	float4 ScaleTranslate;
	float4 Bounds;

	float4 TextureST;
	uint TextureIndex;
	float Depth;
	uint pad[2];
};

// per-object
[[vk::binding(BINDING_START + 0, PER_OBJECT)]] Texture2D<float4> Textures[64] : register(t0);
[[vk::binding(BINDING_START + 1, PER_OBJECT)]] StructuredBuffer<GuiRect> Rects : register(t32);
[[vk::binding(BINDING_START + 2, PER_OBJECT)]] SamplerState Sampler : register(s0);
// per-camera
[[vk::binding(CAMERA_BUFFER_BINDING, PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b1);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	uint StereoEye;
	float2 ScreenSize;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float4 color : COLOR0;
	float4 texcoord : TEXCOORD0;
	#ifndef SCREEN_SPACE
	float4 worldPos : TEXCOORD1;
	#endif
};

float3 hsv2rgb(float3 c) {
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	static const float2 positions[6] = {
		float2(0,0),
		float2(1,0),
		float2(1,1),
		float2(0,1),
		float2(0,0),
		float2(1,1)
	};

	GuiRect r = Rects[instance];

	float2 p = positions[index] * r.ScaleTranslate.xy + r.ScaleTranslate.zw;
	
	v2f o;
	#ifdef SCREEN_SPACE
	o.position = float4((p / ScreenSize) * 2 - 1, r.Depth, 1);
	o.position.y = -o.position.y;
	#else
	float4x4 o2w = r.ObjectToWorld;
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	float4 worldPos = mul(o2w, float4(p, 0, 1.0));
	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.worldPos = float4(worldPos.xyz, o.position.z);
	#endif

	o.texcoord.xy = positions[index] * r.TextureST.xy + r.TextureST.zw;
	o.texcoord.zw = (p - r.Bounds.xy) / r.Bounds.zw;

	o.color = r.Color;

	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1) {
	#ifdef SCREEN_SPACE
	depthNormal = 0;
	#else
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, 1);
	#endif

	#ifdef HUE
	color = float4(hsv2rgb(float3(i.texcoord.y, i.color.yz)), 1);
	//color = float4(i.texcoord.y, 0, 0, 1);
	#else
	color = float4(hsv2rgb(float3(i.color.x, i.texcoord.xy)), 1);
	//color = float4(0, i.texcoord.xy, 1);
	#endif
	clip(i.texcoord.zw);
	clip(1 - i.texcoord.zw);
	depthNormal.a = color.a;
}