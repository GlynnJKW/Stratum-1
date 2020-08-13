#pragma once

#include <Scene/MeshRenderer.hpp>
#include <Scene/Scene.hpp>
#include <XR/OpenVR.hpp>

class VRDial {

private:
	static InputManager* mInputManager;
	static std::shared_ptr<Material> dialMaterial;
	static int dials;
	std::shared_ptr<MeshRenderer> renderer;
	Mesh* mesh;

	OpenVR* mOVR;
	uint32_t mIdx;
	bool held;
	quaternion mPrevRot;
	std::vector<float> mChangeFrames;

public:

	VRDial(const std::string name, Scene* scene);
	~VRDial();

	void Interact(float&value, float minimum, float maximum);

	std::shared_ptr<MeshRenderer> Renderer() { return renderer; }

};