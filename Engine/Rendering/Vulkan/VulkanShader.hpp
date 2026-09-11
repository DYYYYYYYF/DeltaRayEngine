#pragma once

#include <vulkan/vulkan.hpp>
#include "VulkanPipeline.hpp"
#include "VulkanBuffer.hpp"
#include "Rendering/Resources/ResourceTypes.hpp"

class VulkanRenderPass;
class VulkanCommandBuffer;

#define VULKAN_SHADER_MAX_STAGES 8
#define VULKAN_SHADER_MAX_GLOBAL_TEXTURES 31
#define VULKAN_SHADER_MAX_INSTANCE_TEXTURES 31

#define VULKAN_SHADER_MAX_ATTRIBUTES 16
#define VULKAN_SHADER_MAX_UNIFORMS 128

#define VULKAN_SHADER_MAX_BINDINGS 2
#define VULKAN_SHADER_MAX_PUSH_CONST_RANGES 32

struct VulkanShaderStageConfig{
	vk::ShaderStageFlagBits stage;
	char filename[255] = "";
};

struct VulkanDescriptorSetConfig {
	unsigned short binding_count = 0;
	vk::DescriptorSetLayoutBinding bindings[VULKAN_SHADER_MAX_BINDINGS];
	unsigned char sampler_binding_index = 0;
};

struct VulkanShaderConfig {
	uint16_t stage_count = 0;
	VulkanShaderStageConfig stages[VULKAN_SHADER_MAX_STAGES];
	vk::DescriptorPoolSize pool_sizes[2];

	uint16_t max_descriptor_set_count = 0;
	uint16_t descriptor_set_count = 0;
	VulkanDescriptorSetConfig descriptor_sets[2];
	vk::VertexInputAttributeDescription attributes[VULKAN_SHADER_MAX_ATTRIBUTES];

	FaceCullMode cull_mode = FaceCullMode::eFace_Cull_Mode_Back;
	PolygonMode pology_mode = PolygonMode::ePology_Mode_Fill;
	PrimitiveTopology PrimTopo = PrimitiveTopology::eTriangleList;
};

struct VulkanDescriptorState {
	// One per frame
	uint32_t generations[3] = { INVALID_ID };
	uint32_t ids[3] = {INVALID_ID};
};

struct VulkanShaderDescriptorSetState {
	std::array<vk::DescriptorSet, 3> descriptorSets;
	VulkanDescriptorState descriptor_states[VULKAN_SHADER_MAX_BINDINGS];
};

struct VulkanShaderStage {
	vk::ShaderModuleCreateInfo create_info;
	vk::ShaderModule shader_module;
	vk::PipelineShaderStageCreateInfo shader_stage_create_info;
};

struct VulkanShaderInstanceState {
	uint32_t id = INVALID_ID;
	size_t offset = 0;
	VulkanShaderDescriptorSetState descriptor_set_state;
	std::vector<FTextureMap*> instance_texture_maps;
};

// The index of the global descriptor set.
constexpr uint32_t DESC_SET_INDEX_GLOBAL = 0;
// The index of the instance descriptor set.
constexpr uint32_t DESC_SET_INDEX_INSTANCE = 1;

class VulkanShader : public UShader {
public:
	VulkanShader(const FString& Name);
	
	virtual ~VulkanShader() {
		if (Status != EShaderStatus::eShader_State_Not_Created) {
			Destroy();
		}
	}

public:
	virtual bool Initialize() override;
	virtual bool Reload() override;
	virtual void Destroy() override;

	// GPU 操作 
	/**
	 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
	 * and for use in draw calls.
	 *
	 * @return True on success; otherwise false.
	 */
	virtual bool Use() override;

	/**
	 * @brief Binds global resources for use and updating.
	 *
	 * @return True on success; otherwise false.
	 */
	virtual bool BindGlobal() override;

	/**
	 * @brief Binds instance resources for use and updating.
	 *
	 * @param instance_id The identifier of the instance to be bound.
	 * @return True on success; otherwise false.
	 */
	virtual bool BindInstance(uint64_t instance_id) override;

	/**
	 * @brief Applies global data to the uniform buffer.
	 *
	 * @return True on success; otherwise false.
	 */
	virtual bool ApplyGlobal() override;

	/**
	 * @brief Applies data for the currently bound instance.
	 *
	 * @param need_update
	 * @return True on success; otherwise false.
	 */
	virtual bool ApplyInstance() override;

	/**
	 * @brief Sets the uniform of the given shader to the provided value.
	 *
	 * @param uniform A constant pointer to the uniform.
	 * @param value A pointer to the value to be set.
	 * @return b8 True on success; otherwise false.
	 */
	virtual bool SetUniform(const FString& name, const void* value) override;
	virtual bool SetUniform(const ShaderUniform* uniform, const void* value) override;

	virtual bool SetUniformByIndex(uint32_t index, const void* value) override;

	// 设置编译特性
	virtual void SetupCompileOptions(shaderc::CompileOptions& options) override;

private:
	bool CreateModule();
	bool CompileShaderFile(bool writeToDisk = true);

	bool CreateDescriptorSetLayouts();
	bool AllocateGlobalDescriptorSets();
	bool CreateUniformBuffer();
	bool CreatePipeline();

	VulkanCommandBuffer* GetCurrentCommandBuffer();
	bool SetSamplerByIndex(uint32_t index, const FTextureMap* map);
	bool SetSampler(const ShaderUniform* uniform, const FTextureMap* map);

public:
	void*					  MappedUniformBufferBlock = nullptr;
	VulkanShaderConfig        Config;
	VulkanRenderPass*		  Renderpass = nullptr;
	VulkanShaderStage         Stages[VULKAN_SHADER_MAX_STAGES];
	vk::DescriptorPool        DescriptorPool;
	vk::DescriptorSetLayout   DescriptorSetLayouts[2];
	vk::DescriptorSet         GlobalDescriptorSets[3];
	VulkanBuffer              UniformBuffer;
	VulkanPipeline            Pipeline;
	uint32_t                  InstanceCount = 0;
	VulkanShaderInstanceState InstanceStates[VULKAN_MAX_MATERIAL_COUNT];

	unsigned char GlobalUniformCount = 0;
	unsigned char GlobalUniformSamplerCount = 0;
	unsigned char InstanceUniformCount = 0;
	unsigned char InstanceUniformSamplerCount = 0;
	unsigned char LocalUniformCount = 0;
};