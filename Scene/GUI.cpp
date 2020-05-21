#include <Scene/GUI.hpp>
#include <Scene/Scene.hpp>

using namespace std;

#define START_DEPTH 0.01f
#define DEPTH_DELTA -0.001f

unordered_map<string, uint32_t> GUI::mHotControl;
unordered_map<string, uint32_t> GUI::mLastHotControl;
uint32_t GUI::mNextControlId = 10;
InputManager* GUI::mInputManager;
vector<Texture*> GUI::mTextureArray;
unordered_map<Texture*, uint32_t> GUI::mTextureMap;
vector<GUI::GuiRect> GUI::mScreenRects;
vector<GUI::GuiRect> GUI::mScreenTextureRects;
vector<GUI::GuiShader> GUI::mScreenShaderRects;
vector<GUI::GuiRect> GUI::mWorldRects;
vector<GUI::GuiRect> GUI::mWorldTextureRects;
vector<GUI::GuiShader> GUI::mWorldShaderRects;
vector<GUI::GuiLine> GUI::mScreenLines;
vector<float2> GUI::mLinePoints;
vector<GUI::GuiString> GUI::mScreenStrings;
vector<GUI::GuiString> GUI::mWorldStrings;
unordered_map<uint32_t, std::variant<float, std::string>> GUI::mControlData;
stack<GUI::GuiLayout> GUI::mLayoutStack;

GUI::BufferCache* GUI::mCaches;

GUI::LayoutTheme GUI::mLayoutTheme;

void GUI::Initialize(Device* device, AssetManager* assetManager, InputManager* inputManager) {
	mCaches = new BufferCache[device->MaxFramesInFlight()];
	mInputManager = inputManager;
	mNextControlId = 10;

	mLayoutTheme.mBackgroundColor = float4(.3f, .3f, .3f, 1.f);
	mLayoutTheme.mLabelBackgroundColor = 0;
	mLayoutTheme.mControlBackgroundColor = float4(.2f, .2f, .2f, 1.f);
	mLayoutTheme.mControlForegroundColor = 1;
}
void GUI::Destroy(Device* device){
	for (uint32_t i = 0; i < device->MaxFramesInFlight(); i++){
		for (auto& j : mCaches[i].mGlyphCache)
			safe_delete(j.second.first);
		for (auto& j : mCaches[i].mGlyphBufferCache)
			safe_delete(j.first);
	}
	safe_delete_array(mCaches);
}

