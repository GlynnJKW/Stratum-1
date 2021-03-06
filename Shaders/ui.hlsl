#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 4000
#pragma cull false
#pragma blend alpha

#pragma static_sampler Sampler

#pragma multi_compile TEXTURED
#pragma multi_compile SCREEN_SPACE

#pragma array Textures 64

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
#define PRECISION 0.000001

struct v2f {
	float4 position : SV_Position;
	float4 color : COLOR0;
	float4 texcoord : TEXCOORD0;
	#ifndef SCREEN_SPACE
	float4 worldPos : TEXCOORD1;
	#endif
	#ifdef TEXTURED
	uint textureIndex;
	#endif
};

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

	#ifdef TEXTURED
	o.textureIndex = r.TextureIndex;
	#endif

	#ifdef SCREEN_SPACE
	o.texcoord.xy = float2(positions[index].x, 1 - positions[index].y) * r.TextureST.xy + r.TextureST.zw;
	#else
	o.texcoord.xy = positions[index] * r.TextureST.xy + r.TextureST.zw;
	#endif

	o.texcoord.zw = (p - r.Bounds.xy) / (r.Bounds.zw + PRECISION);

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

	#ifdef TEXTURED
	#ifdef SCREEN_SPACE
	color = Textures[i.textureIndex].SampleBias(Sampler, i.texcoord.xy, -.5) * i.color;
	#else
	color = Textures[i.textureIndex].Sample(Sampler, i.texcoord.xy) * i.color;
	#endif
	#else
	color = i.color;
	#endif
	clip(i.texcoord.zw);
	clip(1 - i.texcoord.zw);
	depthNormal.a = color.a;
}