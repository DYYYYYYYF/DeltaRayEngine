#include "VulkanTexture.hpp"
#include "VulkanContext.hpp"

#include "Core/DMemory.hpp"
#include "Core/EngineLogger.hpp"

#include "VulkanBackend.hpp"
#include "Rendering/Renderer.hpp"

VulkanTexture::VulkanTexture(const FString& name) : UTexture(name) {
	IRenderer* Renderer = IRenderer::GetRenderer();
	if (!Renderer) {
		return;
	}

	VulkanRHI* Backend = Cast<VulkanRHI*>(Renderer->GetRenderBackend());
	if (!Backend) {
		return;
	}

	Context = &Backend->Context;
}

bool VulkanTexture::Load(const unsigned char* pixels) {
	if (!Context) {
		GLOG(Log::eError, "VulkanTexture::Load() Context is null.");
		return false;
	}

	// Internal data creation.
	vk::DeviceSize ImageSize = Width * Height * ChannelCount * (Type == TextureType::eTexture_Type_Cube ? 6 : 1);

	// NOTE: Assumes 8 bits per channel.
	vk::Format ImageFormat = vk::Format::eR8G8B8A8Unorm;

	// NOTE: Lots of assumptions here, different texture types will require.
	// different options here.
	CreateImage(ImageFormat,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
		vk::MemoryPropertyFlagBits::eDeviceLocal,
		true, vk::ImageAspectFlagBits::eColor);

	// Load the data.
	if (!WriteTextureData(ImageSize, pixels)) {
		return false;
	}

	SetLoaded();
	return true;
}
bool VulkanTexture::LoadWriteable(){
	vk::ImageUsageFlags Usage;
	vk::ImageAspectFlags Aspect;
	vk::Format ImageFormat;
	if (Flags & TextureFlagBits::eTexture_Flag_Depth) {
		Usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
		Aspect = vk::ImageAspectFlagBits::eDepth;
		ImageFormat = Context->Device.GetDepthFormat();
	}
	else {
		Usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
			| vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
		Aspect = vk::ImageAspectFlagBits::eColor;
		ImageFormat = ChannelCountToFormat((unsigned char)ChannelCount, vk::Format::eR8G8B8A8Unorm);
	}

	CreateImage(ImageFormat, vk::ImageTiling::eOptimal,
		Usage, vk::MemoryPropertyFlagBits::eDeviceLocal, true, Aspect);

	// 标志位加载完成
	SetLoaded();

	return true;
}

bool VulkanTexture::Resize(uint32_t new_width, uint32_t new_height) {
	// Resizing is really just destroying the old image and creating a new one.
	// Data is not preserved because there's no reliable way to map the old data to 
	// the new since the amount of data differs.
	Destroy();
	SetLoaded(false);

	vk::Format ImageFormat = ChannelCountToFormat((unsigned char)ChannelCount, vk::Format::eR8G8B8A8Unorm);

	// TODO: Lots of assumptions here, different texture types will require different options here.
	CreateImage(ImageFormat, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
		vk::MemoryPropertyFlagBits::eDeviceLocal, true, vk::ImageAspectFlagBits::eColor);

	// 标志位加载完成
	SetLoaded();

	return true;
}