void GUI::Draw(CommandBuffer* commandBuffer, PassType pass, Camera* camera) {
	BufferCache& bc = mCaches[commandBuffer->Device()->FrameContextIndex()];
	
	if (mWorldRects.size()) {
		camera->Set(commandBuffer);
		GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, {});
		if (!shader) return;
		VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);
		if (!layout) return;

		Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("WorldRects", mWorldRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		memcpy(screenRects->MappedData(), mWorldRects.data(), mWorldRects.size() * sizeof(GuiRect));

		DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("WorldRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
		ds->CreateStorageBufferDescriptor(screenRects, 0, mWorldRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

		camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
		vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldRects.size(), 0, 0);

		if (camera->StereoMode() != STEREO_NONE) {
			camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
			vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldRects.size(), 0, 0);
		}
	}
	if (mWorldTextureRects.size()) {
		GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "TEXTURED" });
		if (!shader) return;
		VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);
		if (!layout) return;

		Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("WorldRects", mWorldTextureRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		memcpy(screenRects->MappedData(), mWorldTextureRects.data(), mWorldTextureRects.size() * sizeof(GuiRect));

		DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("WorldRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
		ds->CreateStorageBufferDescriptor(screenRects, 0, mWorldTextureRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
		for (uint32_t i = 0; i < mTextureArray.size(); i++)
			ds->CreateSampledTextureDescriptor(mTextureArray[i], i, shader->mDescriptorBindings.at("Textures").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

		camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
		vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldTextureRects.size(), 0, 0);

		if (camera->StereoMode() != STEREO_NONE) {
			camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
			vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldTextureRects.size(), 0, 0);
		}
	}
	if (mWorldShaderRects.size()) {
		for (GUI::GuiShader info : mWorldShaderRects) {
			camera->Set(commandBuffer);
			GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader(info.path)->GetGraphics(pass, info.keywords);
			if (!shader) return;
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);
			if (!layout) return;

			Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("WorldRects", info.rects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(screenRects->MappedData(), info.rects.data(), info.rects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("WorldRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(screenRects, 0, info.rects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
			vkCmdDraw(*commandBuffer, 6, (uint32_t)info.rects.size(), 0, 0);

			if (camera->StereoMode() != STEREO_NONE) {
				camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
				vkCmdDraw(*commandBuffer, 6, (uint32_t)info.rects.size(), 0, 0);
			}
		}
	}
	if (mWorldStrings.size()) {
		GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/font.stm")->GetGraphics(PASS_MAIN, {});
		if (!shader) return;
		VkPipelineLayout layout = commandBuffer->BindShader(shader, PASS_MAIN, nullptr, camera);
		if (!layout) return;

		Buffer* transforms = commandBuffer->Device()->GetTempBuffer("Transforms", sizeof(float4x4) * mWorldStrings.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		float4x4* m = (float4x4*)transforms->MappedData();
		for (const GuiString& s : mWorldStrings) {
			*m = s.mTransform;
			m++;
		}
		uint32_t idx = 0;

		for (const GuiString& s : mWorldStrings) {
			Buffer* glyphBuffer = nullptr;
			char hashstr[256];
			sprintf(hashstr, "%s%f%d%d", s.mString.c_str(), s.mScale, s.mHorizontalAnchor, s.mVerticalAnchor);
			size_t key = 0;
			hash_combine(key, s.mFont);
			hash_combine(key, string(hashstr));
			if (bc.mGlyphCache.count(key)) {
				auto& b = bc.mGlyphCache.at(key);
				b.second = 8;
				glyphBuffer = b.first;
			} else {
				vector<TextGlyph> glyphs(s.mString.length());
				uint32_t glyphCount = s.mFont->GenerateGlyphs(s.mString, s.mScale, nullptr, glyphs, s.mHorizontalAnchor, s.mVerticalAnchor);
				if (glyphCount == 0) { idx++; return; }
				
				for (auto it = bc.mGlyphBufferCache.begin(); it != bc.mGlyphBufferCache.end();) {
					if (it->first->Size() == glyphCount * sizeof(TextGlyph)) {
						glyphBuffer = it->first;
						bc.mGlyphBufferCache.erase(it);
						break;
					}
					it++;
				}
				if (!glyphBuffer)
					glyphBuffer = new Buffer("Glyph Buffer", commandBuffer->Device(), glyphCount * sizeof(TextGlyph), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
				
				glyphBuffer->Upload(glyphs.data(), glyphCount * sizeof(TextGlyph));
				
				bc.mGlyphCache.emplace(key, make_pair(glyphBuffer, 8u));
			}

			DescriptorSet* descriptorSet = commandBuffer->Device()->GetTempDescriptorSet(s.mFont->mName + " DescriptorSet", shader->mDescriptorSetLayouts[PER_OBJECT]);
			descriptorSet->CreateSampledTextureDescriptor(s.mFont->Texture(), BINDING_START + 0);
			descriptorSet->CreateStorageBufferDescriptor(transforms, 0, transforms->Size(), BINDING_START + 1);
			descriptorSet->CreateStorageBufferDescriptor(glyphBuffer, 0, glyphBuffer->Size(), BINDING_START + 2);
			descriptorSet->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *descriptorSet, 0, nullptr);
			commandBuffer->PushConstant(shader, "Color", &s.mColor);
			commandBuffer->PushConstant(shader, "Offset", &s.mOffset);
			commandBuffer->PushConstant(shader, "Bounds", &s.mBounds);
			commandBuffer->PushConstant(shader, "Depth", &s.mDepth);

			camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
			vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, idx);

			if (camera->StereoMode() != STEREO_NONE) {
				camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
				vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, idx);
			}

			idx++;
		}
	}

	if (camera->StereoMode() == STEREO_NONE) {
		camera->Set(commandBuffer);
		if (mScreenRects.size()) {
			GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "SCREEN_SPACE" });
			if (!shader) return;
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr);
			if (!layout) return;

			Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("ScreenRects", mScreenRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(screenRects->MappedData(), mScreenRects.data(), mScreenRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("ScreenRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(screenRects, 0, mScreenRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
			commandBuffer->PushConstant(shader, "ScreenSize", &s);

			vkCmdDraw(*commandBuffer, 6, (uint32_t)mScreenRects.size(), 0, 0);
		}
		if (mScreenTextureRects.size()) {
			GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "SCREEN_SPACE", "TEXTURED" });
			if (!shader) return;
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr);
			if (!layout) return;

			Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("ScreenRects", mScreenTextureRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(screenRects->MappedData(), mScreenTextureRects.data(), mScreenTextureRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("ScreenRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(screenRects, 0, mScreenTextureRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
			for (uint32_t i = 0; i < mTextureArray.size(); i++)
				ds->CreateSampledTextureDescriptor(mTextureArray[i], i, shader->mDescriptorBindings.at("Textures").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
			commandBuffer->PushConstant(shader, "ScreenSize", &s);

			vkCmdDraw(*commandBuffer, 6, (uint32_t)mScreenTextureRects.size(), 0, 0);
		}
		if (mScreenShaderRects.size()) {
			for (GUI::GuiShader info : mScreenShaderRects) {
				info.keywords.insert("SCREEN_SPACE");
				camera->Set(commandBuffer);
				GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader(info.path)->GetGraphics(pass, info.keywords);
				if (!shader) return;
				VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);
				if (!layout) return;

				Buffer* screenRects = commandBuffer->Device()->GetTempBuffer("ScreenRects", info.rects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
				memcpy(screenRects->MappedData(), info.rects.data(), info.rects.size() * sizeof(GuiRect));

				DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("ScreenRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
				ds->CreateStorageBufferDescriptor(screenRects, 0, info.rects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
				ds->FlushWrites();
				vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

				float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
				commandBuffer->PushConstant(shader, "ScreenSize", &s);

				vkCmdDraw(*commandBuffer, 6, (uint32_t)info.rects.size(), 0, 0);
			}
		}
		if (mScreenStrings.size()) {
			GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/font.stm")->GetGraphics(PASS_MAIN, { "SCREEN_SPACE" });
			if (!shader) return;
			VkPipelineLayout layout = commandBuffer->BindShader(shader, PASS_MAIN, nullptr);
			if (!layout) return;
			float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
			commandBuffer->PushConstant(shader, "ScreenSize", &s);

			for (const GuiString& s : mScreenStrings) {
				Buffer* glyphBuffer = nullptr;
				char hashstr[256];
				sprintf(hashstr, "%s%f%d%d", s.mString.c_str(), s.mScale, s.mHorizontalAnchor, s.mVerticalAnchor);
				size_t key = 0;
				hash_combine(key, s.mFont);
				hash_combine(key, string(hashstr));
				if (bc.mGlyphCache.count(key)) {
					auto& b = bc.mGlyphCache.at(key);
					b.second = 8u;
					glyphBuffer = b.first;
				} else {
					vector<TextGlyph> glyphs(s.mString.length());
					uint32_t glyphCount = s.mFont->GenerateGlyphs(s.mString, s.mScale, nullptr, glyphs, s.mHorizontalAnchor, s.mVerticalAnchor);
					if (glyphCount == 0) return;

					for (auto it = bc.mGlyphBufferCache.begin(); it != bc.mGlyphBufferCache.end();) {
						if (it->first->Size() == glyphCount * sizeof(TextGlyph)) {
							glyphBuffer = it->first;
							bc.mGlyphBufferCache.erase(it);
							break;
						}
						it++;
					}
					if (!glyphBuffer)
						glyphBuffer = new Buffer("Glyph Buffer", commandBuffer->Device(), glyphCount * sizeof(TextGlyph), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					
					glyphBuffer->Upload(glyphs.data(), glyphCount * sizeof(TextGlyph));
					bc.mGlyphCache.emplace(key, make_pair(glyphBuffer, 8u));
				}

				DescriptorSet* descriptorSet = commandBuffer->Device()->GetTempDescriptorSet(s.mFont->mName + " DescriptorSet", shader->mDescriptorSetLayouts[PER_OBJECT]);
				descriptorSet->CreateSampledTextureDescriptor(s.mFont->Texture(), BINDING_START + 0);
				descriptorSet->CreateStorageBufferDescriptor(glyphBuffer, 0, glyphBuffer->Size(), BINDING_START + 2);
				descriptorSet->FlushWrites();
				vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *descriptorSet, 0, nullptr);

				commandBuffer->PushConstant(shader, "Color", &s.mColor);
				commandBuffer->PushConstant(shader, "Offset", &s.mOffset);
				commandBuffer->PushConstant(shader, "Bounds", &s.mBounds);
				commandBuffer->PushConstant(shader, "Depth", &s.mDepth);
				vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, 0);
			}
		}

		if (mScreenLines.size()) {
			GraphicsShader* shader = camera->Scene()->AssetManager()->LoadShader("Shaders/line.stm")->GetGraphics(PASS_MAIN, { "SCREEN_SPACE" });
			if (!shader) return;
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, nullptr, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
			if (!layout) return;

			Buffer* b = commandBuffer->Device()->GetTempBuffer("Perf Graph Pts", sizeof(float2) * mLinePoints.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(b->MappedData(), mLinePoints.data(), sizeof(float2) * mLinePoints.size());
			
			DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("Perf Graph DS", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(b, 0, sizeof(float2) * mLinePoints.size(), INSTANCE_BUFFER_BINDING);
			ds->FlushWrites();

			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			float4 sz(0, 0, camera->FramebufferWidth(), camera->FramebufferHeight());
			commandBuffer->PushConstant(shader, "ScreenSize", &sz.z);

			for (const GuiLine& l : mScreenLines) {
				vkCmdSetLineWidth(*commandBuffer, l.mThickness);
				commandBuffer->PushConstant(shader, "Color", &l.mColor);
				commandBuffer->PushConstant(shader, "ScaleTranslate", &l.mScaleTranslate);
				commandBuffer->PushConstant(shader, "Bounds", &l.mBounds);
				commandBuffer->PushConstant(shader, "Depth", &l.mDepth);
				vkCmdDraw(*commandBuffer, l.mCount, 1, l.mIndex, 0);
			}
		}
	}
	
	mWorldRects.clear();
	mWorldTextureRects.clear();
	mWorldShaderRects.clear();
	mWorldStrings.clear();

	mScreenRects.clear();
	mScreenTextureRects.clear();
	mScreenShaderRects.clear();
	mScreenStrings.clear();
	mScreenLines.clear();
	mLinePoints.clear();

	while (mLayoutStack.size()) mLayoutStack.pop();
}
void GUI::PreFrame(CommandBuffer* commandBuffer) {
	mNextControlId = 10;
	mLastHotControl = mHotControl;
	mHotControl.clear();

	mTextureArray.clear();
	mTextureMap.clear();

	mWorldRects.clear();
	mWorldTextureRects.clear();
	mWorldStrings.clear();

	mScreenRects.clear();
	mScreenTextureRects.clear();
	mScreenStrings.clear();
	mScreenLines.clear();
	mLinePoints.clear();

	mLayoutTheme.mBackgroundColor = float4(.3f, .3f, .3f, 1.f);
	mLayoutTheme.mLabelBackgroundColor = 0;
	mLayoutTheme.mControlBackgroundColor = float4(.2f, .2f, .2f, 1.f);
	mLayoutTheme.mControlForegroundColor = 1;

	BufferCache& c = mCaches[commandBuffer->Device()->FrameContextIndex()];

	for (auto it = c.mGlyphBufferCache.begin(); it != c.mGlyphBufferCache.end();) {
		if (it->second == 1) {
			safe_delete(it->first);
			it = c.mGlyphBufferCache.erase(it);
		} else {
			it->second--;
			it++;
		}
	}

	for (auto it = c.mGlyphCache.begin(); it != c.mGlyphCache.end();) {
		if (it->second.second == 1) {
			c.mGlyphBufferCache.push_back(make_pair(it->second.first, 8u));
			it = c.mGlyphCache.erase(it);
		} else {
			it->second.second--;
			it++;
		}
	}
}

void GUI::DrawScreenLine(const float2* points, size_t pointCount, float thickness, const float2& offset, const float2& scale, const float4& color, float z) {
	GuiLine l;
	l.mColor = color;
	l.mScaleTranslate = float4(scale, offset);
	l.mBounds = fRect2D(0, 0, 1e10f, 1e10f);
	l.mCount = pointCount;
	l.mIndex = mLinePoints.size();
	l.mThickness = thickness;
	l.mDepth = z;
	mScreenLines.push_back(l);

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(float2));
}

void GUI::DrawString(Font* font, const string& str, const float4& color, const float2& screenPos, float scale, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect) {
	if (str.length() == 0) return;
	GuiString s;
	s.mFont = font;
	s.mString = str;
	s.mColor = color;
	s.mOffset = screenPos;
	s.mScale = scale;
	s.mVerticalAnchor = verticalAnchor;
	s.mHorizontalAnchor = horizontalAnchor;
	s.mBounds = clipRect;
	s.mDepth = z;
	mScreenStrings.push_back(s);
}
void GUI::DrawString(Font* font, const string& str, const float4& color, const float4x4& objectToWorld, const float2& offset, float scale, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect) {
	if (str.length() == 0) return;
	GuiString s;
	s.mTransform = objectToWorld;
	s.mFont = font;
	s.mString = str;
	s.mColor = color;
	s.mOffset = offset;
	s.mScale = scale;
	s.mVerticalAnchor = verticalAnchor;
	s.mHorizontalAnchor = horizontalAnchor;
	s.mBounds = clipRect;
	mWorldStrings.push_back(s);
}

void GUI::Rect(const fRect2D& screenRect, const float4& color, Texture* texture, const float4& textureST, float z, const fRect2D& clipRect) {
	if (!clipRect.Intersects(screenRect)) return;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		if (screenRect.Contains(c) && clipRect.Contains(c)) i->mMousePointer.mGuiHitT = 0.f;
	}

	GuiRect r = {};
	r.ScaleTranslate = float4(screenRect.mExtent, screenRect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.Depth = z;
	r.TextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.TextureIndex = mTextureMap.at(texture);
		else {
			r.TextureIndex = mTextureArray.size();
			mTextureMap.emplace(texture, mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mScreenTextureRects.push_back(r);
	} else
		mScreenRects.push_back(r);
}
void GUI::Rect(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	if (!clipRect.Intersects(rect)) return;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c)) const_cast<InputPointer*>(p)->mGuiHitT = t;
		}
	}


	GuiRect r = {};
	r.ObjectToWorld = transform;
	r.ScaleTranslate = float4(rect.mExtent, rect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.TextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.TextureIndex = mTextureMap.at(texture);
		else {
			r.TextureIndex = mTextureArray.size();
			mTextureMap.emplace(texture, mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mWorldTextureRects.push_back(r);
	} else
		mWorldRects.push_back(r);
}

void GUI::ShaderRect(const fRect2D& screenRect, const float4& color, std::string path, std::set<std::string> keywords, const float4& textureST, float z, const fRect2D& clipRect) {
	if (!clipRect.Intersects(screenRect)) return;

	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 c = i->CursorPos();
	c.y = i->WindowHeight() - c.y;
	if (screenRect.Contains(c) && clipRect.Contains(c)) i->mMousePointer.mGuiHitT = 0.f;

	GuiRect r = {};
	r.ScaleTranslate = float4(screenRect.mExtent, screenRect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.Depth = z;
	r.TextureST = textureST;

	//Check to see if same pipeline is being used for other rect
	std::vector<GuiShader>::iterator it = std::find_if(mScreenShaderRects.begin(), mScreenShaderRects.end(), [path, keywords](GuiShader s) {
		if (path.compare(s.path) != 0) {
			return false;
		}
		for (std::string key : s.keywords) {
			if (!keywords.count(key)) {
				return false;
			}
		}
		for (std::string key : keywords) {
			if (!s.keywords.count(key)) {
				return false;
			}
		}
		return true;
	});

	//If same shader being used, push into shader's rect list. if not, create new shader
	if (it != mScreenShaderRects.end()) {
		it->rects.push_back(r);
	}
	else {
		GuiShader s = { path, keywords, {} };
		s.rects.push_back(r);
		mScreenShaderRects.push_back(s);
	}
}
void GUI::ShaderRect(const float4x4& transform, const fRect2D& rect, const float4& color, std::string path, std::set<std::string> keywords, const float4& textureST, const fRect2D& clipRect) {
	if (!clipRect.Intersects(rect)) return;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c)) const_cast<InputPointer*>(p)->mGuiHitT = t;
		}
	}


	GuiRect r = {};
	r.ObjectToWorld = transform;
	r.ScaleTranslate = float4(rect.mExtent, rect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.TextureST = textureST;

	//Check to see if same pipeline is being used for other rect
	std::vector<GuiShader>::iterator it = std::find_if(mWorldShaderRects.begin(), mWorldShaderRects.end(), [path, keywords](GuiShader s) {
		if (path.compare(s.path) != 0) {
			return false;
		}
		for (std::string key : s.keywords) {
			if (!keywords.count(key)) {
				return false;
			}
		}
		for (std::string key : keywords) {
			if (!s.keywords.count(key)) {
				return false;
			}
		}
		return true;
		});

	//If same shader being used, push into shader's rect list. if not, create new shader
	if (it != mWorldShaderRects.end()) {
		it->rects.push_back(r);
	}
	else {
		GuiShader s = { path, keywords, {} };
		s.rects.push_back(r);
		mWorldShaderRects.push_back(s);
	}
}

void GUI::Label(Font* font, const string& text, float textScale, const fRect2D& screenRect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect){
	if (!clipRect.Intersects(screenRect)) return;
	if (color.a > 0) Rect(screenRect, color, nullptr, 0, z, clipRect);
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = screenRect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = screenRect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = screenRect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = screenRect.mExtent.y;
		DrawString(font, text, textColor, screenRect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, z + DEPTH_DELTA, clipRect);
	}
}
void GUI::Label(Font* font, const string& text, float textScale, const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect){
	if (!clipRect.Intersects(rect)) return;
	if (color.a > 0) Rect(transform,rect, color, nullptr, 0, clipRect);
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = rect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = rect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = rect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = rect.mExtent.y;
		DrawString(font, text, textColor, transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), rect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, clipRect);
	}
}

