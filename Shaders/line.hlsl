#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 4000
#pragma cull false
#pragma blend alpha

#pragma multi_compile SCREEN_SPACE

#include <include/shadercompat.h>

[[vk::binding(INSTANCE_BUFFER_BINDING, PER_OBJECT)]] StructuredBuffer<float2> Vertices : register(t0);
[[vk::binding(BINDING_START, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t1);
[[vk::binding(CAMERA_BUFFER_BINDING, PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b0);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	float4 Color;
	float4 ScaleTranslate;
	float4 Bounds;
	float2 ScreenSize;
	float Depth;
	uint StereoEye;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
#ifndef SCREEN_SPACE
	float4 worldPos : TEXCOORD0;
#endif
	float2 canvasPos;
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	float2 p = Vertices[index] * ScaleTranslate.xy + ScaleTranslate.zw;
	v2f o;
#ifdef SCREEN_SPACE
	o.position = float4((p / ScreenSize) * 2 - 1, Depth, 1);
	o.position.y = -o.position.y;
#else
	float4x4 o2w = Transforms[instance];
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	float4 worldPos = mul(o2w, float4(p, 0, 1.0));
	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.worldPos = float4(worldPos.xyz, o.position.z);
#endif
	o.canvasPos = (p - Bounds.xy) / Bounds.zw;

	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1) {
	color = Color;
	clip(i.canvasPos.xy);
	clip(1 - i.canvasPos.xy);
	#ifdef SCREEN_SPACE
	depthNormal = float4(0, 0, 0, color.a);
	#else
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, color.a);
	#endif
}