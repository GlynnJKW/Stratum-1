#pragma once
#include <vector>
#include <algorithm>
#include <Util/Util.hpp>
#include <Scene/Scene.hpp>


struct TransferTriangle {
	float bottomWidth;
	float topWidth;
	float center;
	float height;
};

struct TransferGradient {
	float center;
	float3 color;
	float4 pad;
};
class TransferFunction {

private:
	std::vector<TransferTriangle> mTransferFunctionTriangles;
	std::vector<TransferGradient> mTransferFunctionGradients;
	int mSelectedTriangle;
	int mSelectedGradient;

public:
	TransferFunction(std::vector<TransferTriangle> t, std::vector<TransferGradient> g) : 
		mTransferFunctionTriangles(t), mTransferFunctionGradients(g), mSelectedTriangle(0) {

	}

	TransferFunction() {
		mTransferFunctionTriangles = { {1, 0, 1, 1} };
		mTransferFunctionGradients = { {0, float3(0)}, {1, float3(1)} };
		mSelectedGradient = -1;
		mSelectedTriangle = -1;
	}

	//void Sort() {
	//	std::sort(mTransferFunctionGradients.begin(), mTransferFunctionGradients.end(),
	//		[](const TransferGradient& a, const TransferGradient& b) {
	//			return a.center < b.center;
	//		});
	//}

	inline const std::vector<TransferTriangle> GetTriangles() {
		return mTransferFunctionTriangles;
	}

	const std::vector<TransferGradient> GetGradients() {
		std::vector<TransferGradient> gradients = mTransferFunctionGradients;
		std::sort(gradients.begin(), gradients.end(),
			[](const TransferGradient& a, const TransferGradient& b) {
				if (a.center != b.center) {
					return a.center < b.center;
				}
				return length(a.color) < length(b.color);
			});
		gradients.insert(gradients.begin(), gradients[0]);
		gradients.push_back(gradients[gradients.size()-1]);
		gradients[0].center = 0;
		gradients[gradients.size() - 1].center = 1;
		return gradients;
	}

	bool RenderUI(float2 size, Texture* LUT, Scene* scene, CommandBuffer* commandBufffer);

};