#include <Scene/MeshRenderer.hpp>
#include <Content/Font.hpp>
#include <Scene/GUI.hpp>
#include <Util/Profiler.hpp>

#include <Core/EnginePlugin.hpp>
#include <assimp/pbrmaterial.h>

#include <map>

#include "ImageLoader.hpp"
#include "TransferFunction.hpp"

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

	struct MaskColors {
		float3 BladderColor;
		float spacing1;
		float3 KidneyColor;
		float spacing2;
		float3 ColonColor;
		float spacing3;
		float3 SpleenColor;
		float spacing4;
		float3 IleumColor;
		float spacing5;
		float3 AortaColor;
		float spacing6;
	};

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
	TransferFunction mTransferFunction;

	bool mDisplayBody;
	MaskColors mMaskColors;
	MaskValue mOrganToColor;

	Texture* mRawVolume;
	// The mask loaded directly from the folder
	Texture* mRawMask;
	// The baked volume. This CAN be nullptr, in which case the shader will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume;
	// The gradient of the volume. This CAN be nullptr, in which case the shader will compute the gradient on the fly.
	Texture* mGradient;
	// The transfer function lookup table
	Texture* mTransferLUT;

	Texture* mHistoryBuffer;


	// Information about the state of the volume textures

	bool mRawVolumeColored;
	bool mRawVolumeNew;
	bool mBakeDirty;
	bool mGradientDirty;
	bool mLUTDirty;
	
	MouseKeyboardInput* mKeyboardInput;

	float mZoom;

	bool mShowPerformance;
	bool mSnapshotPerformance;
	ProfilerSample mProfilerFrames[PROFILER_FRAME_COUNT - 1];
	uint32_t mSelectedFrame;

	std::thread mScanThread;
	bool mScanDone;

	//Folders containing datasets
	std::list<ScanInfo> mDataFolders;

	//Organized sets of folders - top level is by patient name, bottom level is by date
	std::map<std::string, std::set<ScanInfo> > mOrganizedDataFolders;
	std::string mPatient;


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
			if (type == IMAGE_STACK_NONE || type == IMAGE_STACK_STANDARD) continue;
			ScanInfo info = ImageLoader::GetScanInfo(p.path(), type);
			mDataFolders.push_back(info);
			if (!mOrganizedDataFolders.count(info.patient_name)) {
				mOrganizedDataFolders[info.patient_name] = set<ScanInfo>();
			}
			mOrganizedDataFolders[info.patient_name].insert(info);

		}

		mScanDone = true;
	}

