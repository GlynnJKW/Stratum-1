#include "OpenVR.hpp"
#include <Util/Tokenizer.hpp>
#include <Util/Profiler.hpp>

using namespace std;
using namespace vr;

void ThrowIfFailed(EVRInputError err, const string& msg) {
    if (err != VRInputError_None) {
        fprintf_color(COLOR_YELLOW, stderr, "%s: %d\n", msg.c_str());
        throw;
    }
}

void ConvertMatrix(const HmdMatrix34_t& mat, float3& pos, quaternion& rot) {
    float4x4(
        mat.m[0][0], mat.m[0][1], -mat.m[0][2], mat.m[0][3],
        mat.m[1][0], mat.m[1][1], -mat.m[1][2], mat.m[1][3],
        -mat.m[2][0], -mat.m[2][1], mat.m[2][2], -mat.m[2][3],
        0, 0, 0, 1).Decompose(&pos, &rot, nullptr);
    rot = normalize(rot);
}

VertexInput RenderModelVertexInput = {
    {
        {
            0, // binding
            sizeof(RenderModel_Vertex_t), // stride
            VK_VERTEX_INPUT_RATE_VERTEX // input rate
        }
    },
    {
        {
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT, // format
            offsetof(RenderModel_Vertex_t, vPosition) // offset
        },
        {
            1, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT, // format
            offsetof(RenderModel_Vertex_t, vNormal) // offset
        },
        {
            3, // location
            0, // binding
            VK_FORMAT_R32G32_SFLOAT, // format
            offsetof(RenderModel_Vertex_t, rfTextureCoord) // offset
        }
    }
};

OpenVR::OpenVR() : mSystem(nullptr), mRenderModelInterface(nullptr), mCopyTarget(nullptr), mScrollTouchCount(0), mPoseHistoryFrameCount(8) {
    memset(mTrackedObjects, 0, sizeof(Object*) * k_unMaxTrackedDeviceCount);
}
OpenVR::~OpenVR() {
    safe_delete(mCopyTarget);

    for (auto& kp : mRenderModelObjects)
        mScene->RemoveObject(kp.second);
    for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++)
        if (mTrackedObjects[i])
            mScene->RemoveObject(mTrackedObjects[i]);

    for (auto kp : mRenderModels)
        safe_delete(kp.second.first);
    for (auto kp : mRenderModelMaterials)
        safe_delete(kp.second.first);

    if (mSystem) VR_Shutdown();
}

