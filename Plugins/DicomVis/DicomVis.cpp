#include <Scene/MeshRenderer.hpp>
#include <Content/Font.hpp>
#include <Scene/GUI.hpp>
#include <Util/Profiler.hpp>

#include <Core/EnginePlugin.hpp>
#include <assimp/pbrmaterial.h>

#include "ImageLoader.hpp"

using namespace std;

enum MaskValue {
	MASK_NONE = 0,
	MASK_BLADDER = 1,
	MASK_KIDNEY = 2,
	MASK_COLON = 4,
	MASK_SPLEEN = 8,
	MASK_ILEUM = 16,
	MASK_AORTA = 32,
	MASK_ALL = 63,
};

class DicomVis : public EnginePlugin {
private:
	Scene* mScene;
	vector<Object*> mObjects;
	Camera* mMainCamera;
	Camera* mRenderCamera;

	uint32_t mFrameIndex;

	float3 mVolumePosition;
	quaternion mVolumeRotation;
	float3 mVolumeScale;

	// Render parameters

	bool mLighting;
	bool mColorize;
	float mStepSize;
	float mDensity;
	uint32_t mMaskValue;
	float2 mHueRange;
	float2 mRemapRange;


	// The volume loaded directly from the folder
	Texture* mRawVolume;
	// The mask loaded directly from the folder
	Texture* mRawMask;
	// The baked volume. This CAN be nullptr, in which case the shader will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume;
	// The gradient of the volume. This CAN be nullptr, in which case the shader will compute the gradient on the fly.
	Texture* mGradient;

	Texture* mHistoryBuffer;


	// Information about the state of the volume textures

	bool mRawVolumeColored;
	bool mRawVolumeNew;
	bool mBakeDirty;
	bool mGradientDirty;
	
	MouseKeyboardInput* mKeyboardInput;

	float mZoom;

	bool mShowPerformance;
	bool mSnapshotPerformance;
	ProfilerSample mProfilerFrames[PROFILER_FRAME_COUNT - 1];
	uint32_t mSelectedFrame;

	std::thread mScanThread;
	bool mScanDone;

	std::unordered_map<std::string, ImageStackType> mDataFolders;

