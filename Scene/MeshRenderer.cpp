#include <Core/DescriptorSet.hpp>
#include <Scene/MeshRenderer.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Environment.hpp>
#include <Util/Profiler.hpp>

#include <Shaders/include/shadercompat.h>

using namespace std;

MeshRenderer::MeshRenderer(const string& name)
	: Object(name), mVisible(true), mMesh(nullptr), mRayMask(0) {}
MeshRenderer::~MeshRenderer() {}

bool MeshRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	if (!Mesh())
		mAABB = AABB(WorldPosition(), WorldPosition());
	else
		mAABB = Mesh()->Bounds() * ObjectToWorld();
	return true;
}

void MeshRenderer::PreRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	if (pass == PASS_MAIN) Scene()->Environment()->SetEnvironment(camera, mMaterial.get());
}

void MeshRenderer::DrawInstanced(CommandBuffer* commandBuffer, Camera* camera, uint32_t instanceCount, VkDescriptorSet instanceDS, PassType pass) {
	::Mesh* mesh = Mesh();

	VkCullModeFlags cull = (pass == PASS_DEPTH) ? VK_CULL_MODE_NONE : VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
	VkPipelineLayout layout = commandBuffer->BindMaterial(mMaterial.get(), pass, mesh->VertexInput(), camera, mesh->Topology(), cull);
	if (!layout) return;
	auto shader = mMaterial->GetShader(pass);

	uint32_t lc = (uint32_t)Scene()->ActiveLights().size();
	float2 s = Scene()->ShadowTexelSize();
	float t = Scene()->TotalTime();
	commandBuffer->PushConstant(shader, "Time", &t);
	commandBuffer->PushConstant(shader, "LightCount", &lc);
	commandBuffer->PushConstant(shader, "ShadowTexelSize", &s);
	
	if (instanceDS != VK_NULL_HANDLE)
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, &instanceDS, 0, nullptr);

	commandBuffer->BindVertexBuffer(mesh->VertexBuffer().get(), 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
	commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
		commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	}
}

void MeshRenderer::Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	DrawInstanced(commandBuffer, camera, 1, VK_NULL_HANDLE, pass);
}

bool MeshRenderer::Intersect(const Ray& ray, float* t, bool any) {
	::Mesh* m = Mesh();
	if (!m) return false;
	Ray r;
	r.mOrigin = (WorldToObject() * float4(ray.mOrigin, 1)).xyz;
	r.mDirection = (WorldToObject() * float4(ray.mDirection, 0)).xyz;
	return m->Intersect(r, t, any);
}

void MeshRenderer::DrawGizmos(CommandBuffer* commandBuffer, Camera* camera) {
	if (!Mesh()) return;

	TriangleBvh2* bvh = Mesh()->BVH();

	if (bvh->Nodes().size() == 0) return;

	uint32_t todo[1024];
	int32_t stackptr = 0;

	todo[stackptr] = 0;

	while (stackptr >= 0) {
		int ni = todo[stackptr];
		stackptr--;
		const TriangleBvh2::Node& node(bvh->Nodes()[ni]);

		if (node.mRightOffset == 0) { // leaf node
			for (uint32_t o = 0; o < node.mCount; ++o) {
				uint3 tri = bvh->GetTriangle(node.mStartIndex + o);
				float3 v0 = bvh->GetVertex(tri.x);
				float3 v1 = bvh->GetVertex(tri.y);
				float3 v2 = bvh->GetVertex(tri.z);
				AABB box(min(min(v0, v1), v2), max(max(v0, v1), v2));
				Gizmos::DrawLine((ObjectToWorld() * float4(v0, 1)).xyz, (ObjectToWorld() * float4(v1, 1)).xyz, float4(.2f, .2f, 1, .1f));
				Gizmos::DrawLine((ObjectToWorld() * float4(v0, 1)).xyz, (ObjectToWorld() * float4(v2, 1)).xyz, float4(.2f, .2f, 1, .1f));
				Gizmos::DrawLine((ObjectToWorld() * float4(v1, 1)).xyz, (ObjectToWorld() * float4(v2, 1)).xyz, float4(.2f, .2f, 1, .1f));
				Gizmos::DrawWireCube((ObjectToWorld() * float4(box.Center(), 1)).xyz, box.Extents() * WorldScale(), WorldRotation(), float4(1, .2f, .2f, .1f));
			}
		} else {
			uint32_t n0 = ni + 1;
			uint32_t n1 = ni + node.mRightOffset;
			todo[++stackptr] = n0;
			todo[++stackptr] = n1;
		}
	}
}