#ifndef UTIL_H
#define UTIL_H

float4 ComputeScreenPos(float4 pos) {
	return float4((pos.xy * float2(1, sign(STRATUM_MATRIX_P[1][1])) + pos.w) * .5, pos.zw);
}

float3 ComputeView(float3 worldPos, float4 screenPos) {
	if (Camera.OrthographicSize) {
		float2 uv = screenPos.xy / screenPos.w;
		float3 view = float3(uv * 2 - 1, Camera.Near);
		view.x *= Camera.AspectRatio; // aspect
		view.xy *= Camera.OrthographicSize; // ortho size
		return -mul(float4(view, 1), STRATUM_MATRIX_V).xyz;
	} else
		return normalize(-worldPos.xyz);
	
}
float LinearDepth01(float screenPos_z) {
	return screenPos_z / STRATUM_MATRIX_P[2][2] / (Camera.Far - Camera.Near);
}

#endif