	PLUGIN_EXPORT void ScanFolders() {
		string path = "/Data";
		for (uint32_t i = 0; i < mScene->Instance()->CommandLineArguments().size(); i++)
			if (mScene->Instance()->CommandLineArguments()[i] == "--datapath") {
				i++;
				if (i < mScene->Instance()->CommandLineArguments().size())
					path = mScene->Instance()->CommandLineArguments()[i];
			}
		if (!fs::exists(path)) path = "/Data";
		if (!fs::exists(path)) path = "/data";
		if (!fs::exists(path)) path = "~/Data";
		if (!fs::exists(path)) path = "~/data";
		if (!fs::exists(path)) path = "C:/Data";
		if (!fs::exists(path)) path = "D:/Data";
		if (!fs::exists(path)) path = "E:/Data";
		if (!fs::exists(path)) path = "F:/Data";
		if (!fs::exists(path)) path = "G:/Data";
		if (!fs::exists(path)) {
			fprintf_color(COLOR_RED, stderr, "DicomVis: Could not locate datapath. Please specify with --datapath <path>\n");
			return;
		}

		for (const auto& p : fs::recursive_directory_iterator(path)) {
			if (!p.is_directory() || p.path().stem() == "_mask") continue;
			ImageStackType type = ImageLoader::FolderStackType(p.path());
			if (type == IMAGE_STACK_NONE) continue;
			mDataFolders[p.path().string()] = type;
		}

		mScanDone = true;
	}

public:
	PLUGIN_EXPORT DicomVis(): mScene(nullptr), mShowPerformance(false), mSnapshotPerformance(false),
		mFrameIndex(0), mRawVolume(nullptr), mRawMask(nullptr), mGradient(nullptr), mRawVolumeNew(false), mBakeDirty(false), mGradientDirty(false),
		mColorize(false), mLighting(false), mHistoryBuffer(nullptr), mRenderCamera(nullptr),
		mVolumePosition(float3(0,0,0)), mVolumeRotation(quaternion(0,0,0,1)),
		mDensity(500.f), mMaskValue(MASK_ALL), mRemapRange(float2(.125f, 1.f)), mHueRange(float2(.01f, .5f)), mStepSize(.001f){
		mEnabled = true;
	}
	PLUGIN_EXPORT ~DicomVis() {
		if (mScanThread.joinable()) mScanThread.join();
		safe_delete(mHistoryBuffer);
		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mGradient);
		safe_delete(mBakedVolume);
		for (Object* obj : mObjects)
			mScene->RemoveObject(obj);
	}

	PLUGIN_EXPORT bool Init(Scene* scene) override {
		mScene = scene;
		mKeyboardInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;

		shared_ptr<Camera> camera = make_shared<Camera>("Camera", mScene->Instance()->Window());
		camera->Near(.01f);
		camera->Far(800.f);
		camera->FieldOfView(radians(65.f));
		camera->LocalPosition(0, 1.6f, -mZoom);
		mMainCamera = camera.get();
		mScene->AddObject(camera);
		mObjects.push_back(mMainCamera);
		mRenderCamera = mMainCamera;

		mScene->Environment()->EnvironmentTexture(mScene->AssetManager()->LoadTexture("Assets/Textures/photo_studio_01_2k.hdr"));
		mScene->Environment()->AmbientLight(.5f);

		mScanDone = false;
		mScanThread = thread(&DicomVis::ScanFolders, this);

		return true;
	}
	PLUGIN_EXPORT void Update(CommandBuffer* commandBuffer) override {
		if (mKeyboardInput->KeyDownFirst(KEY_F1)) mScene->DrawGizmos(!mScene->DrawGizmos());
		if (mKeyboardInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;

		// Snapshot profiler frames
		if (mKeyboardInput->KeyDownFirst(KEY_F3)) {
			mFrameIndex = 0;
			mSnapshotPerformance = !mSnapshotPerformance;
			if (mSnapshotPerformance) {
				mSelectedFrame = PROFILER_FRAME_COUNT;
				queue<pair<ProfilerSample*, const ProfilerSample*>> samples;
				for (uint32_t i = 0; i < PROFILER_FRAME_COUNT - 1; i++) {
					mProfilerFrames[i].mParent = nullptr;
					samples.push(make_pair(mProfilerFrames + i, Profiler::Frames() + ((i + Profiler::CurrentFrameIndex() + 2) % PROFILER_FRAME_COUNT)));
					while (samples.size()) {
						auto p = samples.front();
						samples.pop();

						p.first->mStartTime = p.second->mStartTime;
						p.first->mDuration = p.second->mDuration;
						strncpy(p.first->mLabel, p.second->mLabel, PROFILER_LABEL_SIZE);
						p.first->mChildren.resize(p.second->mChildren.size());

						auto it2 = p.second->mChildren.begin();
						for (auto it = p.first->mChildren.begin(); it != p.first->mChildren.end(); it++, it2++) {
							it->mParent = p.first;
							samples.push(make_pair(&*it, &*it2));
						}
					}
				}
			}
		}

		// Prefer a stereo camera over the main camera
		mRenderCamera = mMainCamera;
		for (Camera* c : mScene->Cameras())
			if (c->EnabledHierarchy() && c->StereoMode() != STEREO_NONE) {
				mRenderCamera = c;
				break;
			}

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->ScrollDelta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->ScrollDelta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);

				mFrameIndex = 0;
			}
			if (mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x + mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f){
					mVolumeRotation = quaternion(length(axis) * .003f, -normalize(axis)) * mVolumeRotation;
					mFrameIndex = 0;
				}
			}
		}
	}

	PLUGIN_EXPORT void PreRender(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override {
		if (pass != PASS_MAIN) return;

		bool worldSpace = camera->StereoMode() != STEREO_NONE;

		Font* reg14 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-Regular.ttf", 14);
		Font* sem11 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-SemiBold.ttf", 11);
		Font* sem16 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-SemiBold.ttf", 16);
		Font* bld24 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-Bold.ttf", 24);
		Texture* icons = mScene->AssetManager()->LoadTexture("Assets/Textures/icons.png", true);

		float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
		float2 c = mKeyboardInput->CursorPos();
		c.y = s.y - c.y;

		// Draw performance overlay
		if (mShowPerformance && !worldSpace) {
			Device* device = mScene->Instance()->Device();
			VkDeviceSize memSize = 0;
			for (uint32_t i = 0; i < device->MemoryProperties().memoryHeapCount; i++)
				if (device->MemoryProperties().memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
					memSize += device->MemoryProperties().memoryHeaps[i].size;

			char tmpText[128];
			snprintf(tmpText, 128, "%.2f fps\n%u/%u allocations | %d descriptor sets\n%.3f / %.3f mb (%.1f%%)",
				mScene->FPS(),
				device->MemoryAllocationCount(), device->Limits().maxMemoryAllocationCount, mScene->Instance()->Device()->DescriptorSetCount(),
				device->MemoryUsage() / (1024.f * 1024.f), memSize / (1024.f * 1024.f), 100.f * (float)device->MemoryUsage() / (float)memSize );
			GUI::DrawString(sem16, tmpText, 1.f, float2(5, camera->FramebufferHeight() - 18), 18.f, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MAX);

			#ifdef PROFILER_ENABLE
			const uint32_t pointCount = PROFILER_FRAME_COUNT - 1;
			
			float graphHeight = 100;

			float2 points[pointCount];
			float m = 0;
			for (uint32_t i = 0; i < pointCount; i++) {
				points[i].y = (mSnapshotPerformance ? mProfilerFrames[i] : Profiler::Frames()[(i + Profiler::CurrentFrameIndex() + 2) % PROFILER_FRAME_COUNT]).mDuration.count() * 1e-6f;
				points[i].x = (float)i / (pointCount - 1.f);
				m = fmaxf(points[i].y, m);
			}
			m = fmaxf(m, 5.f) + 3.f;
			for (uint32_t i = 0; i < pointCount; i++)
				points[i].y /= m;

			GUI::Rect(fRect2D(0, 0, s.x, graphHeight), float4(.1f, .1f, .1f, 1));
			GUI::Rect(fRect2D(0, graphHeight - 1, s.x, 2), float4(.2f, .2f, .2f, 1));

			snprintf(tmpText, 64, "%.1fms", m);
			GUI::DrawString(sem11, tmpText, float4(.6f, .6f, .6f, 1.f), float2(2, graphHeight - 10), 11.f);

			for (uint32_t i = 1; i < 3; i++) {
				float x = m * i / 3.f;
				snprintf(tmpText, 128, "%.1fms", x);
				GUI::Rect(fRect2D(0, graphHeight * (i / 3.f) - 1, s.x, 1), float4(.2f, .2f, .2f, 1));
				GUI::DrawString(sem11, tmpText, float4(.6f, .6f, .6f, 1.f), float2(2, graphHeight * (i / 3.f) + 2), 11.f);
			}

			GUI::DrawScreenLine(points, pointCount, 1.5f, 0, float2(s.x, graphHeight), float4(.2f, 1.f, .2f, 1.f));

			if (mSnapshotPerformance) {
				if (c.y < 100) {
					uint32_t hvr = (uint32_t)((c.x / s.x) * (PROFILER_FRAME_COUNT - 2) + .5f);
					GUI::Rect(fRect2D(s.x * hvr / (PROFILER_FRAME_COUNT - 2), 0, 1, graphHeight), float4(1, 1, 1, .15f));
					if (mKeyboardInput->KeyDown(MOUSE_LEFT))
						mSelectedFrame = hvr;
				}

				if (mSelectedFrame < PROFILER_FRAME_COUNT - 1) {
					ProfilerSample* selected = nullptr;
					float sampleHeight = 20;

					// selection line
					GUI::Rect(fRect2D(s.x * mSelectedFrame / (PROFILER_FRAME_COUNT - 2), 0, 1, graphHeight), 1);

					float id = 1.f / (float)mProfilerFrames[mSelectedFrame].mDuration.count();

					queue<pair<ProfilerSample*, uint32_t>> samples;
					samples.push(make_pair(mProfilerFrames + mSelectedFrame, 0));
					while (samples.size()) {
						auto p = samples.front();
						samples.pop();

						float2 pos(s.x * (p.first->mStartTime - mProfilerFrames[mSelectedFrame].mStartTime).count() * id, graphHeight + 20 + sampleHeight * p.second);
						float2 size(s.x * (float)p.first->mDuration.count() * id, sampleHeight);
						float4 col(0, 0, 0, 1);

						if (c.x > pos.x&& c.y > pos.y && c.x < pos.x + size.x && c.y < pos.y + size.y) {
							selected = p.first;
							col.rgb = 1;
						}

						GUI::Rect(fRect2D(pos, size), col);
						GUI::Rect(fRect2D(pos + 1, size - 2), float4(.3f, .9f, .3f, 1));

						for (auto it = p.first->mChildren.begin(); it != p.first->mChildren.end(); it++)
							samples.push(make_pair(&*it, p.second + 1));
					}

					if (selected) {
						snprintf(tmpText, 128, "%s: %.2fms\n", selected->mLabel, selected->mDuration.count() * 1e-6f);
						float2 sp = c + float2(0, 10);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2( 1,  0), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2(-1,  0), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2( 0,  1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2( 0, -1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2(-1, -1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2( 1, -1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2(-1,  1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, float4(0, 0, 0, 1), sp + float2( 1,  1), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
						GUI::DrawString(reg14, tmpText, 1, sp, 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
					}
				}

			}
			return;
			#endif
		}

		if (!mScanDone)  return;
		if (mScanThread.joinable()) mScanThread.join();

		float sliderHeight = 12;
		float sliderKnobSize = 12;
		GUI::LayoutTheme guiTheme = GUI::mLayoutTheme;

		if (worldSpace)
			GUI::BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 300, 850), 10);
		else
			GUI::BeginScreenLayout(LAYOUT_VERTICAL, fRect2D(10, s.y * .5f - 425, 300, 850), 10);

		#pragma region Data set list
		GUI::mLayoutTheme.mBackgroundColor = guiTheme.mControlBackgroundColor;

		GUI::LayoutLabel(bld24, "Load Data Set", 24, 38);
		GUI::LayoutSeparator(.5f);
		GUI::BeginScrollSubLayout(175, mDataFolders.size() * 24, 5);
		for (const auto& p : mDataFolders)
			if (GUI::LayoutTextButton(sem16, fs::path(p.first).stem().string(), 16, 24, TEXT_ANCHOR_MIN))
				LoadVolume(commandBuffer, p.first, p.second);
		GUI::EndLayout();

		GUI::mLayoutTheme.mBackgroundColor = guiTheme.mBackgroundColor;
		#pragma endregion

		#pragma region Toggleable settings
		fRect2D r = GUI::BeginSubLayout(LAYOUT_HORIZONTAL, 24, 0, 2);
		GUI::LayoutLabel(sem16, "Colorize", 16, r.mExtent.x - 24, 0, TEXT_ANCHOR_MIN);
		if (GUI::LayoutImageButton(24, icons, float4(.125f, .125f, mColorize ? .125f : 0, .5f), 0)) {
			mColorize = !mColorize;
			mBakeDirty = true;
			mFrameIndex = 0;
		}
		GUI::EndLayout();

		r = GUI::BeginSubLayout(LAYOUT_HORIZONTAL, 24, 0, 2);
		GUI::LayoutLabel(sem16, "Lighting", 16, r.mExtent.x - 24, 0, TEXT_ANCHOR_MIN);
		if (GUI::LayoutImageButton(24, icons, float4(.125f, .125f, mLighting ? .125f : 0, .5f), 0)) {
			mLighting = !mLighting;
			mFrameIndex = 0;
		}
		GUI::EndLayout();
		#pragma endregion

		GUI::LayoutSeparator(.5f, 3);

		GUI::LayoutLabel(bld24, "Render Settings", 18, 24);
		GUI::LayoutSpace(8);

		GUI::LayoutLabel(sem16, "Step Size: " + to_string(mStepSize), 14, 14, 0, TEXT_ANCHOR_MIN);
		if (GUI::LayoutSlider(mStepSize, .0001f, .01f, sliderHeight, sliderKnobSize)) mFrameIndex = 0;
		GUI::LayoutLabel(sem16, "Density: " + to_string(mDensity), 14, 14, 0, TEXT_ANCHOR_MIN);
		if (GUI::LayoutSlider(mDensity, 10, 50000.f, sliderHeight, sliderKnobSize)) mFrameIndex = 0;
		
		GUI::LayoutSpace(20);

		GUI::LayoutLabel(sem16, "Remap", 14, 14, 0, TEXT_ANCHOR_MIN);
		if (GUI::LayoutRangeSlider(mRemapRange, 0, 1, sliderHeight, sliderKnobSize)) {
			mBakeDirty = true;
			mFrameIndex = 0;
		}

		if (mColorize) {
			GUI::LayoutSpace(20);

			GUI::LayoutLabel(sem16, "Hue Range", 14, 14, 0, TEXT_ANCHOR_MIN);
			if (GUI::LayoutRangeSlider(mHueRange, 0, 1, sliderHeight, sliderKnobSize)) {
				mBakeDirty = true;
				mFrameIndex = 0;
			}
		}

		GUI::EndLayout();

		GUI::mLayoutTheme = guiTheme;
	}

	PLUGIN_EXPORT void PostProcess(CommandBuffer* commandBuffer, Camera* camera) override {
		if (!mRawVolume) return;
		if (camera != mRenderCamera) return; // don't draw volume on window if there's another camera being used
		
		if (!mHistoryBuffer || mHistoryBuffer->Width() != camera->FramebufferWidth() || mHistoryBuffer->Height() != camera->FramebufferHeight()) {
			safe_delete(mHistoryBuffer);
			mHistoryBuffer = new Texture("Volume Render Result", mScene->Instance()->Device(), nullptr, 0,
				camera->FramebufferWidth(), camera->FramebufferHeight(), 1,
				VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
			mHistoryBuffer->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mFrameIndex = 0;
		}

		if (mRawVolumeNew) {
			mRawVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mRawVolumeNew = false;

			if (mRawMask) mRawMask->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			if (mBakedVolume) mBakedVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			if (mGradient) mGradient->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
		}
		
		uint2 res(camera->FramebufferWidth(), camera->FramebufferHeight());
		uint3 vres(mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth());
		float4x4 ivp[2]{
			camera->InverseViewProjection(EYE_LEFT),
			camera->InverseViewProjection(EYE_RIGHT)
		};
		float3 vp[2]{
			mVolumePosition - (camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_LEFT), 1)).xyz,
			mVolumePosition - (camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_RIGHT), 1)).xyz
		};
		float4 ivr = inverse(mVolumeRotation).xyzw;
		float3 ivs = 1.f / mVolumeScale;
		uint2 writeOffset(0);

		// Bake the volume if necessary
		if (mBakeDirty && mBakedVolume) {
			set<string> kw;
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
			ComputeShader* shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeVolume", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("BakeVolume", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateStorageTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Output").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstant(shader, "VolumeResolution", &vres);
			commandBuffer->PushConstant(shader, "MaskValue", &mMaskValue);
			commandBuffer->PushConstant(shader, "RemapRange", &mRemapRange);
			commandBuffer->PushConstant(shader, "HueRange", &mHueRange);
			vkCmdDispatch(*commandBuffer, (mRawVolume->Width() + 3) / 4, (mRawVolume->Height() + 3) / 4, (mRawVolume->Depth() + 3) / 4);

			mBakedVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mBakeDirty = false;
		}

		// Shader keywords shared by the gradient bake and the final render
		set<string> kw;
		if (!mBakedVolume) {
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
		}
		
		// Bake the gradient if necessary
		if (mGradientDirty && mGradient) {
			ComputeShader* shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeGradient", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("BakeGradient", shader->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateStorageTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			else {
				ds->CreateStorageTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
				if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			}
			ds->CreateStorageTextureDescriptor(mGradient, shader->mDescriptorBindings.at("Output").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstant(shader, "VolumeResolution", &vres);
			commandBuffer->PushConstant(shader, "MaskValue", &mMaskValue);
			commandBuffer->PushConstant(shader, "RemapRange", &mRemapRange);
			commandBuffer->PushConstant(shader, "HueRange", &mHueRange);
			vkCmdDispatch(*commandBuffer, (mRawVolume->Width() + 3) / 4, (mRawVolume->Height() + 3) / 4, (mRawVolume->Depth() + 3) / 4);

			mBakedVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mGradientDirty = false;
		}

		// Render the volume
		{
			if (mLighting) kw.emplace("LIGHTING");
			if (mGradient) kw.emplace("GRADIENT_TEXTURE");
			ComputeShader* shader = mScene->AssetManager()->LoadShader("Shaders/volume.stm")->GetCompute("Render", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("Draw Volume", shader->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateSampledTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			else {
				ds->CreateSampledTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
				if (mRawMask) ds->CreateSampledTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			}
			if (mLighting && mGradient)
				ds->CreateStorageTextureDescriptor(mGradient, shader->mDescriptorBindings.at("Gradient").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateStorageTextureDescriptor(mHistoryBuffer, shader->mDescriptorBindings.at("History").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateStorageTextureDescriptor(camera->ResolveBuffer(0), shader->mDescriptorBindings.at("RenderTarget").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateStorageTextureDescriptor(camera->ResolveBuffer(1), shader->mDescriptorBindings.at("DepthNormal").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateSampledTextureDescriptor(mScene->AssetManager()->LoadTexture("Assets/Textures/rgbanoise.png", false), shader->mDescriptorBindings.at("NoiseTex").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstant(shader, "VolumeResolution", &vres);
			commandBuffer->PushConstant(shader, "VolumeRotation", &mVolumeRotation.xyzw);
			commandBuffer->PushConstant(shader, "VolumeScale", &mVolumeScale);
			commandBuffer->PushConstant(shader, "InvVolumeRotation", &ivr);
			commandBuffer->PushConstant(shader, "InvVolumeScale", &ivs);
			commandBuffer->PushConstant(shader, "Density", &mDensity);
			commandBuffer->PushConstant(shader, "MaskValue", &mMaskValue);
			commandBuffer->PushConstant(shader, "RemapRange", &mRemapRange);
			commandBuffer->PushConstant(shader, "HueRange", &mHueRange);
			commandBuffer->PushConstant(shader, "StepSize", &mStepSize);
			commandBuffer->PushConstant(shader, "FrameIndex", &mFrameIndex);

			switch (camera->StereoMode()) {
			case STEREO_NONE:
				commandBuffer->PushConstant(shader, "VolumePosition", &vp[0]);
				commandBuffer->PushConstant(shader, "InvViewProj", &ivp[0]);
				commandBuffer->PushConstant(shader, "WriteOffset", &writeOffset);
				commandBuffer->PushConstant(shader, "ScreenResolution", &res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			case STEREO_SBS_HORIZONTAL:
				res.x /= 2;
				commandBuffer->PushConstant(shader, "VolumePosition", &vp[0]);
				commandBuffer->PushConstant(shader, "InvViewProj", &ivp[0]);
				commandBuffer->PushConstant(shader, "WriteOffset", &writeOffset);
				commandBuffer->PushConstant(shader, "ScreenResolution", &res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				writeOffset.x = res.x;
				commandBuffer->PushConstant(shader, "VolumePosition", &vp[1]);
				commandBuffer->PushConstant(shader, "InvViewProj", &ivp[1]);
				commandBuffer->PushConstant(shader, "WriteOffset", &writeOffset);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			case STEREO_SBS_VERTICAL:
				res.y /= 2;
				commandBuffer->PushConstant(shader, "VolumePosition", &vp[0]);
				commandBuffer->PushConstant(shader, "InvViewProj", &ivp[0]);
				commandBuffer->PushConstant(shader, "WriteOffset", &writeOffset);
				commandBuffer->PushConstant(shader, "ScreenResolution", &res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				writeOffset.y = res.y;
				commandBuffer->PushConstant(shader, "VolumePosition", &vp[1]);
				commandBuffer->PushConstant(shader, "InvViewProj", &ivp[1]);
				commandBuffer->PushConstant(shader, "WriteOffset", &writeOffset);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			}
		}

		mFrameIndex++;
	}
	
	void LoadVolume(CommandBuffer* commandBuffer, const fs::path& folder, ImageStackType type) {
		
		vkDeviceWaitIdle(*mScene->Instance()->Device());

		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mBakedVolume);
		safe_delete(mGradient);

		Texture* vol = nullptr;
		switch (type) {
		case IMAGE_STACK_STANDARD:
			vol = ImageLoader::LoadStandardStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		case IMAGE_STACK_DICOM:
			vol = ImageLoader::LoadDicomStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		case IMAGE_STACK_RAW:
			vol = ImageLoader::LoadRawStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		}
		
		if (!vol) {
			fprintf_color(COLOR_RED, stderr, "Failed to load volume!\n");
			return;
		}

		switch (vol->Format()) {
		default:
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SNORM:
		case VK_FORMAT_R16_USCALED:
		case VK_FORMAT_R16_SSCALED:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16_SFLOAT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_R64_UINT:
		case VK_FORMAT_R64_SINT:
		case VK_FORMAT_R64_SFLOAT:
			mRawVolumeColored = false;
			break;

		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SNORM:
		case VK_FORMAT_R16G16B16A16_USCALED:
		case VK_FORMAT_R16G16B16A16_SSCALED:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_R64G64B64A64_UINT:
		case VK_FORMAT_R64G64B64A64_SINT:
		case VK_FORMAT_R64G64B64A64_SFLOAT:
			mRawVolumeColored = true;
			break;

		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
			break;
		}

		mVolumeRotation = quaternion(0,0,0,1);
		mVolumePosition = float3(0, 1.6f, 0);

		mRawVolume = vol;
		mRawVolumeNew = true;

		mRawMask = ImageLoader::LoadStandardStack(folder.string() + "/_mask", mScene->Instance()->Device(), nullptr, true, 1, false);
		
		// TODO: only create the baked volume and gradient textures if there is enough VRAM (check device->MemoryUsage())

		mBakedVolume = new Texture("Volume", mScene->Instance()->Device(), nullptr, 0, mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R8G8B8A8_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		mBakeDirty = true;
	
		mGradient = new Texture("Gradient", mScene->Instance()->Device(), nullptr, 0, mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R8G8B8A8_SNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
		mGradientDirty = true;

		mFrameIndex = 0;
	}
};

ENGINE_PLUGIN(DicomVis)