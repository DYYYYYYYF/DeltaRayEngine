#pragma once

#include "Rendering/Resources/Asset.hpp"
#include "ShaderType.hpp"
#include <unordered_map>

// Shader compiler
#include <shaderc/shaderc.hpp>

struct FTextureMap;
class IRenderer;

class UShader : public UAsset{
public:
	UShader(const FString& Name) : UAsset(Name) {}

public:
	virtual bool Reload() = 0;
	virtual void Destroy() = 0;

	// 激活此 Shader（绑定 Pipeline）
	virtual bool Use() = 0;

	// 绑定 Global 描述符集，准备写全局 uniform
	virtual bool BindGlobal() = 0;

	// 绑定 Instance 描述符集，准备写实例 uniform
	virtual bool BindInstance(uint64_t instance_id) = 0;

	// 将已写入的全局 uniform 提交到 GPU 
	virtual bool ApplyGlobal() = 0;

	/**
	 * 将已写入的实例 uniform 提交到 GPU。
	 * @param need_update 为 false 时只绑定描述符集，不更新数据（同一帧复用时）
	 */
	virtual bool ApplyInstance() = 0;

	/**
	 * 按 uniform index 写值。
	 * Sampler 类型时 value 应传 const TextureMap*。
	 * Local scope 时写入 Push Constant。
	 */
	virtual bool SetUniformByIndex(uint32_t index, const void* value) = 0;

	/** 按名称写 uniform（内部转 index 后调用 SetUniformByIndex）*/
	virtual bool SetUniform(const FString& name, const void* value) = 0;
	virtual bool SetUniform(const ShaderUniform* uniform, const void* value) = 0;
	
	virtual void ProcessAttributes(const std::vector<ShaderAttributeConfig>& attributes);
	virtual void ProcessUniforms(const std::vector<ShaderUniformConfig>& uniforms);

	virtual void AddAttribute(const ShaderAttributeConfig& config);
	virtual void AddSampler(const ShaderUniformConfig& config);
	virtual void AddUniform(const ShaderUniformConfig& config);
	virtual void AddUniform(const FString& uniform_name, uint32_t size, ShaderUniformType type, 
		ShaderScope scope, ShaderSemantic semantic, uint32_t set_location, bool is_sampler);

	/**
	 * @brief Returns the uniform index for a uniform with the given name, if found.
	 *
	 * @param uniform_name The name of the uniform to search for.
	 * @return The uniform index, if found; otherwise INVALID_ID_U16.
	 */
	uint32_t GetUniformIndex(const FString& name) const;

	/**
	 * @brief Returns the uniform index for a uniform with the given name, if found.
	 *
	 * @param uniform_name The name of the uniform to search for.
	 * @return The uniform point, if found; otherwise nullptr.
	 */
	ShaderUniform* GetUniformHandle(const FString& name);

	const std::vector<ShaderUniform>& GetUniformList() const { return Uniforms; }

protected:
	// Shader utils
	std::vector<uint32_t> CompileShaderToSPV(const FString& filename, enum ShaderStage shaderStage, bool writeToDisk = true);
	virtual void SetupCompileOptions(shaderc::CompileOptions& options) {}

private:
	bool IsUniformNameValid(const FString& uniform_name);
	bool IsUniformAddStateValid();

public:
	IRenderer* Renderer = nullptr;
	uint32_t         ID = INVALID_ID;
	FString          Name;
	EShaderLanguage  Language = EShaderLanguage::eGLSL;
	ShaderFlagBits   Flags = 0;
	EShaderStatus    Status = EShaderStatus::eShader_State_Uninitialized;

	// UBO 布局（由 ShaderSystem::AddUniform 累加，Initialize 时对齐）
	size_t  RequiredUboAlignment = 0;
	size_t  GlobalUboSize = 0;
	size_t  GlobalUboOffset = 0;
	size_t  GlobalUboStride = 0;
	size_t  UboSize = 0;
	size_t  UboStride = 0;

	// Push Constants
	size_t   PushConstantsSize = 0;
	size_t   PushConstantsStride = 128;  // Vulkan 最低保证 128B
	uint16_t PushConstantsRangeCount = 0;
	Range    PushConstantsRanges[32] = {};

	// Attribute
	uint16_t AttributeStride = 0;

	// Instance 绑定状态（跨帧保持，由 BindInstance/BindGlobal 写入）
	ShaderScope  BoundScope = eShader_Scope_Instance;
	uint64_t     BoundInstanceId = INVALID_ID;
	uint32_t     BoundUboOffset = 0;

	// 渲染帧同步
	size_t  RenderFrameNumber = 0;

	// 实例贴图数量（由 ShaderSystem::AddSampler 计数）
	uint32_t InstanceTextureCount = 0;

	// Uniform / Attribute 表
	std::vector<ShaderUniform>              Uniforms;
	std::vector<ShaderAttribute>            Attributes;
	std::vector<FTextureMap*>                GlobalTextureMaps;
	std::unordered_map<FString, uint32_t>   HashMap;
};