bool OpenVR::Init() {
    if (!VR_IsRuntimeInstalled()) {
        printf_color(COLOR_YELLOW, "%s", "OpenVR runtime is not installed\n");
        return false;
    }

    HmdError herr;
    mSystem = VR_Init(&herr, VRApplication_Scene);
    if (herr != VRInitError_None) {
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to initialize OpenVR: %s\n", VR_GetVRInitErrorAsEnglishDescription(herr));
        return false;
    }

    mCompositor = VRCompositor();
    if (!mCompositor) {
        VR_Shutdown();
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to initialize OpenVR compositor\n");
        return false;
    }

    mCompositor->SetExplicitTimingMode(VRCompositorTimingMode_Explicit_ApplicationPerformsPostPresentHandoff);

    mVRInput = VRInput();
    if (!mVRInput) {
        VR_Shutdown();
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to initialize OpenVR input system\n");
        return false;
    }

    mRenderModelInterface = (IVRRenderModels*)VR_GetGenericInterface(IVRRenderModels_Version, &herr);
    if (!mRenderModelInterface) {
        VR_Shutdown();
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to get OpenVR render model interface: %s\n", VR_GetVRInitErrorAsEnglishDescription(herr));
        return false;
    }

    ThrowIfFailed(mVRInput->SetActionManifestPath(fs::absolute("Config/actions.json").string().c_str()), "SetActionManifestPath failed for " + fs::absolute("Config/actions.json").string());
    ThrowIfFailed(mVRInput->GetActionSetHandle("/actions/default",              &mActionSetDefault), "GetActionSetHandle failed for /actions/default");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/aimleft",      &mActionAimLeft), "GetActionHandle failed for /actions/default/in/AimLeft");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/aimright",     &mActionAimRight), "GetActionHandle failed for /actions/default/in/AimRight");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/Menu",         &mActionMenu), "GetActionHandle failed for /actions/default/in/Menu");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/Trigger",      &mActionTrigger), "GetActionHandle failed for /actions/default/in/Trigger");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/TriggerValue", &mActionTriggerValue), "GetActionHandle failed for /actions/default/in/TriggerValue");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/Grip",         &mActionGrip), "GetActionHandle failed for /actions/default/in/Grip");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/ScrollPos",    &mActionScrollPos), "GetActionHandle failed for /actions/default/in/ScrollPos");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/in/ScrollTouch",  &mActionScrollTouch), "GetActionHandle failed for /actions/default/in/ScrollTouch");
    ThrowIfFailed(mVRInput->GetActionHandle("/actions/default/out/Haptics",     &mActionHaptics), "GetActionHandle failed for /actions/default/out/Haptics");

    mVRInput->GetActionHandle("/user/hand/left", &mLeftHand);
    mVRInput->GetActionHandle("/user/hand/right", &mRightHand);

    mInputPointers.resize(2);
    strcpy(mInputPointers[0].mName, "OpenVR Left Hand");
    strcpy(mInputPointers[1].mName, "OpenVR Right Hand");
    mInputPointersLast.resize(2);
    strcpy(mInputPointersLast[0].mName, "OpenVR Left Hand");
    strcpy(mInputPointersLast[1].mName, "OpenVR Right Hand");

    return true;
}

void OpenVR::PreInstanceInit(Instance* instance) {
    char extensions[1024];
    VRCompositor()->GetVulkanInstanceExtensionsRequired(extensions, 1024);

    Tokenizer t(extensions, { ' ' });
    string e;
    while (t.Next(e)) instance->RequestInstanceExtension(e);
}
void OpenVR::PreDeviceInit(Instance* instance, VkPhysicalDevice device) {
    char extensions[1024];
    VRCompositor()->GetVulkanDeviceExtensionsRequired(device, extensions, 1024);

    Tokenizer t(extensions, { ' ' });
    string e;
    while (t.Next(e)) instance->RequestDeviceExtension(e);
}

bool OpenVR::InitScene(Scene* scene) {
    mScene = scene;
    scene->InputManager()->RegisterInputDevice(this);

    shared_ptr<PointerRenderer> leftPointer = make_shared<PointerRenderer>("Left Pointer");
    mScene->AddObject(leftPointer);
    mLeftPointer = leftPointer.get();
    mLeftPointer->Width(.005f);
    mLeftPointer->Color(float4(.5f, .8f, 1.f, .5f));
    mLeftPointer->mVisible = false;
    mLeftPointer->RayDistance(1.f);

    shared_ptr<PointerRenderer> rightPointer = make_shared<PointerRenderer>("Right Pointer");
    mScene->AddObject(rightPointer);
    mRightPointer = rightPointer.get();
    mRightPointer->Width(.005f);
    mRightPointer->Color(float4(.5f, .8f, 1.f, .5f));
    mRightPointer->mVisible = false;
    mRightPointer->RayDistance(1.f);


    auto mat = make_shared<Material>("Untextured", mScene->AssetManager()->LoadShader("Shaders/pbr.stm"));
    mat->SetParameter("Color", float4(1));
    mat->SetParameter("Metallic", 0.f);
    mat->SetParameter("Roughness", .85f);
    mat->SetParameter("Emission", float3(0));
    mRenderModelMaterials.emplace((TextureID_t)-1, make_pair((Texture*)nullptr, mat));

    uint32_t w, h;
    mSystem->GetRecommendedRenderTargetSize(&w, &h);

    auto camera = make_shared<Camera>("OpenVR HMD", mScene->Instance()->Device(), VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_4_BIT);
    mScene->AddObject(camera);
    mHmdCamera = camera.get();
    mHmdCamera->FramebufferWidth(w * 2);
    mHmdCamera->FramebufferHeight(h);
    mHmdCamera->ViewportX(0);
    mHmdCamera->ViewportY(0);
    mHmdCamera->ViewportWidth(w * 2);
    mHmdCamera->ViewportHeight(h);
    mHmdCamera->StereoMode(STEREO_SBS_HORIZONTAL);
    mHmdCamera->Near(.01f);
    mHmdCamera->Far(1024.f);

    float n = mHmdCamera->Near();
    float3 pos;
    quaternion rot;
    float l, r, t, b;

    mSystem->GetProjectionRaw(Eye_Right, &l, &r, &t, &b);
    ConvertMatrix(mSystem->GetEyeToHeadTransform(Eye_Left), pos, rot);
    mHmdCamera->Projection(float4x4::Perspective(l * n, r * n, t * n, b * n, mHmdCamera->Near(), mHmdCamera->Far()), EYE_LEFT);
    mHmdCamera->EyeOffset(pos, rot, EYE_LEFT);

    mSystem->GetProjectionRaw(Eye_Left, &l, &r, &t, &b);
    mHmdCamera->Projection(float4x4::Perspective(l * n, r * n, t * n, b * n, mHmdCamera->Near(), mHmdCamera->Far()), EYE_RIGHT);
    ConvertMatrix(mSystem->GetEyeToHeadTransform(Eye_Right), pos, rot);
    mHmdCamera->EyeOffset(pos, rot, EYE_RIGHT);

    return true;
}