bool GUI::TextButton(Font* font, const string& text, float textScale, const fRect2D& screenRect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect){
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	bool hvr = false;
	bool clk = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hvr = screenRect.Contains(c) && clipRect.Contains(c);
		clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hvr || clk) i->mMousePointer.mGuiHitT = 0.f;
		if (clk) mHotControl[p->mName] = controlId;
		ret = hvr && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	if (color.a > 0) {
		float m = 1.f;
		if (hvr) m = 1.2f;
		if (clk) m = 1.5f;
		Rect(screenRect, float4(color.rgb * m, color.a), nullptr, 0, z, clipRect);
	}
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = screenRect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = screenRect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = screenRect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = screenRect.mExtent.y;
		DrawString(font, text, textColor, screenRect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, z + DEPTH_DELTA, clipRect);
	}
	return ret;
}
bool GUI::TextButton(Font* font, const string& text, float textScale, const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect){
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool hover = false;
	bool click = false;
	bool first = false;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);
			
			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			bool hvr = rect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				click = true;
				mHotControl[p->mName] = controlId;
				if (p->mPrimaryButton && !device->GetPointerLast(i)->mPrimaryButton) first = true;
			}
		}
	}


	if (color.a > 0) {
		float m = 1.f;
		if (hover) m = 1.2f;
		if (click) m = 1.5f;
		Rect(transform, rect, float4(color.rgb * m, color.a), nullptr, 0, clipRect);
	}
	if (textColor.a > 0 && text.length()) {
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = rect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = rect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = rect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = rect.mExtent.y;
		DrawString(font, text, textColor, transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), rect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, clipRect);
	}
	return hover && first;
}

