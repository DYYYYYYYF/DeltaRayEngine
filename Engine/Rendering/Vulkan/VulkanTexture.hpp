#pragma once

#include "vulkan/vulkan.hpp"
#include "Rendering/Resources/Texture/Texture.hpp"

class VulkanContext;
class VulkanCommandBuffer;

class VulkanTexture : public UTexture{
public:
	VulkanTexture(const FString& name);

public:
	virtual bool Load(const unsigned char* pixels) override;
	virtual bool LoadWriteable() override;
	virtual bool Unload() override;
	virtual void Destroy() override;

	virtual bool Resize(uint32_t new_width, uint32_t new_height) override;
	virtual bool WriteTextureData(uint64_t size, const unsigned char* pixels) override;

	virtual TArray<uint8_t> ReadTextureData(uint32_t offset, uint32_t size) override;
	virtual FColor ReadTexturePixel(uint32_t x, uint32_t y) override;

	void SetupAsWrapped(uint32_t width, uint32_t height,
		unsigned char channel_count, bool has_transparency, bool is_writeable);

private:
	vk::Format ChannelCountToFormat(unsigned char channel_count, vk::Format default_format = vk::Format::eR8G8B8A8Unorm);

public:
	void CreateImage(vk::Format format, vk::ImageTiling tiling, 
		vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memory_flags, 
		bool create_view, vk::ImageAspectFlags view_aspect_flags);

	void CreateImageView(vk::Format format, vk::ImageAspectFlags view_aspect_flags);

	/*
	* Transitions the provided image from old_layout to new_layout
	*/
	void TransitionLayout(VulkanCommandBuffer* command_buffer, vk::ImageLayout old_layout, vk::ImageLayout new_layout);

	/*
	* Copies data in buffer to provided image.
	* @param context The Vulkan context.
	* @param image The image to copy the buffer's data to.
	* @param buffer The buffer whose data will be copied
	*/
	void CopyFromBuffer(vk::Buffer buffer, VulkanCommandBuffer* command_buffer);

	/**
	 * @brief Copies data in the provided image to the given buffer.
	 * 
	 * @param context The Vulkan context.
	 * @param type The type of texture. Provides hint to layer count.
	 * @param buffer The buffer to copy to.
	 * @param commandBuffer The command buffer to be used for the copy.
	 */
	void CopyToBuffer(vk::Buffer buffer, VulkanCommandBuffer* commandBuffer);

	/**
	 * @brief Copies data in the provided image to the given buffer.
	 *
	 * @param context The Vulkan context.
	 * @param type The type of texture. Provides hint to layer count.
	 * @param buffer The buffer to copy to.
	 * @param x The x-coordinate of the pixel to copy.
	 * @param y The y-coordinate of the pixel to copy.
	 * @param commandBuffer The command buffer to be used for the copy.
	 */
	void CopyPixelToBuffer(vk::Buffer buffer, uint32_t x, uint32_t y, VulkanCommandBuffer* commandBuffer);

public:
	VulkanContext* Context;

	// 该纹理作为 shader 资源被采样时的期望布局：深度/模板纹理为 eDepthStencilReadOnlyOptimal，颜色纹理为 eShaderReadOnlyOptimal
	vk::ImageLayout ShaderReadLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::Image Image;
	vk::DeviceMemory DeviceMemory;
	vk::ImageView ImageView;
	vk::MemoryRequirements MemoryRequirements;
	vk::MemoryPropertyFlags MemoryFlags;
};