public:
	PLUGIN_EXPORT DicomVis(): mScene(nullptr), mShowPerformance(false), mSnapshotPerformance(false),
		mFrameIndex(0), mRawVolume(nullptr), mRawMask(nullptr), mGradient(nullptr), mTransferLUT(nullptr), mRawVolumeNew(false), mBakeDirty(false), mGradientDirty(false), mLUTDirty(false),
		mColorize(false), mLighting(false), mHistoryBuffer(nullptr), mRenderCamera(nullptr),
		mVolumePosition(float3(0,0,0)), mVolumeRotation(quaternion(0,0,0,1)),
		mPatient(""),
		mDisplayBody(true), 
		mMaskColors({float3(61,1,164)/255,0, float3(2,71,253)/255,0, float3(192,162,254)/255,0, float3(255,222,35)/255,0, float3(249,225,255)/255,0, float3(254,27,93)/255,0}),
		mTransferFunction(),
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
		safe_delete(mTransferLUT);
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

		Texture* patient = mScene->AssetManager()->LoadTexture("Assets/Textures/DicomVis/patient_icon.png");
		Texture* scan = mScene->AssetManager()->LoadTexture("Assets/Textures/DicomVis/cube_icon.png");

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


		if (mPatient.empty()) {
			GUI::LayoutLabel(bld24, "Choose patient", 24, 38);
		}
		else {
			GUI::LayoutLabel(bld24, "Choose data set", 24, 38);
		}
		GUI::LayoutSeparator(.5f, 1);


		float size = 128;
		float numperline = 2;

		if (mPatient.empty()) {
			GUI::BeginScrollSubLayout(150, ((int)mOrganizedDataFolders.size() / numperline) * (size + 7), 5);
			int curr = 0;
			
			for (const auto& p : mOrganizedDataFolders) {
				if (curr == 0) {
					GUI::BeginSubLayout(LAYOUT_HORIZONTAL, size, 0, 2);
				}

				GUI::BeginSubLayout(LAYOUT_VERTICAL, size, 0, 2);
				float z = 0;
				bool screenspace = true;
				fRect2D rect, clipRect;
				GUI::GetCurrentLayout(rect, z, screenspace, clipRect);
				
				if (screenspace) {
					if (GUI::TextButton(sem16, "", 16, rect, guiTheme.mControlBackgroundColor, 1, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID, z, clipRect)) {
						mPatient = p.first;
					}
				}
				else {
					float4x4 buttonTransform = GUI::GetCurrentTransform();
					if (GUI::TextButton(sem16, "", 16, buttonTransform * float4x4::Translate(float3(0, 0, z)), rect, guiTheme.mControlBackgroundColor, 1, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID, clipRect)) {
						mPatient = p.first;
					}
				}

				GUI::LayoutLabel(sem16, p.first, size / 8, size / 8, 0);
				//GUI::LayoutSpace(3.0f * size / 4.0f);
				GUI::LayoutRect(3.0f * size / 4.0f, patient, float4(1, screenspace ? 1 : -1, 0, screenspace ? 0 : 1), 0);

				GUI::LayoutLabel(sem16, p.second.begin()->patient_id, size / 10, size / 10, 0);

				GUI::EndLayout();

				++curr;
				if (curr == numperline) {
					curr = 0;
					GUI::EndLayout();
				}
			}
			
			GUI::EndLayout();
		}
		else {
			GUI::BeginScrollSubLayout(150, ceil((float)(mOrganizedDataFolders[mPatient].size() + 1) / numperline) * (size + 7), 5);
			int curr = 1;
			GUI::BeginSubLayout(LAYOUT_HORIZONTAL, size, 0, 2);

			if (GUI::LayoutTextButton(sem16, "Return\nto\npatients", 16, size, 2, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID)) {
				mPatient = "";
			}
			else {
				for (const auto& p : mOrganizedDataFolders[mPatient]) {
					if (curr == 0) {
						GUI::BeginSubLayout(LAYOUT_HORIZONTAL, size, 0, 2);
					}

					GUI::BeginSubLayout(LAYOUT_VERTICAL, size, 0, 2);
					float z = 0;
					bool screenspace = true;
					fRect2D rect, clipRect;
					GUI::GetCurrentLayout(rect, z, screenspace, clipRect);
					if (screenspace) {
						if (GUI::TextButton(sem16, "", 16, rect, guiTheme.mControlBackgroundColor, float4(0, 0, 0, 1), TEXT_ANCHOR_MID, TEXT_ANCHOR_MID, z, clipRect)) {
							LoadVolume(commandBuffer, p.path, p.type);
							printf_color(COLOR_GREEN, "Scan metadata: %s, %s, %s, %s", p.patient_id, p.patient_name, p.study_date, p.study_time);
						}
					}
					else {
						float4x4 buttonTransform = GUI::GetCurrentTransform();
						if (GUI::TextButton(sem16, "", 16, buttonTransform * float4x4::Translate(float3(0, 0, z)), rect, guiTheme.mControlBackgroundColor, float4(0, 0, 0, 1), TEXT_ANCHOR_MID, TEXT_ANCHOR_MID, clipRect)) {
							LoadVolume(commandBuffer, p.path, p.type);
							printf_color(COLOR_GREEN, "Scan metadata: %s, %s, %s, %s", p.patient_id, p.patient_name, p.study_date, p.study_time);
						}
					}

					GUI::LayoutLabel(sem16, p.study_name, size / 8, size / 8, 0);
					//GUI::LayoutSpace(3 * size / 4);
					GUI::LayoutRect(3.0f * size / 4.0f, scan, float4(1, screenspace ? 1 : -1, 0, screenspace ? 0 : 1), 0);

					GUI::LayoutLabel(sem16, p.study_date, size / 10, size / 10, 0);

					GUI::EndLayout();

					++curr;
					if (curr == numperline) {
						curr = 0;
						GUI::EndLayout();
					}
				}
			}

			GUI::EndLayout();
		}

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

		
		if (mTransferLUT) {
			if(mTransferFunction.RenderUI(float2(400), mTransferLUT, mScene, commandBuffer)) {
					mLUTDirty = true;
					mBakeDirty = true;
					mFrameIndex = 0;
			}
		}
		

		GUI::EndLayout();
