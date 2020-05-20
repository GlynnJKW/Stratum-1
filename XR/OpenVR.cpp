#include "OpenVR.hpp"
#include <Util/Tokenizer.hpp>
#include <Util/Profiler.hpp>

using namespace std;
using namespace vr;

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

OpenVR::OpenVR() : mSystem(nullptr), mRenderModelInterface(nullptr), mCopyTarget(nullptr) {
    memset(mTrackedObjects, 0, sizeof(Object*) * k_unMaxTrackedDeviceCount);
    memset(mTrackedDevices, 0, sizeof(TrackedDevicePose_t) * k_unMaxTrackedDeviceCount);
    memset(mTrackedDevicesPredicted, 0, sizeof(TrackedDevicePose_t) * k_unMaxTrackedDeviceCount);
}
OpenVR::~OpenVR() {
    safe_delete(mCopyTarget);

    for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++)
        if (mTrackedObjects[i]) mScene->RemoveObject(mTrackedObjects[i]);

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

    HmdError err;
    mSystem = VR_Init(&err, VRApplication_Scene);
    if (err != vr::VRInitError_None) {
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to initialize OpenVR: %s\n", VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }

    mRenderModelInterface = (IVRRenderModels*)VR_GetGenericInterface(IVRRenderModels_Version, &err);
    if (!mRenderModelInterface) {
        vr::VR_Shutdown();
        mSystem = nullptr;
        fprintf_color(COLOR_YELLOW, stderr, "Failed to get OpenVR render model interface: %s\n", VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }

    if (!vr::VRCompositor()) {
        vr::VR_Shutdown();
        mSystem = nullptr;
        fprintf_color(COLOR_RED, stderr, "Failed to initialize OpenVR compositor\n");
        return false;
    }

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

    auto mat = make_shared<Material>("Untextured", mScene->AssetManager()->LoadShader("Shaders/pbr.stm"));
    mat->SetParameter("Color", float4(1));
    mat->SetParameter("Metallic", 0.f);
    mat->SetParameter("Roughness", .85f);
    mat->SetParameter("Emission", float3(0));
    mRenderModelMaterials.emplace((TextureID_t)-1, make_pair((Texture*)nullptr, mat));

    uint32_t w, h;
    mSystem->GetRecommendedRenderTargetSize(&w, &h);

    auto camera = make_shared<Camera>("OpenVR HMD", mScene->Instance()->Device(), VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_4_BIT);
    camera->FramebufferWidth(w * 2);
    camera->FramebufferHeight(h);
    camera->ViewportX(0);
    camera->ViewportY(0);
    camera->ViewportWidth(w * 2);
    camera->ViewportHeight(h);
    camera->StereoMode(STEREO_SBS_HORIZONTAL);
    camera->Near(.01f);
    camera->Far(1024.f);
    mScene->AddObject(camera);
    mHmdCamera = camera.get();

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
        if (event.eventType == VREvent_IpdChanged) {
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
        }
    }
    Profiler::EndSample();

    bool controllersVisible = !mSystem->IsSteamVRDrawingControllers();

    uint32_t idx = 0;

    for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++) {
        ETrackedDeviceClass deviceClass = mSystem->GetTrackedDeviceClass(i);
        if (deviceClass == TrackedDeviceClass_HMD) mTrackedObjects[i] = mHmdCamera;

        // Update tracking from previous frame's predicted pose
        if (mTrackedObjects[i] && mTrackedDevicesPredicted[i].bPoseIsValid) {
            float3 pos;
            quaternion rot;
            ConvertMatrix(mTrackedDevicesPredicted[i].mDeviceToAbsoluteTracking, pos, rot);
            mTrackedObjects[i]->LocalPosition(pos);
            mTrackedObjects[i]->LocalRotation(rot);
        }

        // Update controller models and visibility
        
        if (deviceClass != TrackedDeviceClass_Controller) continue;

        // Create InputPointers
        if (mTrackedDevicesPredicted[i].bPoseIsValid) {
            if (idx >= mInputPointers.size()) mInputPointers.resize(idx + 1);
            snprintf(mInputPointers[idx].mName, 16, "OpenVR Device %d", i);
            mInputPointers[idx].mDevice = this;
            mInputPointers[idx].mWorldRay.mOrigin = mTrackedObjects[i]->WorldPosition();
            mInputPointers[idx].mWorldRay.mDirection = mTrackedObjects[i]->WorldRotation() * float3(0, 0, 1);
            mInputPointers[idx].mGuiHitT = -1;
            // TODO
            // mInputPointers[idx].mPrimaryButton;
            // mInputPointers[idx].mSecondaryButton;
            // mInputPointers[idx].mPrimaryAxis;
            // mInputPointers[idx].mSecondaryAxis;
            // mInputPointers[idx].mScrollDelta;
            // mInputPointers[idx].mAxis[16];
            idx++;
        }

        // only enable the scene object if its pose is valid
        if (mTrackedObjects[i]) mTrackedObjects[i]->mEnabled = mTrackedDevicesPredicted[i].bPoseIsValid;
        else if (mTrackedDevicesPredicted[i].bPoseIsValid) {
            // Create an object for tracked devices with a valid pose
            auto mr = make_shared<MeshRenderer>("TrackedDevice" + to_string(i));
            mScene->AddObject(mr);
            mTrackedObjects[i] = mr.get();
        }

        if (MeshRenderer* mr = dynamic_cast<MeshRenderer*>(mTrackedObjects[i])) {
            mr->mVisible = controllersVisible;

            if (!mr->Mesh() || !mr->Material()) {

                // Load render model + texture
                uint32_t len =  mSystem->GetStringTrackedDeviceProperty(i, Prop_RenderModelName_String, nullptr, 0);
                char* name = new char[len];
                mSystem->GetStringTrackedDeviceProperty(i, Prop_RenderModelName_String, name, len);

                if (mRenderModels.count(name)) {
                    // Mesh has been loaded, assign it
                    auto kp = mRenderModels.at(name);
                    mr->Mesh(kp.first);

                    // Find or load texture
                    if (mRenderModelMaterials.count(kp.second))
                        // Texture/material has been loaded, assign it
                        mr->Material(mRenderModelMaterials.at(kp.second).second);
                    else {
                        Texture* diffuse;
                        RenderModel_TextureMap_t* renderModelDiffuse;
                        if (mRenderModelInterface->LoadTexture_Async(kp.second, &renderModelDiffuse) == VRRenderModelError_None) {
                            diffuse = new Texture("DeviceRenderModelTexture" + to_string(i), mScene->Instance()->Device(),
                                renderModelDiffuse->rubTextureMapData, renderModelDiffuse->unWidth * renderModelDiffuse->unHeight * 4,
                                renderModelDiffuse->unWidth, renderModelDiffuse->unHeight, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
                            shared_ptr<Material> mat = make_shared<Material>(name, mScene->AssetManager()->LoadShader("Shaders/pbr.stm"));
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
                    if (mRenderModelInterface->LoadRenderModel_Async(name, &renderModel) == VRRenderModelError_None) {
                        RenderModel_Vertex_t* verts = new RenderModel_Vertex_t[renderModel->unVertexCount];
                        memcpy(verts, renderModel->rVertexData, renderModel->unVertexCount * sizeof(RenderModel_Vertex_t));
                        for (uint32_t i = 0; i < renderModel->unVertexCount; i++) {
                            verts[i].vPosition.v[2] = -verts[i].vPosition.v[2];
                            verts[i].vNormal.v[2] = -verts[i].vNormal.v[2];
                        }

                        Mesh* mesh = new Mesh("DeviceRenderModel" + to_string(i), mScene->Instance()->Device(),
                            verts, renderModel->rIndexData, renderModel->unVertexCount, sizeof(RenderModel_Vertex_t), renderModel->unTriangleCount * 3,
                            &RenderModelVertexInput, VK_INDEX_TYPE_UINT16);
                        mRenderModels.emplace(name, make_pair(mesh, renderModel->diffuseTextureId));
                        mRenderModelInterface->FreeRenderModel(renderModel);
                        mr->Mesh(mesh);

                        delete[] verts;
                    }
                }

                delete[] name;
            }
        }
    }
}
void OpenVR::PostRender(CommandBuffer* commandBuffer) {
    Profiler::BeginSample("OpenVR Copy");
    #pragma region Copy render texture
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
    #pragma endregion
    Profiler::EndSample();

    #pragma region Last-minute pose update
    Profiler::BeginSample("WaitGetPoses");
    VRCompositor()->WaitGetPoses(mTrackedDevices, k_unMaxTrackedDeviceCount, mTrackedDevicesPredicted, k_unMaxTrackedDeviceCount);
    Profiler::EndSample();

    Profiler::BeginSample("OpenVR Camera Update");
    for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++) {
        if (!mTrackedDevices[i].bPoseIsValid) continue;
        if (!mTrackedObjects[i]) continue;

        float3 pos;
        quaternion rot;
        ConvertMatrix(mTrackedDevices[i].mDeviceToAbsoluteTracking, pos, rot);
        mTrackedObjects[i]->LocalPosition(pos);
        mTrackedObjects[i]->LocalRotation(rot);
    }
    mHmdCamera->SetUniforms();
    Profiler::EndSample();
    #pragma endregion
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

    vr::Texture_t texture = {};
    texture.eType = TextureType_Vulkan;
    texture.eColorSpace = ColorSpace_Auto;
    texture.handle = &vkTexture;

    Instance::sDisableDebugCallback = true;
    if (VRCompositor()->Submit(vr::Eye_Left, &texture, &leftBounds) != vr::VRCompositorError_None) fprintf_color(COLOR_YELLOW, stderr, "%s", "Failed to submit left eye\n");
    if (VRCompositor()->Submit(vr::Eye_Right, &texture, &rightBounds) != vr::VRCompositorError_None) fprintf_color(COLOR_YELLOW, stderr, "%s", "Failed to submit right eye\n");
    Instance::sDisableDebugCallback = false;
    Profiler::EndSample();
}

void OpenVR::NextFrame() {
    memcpy(mInputPointersLast.data(), mInputPointers.data(), sizeof(InputPointer) * mInputPointers.size());
    for (InputPointer& p : mInputPointers) p.mGuiHitT = -1.0f;
 }