#include "VulkanPipeline.hpp"
#include "VulkanContext.hpp"

#include "Systems/ShaderSystem.h"

#include "Core/EngineLogger.hpp"
#include "Math/MathTypes.hpp"

bool VulkanPipeline::Create(VulkanContext* context, const VulkanPipelineConfig& config){
	// Viewport state
	vk::PipelineViewportStateCreateInfo ViewportState;
	ViewportState.setViewportCount(1)
		.setPViewports(&config.viewport)
		.setScissorCount(1)
		.setPScissors(&config.scissor);

	// Rasterizer
	vk::PipelineRasterizationStateCreateInfo RasterizerCreateInfo;
	RasterizerCreateInfo.setDepthClampEnable(VK_FALSE)
		.setRasterizerDiscardEnable(VK_FALSE)
		.setPolygonMode(config.is_wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill)
		.setLineWidth(1.0f)
		.setFrontFace(vk::FrontFace::eCounterClockwise)
		.setDepthBiasEnable(VK_FALSE)
		.setDepthBiasConstantFactor(0.0f)
		.setDepthBiasClamp(0.0f)
		.setDepthBiasSlopeFactor(0.0f);
	switch (config.cull_mode)
	{
	case eFace_Cull_Mode_None:
		RasterizerCreateInfo.setCullMode(vk::CullModeFlagBits::eNone);
		break;
	case eFace_Cull_Mode_Front:
		RasterizerCreateInfo.setCullMode(vk::CullModeFlagBits::eFront);
		break;
	case eFace_Cull_Mode_Back:
		RasterizerCreateInfo.setCullMode(vk::CullModeFlagBits::eBack);
		break;
	case eFace_Cull_Mode_Front_And_Back:
		RasterizerCreateInfo.setCullMode(vk::CullModeFlagBits::eFrontAndBack);
		break;
	}

	// Multisampling
	vk::PipelineMultisampleStateCreateInfo MultisamplingCreateInfo;
	MultisamplingCreateInfo.setSampleShadingEnable(VK_FALSE)
		.setRasterizationSamples(vk::SampleCountFlagBits::e1)
		.setMinSampleShading(1.0f)
		.setPSampleMask(nullptr)
		.setAlphaToCoverageEnable(VK_FALSE)
		.setAlphaToOneEnable(VK_FALSE);

	// Depth and stencil testing
	vk::PipelineDepthStencilStateCreateInfo DepthStencil;
	if (config.shaderFlags & eShader_Flag_DepthTest) {
		DepthStencil.setDepthTestEnable(VK_TRUE);
		if (config.shaderFlags & eShader_Flag_DepthWrite) {
			DepthStencil.setDepthWriteEnable(VK_TRUE);
		}
		DepthStencil.setDepthCompareOp(vk::CompareOp::eLess)
			.setDepthBoundsTestEnable(VK_FALSE)
			.setStencilTestEnable(VK_FALSE);
	}

	uint32_t ColorAttachmentCount = 0;
	uint32_t AttachmentCount = static_cast<uint32_t>(config.renderpass->Targets[0].attachments.Size());
	for (uint32_t i = 0; i < AttachmentCount; ++i) {
		if (config.renderpass->Targets[0].attachments[i].type & RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color) {
			ColorAttachmentCount++;
		}
	}

	std::vector<vk::PipelineColorBlendAttachmentState> ColorBlendAttachmentStates(ColorAttachmentCount);
	for (uint32_t i = 0; i < ColorAttachmentCount; ++i) {
		ColorBlendAttachmentStates[i].setBlendEnable(VK_FALSE)		// 开启VK_TRUE时可能会导致透明
			.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setColorWriteMask(
				vk::ColorComponentFlagBits::eR |
				vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB |
				vk::ColorComponentFlagBits::eA
			);
	}
	
	vk::PipelineColorBlendStateCreateInfo ColorBlendStateCreateInfo;
	ColorBlendStateCreateInfo.setLogicOpEnable(VK_FALSE)
		.setLogicOp(vk::LogicOp::eCopy)
		.setAttachmentCount(ColorAttachmentCount)
		.setPAttachments(ColorBlendAttachmentStates.data());

	// Dynamic state
	const uint32_t DynamicStateCount = 3;
	vk::DynamicState DynamicStates[DynamicStateCount] = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
		vk::DynamicState::eLineWidth
	};

	vk::PipelineDynamicStateCreateInfo DynamicStateCreateInfo;
	DynamicStateCreateInfo.setDynamicStateCount(DynamicStateCount)
		.setPDynamicStates(DynamicStates);

	// Vertex input
	vk::VertexInputBindingDescription BindingDescription;
	BindingDescription.setBinding(0)
		.setStride(config.stride)
		.setInputRate(vk::VertexInputRate::eVertex);

	// Attributes
	vk::PipelineVertexInputStateCreateInfo VertexInputInfo;
	VertexInputInfo.setVertexBindingDescriptionCount(1)
		.setPVertexBindingDescriptions(&BindingDescription)
		.setVertexAttributeDescriptionCount(config.attribute_count)
		.setPVertexAttributeDescriptions(config.attributes);

	// Input assembly
	vk::PipelineInputAssemblyStateCreateInfo InputAssembly;
	InputAssembly.setPrimitiveRestartEnable(VK_FALSE);
	switch (config.PrimTopo)
	{
	case PrimitiveTopology::eTriangleList: {
		InputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList);
	} break;
	case PrimitiveTopology::eTriangleStrip: {
		InputAssembly.setTopology(vk::PrimitiveTopology::eTriangleStrip);
	}break;
	default: {
		InputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList);
	}break;
	}

	//Push constants
	vk::PipelineLayoutCreateInfo LayoutCreateInfo;
	vk::PushConstantRange Ranges[32];
	if (config.push_constant_range_count > 0) {
		if (config.push_constant_range_count > 32) {
			GLOG(Log::eError, "Vulkan graphics pipeline create: can not have more push constants.");
			return false;
		}

		// NOTE: 32 is the max number of ranges we can ever have.
		Memory::Zero(Ranges, sizeof(vk::PushConstantRange) * 32);
		for(uint32_t i = 0; i < config.push_constant_range_count; ++i) {
			Ranges[i].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
				.setOffset((uint32_t)config.push_constant_ranges[i].offset)
				.setSize((uint32_t)config.push_constant_ranges[i].size);
		}

		LayoutCreateInfo.setPushConstantRangeCount(config.push_constant_range_count)
			.setPPushConstantRanges(Ranges);
	}
	else {
		LayoutCreateInfo.setPushConstantRangeCount(0)
			.setPPushConstantRanges(nullptr);
	}

	// Pipeline layout
	LayoutCreateInfo.setSetLayoutCount(config.descriptor_set_layout_count)
		.setPSetLayouts(config.descriptor_set_layout);

	// Create pipeline layout
	PipelineLayout = context->Device.GetLogicalDevice().createPipelineLayout(LayoutCreateInfo, context->Allocator);
	ASSERT(PipelineLayout);

	// Create the pipeline
	vk::GraphicsPipelineCreateInfo PipelineCreateInfo;
	PipelineCreateInfo.setStageCount(config.stage_count)
		.setPStages(config.stages)
		.setPVertexInputState(&VertexInputInfo)
		.setPInputAssemblyState(&InputAssembly)

		.setPViewportState(&ViewportState)
		.setPRasterizationState(&RasterizerCreateInfo)
		.setPMultisampleState(&MultisamplingCreateInfo)
		.setPDepthStencilState(config.shaderFlags & eShader_Flag_DepthTest ? &DepthStencil : nullptr)
		.setPColorBlendState(&ColorBlendStateCreateInfo)
		.setPDynamicState(&DynamicStateCreateInfo)
		.setPTessellationState(nullptr)

		.setLayout(PipelineLayout)

		.setRenderPass(config.renderpass->GetRenderPass())
		.setSubpass(0)
		.setBasePipelineHandle(nullptr)
		.setBasePipelineIndex(-1);

	vk::ResultValue<vk::Pipeline> Result = context->Device.GetLogicalDevice()
		.createGraphicsPipeline(VK_NULL_HANDLE, PipelineCreateInfo, context->Allocator);
	if (Result.result != vk::Result::eSuccess) {
		GLOG(Log::eError, "Create graphics pipelines failed.");
		return false;
	}

	Handle = Result.value;
	ASSERT(Handle);

	return true;
}

void VulkanPipeline::Destroy(VulkanContext* context) {
	// Destroy pipeline
	if (Handle) {
		context->Device.GetLogicalDevice().destroyPipeline(Handle, context->Allocator);
		Handle = nullptr;
	}

	// Destroy layout
	if (PipelineLayout) {
		context->Device.GetLogicalDevice().destroyPipelineLayout(PipelineLayout, context->Allocator);
		PipelineLayout = nullptr;
	}
}

void VulkanPipeline::Bind(VulkanCommandBuffer* command_buffer, vk::PipelineBindPoint bind_point) {
	command_buffer->CommandBuffer.bindPipeline(bind_point, Handle);
}