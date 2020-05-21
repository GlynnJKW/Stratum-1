#pragma once

#include <Content/Material.hpp>
#include <Content/Texture.hpp>
#include <Scene/Object.hpp>
#include <Scene/Light.hpp>
#include <Util/Util.hpp>

class Scene;

class Environment {
public:
	inline float3 AmbientLight() const { return mAmbientLight; }
	inline void AmbientLight(const float3& t) { mAmbientLight = t; }

	inline Texture* EnvironmentTexture() const { return mEnvironmentTexture; }
	inline void EnvironmentTexture(Texture* t) { mEnvironmentTexture = t; }

	// Sets the 'ENVIRONMENT_TEXTURE' keyword, 'EnvironmentTexture', and 'AmbientLight' parameters accordingly
	ENGINE_EXPORT void SetEnvironment(Camera* camera, Material* material);
	
private:
	friend class Scene;
	ENGINE_EXPORT Environment(Scene* scene);
	ENGINE_EXPORT void PreRender(CommandBuffer* commandBuffer, Camera* camera);

	Texture* mEnvironmentTexture;

	float3 mAmbientLight;

    Scene* mScene;

	Shader* mShader;
	std::shared_ptr<Material> mSkyboxMaterial;
};