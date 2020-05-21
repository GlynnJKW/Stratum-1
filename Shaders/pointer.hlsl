#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 5000
#pragma cull false
#pragma blend alpha

#include <include/shadercompat.h>

[[vk::binding(CAMERA_BUFFER_BINDING, PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b0);
[[vk::push_constant]] cbuffer PushConstants : register(b1) {
	float4 Color;
	float3 P0;
	float Width;
	float3 P1;
	uint StereoEye;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float4 worldPos : TEXCOORD0;
	float fade : TEXCOORD1;
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	static const float2 positions[6] = {
		float2(-1, 0),
		float2( 1, 0),
		float2( 1, 1),
		
		float2(-1, 1),
		float2(-1, 0),
		float2( 1, 1)
	};
	float2 p = positions[index];

	float3 dir = P1 - P0;
	float3 view = STRATUM_CAMERA_POSITION - P0;
	float3 right = normalize(cross(normalize(dir), normalize(view)));

	float3 worldPos = lerp(P0, P1, p.y) + .5 * Width * right * p.x;

	v2f o;
	o.position = mul(STRATUM_MATRIX_VP, float4(worldPos - STRATUM_CAMERA_POSITION, 1));
	o.worldPos = float4(worldPos.xyz, o.position.z);
	o.fade = p.x;
	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1) {
	color = Color;
	color.a *= (1 - abs(i.fade)) * (1 - abs(i.fade));
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, color.a);
}