#pragma region HSVUtil
float3 HSVToRGB(float3 hsv) {
	float H = 360 * hsv.r;
	float S = hsv.y;
	float V = hsv.z;

	float C = S * V;
	float X = C * (1 - abs(fmod(H / 60.0, 2) - 1));
	float m = V - C;
	float Rs, Gs, Bs;

	if (H >= 0 && H < 60) {
		Rs = C;
		Gs = X;
		Bs = 0;
	}
	else if (H >= 60 && H < 120) {
		Rs = X;
		Gs = C;
		Bs = 0;
	}
	else if (H >= 120 && H < 180) {
		Rs = 0;
		Gs = C;
		Bs = X;
	}
	else if (H >= 180 && H < 240) {
		Rs = 0;
		Gs = X;
		Bs = C;
	}
	else if (H >= 240 && H < 300) {
		Rs = X;
		Gs = 0;
		Bs = C;
	}
	else {
		Rs = C;
		Gs = 0;
		Bs = X;
	}

	return float3(Rs, Gs, Bs) + float3(m);
}

float3 RGBToHSV(float3 rgb) {
	float fCMax = std::max(std::max(rgb.r, rgb.g), rgb.b);
	float fCMin = std::min(std::min(rgb.r, rgb.g), rgb.b);
	float fDelta = fCMax - fCMin;
	float H, S, V;
	if (fDelta > 0) {
		if (fCMax == rgb.r) {
			H = 60 * (fmod(((rgb.g - rgb.b) / fDelta), 6));
		}
		else if (fCMax == rgb.g) {
			H = 60 * (((rgb.b - rgb.r) / fDelta) + 2);
		}
		else if (fCMax == rgb.b) {
			H = 60 * (((rgb.r - rgb.g) / fDelta) + 4);
		}

		if (fCMax > 0) {
			S = fDelta / fCMax;
		}
		else {
			S = 0;
		}

		V = fCMax;
	}
	else {
		H = 0;
		S = 0;
		V = fCMax;
	}

	if (H < 0) {
		H = 360 + H;
	}

	return float3(H / 360, S, V);
}
#pragma endregion

bool GUI::ColorPicker(float3& rgb, const fRect2D& screenRect, float seperation, float z, const fRect2D& clipRect) {
	float3 hsl = RGBToHSV(rgb);

	uint32_t SVcontrolId = mNextControlId++;
	uint32_t HcontrolId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 c = i->CursorPos();
	c.y = i->WindowHeight() - c.y;
	const InputPointer* p = i->GetPointer(0);

	fRect2D SVrect = screenRect;
	fRect2D Hrect = screenRect;

	float nonpadding = screenRect.mExtent.x - seperation;

	SVrect.mExtent.x = nonpadding * 0.8;
	float startpad = min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f;
	SVrect.mExtent.x -= startpad / 2.0;
	SVrect.mOffset.x += startpad / 2.0;

	Hrect.mOffset.x += seperation + nonpadding * 0.8;
	Hrect.mExtent.x = nonpadding * 0.2;

	bool SVhvr = SVrect.Contains(c) && clipRect.Contains(c);
	bool Hhvr = Hrect.Contains(c) && clipRect.Contains(c);

	bool SVclk = (SVhvr && i->KeyDownFirst(MOUSE_LEFT)) || (i->KeyDown(MOUSE_LEFT) && mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == SVcontrolId);
	bool Hclk = (Hhvr && i->KeyDownFirst(MOUSE_LEFT)) || (i->KeyDown(MOUSE_LEFT) && mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == HcontrolId);

	if (SVhvr || SVclk || Hhvr || Hclk) i->mMousePointer.mGuiHitT = 0.f;
	if (SVclk) mHotControl[p->mName] = SVcontrolId;
	if (Hclk) mHotControl[p->mName] = HcontrolId;

	if (SVclk) {
		float2 xy = (c - SVrect.mOffset) / SVrect.mExtent;

		hsl.y = clamp(xy.x, 0.f, 1.f);
		hsl.z = clamp(xy.y, 0.f, 1.f);
	}
	if (Hclk) {
		float2 xy = (c - Hrect.mOffset) / Hrect.mExtent;

		hsl.x = clamp(xy.y, 0.f, 1.f);
	}


	fRect2D SVindicatorrect = screenRect;
	fRect2D Hindicatorrect = screenRect;

	SVindicatorrect.mExtent = float2(min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f);
	SVindicatorrect.mOffset = SVrect.mOffset + float2(hsl.y, hsl.z) * SVrect.mExtent - SVindicatorrect.mExtent / 2.f;

	Hindicatorrect.mExtent.y = min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f;
	Hindicatorrect.mExtent.x = Hrect.mExtent.x;
	Hindicatorrect.mOffset.x = Hrect.mOffset.x;
	Hindicatorrect.mOffset.y = Hrect.mOffset.y + hsl.x * Hrect.mExtent.y - Hindicatorrect.mExtent.y / 2.f;

	fRect2D SVindicatorcol = SVindicatorrect;
	fRect2D Hindicatorcol = Hindicatorrect;

	SVindicatorcol.mExtent -= float2(Hindicatorrect.mExtent.y * 0.2);
	SVindicatorcol.mOffset += float2(Hindicatorrect.mExtent.y * 0.1);

	Hindicatorcol.mExtent -= float2(Hindicatorrect.mExtent.y * 0.2);
	Hindicatorcol.mOffset += float2(Hindicatorrect.mExtent.y * 0.1);


	ShaderRect(SVrect, float4(hsl, 1), "Shaders/colorpicker.stm", {}, float4(1,1,0,0), z, clipRect);
	ShaderRect(Hrect, float4(hsl, 1), "Shaders/colorpicker.stm", { "HUE" }, float4(1, 1, 0, 0), z, clipRect);
	//Rect(SVrect, float4(1, 0, 0, 1), nullptr, 0, z, clipRect);
	//Rect(Hrect, float4(0, 1, 0, 1), nullptr, 0, z, clipRect);

	Rect(SVindicatorrect, float4(0.1, 0.1, 0.1, 1), nullptr, 0, z + DEPTH_DELTA, clipRect);
	Rect(Hindicatorrect, float4(0.1, 0.1, 0.1, 1), nullptr, 0, z + DEPTH_DELTA, clipRect);

	rgb = HSVToRGB(hsl);
	Rect(SVindicatorcol, float4(rgb, 1), nullptr, 0, z + DEPTH_DELTA, clipRect);
	Rect(Hindicatorcol, float4(rgb, 1), nullptr, 0, z + DEPTH_DELTA, clipRect);
}

