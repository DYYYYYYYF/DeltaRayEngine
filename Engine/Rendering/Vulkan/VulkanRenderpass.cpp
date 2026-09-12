#include "VulkanRenderpass.hpp"

#include "VulkanContext.hpp"
#include "VulkanCommandBuffer.hpp"

#include "Core/DMemory.hpp"
#include "Core/EngineLogger.hpp"

bool VulkanRenderPass::Create(VulkanContext* context, const RenderpassConfig& config) {

	Depth = config.depth;
	Stencil = config.stencil;
	Context = context;

	// Main subpass
	vk::SubpassDescription Subpass;
	Subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);

	// Attachments TODO:
	std::vector<vk::AttachmentDescription> AttachmentDescriptions;
	std::vector<vk::AttachmentDescription> ColorAttachmentDescriptions;
	std::vector<vk::AttachmentDescription> DepthAttachmentDescriptions;

	// Can always just look at the first target since they are all the same(one per frame).
	// render target* taget = &Targets[0]
	vk::AttachmentDescription AttachmentDesc;
	for (uint32_t i = 0; i < config.target.attachments.Size(); ++i) {
		const RenderTargetAttachmentConfig* AttachmentConfig = &config.target.attachments[i];
		if (AttachmentConfig->type & RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color) {
			// Color attachment
			bool IsNeedClearColor = (ClearFlags & eRenderpass_Clear_Color_Buffer) != 0;

			if (AttachmentConfig->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default) {
				AttachmentDesc.setFormat(context->Swapchain.ImageFormat.format);
			}
			else {
				// TODO: configurable
				AttachmentDesc.setFormat(vk::Format::eR8G8B8A8Unorm);
			}

			AttachmentDesc.setSamples(vk::SampleCountFlagBits::e1);

			// Determine which load operation to use.
			if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_DontCare) {
				// We dont care, they only other thing that needs checking is if the attachment is being cleared.
				AttachmentDesc.setLoadOp(IsNeedClearColor ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare);
			}
			else if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Clear) {
				AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
			}
			else {
				// If we loading, check if we are also clearing. This combination doesn't make sense, and should be warned about.
				if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load) {
					if (IsNeedClearColor) {
						GLOG(Log::eWarn, "Color attachment load operation set to load, but is also set to clear. This combination is invalid.");
						AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
					}
					else {
						AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eLoad);
					}
				}
				else {
					GLOG(Log::eFatal, "Invalid and unsupported combination of load operation (0x%x) and clear flags (0x%x) for color attachment.", AttachmentDesc.loadOp, ClearFlags);
					return false;
				}
			}

			// Determine which store operation to use.
			if (AttachmentConfig->storeOperation == RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_DontCare) {
				AttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eDontCare);
			}
			else if (AttachmentConfig->storeOperation == RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store) {
				AttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eStore);
			}
			else {
				GLOG(Log::eFatal, "Invalid store operation  (0x%x) for depth attachment. Check onfiguration.", AttachmentConfig->storeOperation);
				return false;
			}

			// NOTE: These will never be used on a color attachment.
			AttachmentDesc.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
			AttachmentDesc.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
			
			// If loading, that means coming from another pass, meaning the format should be  vk::ImageLayout::eColorAttachmentOptimal.
			if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load) {
				// 如果是Load操作，检查是否来自前一个pass
				if (AttachmentConfig->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_View) {
					// G-Buffer attachment在后续pass中被load，应该来自shader-read布局
					AttachmentDesc.setInitialLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
				}
				else {
					// 其他情况使用color attachment布局
					AttachmentDesc.setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal);
				}
			}
			else {
				// Clear或DontCare操作，从UNDEFINED开始
				AttachmentDesc.setInitialLayout(vk::ImageLayout::eUndefined);
			}

			// If this is the last pass writing to this attachment, present after should be set to true.
			if (AttachmentConfig->presentAfter) {
				// 如果需要呈现到屏幕，使用present layout
				AttachmentDesc.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
			}
			else if (AttachmentConfig->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_View) {
				// 如果这是G-Buffer attachment（会被后续pass作为shader输入），使用shader read layout
				AttachmentDesc.setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
			}
			else {
				// 其他情况使用color attachment layout
				AttachmentDesc.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
			}

			// Push to color attachment description.
			ColorAttachmentDescriptions.push_back(AttachmentDesc);
		}	// Color attachment
		else if (AttachmentConfig->type & RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
			// Depth attachment
			bool IsNeedClearDepth = (ClearFlags & eRenderpass_Clear_Depth_Buffer) != 0;

			if (AttachmentConfig->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default) {
				AttachmentDesc.setFormat(Context->Device.GetDepthFormat());
			}
			else {
				// TODO: There may be a more optimal format to use when not the default depth target.
				AttachmentDesc.setFormat(Context->Device.GetDepthFormat());
			}

			AttachmentDesc.setSamples(vk::SampleCountFlagBits::e1);
			// Determine which load operation to use.
			if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_DontCare) {
				// If we don't care, they only other thing that needs checking is if the attachment is being cleared.
				AttachmentDesc.setLoadOp(IsNeedClearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare);
			}
			else if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Clear) {
				AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
			}
			else {
				// If we loading, check if we are also clearing. This combination doesn't make sense, and should be warned about.
				if (AttachmentConfig->loadOperation == RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load) {
					if (IsNeedClearDepth) {
						GLOG(Log::eWarn, "Depth attachment load operation set to load, but is also set to clear. This combination is invalid.");
						AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
					}
					else {
						AttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eLoad);
					}
				}
				else {
					GLOG(Log::eFatal, "Invalid and unsupported combination of load operation (0x%x) and clear flags (0x%x) for depth attachment.", AttachmentDesc.loadOp, ClearFlags);
					return false;
				}
			}

			// Determine which store operation to use.
			if (AttachmentConfig->storeOperation == RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_DontCare) {
				AttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eDontCare);
			}
			else if (AttachmentConfig->storeOperation == RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store) {
				AttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eStore);
			}
			else {
				GLOG(Log::eFatal, "Invalid store operation  (0x%x) for depth attachment. Check onfiguration.", AttachmentConfig->storeOperation);
				return false;
			}

			// TODO: Configurable for stencil attachments.
			AttachmentDesc.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
			AttachmentDesc.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);

			// If loading, that means coming from another pass, meaning the format should be  vk::ImageLayout::eDepthStencilAttachmentOptimal.
			AttachmentDesc.setInitialLayout(AttachmentConfig->loadOperation ==
				RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eUndefined);
			// Final layout for depth stencil attachments: transition to read-only so the
			// depth texture can be sampled by later passes (e.g. DeferredLighting reads ShadowMap).
			AttachmentDesc.setFinalLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);

			// Push to co
			DepthAttachmentDescriptions.push_back(AttachmentDesc);
		}// Depth attachment

		AttachmentDescriptions.push_back(AttachmentDesc);
	}

	// Setup the attachment references.
	uint32_t AttachmentsAdded = 0;
	// Color attachment reference.
	std::vector<vk::AttachmentReference> ColorAttachmentReferences;
	uint32_t ColorAttachmentCount = (uint32_t)ColorAttachmentDescriptions.size();
	if (ColorAttachmentCount > 0) {
		ColorAttachmentReferences.resize(ColorAttachmentCount);
		for (uint32_t i = 0; i < ColorAttachmentCount; ++i) {
			ColorAttachmentReferences[i].setAttachment(AttachmentsAdded);	// Attachment description array index
			ColorAttachmentReferences[i].setLayout(vk::ImageLayout::eColorAttachmentOptimal);
			AttachmentsAdded++;
		}

		Subpass.setColorAttachmentCount(ColorAttachmentCount);
		Subpass.setColorAttachments(ColorAttachmentReferences);
	}
	else {
		Subpass.setColorAttachmentCount(0);
		Subpass.setColorAttachments(nullptr);
	}

	// Depth attachment reference.
	std::vector<vk::AttachmentReference> DepthAttachmentReferences;
	uint32_t DepthAttachmentCount = (uint32_t)DepthAttachmentDescriptions.size();
	if (DepthAttachmentCount > 0) {
		ASSERT(DepthAttachmentCount == 1);	// Multiple depth attachments not supported.
		DepthAttachmentReferences.resize(DepthAttachmentCount);
		for (uint32_t i = 0; i < DepthAttachmentCount; ++i) {
			DepthAttachmentReferences[i].setAttachment(AttachmentsAdded);
			DepthAttachmentReferences[i].setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
			AttachmentsAdded++;
		}

		// Depth stencil data.
		Subpass.setPDepthStencilAttachment(DepthAttachmentReferences.data());
	}
	else {
		Subpass.setPDepthStencilAttachment(nullptr);
	}

	// Input from a shader
	Subpass.setInputAttachmentCount(0);
	Subpass.setPInputAttachments(nullptr);

	// Attachments used for mutisampling color attachments
	Subpass.setPResolveAttachments(nullptr);

	// Attachments not used for in this subpass, but must be preserved for the next
	Subpass.setPreserveAttachmentCount(0);
	Subpass.setPPreserveAttachments(nullptr);

	//增强的Render pass dependencies处理
	std::vector<vk::SubpassDependency> Dependencies;

	// 外部到当前subpass的依赖（标准依赖）
	vk::SubpassDependency ExternalToSubpass;
	ExternalToSubpass.setSrcSubpass(VK_SUBPASS_EXTERNAL)
		.setDstSubpass(0)
		.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setSrcAccessMask(vk::AccessFlagBits::eNone)
		.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite)
		.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
	Dependencies.push_back(ExternalToSubpass);

	// 检查是否有G-Buffer attachments需要被后续pass读取
	bool HasGBufferAttachments = false;
	for (const auto& attachment : config.target.attachments) {
		if (attachment.type & RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color &&
			attachment.source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_View) {
			HasGBufferAttachments = true;
			break;
		}
	}

	// 如果有G-Buffer attachments，添加从当前subpass到外部的依赖
	if (HasGBufferAttachments) {
		vk::SubpassDependency SubpassToExternal;
		SubpassToExternal.setSrcSubpass(0)
			.setDstSubpass(VK_SUBPASS_EXTERNAL)
			.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
			.setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
			.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
			.setDstAccessMask(vk::AccessFlagBits::eShaderRead)
			.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
		Dependencies.push_back(SubpassToExternal);
	}

	// Render pass create
	vk::RenderPassCreateInfo CreateInfo;
	CreateInfo.setAttachmentCount((uint32_t)AttachmentDescriptions.size())
		.setAttachments(AttachmentDescriptions)
		.setSubpassCount(1)
		.setPSubpasses(&Subpass)
		.setDependencyCount((uint32_t)Dependencies.size())
		.setPDependencies(Dependencies.data())
		.setPNext(nullptr);

	if (context->Device.GetLogicalDevice().createRenderPass(&CreateInfo,
		context->Allocator, (vk::RenderPass*)&Renderpass) != vk::Result::eSuccess) {
		GLOG(Log::eError, "VulkanRenderPass::Create() Failed to create renderpass.");
	}

	return true;
}

