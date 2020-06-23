#include "TransferFunction.hpp"
#include "Scene/GUI.hpp"

#include <iostream>

bool TransferFunction::RenderUI(float2 size, Texture* LUT, Scene* scene, CommandBuffer* commandBufffer) {
	bool ret = false;
	Font* sem16 = scene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-SemiBold.ttf", 16);
	Font* bld24 = scene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-Bold.ttf", 24);
	Texture* icons = scene->AssetManager()->LoadTexture("Assets/Textures/icons.png", true);

	GUI::BeginSubLayout(LAYOUT_HORIZONTAL, size.y, 0, 2);

		GUI::BeginSubLayout(LAYOUT_VERTICAL, size.x * 0.66 - 4, 0, 2);
			//Display
			GUI::BeginSubLayout(LAYOUT_VERTICAL, size.y * 0.3 - 4, 0, 2);
				GUI::LayoutRect(size.y * 0.05, LUT, float4(1, 0.5, 0, 0));

				GUI::BeginSubLayout(LAYOUT_VERTICAL, size.y * 0.25 - 8);
					float z = 0;
					bool screenspace = true;
					fRect2D rect, clipRect;
					float4x4 transform;
					GUI::GetCurrentLayout(rect, z, screenspace, clipRect);
					if (!screenspace) {
						transform = GUI::GetCurrentTransform();
					}
					for (auto tri : mTransferFunctionTriangles) {
						fRect2D trirect = rect;
						trirect.mOffset.x += trirect.mExtent.x * (tri.center - tri.bottomWidth);
						trirect.mExtent.x *= tri.bottomWidth * 2;
						trirect.mExtent.y *= tri.height;
						if(screenspace)
							GUI::ShaderRect(trirect, float4(float3(0.8), 1), "Shaders/triangle.stm", {}, float4(1, -1, 0, 1), z, clipRect);
						else
							GUI::ShaderRect(transform * float4x4::Translate(float3(0, 0, z)), trirect, float4(float3(0.8), 1), "Shaders/triangle.stm", {}, float4(1, 1, 0, 0), clipRect);
					}

				GUI::EndLayout();
			GUI::EndLayout();

			//Edit
			GUI::BeginSubLayout(LAYOUT_VERTICAL, size.y * 0.7 - 4, 0, 2);
				if (mSelectedGradient >= 0) {
					if (GUI::LayoutTextButton(bld24, "Remove Gradient", 20, 25)) {
						mTransferFunctionGradients.erase(mTransferFunctionGradients.begin() + mSelectedGradient);
						mSelectedGradient = -1;
						ret = true;
					}

					GUI::LayoutLabel(sem16, "Center: " + std::to_string(mTransferFunctionGradients[mSelectedGradient].center), 16, 20);
					ret = GUI::LayoutSlider(mTransferFunctionGradients[mSelectedGradient].center, 0, 1, 20, 20) || ret;

					ret = GUI::LayoutColorPicker(mTransferFunctionGradients[mSelectedGradient].color, size.y * 0.5 - 60, 60) || ret;
				
				}
				else if (mSelectedTriangle >= 0) {
					if (GUI::LayoutTextButton(bld24, "Remove Triangle", 20, 25)) {
						mTransferFunctionTriangles.erase(mTransferFunctionTriangles.begin() + mSelectedTriangle);
						mSelectedTriangle = -1;
						ret = true;
					}

					GUI::LayoutLabel(sem16, "Center: " + std::to_string(mTransferFunctionTriangles[mSelectedTriangle].center), 16, 20);
					ret = GUI::LayoutSlider(mTransferFunctionTriangles[mSelectedTriangle].center, 0, 1, 20, 20) || ret;

					GUI::LayoutLabel(sem16, "Height: " + std::to_string(mTransferFunctionTriangles[mSelectedTriangle].height), 16, 20);
					ret = GUI::LayoutSlider(mTransferFunctionTriangles[mSelectedTriangle].height, 0, 1, 20, 20) || ret;

					GUI::LayoutLabel(sem16, "Bottom width: " + std::to_string(mTransferFunctionTriangles[mSelectedTriangle].bottomWidth), 16, 20);
					ret = GUI::LayoutSlider(mTransferFunctionTriangles[mSelectedTriangle].bottomWidth, 0, 1, 20, 20) || ret;

					//GUI::LayoutLabel(sem16, "Top width: " + std::to_string(mTransferFunctionTriangles[mSelectedTriangle].topWidth), 16, 20);
					//ret = GUI::LayoutSlider(mTransferFunctionTriangles[mSelectedTriangle].topWidth, 0, 1, 20, 20) || ret;
				
				}
				else {
					GUI::LayoutLabel(bld24, "Select a triangle or gradient\nto edit the transfer function", 24, size.y * 0.5, 0);
				}
			GUI::EndLayout();
		GUI::EndLayout();

		GUI::BeginSubLayout(LAYOUT_VERTICAL, size.x * 0.33 - 4, 0, 2);
			//Triangles
			GUI::BeginScrollSubLayout(size.y * 0.5 - 4, 24 * mTransferFunctionTriangles.size(), 0, 2);
				for (int i = 0; i < mTransferFunctionTriangles.size(); ++i) {
					auto triangle = mTransferFunctionTriangles[i];
					if (GUI::LayoutTextButton(sem16, "Triangle " + std::to_string(i), 20, 24, 0)) {
						mSelectedTriangle = i;
						mSelectedGradient = -1;
					}
				}
				if (GUI::LayoutTextButton(sem16, "Add Triangle", 20, 24, 0)) {
					mTransferFunctionTriangles.push_back({ 1, 0, 1, 1 });
					mSelectedTriangle = mTransferFunctionTriangles.size() - 1;
					mSelectedGradient = -1;
				}
			GUI::EndLayout();

			//Gradients
			GUI::BeginScrollSubLayout(size.y * 0.5 - 4, 24 * mTransferFunctionGradients.size(), 0, 2);
				for (int i = 0; i < mTransferFunctionGradients.size(); ++i) {
					auto triangle = mTransferFunctionGradients[i];
					if (GUI::LayoutTextButton(sem16, "Gradient " + std::to_string(i), 20, 24, 0)) {
						mSelectedGradient = i;
						mSelectedTriangle = -1;
					}
				}
				if (GUI::LayoutTextButton(sem16, "Add Gradient", 20, 24, 0)) {
					mTransferFunctionGradients.push_back({ 1, float3(1,1,1) });
					mSelectedGradient = mTransferFunctionGradients.size() - 1;
					mSelectedTriangle = -1;
				}
			GUI::EndLayout();
		GUI::EndLayout();
	GUI::EndLayout();
	return ret;
}