bool GUI::ColorPicker(float3& rgb, const float4x4& transform, const fRect2D& screenRect, float seperation, const fRect2D& clipRect) {
	float3 hsl = RGBToHSV(rgb);

	uint32_t SVcontrolId = mNextControlId++;
	uint32_t HcontrolId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	fRect2D SVrect = screenRect;
	fRect2D Hrect = screenRect;

	float nonpadding = screenRect.mExtent.x - seperation;

	SVrect.mExtent.x = nonpadding * 0.8;
	float startpad = min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f;
	SVrect.mExtent.x -= startpad / 2.0;
	SVrect.mOffset.x += startpad / 2.0;

	Hrect.mOffset.x += seperation + nonpadding * 0.8;
	Hrect.mExtent.x = nonpadding * 0.2;

	float2 c;
	bool SVhvr = false;
	bool Hhvr = false;

	bool SVclk = false;
	bool Hclk = false;


	float4x4 invTransform = inverse(transform);

	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			c = (ray.mOrigin + ray.mDirection * t).xy;
			bool hvr = SVrect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == SVcontrolId));

			if (hvr || clk) {
				SVhvr = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				mHotControl[p->mName] = SVcontrolId;
				SVclk = true;
			}

			hvr = Hrect.Contains(c) && clipRect.Contains(c);
			clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == HcontrolId));

			if (hvr || clk) {
				Hhvr = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				mHotControl[p->mName] = HcontrolId;
				Hclk = true;
			}

		}


	if (SVclk) {
		float2 xy = (c - SVrect.mOffset) / SVrect.mExtent;

		hsl.y = clamp(xy.x, 0.f, 1.f);
		hsl.z = clamp(xy.y, 0.f, 1.f);
	}
	if (Hclk) {
		float2 xy = (c - Hrect.mOffset) / Hrect.mExtent;
		hsl.x = clamp(xy.y, 0.f, 0.99999f);
	}

	fRect2D SVindicatorrect = screenRect;
	fRect2D Hindicatorrect = screenRect;

	SVindicatorrect.mExtent = float2(min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f);
	SVindicatorrect.mOffset = SVrect.mOffset + float2(hsl.y, hsl.z) * SVrect.mExtent - SVindicatorrect.mExtent / 2.f;

	Hindicatorrect.mExtent.y = min(SVrect.mExtent.x, SVrect.mExtent.y) / 20.f;
	Hindicatorrect.mExtent.x = Hrect.mExtent.x;
	Hindicatorrect.mOffset.x = Hrect.mOffset.x;
	Hindicatorrect.mOffset.y = Hrect.mOffset.y + hsl.x * Hrect.mExtent.y - Hindicatorrect.mExtent.y / 2.f;

	fRect2D SVindicatorcol = SVindicatorrect;
	fRect2D Hindicatorcol = Hindicatorrect;

	SVindicatorcol.mExtent -= float2(Hindicatorrect.mExtent.y * 0.2);
	SVindicatorcol.mOffset += float2(Hindicatorrect.mExtent.y * 0.1);

	Hindicatorcol.mExtent -= float2(Hindicatorrect.mExtent.y * 0.2);
	Hindicatorcol.mOffset += float2(Hindicatorrect.mExtent.y * 0.1);

	float4x4 newtrans = float4x4(transform);
	float3 pos;
	quaternion rot;
	float3 scale;
	newtrans.Decompose(&pos, &rot, &scale);
	float3 norm = rot * float3(0, 0, 1);
	pos -= norm * 0.0001;
	newtrans = float4x4::TRS(pos, rot, scale);

	ShaderRect(transform, SVrect, float4(hsl, 1), "Shaders/colorpicker.stm", {}, float4(1, 1, 0, 0), clipRect);
	ShaderRect(transform, Hrect, float4(hsl, 1), "Shaders/colorpicker.stm", { "HUE" }, float4(1, 1, 0, 0), clipRect);

	Rect(newtrans, SVindicatorrect, float4(0.1, 0.1, 0.1, 1), nullptr, 0, clipRect);
	Rect(newtrans, Hindicatorrect, float4(0.1, 0.1, 0.1, 1), nullptr, 0, clipRect);

	rgb = HSVToRGB(hsl);
	Rect(newtrans, SVindicatorcol, float4(rgb, 1), nullptr, 0, clipRect);
	Rect(newtrans, Hindicatorcol, float4(rgb, 1), nullptr, 0, clipRect);
}

bool GUI::ImageButton(const fRect2D& screenRect, const float4& color, Texture* texture, const float4& textureST, float z, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	bool hvr = false;
	bool clk = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hvr = screenRect.Contains(c) && clipRect.Contains(c);
		clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hvr || clk) i->mMousePointer.mGuiHitT = 0.f;
		if (clk) mHotControl[p->mName] = controlId;
		ret = hvr && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	if (color.a > 0) {
		float m = 1.f;
		if (hvr) m = 1.2f;
		if (clk) m = 1.5f;
		Rect(screenRect, float4(color.rgb * m, color.a), texture, textureST, z, clipRect);
	}
	return ret;
}
bool GUI::ImageButton(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool hover = false;
	bool click = false;
	bool first = false;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			bool hvr = rect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				click = true;
				mHotControl[p->mName] = controlId;
				if (p->mPrimaryButton && !device->GetPointerLast(i)->mPrimaryButton) first = true;
			}
		}
	}

	if (color.a > 0) {
		float m = 1.f;
		if (hover) m = 1.2f;
		if (click) m = 1.5f;
		Rect(transform, rect, float4(color.rgb * m, color.a), texture, textureST, clipRect);
	}
	return hover && first;
}

