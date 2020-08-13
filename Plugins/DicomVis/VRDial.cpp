#include <memory>
#include "VRDial.hpp"
#include <Scene/Object.hpp>

using namespace std;

#define EMPTY_CHANGE_BUFFER { 0,0,0 }
#define MAX_ROTATION 0.75 * PI

InputManager* VRDial::mInputManager = nullptr;
std::shared_ptr<Material> VRDial::dialMaterial;
int VRDial::dials = 0;

VRDial::VRDial(const std::string name, Scene* scene) {
	++dials;
	mInputManager = scene->InputManager();
	mOVR = nullptr;
	held = false;
	mIdx = 0;
	mChangeFrames = EMPTY_CHANGE_BUFFER;
		
	mesh = scene->AssetManager()->LoadMesh("Assets/dial.gltf");
	if (!dialMaterial) {
		dialMaterial = make_shared<Material>("Dial", scene->AssetManager()->LoadShader("Shaders/pbr.stm"));
		dialMaterial->SetParameter("BaseColor", float4(1));
		dialMaterial->SetParameter("Metallic", 0.5f);
		dialMaterial->SetParameter("Roughness", 0.5f);
		dialMaterial->SetParameter("BumpStrength", 0.f);

	}
	else {
		dialMaterial->SetParameter("Instances", dials);
	}
	renderer = make_shared<MeshRenderer>("Dial");
	renderer->Mesh(mesh);
	renderer->Material(dialMaterial);
	renderer->LocalPosition(float3(0, 0, 0));
	renderer->LocalScale(float3(0.2));
}

void VRDial::Interact(float& value, float minimum, float maximum) {
	if (held) {
		if (mOVR->GetPointer(mIdx)->mPrimaryButton) {
			PointerRenderer* pr = mOVR->GetPointerRenderer(mIdx);
			quaternion diff = inverse(mPrevRot) * pr->LocalRotation();
			mPrevRot = pr->LocalRotation();

			mChangeFrames.insert(mChangeFrames.begin(), diff.toEuler().z);
			mChangeFrames.pop_back();
			float changesum = 0;
			for (float changeframe : mChangeFrames) {
				changesum += changeframe;
			}
			changesum /= (float)mChangeFrames.size();

			float currpercent = (maximum - value) / (maximum - minimum);
			double currrot = (currpercent - 0.5) * 2 * MAX_ROTATION;
			currrot += changesum;
			currrot = clamp(currrot, -MAX_ROTATION, MAX_ROTATION);

			renderer->LocalRotation(quaternion(float3(0, 0, currrot)));

			float nextpercent = (currrot / (2 * MAX_ROTATION)) + 0.5;
			value = maximum - nextpercent * (maximum - minimum);
		}
		else {
			mOVR = nullptr;
			held = false;
			mChangeFrames = EMPTY_CHANGE_BUFFER;
		}
	}
	else {
		std::vector<OpenVR*> vrDevices;
		mInputManager->GetDevices<OpenVR>(vrDevices);
		for (OpenVR* device : vrDevices) {
			for (uint32_t i = 0; i < device->PointerCount(); i++) {
				const InputPointer* p = device->GetPointer(i);

				Ray ray = p->mWorldRay;

				float2 t;
				bool hit = ray.Intersect(renderer->Bounds(), t);
				if (p->mGuiHitT > 0 && min(t.x, t.y) > p->mGuiHitT) continue;

				const InputPointer* p2 = device->GetPointerLast(i);
				bool firstpress = p->mPrimaryButton && !p2->mPrimaryButton;

				bool clk = (hit && firstpress);

				if (hit || clk) {
					const_cast<InputPointer*>(p)->mGuiHitT = min(t.x, t.y);
				}
				if (clk) {
					mOVR = device;
					mIdx = i;
					held = true;
					mPrevRot = mOVR->GetPointerRenderer(mIdx)->LocalRotation();
				}
			}
		}

	}

}

VRDial::~VRDial() {
	--dials;
}
