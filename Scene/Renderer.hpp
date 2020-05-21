#pragma once 

#include <Scene/Object.hpp>
#include <Scene/Scene.hpp>
#include <Util/Util.hpp>

class Renderer : public virtual Object {
public:
	// Renderers are drawn by the scene in order of increasing RenderQueue
	inline virtual uint32_t RenderQueue() { return 1000; }
	// Since passes correspond to LayerMasks as well, renderers are only drawn for passes that match PassMask()
	virtual PassType PassMask() { return PASS_MAIN; };
	inline virtual bool Visible() { return EnabledHierarchy(); };

	inline virtual void PreFrame(CommandBuffer* commandBuffer) {};
	// Called before a RenderPass that will draw this renderer begins
	inline virtual void PreRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {};
	virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) = 0;

	// Since passes correspond to LayerMasks as well, renderers are only drawn for passes that match PassMask()
	inline virtual uint32_t LayerMask() override { return Visible() ? Object::LayerMask() | PassMask() : 0; };
};