bool GUI::Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect, const float4& barColor, const float4& knobColor, float z, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	bool ret = false;

	fRect2D barRect = screenRect;
	fRect2D knobRect = screenRect;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Offset bar by knob size to allow knob to be centered on endpoints
	barRect.mOffset[scrollAxis] += knobSize * .5f;
	barRect.mExtent[scrollAxis] -= knobSize;
	// Pad the bar on the "other axis"
	barRect.mOffset[otherAxis] += screenRect.mExtent[otherAxis] * .125f;
	barRect.mExtent[otherAxis] *= 0.75f;

	// Determine knob position
	knobRect.mExtent[scrollAxis] = knobSize;
	float pos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify position from input
	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 c = i->CursorPos();
	c.y = i->WindowHeight() - c.y;
	const InputPointer* p = i->GetPointer(0);
	if (screenRect.Contains(c) && clipRect.Contains(c))
		pos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .025f;
	if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId) {
		pos += c[scrollAxis] - i->LastCursorPos()[scrollAxis];
		ret = true;
	}

	// Derive modified value from modified position
	value = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	value = clamp(value, minimum, maximum);

	// Derive final knob position from the modified, clamped value
	knobRect.mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRect.mOffset[scrollAxis] -= knobSize * .5f;

	bool hvr = knobRect.Contains(c) && clipRect.Contains(c);
	bool clk = i->KeyDown(MOUSE_LEFT) && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

	if (hvr || clk) i->mMousePointer.mGuiHitT = 0.f;
	if (clk) mHotControl[p->mName] = controlId;

	float m = 1.25f;
	if (hvr) m *= 1.2f;
	if (clk) m *= 1.5f;

	Rect(barRect, barColor, nullptr, 0, z, clipRect);
	Rect(knobRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, z + DEPTH_DELTA, clipRect);

	return ret;
}
bool GUI::Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;
	fRect2D knobRect = rect;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Offset bar by knob size to allow knob to be centered on endpoints
	barRect.mOffset[scrollAxis] += knobSize * .5f;
	barRect.mExtent[scrollAxis] -= knobSize;
	// Pad the bar on the "other axis"
	barRect.mOffset[otherAxis] += rect.mExtent[otherAxis] * .25f;
	barRect.mExtent[otherAxis] *= 0.5f;

	// Determine knob position
	knobRect.mExtent[scrollAxis] = knobSize;
	float pos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify kob position from input
	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c))
				pos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;

			if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId) {
				Ray ray = p->mWorldRay;
				Ray lastRay = device->GetPointerLast(i)->mWorldRay;
				ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
				ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;
				lastRay.mOrigin = (invTransform * float4(lastRay.mOrigin, 1)).xyz;
				lastRay.mDirection = (invTransform * float4(lastRay.mDirection, 0)).xyz;
				float c = (ray.mOrigin + ray.mDirection * ray.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
				float lc = (lastRay.mOrigin + lastRay.mDirection * lastRay.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
				pos += c - lc;
				ret = true;
			}
		}

	// Derive modified value from modified position
	value = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	value = clamp(value, minimum, maximum);

	// Derive final knob position from the modified, clamped value
	knobRect.mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRect.mOffset[scrollAxis] -= knobSize * .5f;

	bool hover = false;
	bool click = false;

	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			bool hvr = knobRect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				mHotControl[p->mName] = controlId;
				click = true;
			}
		}

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(transform, barRect, barColor, nullptr, 0, clipRect);
	Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);

	return ret;
}