#pragma endregion

#pragma region Mask settings

		if (worldSpace) {
			GUI::BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.45f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 200, 400), 10);
		}
		else {
			GUI::BeginScreenLayout(LAYOUT_VERTICAL, fRect2D(320, s.y * .5f - 25, 200, 400), 10);
		}
		
		GUI::LayoutLabel(bld24, "Mask Controls", 24, 36);

		GUI::BeginSubLayout(LAYOUT_HORIZONTAL, 24, 0);
		GUI::LayoutLabel(sem16, "Body", 20, 120);
		if (GUI::LayoutImageButton(24, icons, float4(.125f, .125f, mDisplayBody ? .125f : 0, .5f), 5)) {
			mDisplayBody = !mDisplayBody;
			mBakeDirty = true;
			mFrameIndex = 0;
		}
		GUI::EndLayout();


		std::pair<std::string, MaskValue> masks[6] = {
			{"Bladder", MASK_BLADDER},
			{"Kidney", MASK_KIDNEY},
			{"Colon", MASK_COLON},
			{"Spleen", MASK_SPLEEN},
			{"Ileum", MASK_ILEUM},
			{"Aorta", MASK_AORTA},
		};

		for (int i = 0; i < 6; ++i) {
			auto mask = masks[i];
			GUI::BeginSubLayout(LAYOUT_HORIZONTAL, 24, 0);
			GUI::LayoutLabel(sem16, mask.first, 20, 120);
			bool lval = mMaskValue & mask.second;
			if (GUI::LayoutImageButton(24, icons, float4(.125f, .125f, lval ? .125f : 0, .5f), 5)) {
				mMaskValue = mMaskValue ^ mask.second;
				printf("%d\n", mMaskValue);
				mBakeDirty = true;
				mFrameIndex = 0;
			}
			float3 color = *(float3*)((float4*)(&mMaskColors) + i);
			GUI::mLayoutTheme.mControlBackgroundColor = float4(color, 1);
			if (GUI::LayoutTextButton(nullptr, "", 0, 24, 0)) {
				mOrganToColor = mOrganToColor == mask.second ? MASK_NONE : mask.second;
			}
			GUI::EndLayout();
		}

		GUI::EndLayout();

		
		if (mOrganToColor != MASK_NONE) {
			if (worldSpace) {
				GUI::BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.225f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 216, 216), 4);
			}
			else {
				GUI::BeginScreenLayout(LAYOUT_VERTICAL, fRect2D(530, s.y * .5f + 125, 216, 216), 4);
			}

			//GUI::BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0,0,0,1), .001f), fRect2D(0, 0, 300, 850), float4(.3f, .3f, .3f, 1), 10);
			bool changed = false;
			switch (mOrganToColor) {
			case MASK_BLADDER:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.BladderColor, 200, 10, 4);
				break;
			case MASK_KIDNEY:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.KidneyColor, 200, 10, 4);
				break;
			case MASK_COLON:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.ColonColor, 200, 10, 4);
				break;
			case MASK_SPLEEN:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.SpleenColor, 200, 10, 4);
				break;
			case MASK_ILEUM:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.IleumColor, 200, 10, 4);
				break;
			case MASK_AORTA:
				changed = changed || GUI::LayoutColorPicker(mMaskColors.AortaColor, 200, 10, 4);
				break;
			}
			if (changed) {
				mBakeDirty = true;
				mFrameIndex = 0;
			}
			GUI::EndLayout();
		}
		
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
			if (mTransferLUT) mTransferLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
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

		if ( mTransferLUT) {
			set<string> kw;
			//kw.emplace("NON_BAKED_R_LUT");
			ComputeShader* shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("ClearTransferFunction", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);
			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("BakeTransferFunctionRGB", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mTransferLUT, shader->mDescriptorBindings.at("TransferLUT").second.binding, VK_IMAGE_LAYOUT_GENERAL);

			ds->FlushWrites();

			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			uint3 res = uint3(mTransferLUT->Width(), mTransferFunction.GetGradients().size(), 1);
			commandBuffer->PushConstant(shader, "VolumeResolution", &res);

			vkCmdDispatch(*commandBuffer, (mTransferLUT->Width() + 7) / 8, mTransferFunction.GetGradients().size(), 1);
			mTransferLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);



			shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeTransferFunctionRGB", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);
			ds = commandBuffer->Device()->GetTempDescriptorSet("BakeTransferFunctionRGB", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mTransferLUT, shader->mDescriptorBindings.at("TransferLUT").second.binding, VK_IMAGE_LAYOUT_GENERAL);

			Buffer* gradients = commandBuffer->Device()->GetTempBuffer("GradientRGB", mTransferFunction.GetGradients().size() * sizeof(TransferGradient), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(gradients->MappedData(), mTransferFunction.GetGradients().data(), mTransferFunction.GetGradients().size() * sizeof(TransferGradient));

			//DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("GradientRGB", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(gradients, 0, mTransferFunction.GetGradients().size() * sizeof(TransferGradient), shader->mDescriptorBindings.at("GradientRGB").second.binding);
			ds->FlushWrites();

			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);
			
			res = uint3(mTransferLUT->Width(), mTransferFunction.GetGradients().size(), 1);
			commandBuffer->PushConstant(shader, "VolumeResolution", &res);

			vkCmdDispatch(*commandBuffer, (mTransferLUT->Width() + 7) / 8, mTransferFunction.GetGradients().size() - 1, 1);



			shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeTransferFunctionA", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);
			ds = commandBuffer->Device()->GetTempDescriptorSet("BakeTransferFunctionA", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mTransferLUT, shader->mDescriptorBindings.at("TransferLUT").second.binding, VK_IMAGE_LAYOUT_GENERAL);

			Buffer* triangles = commandBuffer->Device()->GetTempBuffer("GradientA", mTransferFunction.GetTriangles().size() * sizeof(TransferTriangle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(triangles->MappedData(), mTransferFunction.GetTriangles().data(), mTransferFunction.GetTriangles().size() * sizeof(TransferTriangle));

			//DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("GradientRGB", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(triangles, 0, mTransferFunction.GetTriangles().size() * sizeof(TransferTriangle), shader->mDescriptorBindings.at("GradientA").second.binding);
			ds->FlushWrites();

			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			res = uint3(mTransferLUT->Width(), mTransferFunction.GetTriangles().size(), 1);
			commandBuffer->PushConstant(shader, "VolumeResolution", &res);

			vkCmdDispatch(*commandBuffer, (mTransferLUT->Width() + 7) / 8, mTransferFunction.GetTriangles().size(), 1);


			mTransferLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mLUTDirty = false;
		}

		// Bake the volume if necessary
		if (mBakeDirty && mBakedVolume) {
			set<string> kw;
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else if (mTransferLUT) kw.emplace("NON_BAKED_R_LUT");
			else kw.emplace("NON_BAKED_R");
			ComputeShader* shader = mScene->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeVolume", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("BakeVolume", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			ds->CreateStorageTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Output").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			if (mRawMask) {
				Buffer* colbuffer = commandBuffer->Device()->GetTempBuffer("MaskCols", sizeof(mMaskColors), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
				memcpy(colbuffer->MappedData(), &mMaskColors, sizeof(mMaskColors));

				ds->CreateUniformBufferDescriptor(colbuffer, 0, sizeof(mMaskColors), shader->mDescriptorBindings.at("MaskCols").second.binding);
			}
			if (mTransferLUT) {
				ds->CreateSampledTextureDescriptor(mTransferLUT, shader->mDescriptorBindings.at("TransferLUTTex").second.binding, VK_IMAGE_LAYOUT_GENERAL);
			}
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstant(shader, "VolumeResolution", &vres);
			commandBuffer->PushConstant(shader, "MaskValue", &mMaskValue);
			commandBuffer->PushConstant(shader, "RemapRange", &mRemapRange);
			commandBuffer->PushConstant(shader, "HueRange", &mHueRange);

			int body = mDisplayBody;
			commandBuffer->PushConstant(shader, "DisplayBody", &body);

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
				if (mRawMask) {
					ds->CreateSampledTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, VK_IMAGE_LAYOUT_GENERAL);

					Buffer* colbuffer = commandBuffer->Device()->GetTempBuffer("MaskCols", sizeof(mMaskColors), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
					memcpy(colbuffer->MappedData(), &mMaskColors, sizeof(mMaskColors));

					ds->CreateUniformBufferDescriptor(colbuffer, 0, sizeof(mMaskColors), shader->mDescriptorBindings.at("MaskCols").second.binding);
				}
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

			int body = mDisplayBody;
			commandBuffer->PushConstant(shader, "DisplayBody", &body);

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
		safe_delete(mTransferLUT);

		float4x4 orientation = float4x4(1);
		Texture* vol = nullptr;
		switch (type) {
		case IMAGE_STACK_STANDARD:
			vol = ImageLoader::LoadStandardStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		case IMAGE_STACK_DICOM:
			vol = ImageLoader::LoadDicomStack(folder, mScene->Instance()->Device(), &mVolumeScale, &orientation);
			mVolumeScale.z *= -1;
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

		float3 position;
		quaternion rotation;
		float3 scale;
		orientation.Decompose(&position, &rotation, &scale);
		quaternion extrarot = quaternion(PI / 2, float3(1, 0, 0));
		rotation *= extrarot;

		//printf_color(COLOR_BLUE, "rot: <%f, %f, %f, %f>, scale: <%f, %f, %f>\n", rotation.x, rotation.y, rotation.z, rotation.w, scale.x, scale.y, scale.z);

		mVolumeRotation = rotation;
		mVolumeScale *= scale;

		mRawVolume = vol;
		mRawVolumeNew = true;

		mRawMask = ImageLoader::LoadStandardStack(folder.string() + "/mask", mScene->Instance()->Device(), nullptr, true, 1, false);
		
		// TODO: only create the baked volume and gradient textures if there is enough VRAM (check device->MemoryUsage())

		mBakedVolume = new Texture("Volume", mScene->Instance()->Device(), nullptr, 0, mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R8G8B8A8_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		mBakeDirty = true;
	
		mGradient = new Texture("Gradient", mScene->Instance()->Device(), nullptr, 0, mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R8G8B8A8_SNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
		mGradientDirty = true;

		mTransferLUT = new Texture("Transfer LUT", mScene->Instance()->Device(), nullptr, 0, 4096, 2, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		mLUTDirty = true;

		mFrameIndex = 0;
	}
};

ENGINE_PLUGIN(DicomVis)