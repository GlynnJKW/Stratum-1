#include <XR/PointerRenderer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Environment.hpp>
#include <Util/Profiler.hpp>

#include <Shaders/include/shadercompat.h>

using namespace std;

PointerRenderer::PointerRenderer(const string& name)
	: Object(name), mVisible(true), mColor(1.f), mWidth(.01f), mRayDistance(1.f) {}
PointerRenderer::~PointerRenderer() {}

bool PointerRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB.mMin = min(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance));
	mAABB.mMax = max(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance));
	return true;
}

void PointerRenderer::Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	GraphicsShader* shader = Scene()->AssetManager()->LoadShader("Shaders/pointer.stm")->GetGraphics(pass, {});
	if (!shader) return;
	VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	if (!layout) return;

	float3 p0 = WorldPosition();
	float3 p1 = WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance);
	commandBuffer->PushConstant(shader, "P0", &p0);
	commandBuffer->PushConstant(shader, "P1", &p1);
	commandBuffer->PushConstant(shader, "Width", &mWidth);
	commandBuffer->PushConstant(shader, "Color", &mColor);

	camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
	vkCmdDraw(*commandBuffer, 6, 1, 0, 0);

	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
		vkCmdDraw(*commandBuffer, 6, 1, 0, 0);
	}
}