bool GUI::RangeSlider(float2& valueRange, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect, const float4& barColor, const float4& knobColor, float z, const fRect2D& clipRect) {
	uint32_t controlIds[3] = { mNextControlId++, mNextControlId++, mNextControlId++ };
	if (!clipRect.Intersects(screenRect)) return false;

	bool ret = false;

	fRect2D barRect = screenRect;
	fRect2D knobRects[2] = { screenRect, screenRect };

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Offset bar by knob size to allow knobs to be centered on endpoints
	barRect.mOffset[scrollAxis] += knobSize * .5f;
	barRect.mExtent[scrollAxis] -= knobSize;
	// Pad the bar on the "other axis"
	barRect.mOffset[otherAxis] += screenRect.mExtent[otherAxis] * .25f;
	barRect.mExtent[otherAxis] *= 0.5f;

	knobRects[0].mExtent[scrollAxis] = knobSize;
	knobRects[1].mExtent[scrollAxis] = knobSize;

	// Determine knob positions
	float2 pos = barRect.mOffset[scrollAxis] + (valueRange - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify kob positions from input
	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 c = i->CursorPos();
	c.y = i->WindowHeight() - c.y;
	const InputPointer* p = i->GetPointer(0);
	if (screenRect.Contains(c) && clipRect.Contains(c)) {
		if (c.x < pos[0] + knobSize * .5f)
			pos[0] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .025f;
		else if (c.x > pos[1] - knobSize * .5f)
			pos[1] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .025f;
		else
			pos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .025f;
	}

	for (uint32_t j = 0; j < 3; j++) {
		if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]) {
			if (j == 2) pos += c[scrollAxis] - i->LastCursorPos()[scrollAxis];
			else pos[j] += c[scrollAxis] - i->LastCursorPos()[scrollAxis];
			ret = true;
		}
	}

	// Derive modified value from modified positions
	valueRange = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	valueRange = clamp(valueRange, minimum, maximum);
	if (valueRange.x > valueRange.y) swap(valueRange.x, valueRange.y);

	// Derive final knob position from the modified, clamped value
	knobRects[0].mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (valueRange[0] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[1].mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (valueRange[1] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[0].mOffset[scrollAxis] -= knobSize * .5f;
	knobRects[1].mOffset[scrollAxis] -= knobSize * .5f;

	fRect2D middleRect = screenRect;
	middleRect.mOffset[otherAxis] += screenRect.mExtent[otherAxis] * .125f;
	middleRect.mExtent[otherAxis] *= 0.75f;
	middleRect.mOffset[scrollAxis] = knobRects[0].mOffset[scrollAxis] + knobSize;
	middleRect.mExtent[scrollAxis] = knobRects[1].mOffset[scrollAxis] - (knobRects[0].mOffset[scrollAxis] + knobSize);

	bool hover = false;
	bool click = false;

	for (uint32_t j = 0; j < 2; j++) {
		bool hvr = knobRects[j].Contains(c) && clipRect.Contains(c);
		bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]));

		if (hvr || clk) {
			hover = true;
			const_cast<InputPointer*>(p)->mGuiHitT = 0.f;
		}
		if (clk) {
			mHotControl[p->mName] = controlIds[j];
			click = true;
		}
	}

	if (middleRect.mExtent[scrollAxis] > 0) {
		bool hvr = middleRect.Contains(c) && clipRect.Contains(c);
		bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[2]));

		if (hvr || clk) {
			hover = true;
			const_cast<InputPointer*>(p)->mGuiHitT = 0.f;
		}
		if (clk) {
			mHotControl[p->mName] = controlIds[2];
			click = true;
		}
	}

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, barColor, nullptr, 0, z, clipRect);
	Rect(knobRects[0], float4(knobColor.rgb * m, knobColor.a), nullptr, 0, z, clipRect);
	Rect(knobRects[1], float4(knobColor.rgb * m, knobColor.a), nullptr, 0, z, clipRect);
	if (middleRect.mExtent[scrollAxis] > 0)
		Rect(middleRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, z, clipRect);
	return ret;
}
bool GUI::RangeSlider(float2& valueRange, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlIds[3] = { mNextControlId++, mNextControlId++, mNextControlId++ };
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;
	fRect2D knobRects[2] = { rect, rect };

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Offset bar by knob size to allow knobs to be centered on endpoints
	barRect.mOffset[scrollAxis] += knobSize * .5f;
	barRect.mExtent[scrollAxis] -= knobSize;
	// Pad the bar on the "other axis"
	barRect.mOffset[otherAxis] += rect.mExtent[otherAxis] * .25f;
	barRect.mExtent[otherAxis] *= 0.5f;

	knobRects[0].mExtent[scrollAxis] = knobSize;
	knobRects[1].mExtent[scrollAxis] = knobSize;

	// Determine knob positions
	float2 pos = barRect.mOffset[scrollAxis] + (valueRange - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify kob positions from input
	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c)) {
				if (c.x < pos[0] + knobSize * .5f)
					pos[0] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
				else if (c.x > pos[1] - knobSize * .5f)
					pos[1] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
				else
					pos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
			}

			for (uint32_t j = 0; j < 3; j++) {
				if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]) {
					Ray ray = p->mWorldRay;
					Ray lastRay = device->GetPointerLast(i)->mWorldRay;
					ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
					ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;
					lastRay.mOrigin = (invTransform * float4(lastRay.mOrigin, 1)).xyz;
					lastRay.mDirection = (invTransform * float4(lastRay.mDirection, 0)).xyz;
					float c = (ray.mOrigin + ray.mDirection * ray.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
					float lc = (lastRay.mOrigin + lastRay.mDirection * lastRay.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
					if (j == 2) pos += c - lc;
					else pos[j] += c - lc;
					ret = true;
				}
			}
		}

	// Derive modified value from modified positions
	valueRange = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	valueRange = clamp(valueRange, minimum, maximum);
	if (valueRange.x > valueRange.y) swap(valueRange.x, valueRange.y);

	// Derive final knob position from the modified, clamped value
	knobRects[0].mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (valueRange[0] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[1].mOffset[scrollAxis] = barRect.mOffset[scrollAxis] + (valueRange[1] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[0].mOffset[scrollAxis] -= knobSize * .5f;
	knobRects[1].mOffset[scrollAxis] -= knobSize * .5f;

	fRect2D middleRect = rect;
	middleRect.mOffset[otherAxis] += rect.mExtent[otherAxis] * .125f;
	middleRect.mExtent[otherAxis] *= 0.75f;
	middleRect.mOffset[scrollAxis] = knobRects[0].mOffset[scrollAxis] + knobSize;
	middleRect.mExtent[scrollAxis] = knobRects[1].mOffset[scrollAxis] - (knobRects[0].mOffset[scrollAxis] + knobSize);

	bool hover = false;
	bool click = false;

	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			for (uint32_t j = 0; j < 2; j++) {
				bool hvr = knobRects[j].Contains(c) && clipRect.Contains(c);
				bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]));

				if (hvr || clk) {
					hover = true;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
				if (clk) {
					mHotControl[p->mName] = controlIds[j];
					click = true;
				}
			}

			if (middleRect.mExtent[scrollAxis] > 0) {
				bool hvr = middleRect.Contains(c) && clipRect.Contains(c);
				bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[2]));

				if (hvr || clk) {
					hover = true;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
				if (clk) {
					mHotControl[p->mName] = controlIds[2];
					click = true;
				}
			}
		}

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(transform, barRect, barColor, nullptr, 0, clipRect);
	Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRects[0], float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	Rect(transform* float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRects[1], float4(knobColor.rgb* m, knobColor.a), nullptr, 0, clipRect);
	if (middleRect.mExtent[scrollAxis] > 0)
		Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), middleRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	return ret;
}


fRect2D GUI::GuiLayout::Get(float size, float padding) {
	fRect2D layoutRect = mRect;
	switch (mAxis) {
	case LAYOUT_VERTICAL:
		layoutRect.mExtent.y = size;
		layoutRect.mOffset.y += mRect.mExtent.y - (mLayoutPosition + padding + size);
		break;
	case LAYOUT_HORIZONTAL:
		layoutRect.mOffset.x += mLayoutPosition + padding;
		layoutRect.mExtent.x = size;
		break;
	}
	mLayoutPosition += size + padding * 2;
	return layoutRect;
}

void GUI::GetCurrentLayout(fRect2D& rect, float& depth, bool& screenspace, fRect2D& clipRect) {
	GuiLayout& l = mLayoutStack.top();
	rect = l.mRect;
	depth = l.mLayoutDepth;
	screenspace = l.mScreenSpace;
	clipRect = l.mClipRect;
}

