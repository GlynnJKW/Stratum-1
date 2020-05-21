#include <Scene/Environment.hpp>

#include <Core/Buffer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshRenderer.hpp>

using namespace std;

Environment::Environment(Scene* scene) : 
	mScene(scene),
	mAmbientLight(0),
	mEnvironmentTexture(nullptr) {

	mSkyboxMaterial = make_shared<Material>("Skybox", mScene->AssetManager()->LoadShader("Shaders/skybox.stm"));
}

void Environment::SetEnvironment(Camera* camera, Material* mat) {
	if (mEnvironmentTexture) {
		mat->EnableKeyword("ENVIRONMENT_TEXTURE");
		mat->SetParameter("EnvironmentTexture", mEnvironmentTexture);
	} else {
		mat->DisableKeyword("ENVIRONMENT_TEXTURE");
	}
	mat->SetParameter("AmbientLight", mAmbientLight);
}

void Environment::PreRender(CommandBuffer* commandBuffer, Camera* camera) {
	if (mEnvironmentTexture){
		if ((mEnvironmentTexture->Format() == VK_FORMAT_R32G32B32A32_SFLOAT || mEnvironmentTexture->Format() == VK_FORMAT_R16G16B16A16_SFLOAT)) {
			mSkyboxMaterial->DisableKeyword("ENVIRONMENT_TEXTURE");
			mSkyboxMaterial->EnableKeyword("ENVIRONMENT_TEXTURE_HDR");
		} else {
			mSkyboxMaterial->EnableKeyword("ENVIRONMENT_TEXTURE");
			mSkyboxMaterial->DisableKeyword("ENVIRONMENT_TEXTURE_HDR");
		}
		mSkyboxMaterial->SetParameter("EnvironmentTexture", mEnvironmentTexture);
	} else {
		mSkyboxMaterial->DisableKeyword("ENVIRONMENT_TEXTURE");
		mSkyboxMaterial->DisableKeyword("ENVIRONMENT_TEXTURE_HDR");
	}
	mSkyboxMaterial->SetParameter("AmbientLight", mAmbientLight);
}