void OpenVR::BeginFrame() {
    Profiler::BeginSample("OpenVR Poll Event");
    VREvent_t event;
    while (mSystem->PollNextEvent(&event, sizeof(event))) {
        switch (event.eventType) {
        case VREvent_IpdChanged: {
            float n = mHmdCamera->Near();
            float3 pos;
            quaternion rot;
            float l, r, t, b;

            mSystem->GetProjectionRaw(Eye_Left, &l, &r, &t, &b);
            ConvertMatrix(mSystem->GetEyeToHeadTransform(Eye_Left), pos, rot);
            mHmdCamera->Projection(float4x4::Perspective(l * n, r * n, t * n, b * n, mHmdCamera->Near(), mHmdCamera->Far()), EYE_LEFT);
            mHmdCamera->EyeOffset(pos, rot, EYE_LEFT);

            mSystem->GetProjectionRaw(Eye_Right, &l, &r, &t, &b);
            mHmdCamera->Projection(float4x4::Perspective(l * n, r * n, t * n, b * n, mHmdCamera->Near(), mHmdCamera->Far()), EYE_RIGHT);
            ConvertMatrix(mSystem->GetEyeToHeadTransform(Eye_Right), pos, rot);
            mHmdCamera->EyeOffset(pos, rot, EYE_RIGHT);
            break;
        }
        case VREvent_TrackedDeviceActivated:
        case VREvent_TrackedDeviceDeactivated:
            mVRInput->GetActionHandle("/user/hand/left", &mLeftHand);
            mVRInput->GetActionHandle("/user/hand/right", &mRightHand);
            break;
        }
    }
    Profiler::EndSample();

    EVRInputError err;

    VRActiveActionSet_t actionSet = {};
    actionSet.ulActionSet = mActionSetDefault;
    if ((err = mVRInput->UpdateActionState(&actionSet, sizeof(VRActiveActionSet_t), 1)) != VRInputError_None)
        fprintf_color(COLOR_YELLOW, stderr, "UpdateActionState failed: %d\n", err);

    bool controllersVisible = !mSystem->IsSteamVRDrawingControllers();

    InputAnalogActionData_t adata;
    InputDigitalActionData_t ddata;
    InputPoseActionData_t pdata;

    err = mVRInput->GetPoseActionDataForNextFrame(mActionAimLeft, TrackingUniverseStanding, &pdata, sizeof(InputPoseActionData_t), mLeftHand);
    if (err == VRInputError_None && pdata.bActive && pdata.pose.bPoseIsValid) {
        mLeftPointer->RayDistance(fmaxf(0, mInputPointersLast[0].mGuiHitT));
        mLeftPointer->mVisible = true;

        float3 pos;
        quaternion rot;
        ConvertMatrix(pdata.pose.mDeviceToAbsoluteTracking, pos, rot);
        mPoseHistoryLeftPointer.push_back(make_pair(pos, rot));
        if (mPoseHistoryLeftPointer.size() > mPoseHistoryFrameCount) mPoseHistoryLeftPointer.erase(mPoseHistoryLeftPointer.begin());

        // Average pose over history for smoothing
        pos = 0;
        rot.xyzw = 0;
        for (auto it : mPoseHistoryLeftPointer) {
            pos += it.first;
            rot.xyzw += it.second.xyzw;
        }
        pos /= mPoseHistoryLeftPointer.size();
        rot = normalize(rot / mPoseHistoryLeftPointer.size());

        mLeftPointer->LocalPosition(pos);
        mLeftPointer->LocalRotation(rot);
        mInputPointers[0].mWorldRay.mOrigin = pos;
        mInputPointers[0].mWorldRay.mDirection = rot * float3(0, 0, 1);
    } else
        mLeftPointer->mVisible = false;

    err = mVRInput->GetPoseActionDataForNextFrame(mActionAimRight, TrackingUniverseStanding, &pdata, sizeof(InputPoseActionData_t), mRightHand);
    if (err == VRInputError_None && pdata.bActive && pdata.pose.bPoseIsValid) {
        mRightPointer->RayDistance(fmaxf(0, mInputPointersLast[1].mGuiHitT));
        mRightPointer->mVisible = true;

        float3 pos;
        quaternion rot;
        ConvertMatrix(pdata.pose.mDeviceToAbsoluteTracking, pos, rot);
        mPoseHistoryRightPointer.push_back(make_pair(pos, rot));
        if (mPoseHistoryRightPointer.size() > mPoseHistoryFrameCount) mPoseHistoryRightPointer.erase(mPoseHistoryRightPointer.begin());

        // Average pose over history for smoothing
        pos = 0;
        rot.xyzw = 0;
        for (auto it : mPoseHistoryRightPointer) {
            pos += it.first;
            rot.xyzw += it.second.xyzw;
        }
        pos /= mPoseHistoryRightPointer.size();
        rot = normalize(rot / mPoseHistoryRightPointer.size());

        mRightPointer->LocalPosition(pos);
        mRightPointer->LocalRotation(rot);
        mInputPointers[1].mWorldRay.mOrigin = pos;
        mInputPointers[1].mWorldRay.mDirection = rot * float3(0, 0, 1);
    } else
        mRightPointer->mVisible = false;

    mVRInput->GetAnalogActionData(mActionTriggerValue, &adata, sizeof(InputAnalogActionData_t), mLeftHand);
    mInputPointers[0].mPrimaryAxis = adata.x;
    mInputPointers[0].mSecondaryAxis = adata.y;
    mVRInput->GetAnalogActionData(mActionTriggerValue, &adata, sizeof(InputAnalogActionData_t), mRightHand);
    mInputPointers[1].mPrimaryAxis = adata.x;
    mInputPointers[1].mSecondaryAxis = adata.y;

    mVRInput->GetDigitalActionData(mActionTrigger, &ddata, sizeof(InputDigitalActionData_t), mLeftHand);
    mInputPointers[0].mPrimaryButton = ddata.bState;
    mVRInput->GetDigitalActionData(mActionTrigger, &ddata, sizeof(InputDigitalActionData_t), mRightHand);
    mInputPointers[1].mPrimaryButton = ddata.bState;

    mVRInput->GetDigitalActionData(mActionMenu, &ddata, sizeof(InputDigitalActionData_t), mLeftHand);
    mInputPointers[0].mSecondaryButton = ddata.bState;
    mVRInput->GetDigitalActionData(mActionMenu, &ddata, sizeof(InputDigitalActionData_t), mRightHand);
    mInputPointers[1].mSecondaryButton = ddata.bState;

    mVRInput->GetDigitalActionData(mActionScrollTouch, &ddata, sizeof(InputDigitalActionData_t), mLeftHand);
    if (ddata.bState) {
        mScrollTouchCount[0]++;
        if (mScrollTouchCount[0] > 6) {
            mVRInput->GetAnalogActionData(mActionScrollPos, &adata, sizeof(InputAnalogActionData_t), mLeftHand);
            mInputPointers[0].mScrollDelta += float2(adata.deltaX, adata.deltaY);
        }
    } else
        mScrollTouchCount[0] = 0;

    mVRInput->GetDigitalActionData(mActionScrollTouch, &ddata, sizeof(InputDigitalActionData_t), mRightHand);
    if (ddata.bState) {
        mScrollTouchCount[1]++;
        if (mScrollTouchCount[1] > 6) {
            mVRInput->GetAnalogActionData(mActionScrollPos, &adata, sizeof(InputAnalogActionData_t), mRightHand);
            mInputPointers[1].mScrollDelta += float2(adata.deltaX, adata.deltaY);
        }
    } else
        mScrollTouchCount[1] = 0;

    VRInputValueHandle_t hands[2]{ mLeftHand, mRightHand };
    for (uint32_t i = 0; i < 2; i++) {
        InputOriginInfo_t info;
        if (mVRInput->GetOriginTrackedDeviceInfo(hands[i], &info, sizeof(InputOriginInfo_t)) != VRInputError_None) continue;
        if (!mTrackedObjects[info.trackedDeviceIndex]) continue;
        
        uint32_t len = mSystem->GetStringTrackedDeviceProperty(info.trackedDeviceIndex, Prop_RenderModelName_String, nullptr, 0);
        char* deviceRenderModelName = new char[len];
        mSystem->GetStringTrackedDeviceProperty(info.trackedDeviceIndex, Prop_RenderModelName_String, deviceRenderModelName, len);

        uint32_t componentCount = mRenderModelInterface->GetComponentCount(deviceRenderModelName);

        for (uint32_t j = 0; j < componentCount; j++) {
            len = mRenderModelInterface->GetComponentName(deviceRenderModelName, j, nullptr, 0);
            char* componentName = new char[len];
            mRenderModelInterface->GetComponentName(deviceRenderModelName, j, componentName, len);

            len = mRenderModelInterface->GetComponentRenderModelName(deviceRenderModelName, componentName, nullptr, 0);
            char* renderModelName = new char[len];
            len = mRenderModelInterface->GetComponentRenderModelName(deviceRenderModelName, componentName, renderModelName, len);

            // Retreive or create renderer
            string mrname = to_string(i) + "/" + componentName + to_string(j);
            MeshRenderer* mr;
            if (mRenderModelObjects.count(mrname))
                mr = mRenderModelObjects.at(mrname);
            else {
                shared_ptr<MeshRenderer> renderer = make_shared<MeshRenderer>(renderModelName);
                mScene->AddObject(renderer);
                mRenderModelObjects.emplace(mrname, renderer.get());
                mTrackedObjects[info.trackedDeviceIndex]->AddChild(renderer.get());
                mr = renderer.get();
            }

            // Load render model
            if (!mr->Mesh() || !mr->Material()) {
                if (mRenderModels.count(string(renderModelName))) {
                    // Mesh has been loaded, assign it
                    auto kp = mRenderModels.at(string(renderModelName));
                    mr->Mesh(kp.first);

                    // load render model materialw data asynchronously
                    if (mRenderModelMaterials.count(kp.second))
                        // Texture/material has been loaded, assign it
                        mr->Material(mRenderModelMaterials.at(kp.second).second);
                    else {
                        RenderModel_TextureMap_t* renderModelDiffuse;
                        if (mRenderModelInterface->LoadTexture_Async(kp.second, &renderModelDiffuse) == VRRenderModelError_None) {
                            Texture* diffuse = new Texture(to_string(kp.second), mScene->Instance()->Device(),
                                renderModelDiffuse->rubTextureMapData, renderModelDiffuse->unWidth * renderModelDiffuse->unHeight * 4,
                                renderModelDiffuse->unWidth, renderModelDiffuse->unHeight, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
                            shared_ptr<Material> mat = make_shared<Material>(renderModelName, mScene->AssetManager()->LoadShader("Shaders/pbr.stm"));
                            mat->EnableKeyword("TEXTURED_COLORONLY");
                            mat->SetParameter("MainTextures", 0, diffuse);
                            mat->SetParameter("Color", float4(1));
                            mat->SetParameter("Metallic", 0.f);
                            mat->SetParameter("Roughness", .85f);
                            mat->SetParameter("Emission", float3(0));
                            mat->SetParameter("TextureIndex", 0u);
                            mat->SetParameter("TextureST", float4(1, 1, 0, 0));
                            mRenderModelMaterials.emplace(kp.second, make_pair(diffuse, mat));
                            mr->Material(mat);
                        }
                    }
                } else {
                    RenderModel_t* renderModel;
                    if (mRenderModelInterface->LoadRenderModel_Async(renderModelName, &renderModel) == VRRenderModelError_None) {
                        RenderModel_Vertex_t* verts = new RenderModel_Vertex_t[renderModel->unVertexCount];
                        memcpy(verts, renderModel->rVertexData, renderModel->unVertexCount * sizeof(RenderModel_Vertex_t));
                        for (uint32_t k = 0; k < renderModel->unVertexCount; k++) {
                            verts[k].vPosition.v[2] = -verts[k].vPosition.v[2];
                            verts[k].vNormal.v[2] = -verts[k].vNormal.v[2];
                        }

                        Mesh* mesh = new Mesh(renderModelName, mScene->Instance()->Device(),
                            verts, renderModel->rIndexData, renderModel->unVertexCount, sizeof(RenderModel_Vertex_t), renderModel->unTriangleCount * 3,
                            &RenderModelVertexInput, VK_INDEX_TYPE_UINT16);
                        mRenderModels.emplace(renderModelName, make_pair(mesh, renderModel->diffuseTextureId));
                        mRenderModelInterface->FreeRenderModel(renderModel);
                        mr->Mesh(mesh);

                        delete[] verts;
                    }
                }
            }

            RenderModel_ControllerMode_State_t state = {};
            state.bScrollWheelVisible = true;
            RenderModel_ComponentState_t cstate;
            mr->mVisible = mRenderModelInterface->GetComponentStateForDevicePath(deviceRenderModelName, componentName, hands[i], &state, &cstate) && controllersVisible;

            float3 position;
            quaternion rotation;
            ConvertMatrix(cstate.mTrackingToComponentRenderModel, position, rotation);
            mr->LocalPosition(position);
            mr->LocalRotation(rotation);

            delete[] componentName;
            delete[] renderModelName;
        }

        delete[] deviceRenderModelName;
    }
}
void OpenVR::PostRender(CommandBuffer* commandBuffer) {
    Profiler::BeginSample("OpenVR Copy");
    Texture* src = mHmdCamera->ResolveBuffer();
    src->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, commandBuffer);

    if (!mCopyTarget){
        mCopyTarget = new Texture("HMD Copy Target", mScene->Instance()->Device(),
            mHmdCamera->FramebufferWidth(), mHmdCamera->FramebufferHeight(), 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        mCopyTarget->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);
    } else
        mCopyTarget->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);

    VkImageCopy rgn = {};
    rgn.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rgn.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rgn.srcSubresource.layerCount = 1;
    rgn.dstSubresource.layerCount = 1;
    rgn.extent = { mCopyTarget->Width(), mCopyTarget->Height(), 1 };
    vkCmdCopyImage(*commandBuffer, 
        src->Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        mCopyTarget->Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &rgn);

    src->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
    mCopyTarget->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, commandBuffer);
    Profiler::EndSample();

    // last-minute pose update
    TrackedDevicePose_t renderPoses[k_unMaxTrackedDeviceCount];
    TrackedDevicePose_t gamePoses[k_unMaxTrackedDeviceCount];
    VRCompositor()->WaitGetPoses(renderPoses, k_unMaxTrackedDeviceCount, gamePoses, k_unMaxTrackedDeviceCount);

    for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++) {
        ETrackedDeviceClass deviceClass = mSystem->GetTrackedDeviceClass(i);
        if (deviceClass == TrackedDeviceClass_HMD)
            mTrackedObjects[i] = mHmdCamera;
        else {
            // only enable the scene object if its pose is valid
            if (mTrackedObjects[i])
                mTrackedObjects[i]->mEnabled = renderPoses[i].bPoseIsValid;
            else if (renderPoses[i].bPoseIsValid) {
                // Create an object for tracked devices with a valid pose
                auto o = make_shared<Object>("TrackedDevice" + to_string(i));
                mScene->AddObject(o);
                mTrackedObjects[i] = o.get();
            }
        }

        // Update tracking from render pose
        if (mTrackedObjects[i] && renderPoses[i].bPoseIsValid) {
            float3 pos;
            quaternion rot;
            ConvertMatrix(renderPoses[i].mDeviceToAbsoluteTracking, pos, rot);
            mTrackedObjects[i]->LocalPosition(pos);
            mTrackedObjects[i]->LocalRotation(rot);
        }
    }

    VRCompositor()->SubmitExplicitTimingData();
}
void OpenVR::EndFrame() {
    Profiler::BeginSample("OpenVR Submit");
    VRTextureBounds_t leftBounds = {};
    leftBounds.uMax = .5f;
    leftBounds.vMax = 1.f;
    VRTextureBounds_t rightBounds = {};
    rightBounds.uMin = .5f;
    rightBounds.uMax = 1.f;
    rightBounds.vMax = 1.f;

    VRVulkanTextureData_t vkTexture = {};
    vkTexture.m_nImage = (uint64_t)mCopyTarget->Image();
    vkTexture.m_pDevice = *mScene->Instance()->Device();
    vkTexture.m_pPhysicalDevice = mScene->Instance()->Device()->PhysicalDevice();
    vkTexture.m_pInstance = *mScene->Instance();
    vkTexture.m_pQueue = mScene->Instance()->Device()->GraphicsQueue();
    vkTexture.m_nQueueFamilyIndex = mScene->Instance()->Device()->GraphicsQueueFamilyIndex();
    vkTexture.m_nWidth = mCopyTarget->Width();
    vkTexture.m_nHeight = mCopyTarget->Height();
    vkTexture.m_nFormat = mCopyTarget->Format();
    vkTexture.m_nSampleCount = 1;

    Texture_t texture = {};
    texture.eType = TextureType_Vulkan;
    texture.eColorSpace = ColorSpace_Auto;
    texture.handle = &vkTexture;

    Instance::sDisableDebugCallback = true;
    if (VRCompositor()->Submit(Eye_Left, &texture, &leftBounds) != VRCompositorError_None) fprintf_color(COLOR_YELLOW, stderr, "%s", "Failed to submit left eye\n");
    if (VRCompositor()->Submit(Eye_Right, &texture, &rightBounds) != VRCompositorError_None) fprintf_color(COLOR_YELLOW, stderr, "%s", "Failed to submit right eye\n");
    Instance::sDisableDebugCallback = false;
    Profiler::EndSample();

    mCompositor->PostPresentHandoff();
}

void OpenVR::NextFrame() {
    memcpy(mInputPointersLast.data(), mInputPointers.data(), sizeof(InputPointer) * mInputPointers.size());
    for (InputPointer& p : mInputPointers) {
        p.mGuiHitT = -1.0f;
        p.mScrollDelta = 0.f;
    }
 }