fRect2D GUI::BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect, float insidePadding) {
	fRect2D layoutRect(screenRect.mOffset + insidePadding, screenRect.mExtent - insidePadding * 2);
	mLayoutStack.push({ float4x4(1), true, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(screenRect, mLayoutTheme.mBackgroundColor, nullptr, 0, START_DEPTH);
	return layoutRect;
}
fRect2D GUI::BeginWorldLayout(LayoutAxis axis, const float4x4& tranform, const fRect2D& rect, float insidePadding) {
	fRect2D layoutRect(rect.mOffset + insidePadding, rect.mExtent - insidePadding * 2);
	mLayoutStack.push({ tranform, false, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(tranform * float4x4::Translate(float3(0, 0, START_DEPTH)), rect, mLayoutTheme.mBackgroundColor);
	return layoutRect;
}

fRect2D GUI::BeginSubLayout(LayoutAxis axis, float size, float insidePadding, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mLayoutDepth + DEPTH_DELTA, l.mClipRect);
		else
			Rect(l.mTransform + float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	layoutRect.mOffset += insidePadding;
	layoutRect.mExtent -= insidePadding * 2;

	fRect2D clip = l.mClipRect ^ layoutRect;

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, axis, layoutRect, clip, 0, l.mLayoutDepth + DEPTH_DELTA });

	return layoutRect;
}
fRect2D GUI::BeginScrollSubLayout(float size, float contentSize, float insidePadding, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);
	uint32_t controlId = mNextControlId++;

	float scrollAmount = 0;
	if (mControlData.count(controlId)) {
		const auto& v = mControlData.at(controlId);
		if (v.index() == 0) scrollAmount = get<float>(v);
	}

	if (l.mScreenSpace) {
		if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
			float2 c = i->CursorPos();
			c.y = i->WindowHeight() - c.y;
			if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
				scrollAmount -= i->ScrollDelta() * 60;
				i->mMousePointer.mGuiHitT = 0.f;
			}
		}
	} else {
		float4x4 invTransform = inverse(l.mTransform);
		for (const InputDevice* d : mInputManager->InputDevices())
			for (uint32_t i = 0; i < d->PointerCount(); i++) {
				const InputPointer* p = d->GetPointer(i);

				Ray ray = p->mWorldRay;
				ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
				ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

				float t = ray.Intersect(float4(0, 0, 1, l.mLayoutDepth));

				float2 c = (ray.mOrigin + ray.mDirection * t).xy;
				if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
					scrollAmount -= p->mScrollDelta[l.mAxis == LAYOUT_HORIZONTAL ? 0 : 1] * contentSize * .25f;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
			}
	}

	float scrollMax = max(0.f, contentSize - layoutRect.mExtent.y);
	scrollAmount = clamp(scrollAmount, 0.f, scrollMax);

	mControlData[controlId] = scrollAmount;

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mLayoutDepth, l.mClipRect);
		else
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	fRect2D contentRect = layoutRect;
	contentRect.mOffset += insidePadding;
	contentRect.mExtent -= insidePadding * 2;
	switch (l.mAxis) {
	case LAYOUT_HORIZONTAL:
		contentRect.mOffset.x -= scrollAmount + (layoutRect.mExtent.x - contentSize);
		contentRect.mExtent.x = contentSize - insidePadding * 2;
		break;
	case LAYOUT_VERTICAL:
		contentRect.mOffset.y += (layoutRect.mExtent.y - contentSize) + scrollAmount;
		contentRect.mExtent.y = contentSize - insidePadding * 2;
		break;
	}
	
	// scroll bar slider
	if (scrollMax > 0) {
		fRect2D slider;
		fRect2D sliderbg;

		switch (l.mAxis) {
		case LAYOUT_HORIZONTAL:
			slider.mExtent = float2(20 * layoutRect.mExtent.x * (layoutRect.mExtent.x / contentSize), 6);
			slider.mOffset = layoutRect.mOffset + float2((layoutRect.mExtent.x - slider.mExtent.x) * (scrollAmount / scrollMax), 0);
			sliderbg.mOffset = layoutRect.mOffset;
			sliderbg.mExtent = float2(layoutRect.mExtent.x, slider.mExtent.y);

			layoutRect.mOffset.y += slider.mExtent.y;
			layoutRect.mExtent.y -= slider.mExtent.y;
			layoutRect.mOffset.y += slider.mExtent.y;
			layoutRect.mExtent.y -= slider.mExtent.y;
			break;

		case LAYOUT_VERTICAL:
			slider.mExtent = float2(6, layoutRect.mExtent.y * (layoutRect.mExtent.y / contentSize));
			slider.mOffset = layoutRect.mOffset + float2(layoutRect.mExtent.x - slider.mExtent.x, (layoutRect.mExtent.y - slider.mExtent.y) * (1 - scrollAmount / scrollMax));
			sliderbg.mOffset = layoutRect.mOffset + float2(layoutRect.mExtent.x - slider.mExtent.x, 0);
			sliderbg.mExtent = float2(slider.mExtent.x, layoutRect.mExtent.y);

			layoutRect.mExtent.x -= slider.mExtent.x;
			break;
		}

		if (l.mScreenSpace) {
			GUI::Rect(sliderbg, mLayoutTheme.mControlBackgroundColor, nullptr, 0, l.mLayoutDepth + DEPTH_DELTA);
			GUI::Rect(slider, mLayoutTheme.mControlForegroundColor, nullptr, 0, l.mLayoutDepth + 2*DEPTH_DELTA);
		} else {
			GUI::Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), sliderbg, mLayoutTheme.mControlBackgroundColor);
			GUI::Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), slider, mLayoutTheme.mControlForegroundColor);
		}
	}

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, l.mAxis, contentRect, layoutRect, 0, l.mLayoutDepth + 3*DEPTH_DELTA });

	return contentRect;
}
void GUI::EndLayout() {
	mLayoutStack.pop();
}

void GUI::LayoutSpace(float size) {
	mLayoutStack.top().mLayoutPosition += size;
}
void GUI::LayoutSeparator(float thickness, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(thickness, padding);

	if (l.mScreenSpace)
		GUI::Rect(layoutRect, mLayoutTheme.mControlForegroundColor, nullptr, 0, l.mLayoutDepth, l.mClipRect);
	else
		GUI::Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlForegroundColor, nullptr, 0, l.mClipRect);
}
void GUI::LayoutRect(float size, Texture* texture, const float4& textureST, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);

	if (l.mScreenSpace)
		Rect(layoutRect, mLayoutTheme.mControlForegroundColor, texture, textureST, l.mLayoutDepth, l.mClipRect);
	else
		Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlForegroundColor, texture, textureST, l.mClipRect);
}
void GUI::LayoutLabel(Font* font, const string& text, float textHeight, float labelSize, float padding, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(labelSize, padding);

	if (l.mScreenSpace)
		Label(font, text, textHeight, layoutRect, mLayoutTheme.mLabelBackgroundColor, mLayoutTheme.mControlForegroundColor, horizontalAnchor, verticalAnchor, l.mLayoutDepth, l.mClipRect);
	else
		Label(font, text, textHeight, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mLabelBackgroundColor, mLayoutTheme.mControlForegroundColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GUI::LayoutTextButton(Font* font, const string& text, float textHeight, float buttonSize, float padding, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(buttonSize, padding);

	if (l.mScreenSpace)
		return TextButton(font, text, textHeight, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, horizontalAnchor, verticalAnchor, l.mLayoutDepth, l.mClipRect);
	else
		return TextButton(font, text, textHeight, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GUI::LayoutImageButton(float size, Texture* texture, const float4& textureST, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);

	if (l.mScreenSpace)
		return ImageButton(layoutRect, mLayoutTheme.mControlForegroundColor, texture, textureST, l.mLayoutDepth, l.mClipRect);
	else
		return ImageButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlForegroundColor, texture, textureST, l.mClipRect);
}
bool GUI::LayoutSlider(float& value, float minimum, float maximum, float size, float knobSize, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);
	LayoutAxis axis = l.mAxis == LAYOUT_HORIZONTAL ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
	if (l.mScreenSpace)
		return Slider(value, minimum, maximum, axis, knobSize, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, l.mLayoutDepth, l.mClipRect);
	else
		return Slider(value, minimum, maximum, axis, knobSize, l.mTransform, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, l.mClipRect);
}

bool GUI::LayoutRangeSlider(float2& valueRange, float minimum, float maximum, float size, float knobSize, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);
	LayoutAxis axis = l.mAxis == LAYOUT_HORIZONTAL ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
	if (l.mScreenSpace)
		return RangeSlider(valueRange, minimum, maximum, axis, knobSize, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, l.mLayoutDepth, l.mClipRect);
	else
		return RangeSlider(valueRange, minimum, maximum, axis, knobSize, l.mTransform, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mControlForegroundColor, l.mClipRect);
}

bool GUI::LayoutColorPicker(float3& color, float size, float insidePadding, float padding) {
	GuiLayout& l = mLayoutStack.top();
	fRect2D layoutRect = l.Get(size, padding);
	LayoutAxis axis = l.mAxis == LAYOUT_HORIZONTAL ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
	if (l.mScreenSpace)
		return ColorPicker(color, layoutRect, insidePadding, l.mLayoutDepth, l.mClipRect);
	else
		return ColorPicker(color, l.mTransform, layoutRect, insidePadding, l.mClipRect);
}