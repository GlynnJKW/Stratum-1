#pragma once
#include <Content/Mesh.hpp>
#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/RenderPass.hpp>
#include <Core/Window.hpp>
#include <Scene/Object.hpp>
#include <Util/Util.hpp>

enum StereoEye {
	EYE_NONE = 0,
	EYE_LEFT = 0,
	EYE_RIGHT = 1
};

enum StereoMode {
	STEREO_NONE = 0,
	STEREO_SBS_VERTICAL = 1,
	STEREO_SBS_HORIZONTAL = 2
};

// A scene object that renders the scene
// Stores an internal ResolveBuffer for render buffer if the framebuffer's sample count is not VK_SAMPLE_COUNT_1_BIT
class Camera : public virtual Object {
public:
	ENGINE_EXPORT Camera(const std::string& name, Window* targetWindow, VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT);
	ENGINE_EXPORT Camera(const std::string& name, ::Device* device, VkFormat renderFormat = VK_FORMAT_R8G8B8A8_UNORM, VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT);
	ENGINE_EXPORT Camera(const std::string& name, ::Framebuffer* framebuffer);
	ENGINE_EXPORT virtual ~Camera();

	inline ::Device* Device() const { return mDevice; }

	// If the target window is not nullptr, sets the internal framebuffer and viewport size to match the target window
	ENGINE_EXPORT virtual void PreRender();
	// If the sample count is VK_SAMPLE_COUNT_1, transitions the framebuffer back to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	ENGINE_EXPORT virtual void PostRender(CommandBuffer* commandBuffer);

	// Updates the uniform buffer
	ENGINE_EXPORT virtual void SetUniforms();
	// Updates the uniform buffer and sets the non-stereo viewport
	ENGINE_EXPORT virtual void Set(CommandBuffer* commandBuffer);
	// Sets the viewport and StereoEye push constant
	ENGINE_EXPORT virtual void SetStereoViewport(CommandBuffer* commandBuffer, ShaderVariant* shader, StereoEye eye);

