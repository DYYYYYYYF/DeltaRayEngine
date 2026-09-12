#include "VulkanShader.hpp"
#include "Systems/TextureSystem.h"
#include "Systems/ResourceSystem.h"
#include "Core/EngineLogger.hpp"
#include "Platform/File/File.hpp"
#include "Rendering/Renderer.hpp"
#include "VulkanBackend.hpp"
#include "Core/Utils.hpp"
#include "VulkanTexture.hpp"

VulkanShader::VulkanShader(const FString& Name) : UShader(Name) {
	ID = INVALID_ID;
	MappedUniformBufferBlock = nullptr;
	Renderpass = nullptr;
	InstanceCount = 0;
	GlobalUniformCount = 0;
	GlobalUniformSamplerCount = 0;
	InstanceUniformCount = 0;
	InstanceUniformSamplerCount = 0;
	LocalUniformCount = 0;
	Renderer = IRenderer::GetRenderer();
}

bool VulkanShader::Initialize() {
	if (Renderer == nullptr) {
		GLOG(Log::eError, "VulkanShader::Initialize Falied. Renderer ptr is nullptr please offer a valued ptr in construction.");
		return false;
	}

	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;
	vk::Device LogicalDevice = vkRenderer->Context.Device.GetLogicalDevice();
	vk::AllocationCallbacks* VkAllocator = Context.Allocator;
	ASSERT(VkAllocator);

	// Static lookup table for our types->vulkan once.
	static vk::Format* Types = nullptr;
	static vk::Format t[11];
	if (!Types) {
		t[ShaderAttributeType::eShader_Attribute_Type_Float] = vk::Format::eR32Sfloat;
		t[ShaderAttributeType::eShader_Attribute_Type_Float_2] = vk::Format::eR32G32Sfloat;
		t[ShaderAttributeType::eShader_Attribute_Type_Float_3] = vk::Format::eR32G32B32Sfloat;
		t[ShaderAttributeType::eShader_Attribute_Type_Float_4] = vk::Format::eR32G32B32A32Sfloat;
		t[ShaderAttributeType::eShader_Attribute_Type_Int8] = vk::Format::eR8Sint;
		t[ShaderAttributeType::eShader_Attribute_Type_UInt8] = vk::Format::eR8Uint;
		t[ShaderAttributeType::eShader_Attribute_Type_Int16] = vk::Format::eR16Sint;
		t[ShaderAttributeType::eShader_Attribute_Type_UInt16] = vk::Format::eR16Uint;
		t[ShaderAttributeType::eShader_Attribute_Type_Int32] = vk::Format::eR32Sint;
		t[ShaderAttributeType::eShader_Attribute_Type_UInt32] = vk::Format::eR32Uint;
		Types = t;
	}

	// Process attributes.
	uint32_t AttributeCount = (uint32_t)Attributes.Size();
	uint32_t Offset = 0;
	for (uint32_t i = 0; i < AttributeCount; ++i) {
		// Setup the new attribute.
		vk::VertexInputAttributeDescription Attribute;
		Attribute.setLocation(i)
			.setBinding(0)
			.setOffset(Offset)
			.setFormat(Types[Attributes[i].type]);

		// Push into the config's attribute collection and add to the stride.
		Config.attributes[i] = Attribute;
		Offset += Attributes[i].size;
	}

	// Descriptor pool.
	if (Status == EShaderStatus::eShader_State_Uninitialized) {
		vk::DescriptorPoolCreateInfo PoolInfo;
		PoolInfo.setPoolSizeCount(2)
			.setPPoolSizes(Config.pool_sizes)
			.setMaxSets(Config.max_descriptor_set_count)
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);

		// Create descriptor pool.
		DescriptorPool = LogicalDevice.createDescriptorPool(PoolInfo, VkAllocator);
		ASSERT(DescriptorPool);
	}

	if (!CompileShaderFile()) {
		GLOG(Log::eError, "VulkanShader::Reload: compile shader failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreateModule()) {
		GLOG(Log::eError, "VulkanShader::Initialize: create shader module failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreateDescriptorSetLayouts()) {
		GLOG(Log::eError, "VulkanShader::Initialize: create descriptor set layouts failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreatePipeline()) {
		GLOG(Log::eError, "VulkanShader::Initialize: create pipeline failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreateUniformBuffer()) {
		GLOG(Log::eError, "VulkanShader::Initialize: create uniform buffer failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!AllocateGlobalDescriptorSets()) {
		GLOG(Log::eError, "VulkanShader::Initialize: allocate descriptor sets failed for shader '%s'.", Name.CStr());
		return false;
	}

	return true;
}

bool VulkanShader::Reload() {
	if (Renderer == nullptr) {
		GLOG(Log::eError, "VulkanShader::Initialize Falied. Renderer ptr is nullptr please offer a valued ptr in construction.");
		return false;
	}

	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;
	vk::Device LogicalDevice = Context.Device.GetLogicalDevice();
	vk::AllocationCallbacks* VkAllocator = Context.Allocator;

	// Wait for frame. Can not reload when the shader in used.
	LogicalDevice.waitIdle();

	// Grab the UBO alignment requirement from the device.
	RequiredUboAlignment = Context.Device.GetDeviceProperties().limits.minUniformBufferOffsetAlignment;

	// Make sure the UBO is aligned according to device requirements.
	GlobalUboStride = PaddingAligned(GlobalUboSize, RequiredUboAlignment);
	UboStride = PaddingAligned(UboSize, RequiredUboAlignment);

	// Uniform buffer.
	vk::MemoryPropertyFlags DeviceLocalBits = Context.Device.GetIsSupportDeviceLocalHostVisible() ? vk::MemoryPropertyFlagBits::eDeviceLocal : vk::MemoryPropertyFlags();

	// Uniform buffer.
	UniformBuffer.UnmapMemory();
	MappedUniformBufferBlock = nullptr;

	// Pipeline
	Pipeline.Destroy(&vkRenderer->Context);

	// Shader modules.
	for (uint32_t i = 0; i < Config.stage_count; ++i) {
		LogicalDevice.destroyShaderModule(Stages[i].shader_module, VkAllocator);
	}

	if (!CompileShaderFile()) {
		GLOG(Log::eError, "VulkanShader::Reload: compile shader failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreateModule()) {
		GLOG(Log::eError, "VulkanShader::Reload: create shader module failed for shader '%s'.", Name.CStr());
		return false;
	}

	if (!CreatePipeline()) {
		GLOG(Log::eError, "VulkanShader::Reload: create pipeline failed for shader '%s'.", Name.CStr());
		return false;
	}

	return true;
}

void VulkanShader::Destroy(){
	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;

	vk::Device LogicalDevice = Context.Device.GetLogicalDevice();
	vk::AllocationCallbacks* VkAllocator = Context.Allocator;

	LogicalDevice.waitIdle();

	// Descriptor set layouts.
	for (uint32_t i = 0; i < Config.descriptor_set_count; ++i) {
		if (DescriptorSetLayouts[i]) {
			LogicalDevice.destroyDescriptorSetLayout(DescriptorSetLayouts[i], VkAllocator);
			DescriptorSetLayouts[i] = nullptr;
		}
	}

	// Descriptor pool
	if (DescriptorPool) {
		LogicalDevice.destroyDescriptorPool(DescriptorPool, VkAllocator);
		DescriptorPool = nullptr;
	}

	// Uniform buffer.
	UniformBuffer.UnmapMemory();
	MappedUniformBufferBlock = nullptr;

	// Pipeline
	Pipeline.Destroy(&Context);

	// Shader modules.
	for (uint32_t i = 0; i < Config.stage_count; ++i) {
		LogicalDevice.destroyShaderModule(Stages[i].shader_module, VkAllocator);
	}

	// Destroy the configuration.
	Memory::Zero(&Config, sizeof(VulkanShaderConfig));

	// Free hash mem.
	HashMap.Clear();

	// Reset status.
	Status = EShaderStatus::eShader_State_Not_Created;

	uint32_t SamplerCount = (uint32_t)GlobalTextureMaps.Size();
	for (uint32_t i = 0; i < SamplerCount; ++i) {
		GlobalTextureMaps[i] = nullptr;
	}
	GlobalTextureMaps.Clear();
	GlobalTextureMaps.Empty();
}

bool VulkanShader::Use() {
	Pipeline.Bind(GetCurrentCommandBuffer(), vk::PipelineBindPoint::eGraphics);
	return true;
}

bool VulkanShader::BindGlobal() {
	BoundScope = eShader_Scope_Global;
	BoundUboOffset = (uint32_t)GlobalUboOffset;
	return true;
}

bool VulkanShader::BindInstance(uint64_t instance_id) {
	BoundInstanceId = instance_id;
	BoundScope = eShader_Scope_Instance;

	const VulkanShaderInstanceState& State = InstanceStates[instance_id];
	BoundUboOffset = (uint32_t)State.offset;

	return true;
}

bool VulkanShader::ApplyGlobal() {
	VulkanRHI* Backend = (VulkanRHI*)Renderer->GetRenderBackend();
	VulkanContext& Context = Backend->Context;
	uint32_t       ImageIndex = Context.ImageIndex;

	vk::DescriptorSet GlobalDescSet = GlobalDescriptorSets[ImageIndex];

	// UBO
	vk::DescriptorBufferInfo BufferInfo;
	BufferInfo.setBuffer(UniformBuffer.Buffer)
		.setOffset(GlobalUboOffset)
		.setRange(GlobalUboStride);

	vk::WriteDescriptorSet UboWrite;
	UboWrite.setDstSet(GlobalDescSet)
		.setDstBinding(0)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setDescriptorCount(1)
		.setPBufferInfo(&BufferInfo);

	// Global Samplers
	uint32_t SamplerCount = (uint32_t)GlobalTextureMaps.Size();
	std::vector<vk::DescriptorImageInfo> ImageInfos(SamplerCount);
	uint32_t ValidCount = 0;

	for (uint32_t i = 0; i < SamplerCount; ++i) {
		FTextureMap* Map = GlobalTextureMaps[i];
		VulkanTexture* VkTex = Map && Map->texture
			? static_cast<VulkanTexture*>(Map->texture)
			: nullptr;
		if (!VkTex) continue;

		ImageInfos[ValidCount]
			.setImageLayout(VkTex->ShaderReadLayout)
			.setImageView(VkTex->ImageView)
			.setSampler(*reinterpret_cast<vk::Sampler*>(&Map->internal_data));
		ValidCount++;
	}

	std::vector<vk::WriteDescriptorSet> Writes;
	Writes.push_back(UboWrite);

	uint32_t GlobalSetBindingCount = Config.descriptor_sets[DESC_SET_INDEX_GLOBAL].binding_count;
	if (ValidCount > 0 && GlobalSetBindingCount > 1) {
		vk::WriteDescriptorSet SamplerWrite;
		SamplerWrite.setDstSet(GlobalDescSet)
			.setDstBinding(1)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setDescriptorCount(ValidCount)
			.setPImageInfo(ImageInfos.data());

		GLOG(Log::eError, "Global image samplers are not yet supported.");
		//Writes.push_back(SamplerWrite);
	}
	
	Context.Device.GetLogicalDevice().updateDescriptorSets(
		(uint32_t)Writes.size(), Writes.data(), 0, nullptr);

	// 绑定到命令缓冲区
	GetCurrentCommandBuffer()->CommandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		Pipeline.PipelineLayout,
		0, 1, &GlobalDescSet,
		0, nullptr);

	return true;
}

bool VulkanShader::ApplyInstance() {
	VulkanRHI* Backend = (VulkanRHI*)Renderer->GetRenderBackend();
	VulkanContext& Context = Backend->Context;
	uint32_t       ImageIndex = Context.ImageIndex;

	VulkanShaderInstanceState& State = InstanceStates[BoundInstanceId];
	vk::DescriptorSet          DescSet = State.descriptor_set_state.descriptorSets[ImageIndex];

	std::vector<vk::WriteDescriptorSet> DescriptorWrites;
	uint32_t DescriptorIndex = 0;

	// ── UBO ──────────────────────────────────────────────────────────────
	if (InstanceUniformCount > 0) {
		uint32_t* UboGeneration =
			&State.descriptor_set_state.descriptor_states[DescriptorIndex].generations[ImageIndex];

		if (*UboGeneration == INVALID_ID) {
			vk::DescriptorBufferInfo BufferInfo;
			BufferInfo.setBuffer(UniformBuffer.Buffer)
				.setOffset(State.offset)
				.setRange(UboStride);

			vk::WriteDescriptorSet UboWrite;
			UboWrite.setDstSet(DescSet)
				.setDstBinding(DescriptorIndex)
				.setDescriptorType(vk::DescriptorType::eUniformBuffer)
				.setDescriptorCount(1)
				.setPBufferInfo(&BufferInfo);

			DescriptorWrites.push_back(UboWrite);
			*UboGeneration = 1;
		}
		DescriptorIndex++;
	}

	// ── Samplers ──────────────────────────────────────────────────────────
	if (InstanceUniformSamplerCount > 0) {
		const VulkanDescriptorSetConfig& InstSetConfig =
			Config.descriptor_sets[DESC_SET_INDEX_INSTANCE];
		unsigned char SamplerBindingIndex = InstSetConfig.sampler_binding_index;
		uint32_t TotalSamplerCount =
			InstSetConfig.bindings[SamplerBindingIndex].descriptorCount;

		vk::DescriptorImageInfo ImageInfos[VULKAN_SHADER_MAX_INSTANCE_TEXTURES];
		uint32_t UpdateSamplerCount = 0;

		for (uint32_t i = 0; i < TotalSamplerCount; ++i) {
			FTextureMap* Map = State.instance_texture_maps[i];
			if (!Map) continue;

			UTexture* Tex = Map->texture;
			if (!Tex || !Tex->IsLoaded()) {
				TextureSystem& TextureSys = TextureSystem::Get();
				// fallback 到默认贴图
				switch (Map->usage) {
				case eTexture_Usage_Map_Diffuse:
					Tex = TextureSys.GetDefaultDiffuseTexture(); break;
				case eTexture_Usage_Map_Normal:
					Tex = TextureSys.GetDefaultNormalTexture(); break;
				case eTexture_Usage_Map_Specular:
					Tex = TextureSys.GetDefaultSpecularTexture(); break;
				case eTexture_Usage_Map_RoughnessMetallic:
					Tex = TextureSys.GetDefaultRoughnessMetallicTexture(); break;
				default:
					Tex = TextureSys.GetDefaultDiffuseTexture(); break;
				}
			}

			VulkanTexture* VkTex = static_cast<VulkanTexture*>(Tex);
			if (!VkTex) continue;

			ImageInfos[UpdateSamplerCount]
				.setImageLayout(VkTex->ShaderReadLayout)
				.setImageView(VkTex->ImageView)
				.setSampler(*reinterpret_cast<vk::Sampler*>(&Map->internal_data));
			UpdateSamplerCount++;
		}

		if (UpdateSamplerCount > 0) {
			vk::WriteDescriptorSet SamplerWrite;
			SamplerWrite.setDstSet(DescSet)
				.setDstBinding(SamplerBindingIndex)   // ← 从配置读
				.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
				.setDescriptorCount(UpdateSamplerCount)
				.setPImageInfo(ImageInfos);
			DescriptorWrites.push_back(SamplerWrite);
		}
	}

	if (!DescriptorWrites.empty()) {
		Context.Device.GetLogicalDevice().updateDescriptorSets(
			(uint32_t)DescriptorWrites.size(), DescriptorWrites.data(), 0, nullptr);
	}

	GetCurrentCommandBuffer()->CommandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		Pipeline.PipelineLayout,
		1, 1, &DescSet, 0, nullptr);

	return true;
}

bool VulkanShader::SetUniform(const FString& name, const void* value) {
	uint32_t Index = GetUniformIndex(name);
	if (Index == INVALID_ID) return false;
	return SetUniformByIndex(Index, value);
}

bool VulkanShader::SetUniformByIndex(uint32_t index, const void* value) {
	if (index == INVALID_ID || !value) {
		GLOG(Log::eWarn, "VulkanShader::SetUniformByIndex — 无效 index 或 value 为空。");
		return false;
	}

	ShaderUniform* Uniform = &Uniforms[index];
	return SetUniform(Uniform, value);
}

bool VulkanShader::SetUniform(const ShaderUniform* uniform, const void* value) {
	// Sampler 走单独路径
	if (uniform->type == eShader_Uniform_Type_Sampler) {
		return SetSampler(uniform, static_cast<const FTextureMap*>(value));
	}

	switch (uniform->scope) {
	case eShader_Scope_Global: {
		size_t Addr = (size_t)MappedUniformBufferBlock + GlobalUboOffset + uniform->offset;
		Memory::Copy(reinterpret_cast<void*>(Addr), value, uniform->size);
		break;
	}
	case eShader_Scope_Instance: {
		size_t Addr = (size_t)MappedUniformBufferBlock + BoundUboOffset + uniform->offset;
		Memory::Copy(reinterpret_cast<void*>(Addr), value, uniform->size);
		break;
	}
	case eShader_Scope_Local: {
		GetCurrentCommandBuffer()->CommandBuffer.pushConstants(
			Pipeline.PipelineLayout,
			vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			(uint32_t)uniform->offset,
			uniform->size,
			value);
		break;
	}
	}

	return true;
}

void VulkanShader::SetupCompileOptions(shaderc::CompileOptions& options) {
	VulkanRHI* Backend = (VulkanRHI*)Renderer->GetRenderBackend();
	uint32_t Api = Backend->Context.ApiVersion;

	if (Api >= VK_API_VERSION_1_3) {
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		options.SetTargetSpirv(shaderc_spirv_version_1_6);
	}
	else if (Api >= VK_API_VERSION_1_2) {
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
		options.SetTargetSpirv(shaderc_spirv_version_1_5);
	}
	else if (Api >= VK_API_VERSION_1_1) {
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
		options.SetTargetSpirv(shaderc_spirv_version_1_3);
	}
	else {
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
		options.SetTargetSpirv(shaderc_spirv_version_1_0);
	}
}

VulkanCommandBuffer* VulkanShader::GetCurrentCommandBuffer() {
	VulkanRHI* Backend = (VulkanRHI*)Renderer->GetRenderBackend();
	return &Backend->Context.GraphicsCommandBuffers[Backend->Context.ImageIndex];
}

bool VulkanShader::SetSamplerByIndex(uint32_t index, const FTextureMap* map) {
	ShaderUniform* Uniform = &Uniforms[index];
	return SetSampler(Uniform, map);
}

bool VulkanShader::SetSampler(const ShaderUniform* uniform, const FTextureMap* map){
	if (uniform->scope == eShader_Scope_Global) {
		if (uniform->location >= (uint32_t)GlobalTextureMaps.Size()) {
			GLOG(Log::eError, "SetSamplerByIndex — Global sampler location 越界。");
			return false;
		}
		GlobalTextureMaps[uniform->location] = const_cast<FTextureMap*>(map);
	}
	else {
		VulkanShaderInstanceState& State = InstanceStates[BoundInstanceId];
		if (uniform->location >= (uint32_t)State.instance_texture_maps.Size()) {
			GLOG(Log::eError, "SetSamplerByIndex — Instance sampler location 越界。");
			return false;
		}
		State.instance_texture_maps[uniform->location] = const_cast<FTextureMap*>(map);
	}

	return true;
}

bool VulkanShader::CreateModule() {
	if (Renderer == nullptr) return false;
	Memory::Zero(Stages, sizeof(VulkanShaderStage) * VULKAN_SHADER_MAX_STAGES);

	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	for (uint32_t i = 0; i < Config.stage_count; ++i) {
		VulkanShaderConfig vkShaderConfig = Config;
		VulkanShaderStageConfig vkShaderStageConfig = Config.stages[i];
		VulkanShaderStage* vkShaderStage = &Stages[i];

		FString FullFilePath = FString::Format("%s/%s", ResourceSystem::Get().GetRootPath(), vkShaderStageConfig.filename);
		File AssetFile(FullFilePath.CStr());
		if (!AssetFile.IsExist()) {
			GLOG(Log::eError, "Unable to create %s shader module for '%s'. Shader will be destroyed.", vkShaderStageConfig.filename, Name.CStr());
			return false;
		}

		std::vector<unsigned char> FileData = AssetFile.ReadBytes();

		Memory::Zero(&vkShaderStage->create_info, sizeof(vk::ShaderModuleCreateInfo));
		vkShaderStage->create_info.sType = vk::StructureType::eShaderModuleCreateInfo;
		// Use the resource's size and data directly.
		vkShaderStage->create_info.codeSize = FileData.size();
		vkShaderStage->create_info.pCode = (uint32_t*)FileData.data();

		vkShaderStage->shader_module = vkRenderer->Context.Device.GetLogicalDevice().createShaderModule(vkShaderStage->create_info, vkRenderer->Context.Allocator);
		ASSERT(vkShaderStage->shader_module);

		// Shader stage info.
		Memory::Zero(&vkShaderStage->shader_stage_create_info, sizeof(vk::ShaderModuleCreateInfo));
		vkShaderStage->shader_stage_create_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
		vkShaderStage->shader_stage_create_info.stage = vkShaderStageConfig.stage;
		vkShaderStage->shader_stage_create_info.module = vkShaderStage->shader_module;
		vkShaderStage->shader_stage_create_info.pName = "main";

		// Shader stage info.
		Memory::Zero(&vkShaderStage->shader_stage_create_info, sizeof(vk::ShaderModuleCreateInfo));
		vkShaderStage->shader_stage_create_info.sType = vk::StructureType::ePipelineShaderStageCreateInfo;
		vkShaderStage->shader_stage_create_info.stage = vkShaderStageConfig.stage;
		vkShaderStage->shader_stage_create_info.module = vkShaderStage->shader_module;
		vkShaderStage->shader_stage_create_info.pName = "main";
	}

	return true;
}

bool VulkanShader::CompileShaderFile(bool writeToDisk/* = true*/){
	for (uint32_t i = 0; i < Config.stage_count; ++i) {
		VulkanShaderConfig vkShaderConfig = Config;
		VulkanShaderStageConfig vkShaderStageConfig = Config.stages[i];

		// Read the resource.
		FString ShaderFile = ResourceSystem::Get().GetRootPath() + FString("/")
			+ FString(vkShaderStageConfig.filename);

		UAsset BinaryResource(ShaderFile);
		File SPVFile(ShaderFile);
		if (!SPVFile.IsExist() || Status == EShaderStatus::eShader_State_Reloading){
			ShaderStage ShadercStage;
			switch (vkShaderStageConfig.stage)
			{
			case vk::ShaderStageFlagBits::eVertex:
				ShadercStage = ShaderStage::eShader_Stage_Vertex;
				break;
			case vk::ShaderStageFlagBits::eGeometry:
				ShadercStage = ShaderStage::eShader_Stage_Geometry;
				break;
			case vk::ShaderStageFlagBits::eFragment:
				ShadercStage = ShaderStage::eShader_Stage_Fragment;
				break;
			default:
				GLOG(Log::eError, "Unknown shader stage flag.");
				return false;
			}

			CompileShaderToSPV(vkShaderStageConfig.filename, ShadercStage);
		}
	}

	return true;
}

bool VulkanShader::CreateDescriptorSetLayouts(){
	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;
	vk::Device LogicalDevice = Context.Device.GetLogicalDevice();
	vk::AllocationCallbacks* VkAllocator = vkRenderer->Context.Allocator;
	ASSERT(VkAllocator);

	// Create descriptor set layouts.
	for (uint32_t i = 0; i < Config.descriptor_set_count; ++i) {
		if (!DescriptorSetLayouts[i]) {
			vk::DescriptorSetLayoutCreateInfo LayoutInfo;
			LayoutInfo.setBindingCount(Config.descriptor_sets[i].binding_count)
				.setPBindings(Config.descriptor_sets[i].bindings);
			DescriptorSetLayouts[i] = LogicalDevice.createDescriptorSetLayout(LayoutInfo, VkAllocator);
		}
		ASSERT(DescriptorSetLayouts[i]);
	}

	return true;
}

bool VulkanShader::AllocateGlobalDescriptorSets(){
	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	vk::Device LogicalDevice = vkRenderer->Context.Device.GetLogicalDevice();

	// Allocate global descriptor sets, one per frame. Global is always the first set.
	vk::DescriptorSetLayout GlobalLayouts[3] = {
		DescriptorSetLayouts[DESC_SET_INDEX_GLOBAL],
		DescriptorSetLayouts[DESC_SET_INDEX_GLOBAL],
		DescriptorSetLayouts[DESC_SET_INDEX_GLOBAL]
	};

	vk::DescriptorSetAllocateInfo AllocInfo;
	AllocInfo.setDescriptorPool(DescriptorPool)
		.setDescriptorSetCount(3)
		.setPSetLayouts(GlobalLayouts);
	if (LogicalDevice.allocateDescriptorSets(&AllocInfo, GlobalDescriptorSets)
		!= vk::Result::eSuccess) {
		GLOG(Log::eError, "Allocate descriptor sets failed.");
		return false;
	}

	return true;
}

bool VulkanShader::CreateUniformBuffer() {
	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;

	// Uniform buffer.
	vk::MemoryPropertyFlags DeviceLocalBits = Context.Device.GetIsSupportDeviceLocalHostVisible() ? vk::MemoryPropertyFlagBits::eDeviceLocal : vk::MemoryPropertyFlags();
	// TODO: max count should be configurable, or perhaps long term support of buffer resizing.
	size_t TotalBufferSize = GlobalUboStride + (UboStride * VULKAN_MAX_MATERIAL_COUNT);
	UniformBuffer.Type = EGPUBufferType::eRenderbuffer_Type_Uniform;
	UniformBuffer.TotalSize = TotalBufferSize;
	UniformBuffer.UseFreelist = true;
	if (!UniformBuffer.Create()) {
		GLOG(Log::eError, "Vulkan buffer creation failed for object shader.");
		return false;
	}
	UniformBuffer.Bind(0);

	// Allocate space for the global UBO, which should occupy the _stride_ space, _not_ the actual size used.
	if (!UniformBuffer.AllocateMemory(GlobalUboStride, &GlobalUboOffset)) {
		GLOG(Log::eError, "Failed to allocate space for the uniform buffer!");
		return false;
	}

	// Map the entire buffer's memory.
	MappedUniformBufferBlock = UniformBuffer.MapMemory(0, TotalBufferSize);

	return true;
}

bool VulkanShader::CreatePipeline() {
	VulkanRHI* vkRenderer = static_cast<VulkanRHI*>(Renderer->GetRenderBackend());
	VulkanContext& Context = vkRenderer->Context;

	RequiredUboAlignment = Context.Device.GetDeviceProperties().limits.minUniformBufferOffsetAlignment;
	GlobalUboStride = PaddingAligned(GlobalUboSize, RequiredUboAlignment);
	UboStride = PaddingAligned(UboSize, RequiredUboAlignment);

	// Viewport.
	vk::Viewport Viewport;
	Viewport.setX(0.0f)
		.setY((float)Context.FrameBufferHeight)
		.setWidth((float)Context.FrameBufferWidth)
		.setHeight(-(float)Context.FrameBufferHeight)
		.setMinDepth(0.0f)
		.setMaxDepth(1.0f);

	// Scissor
	vk::Rect2D Scissor;
	Scissor.setOffset({ 0, 0 })
		.setExtent({ Context.FrameBufferWidth, Context.FrameBufferHeight });

	vk::PipelineShaderStageCreateInfo StageCreateInfos[VULKAN_SHADER_MAX_STAGES];
	Memory::Zero(StageCreateInfos, sizeof(vk::PipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
	for (uint32_t i = 0; i < Config.stage_count; ++i) {
		StageCreateInfos[i] = Stages[i].shader_stage_create_info;
	}

	VulkanPipelineConfig PipelineConfig;
	PipelineConfig.renderpass = Renderpass;
	PipelineConfig.stride = AttributeStride;
	PipelineConfig.attribute_count = (uint32_t)Attributes.Size();
	PipelineConfig.attributes = Config.attributes;
	PipelineConfig.descriptor_set_layout_count = Config.descriptor_set_count;
	PipelineConfig.descriptor_set_layout = DescriptorSetLayouts;
	PipelineConfig.stage_count = Config.stage_count;
	PipelineConfig.stages = StageCreateInfos;
	PipelineConfig.viewport = Viewport;
	PipelineConfig.scissor = Scissor;
	PipelineConfig.cull_mode = Config.cull_mode;
	PipelineConfig.PrimTopo = Config.PrimTopo;
	PipelineConfig.is_wireframe = Config.pology_mode == ePology_Mode_Fill ? ePology_Mode_Fill : ePology_Mode_Line;
	PipelineConfig.shaderFlags = Flags;
	PipelineConfig.push_constant_range_count = PushConstantsRangeCount;
	PipelineConfig.push_constant_ranges = PushConstantsRanges;

	return Pipeline.Create(&Context, PipelineConfig);
}