bool VulkanTexture::WriteTextureData(uint64_t size, const unsigned char* pixels){
	// Create a staging buffer and load data into it.
	VulkanBuffer Staging;
	Staging.Type = EGPUBufferType::eRenderbuffer_Type_Staging;
	Staging.TotalSize = size;
	Staging.UseFreelist = false;
	if (!Staging.Create()) {
		GLOG(Log::eError, "Failed to create staging buffer for texture write.");
		return false;
	}

	Staging.Bind(0);
	Staging.Load(0, size, pixels);

	VulkanCommandBuffer TempBuffer;
	vk::CommandPool Pool = Context->Device.GetGraphicsCommandPool();
	vk::Queue Queue = Context->Device.GetGraphicsQueue();
	TempBuffer.AllocateAndBeginSingleUse(Context, Pool);

	// Transition the layout from whatever it is currently to optimal for reciving data.
	TransitionLayout(&TempBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

	// Copy the data from the buffer.
	CopyFromBuffer(Staging.Buffer, &TempBuffer);

	// Transition from optimal for data reciept to shader-read-only optimal layout.
	TransitionLayout(&TempBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	TempBuffer.EndSingleUse(Context, Pool, Queue);

	Staging.UnBind();
	Staging.Destroy();

	return true;
}

TArray<uint8_t> VulkanTexture::ReadTextureData(uint32_t offset, uint32_t size) {
	// Create a staging buffer and load data into it.
	VulkanBuffer Staging;
	Staging.Type = EGPUBufferType::eRenderbuffer_Type_Read;
	Staging.TotalSize = size;
	Staging.UseFreelist = false;
	if (!Staging.Create()) {
		GLOG(Log::eError, "Failed to create staging buffer for texture read.");
		return TArray<uint8_t>();
	}
	Staging.Bind(0);

	VulkanCommandBuffer TempBuffer;
	vk::CommandPool Pool = Context->Device.GetGraphicsCommandPool();
	vk::Queue Queue = Context->Device.GetGraphicsQueue();
	TempBuffer.AllocateAndBeginSingleUse(Context, Pool);

	// NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	// Transition the layout from whatever it is currently to optimal for handing out data.
	TransitionLayout(&TempBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);

	// Copy the data to the buffer.
	CopyToBuffer(Staging.Buffer, &TempBuffer);

	TransitionLayout(&TempBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	TempBuffer.EndSingleUse(Context, Pool, Queue);

	TArray<uint8_t> TextureData = Staging.Read(offset, size);
	Staging.UnBind();
	Staging.Destroy();

	return TextureData;
}

FColor VulkanTexture::ReadTexturePixel(uint32_t x, uint32_t y) {
	// TODO: creating a buffer every time isn't great. Could optimize this by creating a buffer once
	// and just reusing it.
	// 
	// Create a staging buffer and load data into it.
	VulkanBuffer Staging;
	Staging.Type = EGPUBufferType::eRenderbuffer_Type_Read;
	Staging.TotalSize = sizeof(unsigned char) * 4;
	Staging.UseFreelist = false;
	if (!Staging.Create()) {
		GLOG(Log::eError, "Failed to create staging buffer for pixel read. Return Vector4()");
		return FColor();
	}
	Staging.Bind(0);

	VulkanCommandBuffer TempBuffer;
	vk::CommandPool Pool = Context->Device.GetGraphicsCommandPool();
	vk::Queue Queue = Context->Device.GetGraphicsQueue();
	TempBuffer.AllocateAndBeginSingleUse(Context, Pool);

	// NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	// Transition the layout from whatever it is currently to optimal for handing out data.
	TransitionLayout(&TempBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);

	// Copy the data to the buffer.
	CopyPixelToBuffer(Staging.Buffer, x, y, &TempBuffer);

	TransitionLayout(&TempBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	TempBuffer.EndSingleUse(Context, Pool, Queue);

	TArray<uint8_t> PixelData = Staging.Read(0, sizeof(unsigned char) * 4);

	Staging.UnBind();
	Staging.Destroy();

	return FColor(PixelData);
}

void VulkanTexture::SetupAsWrapped(uint32_t width, uint32_t height, 
	unsigned char channel_count, bool has_transparency, bool is_writeable) {
	SetTextureType(TextureType::eTexture_Type_2D);
	SetWidth(width);
	SetHeight(height);
	SetChannelCount(channel_count);
	AddFlag(has_transparency ? TextureFlagBits::eTexture_Flag_Has_Transparency : 0);
	AddFlag(is_writeable ? TextureFlagBits::eTexture_Flag_Is_Writeable : 0);
	AddFlag(TextureFlagBits::eTexture_Flag_Is_Wrapped);
}

bool VulkanTexture::Unload() {
	return true;
}

void VulkanTexture::Destroy() {
	if (!Context) {
		GLOG(Log::eError, "VulkanTexture::Destroy() Context is null.");
		return;
	}

	vk::Device LogicalDevice = Context->Device.GetLogicalDevice();
	LogicalDevice.waitIdle();

	if (ImageView) {
		LogicalDevice.destroyImageView(ImageView, Context->Allocator);
		ImageView = nullptr;
	}

	if (DeviceMemory) {
		LogicalDevice.freeMemory(DeviceMemory, Context->Allocator);
		DeviceMemory = nullptr;
	}

	if (Image) {
		LogicalDevice.destroyImage(Image, Context->Allocator);
		Image = nullptr;
	}

	// Report the memory as no longer in-use.
	bool IsDeviceMemory = (MemoryFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal;
	Memory::FreeReport(MemoryRequirements.size, IsDeviceMemory ? MemoryType::eMemory_Type_GPU_Local : MemoryType::eMemory_Type_Vulkan);
	Memory::Zero(&MemoryRequirements, sizeof(vk::MemoryRequirements));
}

void VulkanTexture::CreateImage(vk::Format format, vk::ImageTiling tiling,
	vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memory_flags, bool create_view, vk::ImageAspectFlags view_aspect_flags) {
	MemoryFlags = memory_flags;

	// 按视图的 aspect 自动判定该纹理被采样时的期望布局：深度/模板 -> eDepthStencilReadOnlyOptimal，其余 -> eShaderReadOnlyOptimal
	ShaderReadLayout = (view_aspect_flags & vk::ImageAspectFlagBits::eDepth) ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::Device LogicalDevice = Context->Device.GetLogicalDevice();

	vk::Extent3D Extent;
	Extent.setWidth(Width)
		.setHeight(Height)
		.setDepth(1);			// TODO: Support configurable depth

	vk::ImageCreateInfo ImageCreateInfo;
	switch (Type)
	{
	default:
	case eTexture_Type_2D:
	case eTexture_Type_Cube:
		ImageCreateInfo.setImageType(vk::ImageType::e2D);
		break;
	}

	ImageCreateInfo.setExtent(Extent)
		.setMipLevels(4)		// TODO: Support mip mapping
		.setArrayLayers(1)		
		.setFormat(format)
		.setTiling(tiling)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setUsage(usage)
		.setSamples(vk::SampleCountFlagBits::e1)		// TODO: Configurable sample count
		.setSharingMode(vk::SharingMode::eExclusive);	// TODO: Configurable sharing mode
	if (Type == TextureType::eTexture_Type_Cube) {
		ImageCreateInfo.setFlags(vk::ImageCreateFlagBits::eCubeCompatible)
			.setArrayLayers(6);
	}

	Image = LogicalDevice.createImage(ImageCreateInfo, Context->Allocator);
	ASSERT(Image);

	// Query memory requirements
	MemoryRequirements = LogicalDevice.getImageMemoryRequirements(Image);
	uint32_t MemoryType = Context->FindMemoryIndex(MemoryRequirements.memoryTypeBits, MemoryFlags);
	if (MemoryType == INVALID_ID) {
		GLOG(Log::eError, "Required memory type not found. Image not vaild.");
	}

	// Allocate memory
	vk::MemoryAllocateInfo MemoryAllocateInfo;
	MemoryAllocateInfo.setAllocationSize(MemoryRequirements.size)
		.setMemoryTypeIndex(MemoryType);
	DeviceMemory = LogicalDevice.allocateMemory(MemoryAllocateInfo, Context->Allocator);
	ASSERT(DeviceMemory);

	// Bind memory
	LogicalDevice.bindImageMemory(Image, DeviceMemory, 0);

	// Report the memory as in-use.
	bool IsDeviceMemory = (MemoryFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal;
	Memory::AllocateReport(MemoryRequirements.size, IsDeviceMemory ? MemoryType::eMemory_Type_GPU_Local : MemoryType::eMemory_Type_Vulkan);

	// Create image view
	if (create_view) {
		CreateImageView(format, view_aspect_flags);
	}


#ifdef LEVEL_DEBUG
	FString TypeName = "VulkanTexture_Image_" + Name;
	vk::DebugUtilsObjectNameInfoEXT NameInfo;
	NameInfo.setObjectType(vk::ObjectType::eImage)
		.setObjectHandle(reinterpret_cast<uint64_t>(static_cast<VkImage>(Image)))
		.setPObjectName(TypeName.CStr());

	Context->Device.GetLogicalDevice().setDebugUtilsObjectNameEXT(
		NameInfo, Context->DynamicLoader);
#endif
}

void VulkanTexture::CreateImageView(vk::Format format, vk::ImageAspectFlags view_aspect_flags) {
	vk::ImageSubresourceRange Range;
	Range.setAspectMask(view_aspect_flags)
		// TODO: Make configurable
		.setBaseMipLevel(0)
		.setLevelCount(1)
		.setBaseArrayLayer(0)
		.setLayerCount(Type == TextureType::eTexture_Type_Cube ? 6 : 1);

	vk::ImageViewCreateInfo ImageViewCreateInfo;
	ImageViewCreateInfo.setImage(Image)
		.setFormat(format)
		.setSubresourceRange(Range);

	switch (Type)
	{
	default:
	case eTexture_Type_2D:
		ImageViewCreateInfo.setViewType(vk::ImageViewType::e2D);
		break;
	case eTexture_Type_Cube:
		ImageViewCreateInfo.setViewType(vk::ImageViewType::eCube);
		break;
	}

	ImageView = Context->Device.GetLogicalDevice().createImageView(ImageViewCreateInfo, Context->Allocator);
	ASSERT(ImageView);
}

void VulkanTexture::TransitionLayout(VulkanCommandBuffer* command_buffer, vk::ImageLayout old_layout, vk::ImageLayout new_layout) {
	vk::ImageMemoryBarrier Barrier;
	Barrier.setOldLayout(old_layout)
		.setNewLayout(new_layout)
		.setSrcQueueFamilyIndex(Context->Device.GetQueueFamilyInfo()->graphics_index)
		.setDstQueueFamilyIndex(Context->Device.GetQueueFamilyInfo()->graphics_index)
		.setImage(Image);

	vk::ImageSubresourceRange Range;
	Range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseMipLevel(0)
		.setLevelCount(1)
		.setBaseArrayLayer(0)
		.setLayerCount(Type == TextureType::eTexture_Type_Cube ? 6 : 1);
	Barrier.setSubresourceRange(Range);

	vk::PipelineStageFlags SrcStage;
	vk::PipelineStageFlags DstStage;

	// Dont't care about the old layout - transition to optimal layout
	if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
		Barrier.setSrcAccessMask(vk::AccessFlagBits::eNone)
			.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
		// Dont't care what stage the pipeline is in at the start.
		SrcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		// Used for copying
		DstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		// Transition from a transfer destination layout to a shader-readonly layout
		Barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
		// From a copying stage to.
		SrcStage = vk::PipelineStageFlagBits::eTransfer;
		// The fragment stage.
		DstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferSrcOptimal) {
		// Transition from a transfer destination layout to a shader-readonly layout
		Barrier.setSrcAccessMask(vk::AccessFlagBits::eNone)
			.setDstAccessMask(vk::AccessFlagBits::eTransferRead);
		// From a copying stage to.
		SrcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		// The transfer stage.
		DstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (old_layout == vk::ImageLayout::eTransferSrcOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		// Transition from a transfer destination layout to a shader-readonly layout
		Barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
			.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
		// From a copying stage to.
		SrcStage = vk::PipelineStageFlagBits::eTransfer;
		// The fragment stage.
		DstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else {
		GLOG(Log::eFatal, "Unsupported layout transition!");
		return;
	}

	command_buffer->CommandBuffer.pipelineBarrier(SrcStage, DstStage, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, 1, &Barrier);
}

void VulkanTexture::CopyFromBuffer(vk::Buffer buffer, VulkanCommandBuffer* command_buffer) {
	// Region to copy
	vk::BufferImageCopy Region;
	Memory::Zero(&Region, sizeof(vk::BufferImageCopy));
	Region.setBufferOffset(0)
		.setBufferImageHeight(0)
		.setBufferRowLength(0);

	// Subresouce
	vk::ImageSubresourceLayers Subresource;
	Subresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setMipLevel(0)
		.setBaseArrayLayer(0)
		.setLayerCount(Type == TextureType::eTexture_Type_Cube ? 6 : 1);
	Region.setImageSubresource(Subresource);

	// Extent
	vk::Extent3D Extent;
	Extent.setWidth(Width)
		.setHeight(Height)
		.setDepth(1);
	Region.setImageExtent(Extent);

	command_buffer->CommandBuffer.copyBufferToImage(buffer, Image, vk::ImageLayout::eTransferDstOptimal, 1, &Region);
}

void VulkanTexture::CopyToBuffer(vk::Buffer buffer, VulkanCommandBuffer* commandBuffer) {
	vk::BufferImageCopy Region;
	Region.setBufferOffset(0)
		.setBufferRowLength(0)
		.setBufferImageHeight(0);

	// Subresouce
	vk::ImageSubresourceLayers Subresource;
	Subresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setMipLevel(0)
		.setBaseArrayLayer(0)
		.setLayerCount(Type == TextureType::eTexture_Type_Cube ? 6 : 1);
	Region.setImageSubresource(Subresource);

	// Extent
	vk::Extent3D Extent;
	Extent.setWidth(Width)
		.setHeight(Height)
		.setDepth(1);
	Region.setImageExtent(Extent);

	commandBuffer->CommandBuffer.copyImageToBuffer(Image, vk::ImageLayout::eTransferSrcOptimal, buffer, 1, &Region);
}

void VulkanTexture::CopyPixelToBuffer(vk::Buffer buffer, uint32_t x, uint32_t y, VulkanCommandBuffer* commandBuffer) {
	vk::BufferImageCopy Region;
	Region.setBufferOffset(0)
		.setBufferRowLength(0)
		.setBufferImageHeight(0);

	// Subresouce
	vk::ImageSubresourceLayers Subresource;
	Subresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setMipLevel(0)
		.setBaseArrayLayer(0)
		.setLayerCount(Type == TextureType::eTexture_Type_Cube ? 6 : 1);
	Region.setImageSubresource(Subresource);

	// Extent
	vk::Extent3D Extent;
	Extent.setWidth(1)
		.setHeight(1)
		.setDepth(1);
	Region.setImageExtent(Extent)
		.setImageOffset({ (int)x, (int)y });

	commandBuffer->CommandBuffer.copyImageToBuffer(Image, vk::ImageLayout::eTransferSrcOptimal, buffer, 1, &Region);
}


vk::Format VulkanTexture::ChannelCountToFormat(unsigned char channel_count, vk::Format default_format /*= vk::Format::eR8G8B8A8Unorm*/) {
	switch (channel_count)
	{
	case 1:
		return vk::Format::eR8Unorm;
	case 2:
		return vk::Format::eR8G8Unorm;
	case 3:
		return vk::Format::eR8G8B8Unorm;
	case 4:
		return vk::Format::eR8G8B8A8Unorm;
	default:
		return default_format;
	}
}