	ENGINE_EXPORT virtual float4 WorldToClip(const float3& worldPos, StereoEye eye = EYE_NONE);
	ENGINE_EXPORT virtual float3 ClipToWorld(const float3& clipPos, StereoEye eye = EYE_NONE);
	ENGINE_EXPORT virtual Ray ScreenToWorldRay(const float2& uv, StereoEye eye = EYE_NONE);
	
	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }

	inline Window* TargetWindow() const { return mTargetWindow; }

	// If the framebuffer's sample count is not VK_SAMPLE_COUNT_1, resolves the framebuffer to the internal ResolveBuffer and transitions the internal ResolveBuffer to VK_IMAGE_LAYOUT_GENERAL
	// If the framebuffer's sample count is VK_SAMPLE_COUNT_1, transitions the framebuffer to VK_IMAGE_LAYOUT_GENERAL
	ENGINE_EXPORT virtual void Resolve(CommandBuffer* commandBuffer);

	ENGINE_EXPORT virtual void DrawGizmos(CommandBuffer* commandBuffer, Camera* camera) override;


	// Setters

	inline virtual void StereoMode(::StereoMode s) { mStereoMode = s; Dirty(); }

	inline virtual void Orthographic(bool o) { mOrthographic = o; Dirty(); }
	inline virtual void OrthographicSize(float s) { mOrthographicSize = s; Dirty(); }

	inline virtual void Near(float n) { mNear = n; Dirty(); }
	inline virtual void Far(float f) { mFar = f;  Dirty(); }
	inline virtual void FieldOfView(float f) { mFieldOfView = f; Dirty(); }

	inline virtual void ViewportX(float x) { mViewport.x = x; }
	inline virtual void ViewportY(float y) { mViewport.y = y; }
	inline virtual void ViewportWidth(float f) { mViewport.width = f; Dirty(); }
	inline virtual void ViewportHeight(float f) { mViewport.height = f; Dirty(); }

	inline virtual void FramebufferWidth(uint32_t w) { mFramebuffer->Width(w);  Dirty(); }
	inline virtual void FramebufferHeight(uint32_t h) { mFramebuffer->Height(h);  Dirty(); }
	inline virtual void SampleCount(VkSampleCountFlagBits s) { mFramebuffer->SampleCount(s); }

	inline virtual void EyeOffset(const float3& translate, const quaternion& rotate, StereoEye eye = EYE_NONE) { mEyeOffsetTranslate[eye] = translate; mEyeOffsetRotate[eye] = rotate;  Dirty(); }
	inline virtual void Projection(const float4x4& projection, StereoEye eye = EYE_NONE) { mFieldOfView = 0; mOrthographic = false; mProjection[eye] = projection; Dirty(); }

	// Getters

	inline virtual ::StereoMode StereoMode() { return mStereoMode; }

	inline virtual float Near() const { return mNear; }
	inline virtual float Far() const { return mFar; }

	inline virtual bool Orthographic() const { return mOrthographic; }
	inline virtual float OrthographicSize() const { return mOrthographicSize; }
	inline virtual float FieldOfView() const { return mFieldOfView; }

	inline virtual float ViewportX() const { return mViewport.x; }
	inline virtual float ViewportY() const { return mViewport.y; }
	inline virtual float ViewportWidth() const { return mViewport.width; }
	inline virtual float ViewportHeight() const { return mViewport.height; }
	inline virtual float Aspect() const { return mViewport.width / mViewport.height; }

	inline virtual uint32_t FramebufferWidth()  const { return mFramebuffer->Width();  }
	inline virtual uint32_t FramebufferHeight() const { return mFramebuffer->Height(); }
	inline virtual VkSampleCountFlagBits SampleCount() const { return mFramebuffer->SampleCount(); }

	inline virtual float3 EyeOffsetTranslate(StereoEye eye = EYE_NONE) const { return mEyeOffsetTranslate[eye]; }
	inline virtual quaternion EyeOffsetRotate(StereoEye eye = EYE_NONE) const { return mEyeOffsetRotate[eye]; }

	inline virtual ::Framebuffer* Framebuffer() const { return mFramebuffer; }
	inline virtual Texture* ColorBuffer(uint32_t index = 0) const { return mFramebuffer->ColorBuffer(index); }
	inline virtual Texture* ResolveBuffer(uint32_t index = 0) const { return mFramebuffer->SampleCount() == VK_SAMPLE_COUNT_1_BIT ? mFramebuffer->ColorBuffer(index) : mResolveBuffers[mDevice->FrameContextIndex()][index]; }

	inline virtual Buffer* UniformBuffer() const { return mUniformBuffer; }
	ENGINE_EXPORT virtual ::DescriptorSet* DescriptorSet(VkShaderStageFlags stage);

	// Note: The view matrix is calculated placing the camera at the origin. To transform from world->view, one must apply:
	// view * (worldPos-cameraPos)
	inline virtual float4x4 View(StereoEye eye = EYE_NONE) { UpdateTransform(); return mView[eye]; }
	inline virtual float4x4 InverseView(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvView[eye]; }
	inline virtual float4x4 Projection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mProjection[eye]; }
	inline virtual float4x4 InverseProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvProjection[eye]; }
	inline virtual float4x4 ViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mViewProjection[eye]; }
	inline virtual float4x4 InverseViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvViewProjection[eye]; }

	inline virtual const float4* Frustum() { UpdateTransform(); return mFrustum; }

private:
	uint32_t mRenderPriority;

	::StereoMode mStereoMode;

	bool mOrthographic;
	float mOrthographicSize;
	float mFieldOfView; 

	float mNear;
	float mFar;

	float4x4 mView[2];
	float4x4 mProjection[2];
	float4x4 mViewProjection[2];
	float4x4 mInvProjection[2];
	float4x4 mInvView[2];
	float4x4 mInvViewProjection[2];

	float3 mEyeOffsetTranslate[2];
	quaternion mEyeOffsetRotate[2];

	float4 mFrustum[6];

	VkViewport mViewport;

	Window* mTargetWindow;
	::Device* mDevice;
	::Framebuffer* mFramebuffer;
	// If the framebuffer was not supplied to the camera on creation, then delete it
	bool mDeleteFramebuffer;
	std::vector<Texture*>* mResolveBuffers;

	void** mUniformBufferPtrs;
	Buffer* mUniformBuffer;
	std::vector<std::unordered_map<VkShaderStageFlags, ::DescriptorSet*>> mDescriptorSets;

	void CreateDescriptorSet();

protected:
	ENGINE_EXPORT virtual bool UpdateTransform() override;
};