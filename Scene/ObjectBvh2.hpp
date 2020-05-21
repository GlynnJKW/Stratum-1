#pragma once

#include <Scene/Object.hpp>

#ifdef GetObject
#undef GetObject
#endif

// Stores a binary bvh of Objects, based off each object's Object::Bounds() 
class ObjectBvh2 {
public:
	struct Node {
		AABB mBounds;
		// index of the first primitive inside this node
		uint32_t mStartIndex;
		// number of primitives inside this node
		uint32_t mRightOffset; // 1st child is at node[index + 1], 2nd child is at node[index + mRightOffset]
		uint32_t mCount;
	};

	inline ObjectBvh2() {};
	inline ~ObjectBvh2() {}

	const std::vector<Node>& Nodes() const { return mNodes; }
	Object* GetObject(uint32_t index) const { return mPrimitives[index].mObject; }

	inline AABB RendererBounds() { return mRendererBounds; }

	ENGINE_EXPORT void Build(Object** objects, uint32_t objectCount);
	ENGINE_EXPORT void FrustumCheck(const float4 frustum[6], std::vector<Object*>& objects, uint32_t mask);
	ENGINE_EXPORT Object* Intersect(const Ray& ray, float* t, bool any, uint32_t mask);

	ENGINE_EXPORT void DrawGizmos(CommandBuffer* commandBuffer, Camera* camera, Scene* scene);

private:
	struct Primitive {
		AABB mBounds;
		Object* mObject;
	};
	AABB mRendererBounds;
	std::vector<Node> mNodes;
	std::vector<Primitive> mPrimitives;
};