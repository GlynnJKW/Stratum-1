#pragma once

#include <openvr.h>

#include "XRRuntime.hpp"
#include "PointerRenderer.hpp"
#include <Scene/Scene.hpp>
#include <Scene/MeshRenderer.hpp>

class OpenVR : public XRRuntime, public InputDevice {
public:
    ENGINE_EXPORT OpenVR();
    ENGINE_EXPORT virtual ~OpenVR();

    // XRRuntime implementation

    ENGINE_EXPORT bool Init() override;
    ENGINE_EXPORT bool InitScene(Scene* scene) override;

    ENGINE_EXPORT void PreInstanceInit(Instance* instance);
    ENGINE_EXPORT void PreDeviceInit(Instance* instance, VkPhysicalDevice device);

    ENGINE_EXPORT void BeginFrame();
    ENGINE_EXPORT void PostRender(CommandBuffer* commandBuffer);
    ENGINE_EXPORT void EndFrame();

    // InputDevice implementation

    inline uint32_t PointerCount() const { return mInputPointers.size(); }
    inline const InputPointer* GetPointer(uint32_t index) const { return &mInputPointers[index]; }
    inline const InputPointer* GetPointerLast(uint32_t index) const { return &mInputPointersLast[index]; }
    ENGINE_EXPORT void NextFrame();

private:
    Scene* mScene;
    Camera* mHmdCamera;

    vr::IVRSystem* mSystem;
    vr::IVRCompositor* mCompositor;
    vr::IVRInput* mVRInput;
    vr::IVRRenderModels* mRenderModelInterface;

    PointerRenderer* mLeftPointer;
    PointerRenderer* mRightPointer;

    std::list<std::pair<float3, quaternion>> mPoseHistoryLeftPointer;
    std::list<std::pair<float3, quaternion>> mPoseHistoryRightPointer;
    uint32_t mPoseHistoryFrameCount;

    Texture* mCopyTarget;

    std::vector<InputPointer> mInputPointers;
    std::vector<InputPointer> mInputPointersLast;

    std::unordered_map<std::string, std::pair<Mesh*, vr::TextureID_t>> mRenderModels;
    std::unordered_map<vr::TextureID_t, std::pair<Texture*, std::shared_ptr<Material>>> mRenderModelMaterials;

    Object* mTrackedObjects[vr::k_unMaxTrackedDeviceCount];
    std::unordered_map<std::string, MeshRenderer*> mRenderModelObjects;

    vr::VRActionSetHandle_t mActionSetDefault;
    vr::VRInputValueHandle_t mLeftHand;
    vr::VRInputValueHandle_t mRightHand;
    vr::VRActionHandle_t mActionAimLeft;
    vr::VRActionHandle_t mActionAimRight;
    vr::VRActionHandle_t mActionTrigger;
    vr::VRActionHandle_t mActionTriggerValue;
    vr::VRActionHandle_t mActionMenu;
    vr::VRActionHandle_t mActionGrip;
    vr::VRActionHandle_t mActionScrollPos;
    vr::VRActionHandle_t mActionScrollTouch;
    vr::VRActionHandle_t mActionHaptics;

    uint2 mScrollTouchCount;
};