void VulkanRenderPass::Destroy() {
	if (Renderpass) {
		Context->Device.GetLogicalDevice().destroyRenderPass(GetRenderPass(), Context->Allocator);
		Renderpass = nullptr;
	}
}

void VulkanRenderPass::Begin(RenderTarget* target) {
	if (!target) {
		GLOG(Log::eError, "VulkanRenderPass::Begin - RenderTarget is null");
		return;
	}

	VulkanCommandBuffer* CmdBuffer = &Context->GraphicsCommandBuffers[Context->ImageIndex];

	// 设置渲染区域
	vk::Rect2D Area;
	Area.setOffset({ (int32_t)RenderArea.x, (int32_t)RenderArea.y })
		.setExtent({ (uint32_t)RenderArea.z, (uint32_t)RenderArea.w });

	// 准备RenderPassBeginInfo
	vk::RenderPassBeginInfo BeginInfo;
	BeginInfo.setRenderPass(*(vk::RenderPass*)&Renderpass)
		.setFramebuffer(*((vk::Framebuffer*)&target->internal_framebuffer))
		.setRenderArea(Area);

	// 计算所需的清除值数组大小
	uint32_t AttachmentCount = (uint32_t)target->attachments.Size();
	if (AttachmentCount == 0) {
		GLOG(Log::eWarn, "VulkanRenderPass::Begin - No attachments in target");
		BeginInfo.setClearValueCount(0);
		BeginInfo.setPClearValues(nullptr);
	}
	else {
		// 为所有附件分配清除值（按附件索引组织）
		const uint32_t MAX_ATTACHMENTS = 8; // 定义最大附件数，避免栈溢出
		vk::ClearValue ClearValues[MAX_ATTACHMENTS];

		if (AttachmentCount > MAX_ATTACHMENTS) {
			GLOG(Log::eError, "VulkanRenderPass::Begin - Too many attachments: %u (max: %u)",
				AttachmentCount, MAX_ATTACHMENTS);
			AttachmentCount = MAX_ATTACHMENTS;
		}

		// 零初始化所有清除值
		Memory::Zero(ClearValues, sizeof(vk::ClearValue) * AttachmentCount);

		// 验证清除标志和深度值
		bool IsNeedClearColor = (ClearFlags & eRenderpass_Clear_Color_Buffer) != 0;
		bool IsNeedClearDepth = (ClearFlags & eRenderpass_Clear_Depth_Buffer) != 0;
		bool IsNeedClearStencil = (ClearFlags & eRenderpass_Clear_Stencil_Buffer) != 0;

		// 验证深度值范围
		if (IsNeedClearDepth && (Depth < 0.0f || Depth > 1.0f)) {
			GLOG(Log::eError, "VulkanRenderPass::Begin - Invalid depth clear value: %f (must be [0.0, 1.0])", Depth);
			// 修正深度值
			float SafeDepth = Clamp(Depth, 0.0f, 1.0f);
			GLOG(Log::eWarn, "VulkanRenderPass::Begin - Clamping depth from %f to %f", Depth, SafeDepth);
		}

		// 验证模板值范围
		if (IsNeedClearStencil && Stencil > 255) {
			GLOG(Log::eError, "VulkanRenderPass::Begin - Invalid stencil clear value: %u (must be [0, 255])", Stencil);
		}

		// 按附件索引设置清除值
		for (uint32_t i = 0; i < AttachmentCount; ++i) {
			const RenderTargetAttachment& attachment = target->attachments[i];

			if (attachment.type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color) {
				// 颜色附件
				if (IsNeedClearColor) {
					// 验证颜色值
					if (isnan(ClearColor.x) || isnan(ClearColor.y) || isnan(ClearColor.z) || isnan(ClearColor.w) ||
						isinf(ClearColor.x) || isinf(ClearColor.y) || isinf(ClearColor.z) || isinf(ClearColor.w)) {
						GLOG(Log::eError, "VulkanRenderPass::Begin - Invalid color clear values (NaN/Inf): (%f, %f, %f, %f)",
							ClearColor.x, ClearColor.y, ClearColor.z, ClearColor.w);
						// 使用安全的默认值
						ClearValues[i].color.float32[0] = 0.0f;
						ClearValues[i].color.float32[1] = 0.0f;
						ClearValues[i].color.float32[2] = 0.0f;
						ClearValues[i].color.float32[3] = 0.0f;
					}
					else {
						ClearValues[i].color.float32[0] = ClearColor.x;
						ClearValues[i].color.float32[1] = ClearColor.y;
						ClearValues[i].color.float32[2] = ClearColor.z;
						ClearValues[i].color.float32[3] = ClearColor.w;
					}
				}
				else {
					// 即使不清除，也设置默认值
					ClearValues[i].color.float32[0] = 0.0f;
					ClearValues[i].color.float32[1] = 0.0f;
					ClearValues[i].color.float32[2] = 0.0f;
					ClearValues[i].color.float32[3] = 0.0f;
				}
			}
			else if (attachment.type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
				// 深度附件
				if (IsNeedClearDepth) {
					float SafeDepth = Clamp(Depth, 0.0f, 1.0f);
					ClearValues[i].depthStencil.depth = SafeDepth;
				}
				else {
					ClearValues[i].depthStencil.depth = 1.0f; // 默认最远深度
				}

				if (IsNeedClearStencil) {
					uint32_t SafeStencil = Clamp(Stencil, 0u, 255u);
					ClearValues[i].depthStencil.stencil = SafeStencil;
				}
				else {
					ClearValues[i].depthStencil.stencil = 0;
				}
			}
			else {
				GLOG(Log::eWarn, "VulkanRenderPass::Begin - Unknown attachment type %d at index %u",
					(int)attachment.type, i);
			}
		}

		// 设置清除值
		BeginInfo.setClearValueCount(AttachmentCount);
		BeginInfo.setPClearValues(ClearValues);
	}

	// 开始渲染通道
	try {
		CmdBuffer->CommandBuffer.beginRenderPass(BeginInfo, vk::SubpassContents::eInline);
		CmdBuffer->State = VulkanCommandBufferState::eCommand_Buffer_State_In_Renderpass;
	}
	catch (const vk::SystemError& e) {
		GLOG(Log::eError, "VulkanRenderPass::Begin - Failed to begin render pass: %s", e.what());

		// 输出调试信息
		GLOG(Log::eError, "Debug info:");
		GLOG(Log::eError, "  Attachment count: %u", AttachmentCount);
		GLOG(Log::eError, "  Clear value count: %u", BeginInfo.clearValueCount);
		GLOG(Log::eError, "  Clear flags: 0x%x", ClearFlags);
		GLOG(Log::eError, "  Render area: (%f, %f, %f, %f)", RenderArea.x, RenderArea.y, RenderArea.z, RenderArea.w);

		throw; // 重新抛出异常
	}
}

void VulkanRenderPass::End() {
	VulkanCommandBuffer* CmdBuffer = &Context->GraphicsCommandBuffers[Context->ImageIndex];
	CmdBuffer->CommandBuffer.endRenderPass();
	CmdBuffer->State = VulkanCommandBufferState::eCommand_Buffer_State_Recording;
}
