#ifndef SHADER_COMPAT_H
#define SHADER_COMPAT_H

#ifdef __cplusplus
#define uint uint32_t
#endif

#define PER_CAMERA 0
#define PER_MATERIAL 1
#define PER_OBJECT 2

#define CAMERA_BUFFER_BINDING 0
#define INSTANCE_BUFFER_BINDING 1
#define LIGHT_BUFFER_BINDING 2
#define SHADOW_ATLAS_BINDING 3
#define SHADOW_BUFFER_BINDING 4
#define BINDING_START 5

#define LIGHT_SUN 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#ifndef __cplusplus
#define STRATUM_PUSH_CONSTANTS \
uint StereoEye; \
float3 AmbientLight; \
uint LightCount; \
float2 ShadowTexelSize;

#define STRATUM_MATRIX_V Camera.View[StereoEye]
#define STRATUM_MATRIX_P Camera.Projection[StereoEye]
#define STRATUM_MATRIX_VP Camera.ViewProjection[StereoEye]
#define STRATUM_CAMERA_POSITION Camera.Position[StereoEye].xyz
#endif

struct InstanceBuffer {
	float4x4 ObjectToWorld;
	float4x4 WorldToObject;
};

struct CameraBuffer {
	float4x4 View[2];
	float4x4 Projection[2];
	float4x4 ViewProjection[2];
	float4x4 InvProjection[2];
	float4 Position[2];
	float Near;
	float Far;
	float AspectRatio;
	float OrthographicSize;
};

struct GPULight {
	float4 CascadeSplits;
	float3 WorldPosition;
	float InvSqrRange;
	float3 Direction;
	float SpotAngleScale;
	float3 Color;
	float SpotAngleOffset;
	uint Type;
	int ShadowIndex;
	int2 pad;
};

struct ShadowData {
	float4x4 WorldToShadow; // ViewProjection matrix for the shadow render
	float4 ShadowST;
	float3 CameraPosition;
	float InvProj22;
};

struct VertexWeight {
	float4 Weights;
	uint4 Indices;
};

#ifdef __cplusplus
#undef uint
#endif

#endif