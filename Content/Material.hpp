#pragma once

#include <Content/Shader.hpp>
#include <Content/Texture.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Sampler.hpp>
#include <Core/RenderPass.hpp>
#include <Core/DescriptorSet.hpp>

typedef std::variant<
	std::shared_ptr<Texture>,
	std::shared_ptr<Sampler>,
	Texture*,
	Sampler*,
	float,
	float2,
	float3,
	float4,
	uint32_t,
	uint2,
	uint3,
	uint4,
	int32_t,
	int2,
	int3,
	int4,
	float4x4
> MaterialParameter;

class Material {
public:
	const std::string mName;

	ENGINE_EXPORT Material(const std::string& name, ::Shader* shader);
	ENGINE_EXPORT Material(const std::string& name, std::shared_ptr<::Shader> shader);
	ENGINE_EXPORT ~Material();

	inline ::Shader* Shader() const { return mShader.index() == 0 ? std::get<::Shader*>(mShader) : std::get<std::shared_ptr<::Shader>>(mShader).get(); };
	ENGINE_EXPORT GraphicsShader* GetShader(PassType pass);

	// Set the pass mask override
	// Default to PASS_MASK_MAX_ENUM, which uses the shader's pass mask
	inline void PassMask(PassType p) { mPassMask = p; }
	inline PassType PassMask() { return mPassMask == PASS_MASK_MAX_ENUM ? Shader()->PassMask() : mPassMask; }

	// Set the render queue override
	// Default to ~0, which uses the shader's render queue
	inline void RenderQueue(uint32_t q) { mRenderQueue = q; }
	inline uint32_t RenderQueue() const { return mRenderQueue == ~0 ? Shader()->RenderQueue() : mRenderQueue; }

	// Set the cull mode override
	// Default to VK_CULL_MODE_FLAG_BITS_MAX_ENUM, which uses the shader's cull mode
	inline void CullMode(VkCullModeFlags c) { mCullMode = c; }
	inline VkCullModeFlags CullMode() const { return mCullMode; }

	// Set the blend mode override
	// Default to BLEND_MODE_MAX_ENUM, which uses the shader's blend mode
	inline void BlendMode(::BlendMode c) { mBlendMode = c; }
	inline ::BlendMode BlendMode() const { return mBlendMode; }

	ENGINE_EXPORT void SetUniformBuffer(const std::string& name, VkDeviceSize offset, VkDeviceSize range, std::shared_ptr<Buffer> param);
	ENGINE_EXPORT void SetUniformBuffer(const std::string& name, VkDeviceSize offset, VkDeviceSize range, Buffer* param);

	// Set an element of a texture array
	ENGINE_EXPORT void SetParameter(const std::string& name, uint32_t index, Texture* param);
	// Set an element of a texture array
	ENGINE_EXPORT void SetParameter(const std::string& name, uint32_t index, std::shared_ptr<Texture> param);
	// Set a material parameter
	// scalar parameters are used as push constants, while buffers, textures, etc. are set as descriptors
	// Buffers are written as storage buffers. To write a uniform buffer, use Material::SetUniformBuffer()
	ENGINE_EXPORT void SetParameter(const std::string& name, const MaterialParameter& param);

	// Get a material parameter
	inline MaterialParameter GetParameter(const std::string& name) const { return mParameters.at(name); }
	// Get an element of a texture array
	inline std::variant<std::shared_ptr<Texture>, Texture*> GetParameter(const std::string& name, uint32_t index) const { return mArrayParameters.at(name).at(index);  }

	// Enable a keyword to be used to select a shader variant
	ENGINE_EXPORT void EnableKeyword(const std::string& kw);
	// Disable a keyword to be used to select a shader variant
	ENGINE_EXPORT void DisableKeyword(const std::string& kw);

private:
	struct VariantData {
		GraphicsShader* mShaderVariant;
		DescriptorSet** mDescriptorSets;
		bool* mDirty;
	};

	friend class CommandBuffer;
	ENGINE_EXPORT void SetDescriptorParameters(CommandBuffer* commandBuffer, Camera* camera, VariantData* data);
	ENGINE_EXPORT void SetPushConstantParameters(CommandBuffer* commandBuffer, Camera* camera, VariantData* data);

	ENGINE_EXPORT VariantData* GetData(PassType pass);

	Device* mDevice;

	std::variant<::Shader*, std::shared_ptr<::Shader>> mShader;
	std::set<std::string> mShaderKeywords;
	VkCullModeFlags mCullMode;
	::BlendMode mBlendMode;

	PassType mPassMask;
	uint32_t mRenderQueue;

	struct UniformBufferParameter {
		std::variant<std::shared_ptr<Buffer>, Buffer*> mBuffer;
		VkDeviceSize mOffset;
		VkDeviceSize mRange;
	};

	std::unordered_map<std::string, UniformBufferParameter> mUniformBuffers;
	std::unordered_map<std::string, MaterialParameter> mParameters;
	std::unordered_map<std::string, std::unordered_map<uint32_t, std::variant<std::shared_ptr<Texture>, Texture*>>> mArrayParameters;

	std::unordered_map<PassType, VariantData*> mVariantData;
};