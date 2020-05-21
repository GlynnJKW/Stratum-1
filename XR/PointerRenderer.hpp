#pragma once

#include <Content/Material.hpp>
#include <Content/Mesh.hpp>
#include <Core/DescriptorSet.hpp>
#include <Scene/Renderer.hpp>
#include <Util/Util.hpp>

class PointerRenderer : public Renderer {
public:
	bool mVisible;

	ENGINE_EXPORT PointerRenderer(const std::string& name);
	ENGINE_EXPORT ~PointerRenderer();

	inline virtual PassType PassMask() override { return PASS_MAIN; }

	inline virtual bool Visible() override { return mVisible && mRayDistance != 0 && EnabledHierarchy(); }
	inline virtual uint32_t RenderQueue() override { return 5000; }
	ENGINE_EXPORT virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;

	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }

	inline void RayDistance(float d) { mRayDistance = d; }
	inline void Color(const float4& c) { mColor = c; }
	inline void Width(float w) { mWidth = w; }

	inline float RayDistance() const { return mRayDistance; }
	inline float4 Color() const { return mColor; }
	inline float Width() const { return mWidth; }

protected:
	float mRayDistance;
	float mWidth;
	float4 mColor;

	AABB mAABB;
	ENGINE_EXPORT virtual bool UpdateTransform() override;
};