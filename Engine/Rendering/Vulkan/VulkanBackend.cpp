#include "VulkanBackend.hpp"
#include "VulkanPlatform.hpp"
#include "VulkanDevice.hpp"
#include "VulkanTexture.hpp"
#include "VulkanAllocator.hpp"

#include "Core/Event.hpp"
#include "Core/EngineLogger.hpp"
#include "Containers/TArray.hpp"
#include "Platform/File/File.hpp"
#include "Platform/Platform.hpp"
#include "Math/MathTypes.hpp"

#include "Rendering/Resources/Texture/Texture.hpp"
#include "Rendering/Resources/Geometry/Geometry.hpp"

#include "Systems/MaterialSystem.h"
#include "Systems/GeometrySystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/TextureSystem.h"
#include "Systems/ShaderSystem.h"

#ifdef LEVEL_DEBUG
/*
* Vulkan debug callback
*/
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_servity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
	void* user_data
) {
	switch (message_servity) {
	default:
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		GLOG(Log::eError, callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		GLOG(Log::eWarn, callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		GLOG(Log::eFatal, callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		GLOG(Log::eInfo, callback_data->pMessage);
		break;
	}

	return VK_FALSE;
}
#endif


VulkanRHI::VulkanRHI() {

	Context.Allocator = nullptr;

	// TODO: Implement muti-thread
	Context.EnableMultithreading = Platform::GetProcessorCount() > 1;
	Context.FrameBufferWidth = 800;
	Context.FrameBufferHeight = 600;
}

VulkanRHI::~VulkanRHI() {

}

bool VulkanRHI::Initialize(const RenderBackendConfig* config, unsigned char* out_window_render_target_count, struct SPlatformState* plat_state) {
	// NOTE: Custom allocator;
#if DVULKAN_USE_CUSTOM_ALLOCATOR == 1
	Context.Allocator = (vk::AllocationCallbacks*)Memory::Allocate(sizeof(vk::AllocationCallbacks), MemoryType::eMemory_Type_Renderer);
	if (Context.Allocator != nullptr) {
		if (!VulkanAllocator::Create(Context.Allocator, &Context)) {
			GLOG(Log::eWarn, "Failed to create custom vulkan allocator! Continuing using the default allocator.");
			Memory::Free(Context.Allocator, MemoryType::eMemory_Type_Application);
			Context.Allocator = nullptr;
		}
	}
#endif

	vk::InstanceCreateInfo InstanceInfo;

	// Check version
	uint32_t InstanceVersion = QueryInstanceVersion();
	uint32_t TargetVersion = SelectTargetApiVersion(InstanceVersion);
	Context.ApiVersion = TargetVersion;

	// Application
	vk::ApplicationInfo ApplicationInfo;
	ApplicationInfo.setApiVersion(TargetVersion)
		.setApplicationVersion(1)
		.setPApplicationName(config->application_name.c_str())
		.setEngineVersion(1)
		.setPEngineName("Dimension Engine");
	InstanceInfo.setPApplicationInfo(&ApplicationInfo);

	// Obtain a list of required extensions
	std::vector<const char*> RequiredExtensions;
	RequiredExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	GetPlatformRequiredExtensionNames(RequiredExtensions);
#ifdef LEVEL_DEBUG
	RequiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	std::string output = "\nRequeired extensions : \n";
	for (int i = 0; i < RequiredExtensions.size(); i++) {
		const char* temp = RequiredExtensions[i];
		output.append("\t");
		output.append(temp);
		output.append("\n");
	}
	GLOG(Log::eDebug, "%s", output.c_str());
#endif

	// Extemsop
	uint32_t AvailableExtensionsCount = 0;
	vk::Result Result = vk::enumerateInstanceExtensionProperties(nullptr, &AvailableExtensionsCount, nullptr);
	if (Result != vk::Result::eSuccess) {
		GLOG(Log::eFatal, "Enum instance extension properties failed.");
		return false;
	}
	std::vector<vk::ExtensionProperties> AvailableExtensions;
	AvailableExtensions.resize(AvailableExtensionsCount);
	Result = vk::enumerateInstanceExtensionProperties(nullptr, &AvailableExtensionsCount, AvailableExtensions.data());
	ASSERT(Result == vk::Result::eSuccess);

	// Verify all extensions are available.
	for (uint32_t i = 0; i < RequiredExtensions.size(); ++i) {
		bool Found = false;
		for (uint32_t j = 0; j < AvailableExtensionsCount; j++) {
			if (strcmp(RequiredExtensions[i], AvailableExtensions[j].extensionName) == 0) {
				Found = true;
				GLOG(Log::eDebug, "Required extension found: %s.", RequiredExtensions[i]);
				break;
			}
		}

		if (!Found) {
			GLOG(Log::eFatal, "Required extension is missing: %s!", RequiredExtensions[i]);
			return false;
		}
	}

	AvailableExtensions.clear();
	std::vector<vk::ExtensionProperties>().swap(AvailableExtensions);

	// Validation layers
	std::vector<const char*> RequiredValidationLayerName;

#ifdef LEVEL_DEBUG
	GLOG(Log::eInfo, "Validation layers enabled. Enumerating ...");
	// List of validation layers required
	RequiredValidationLayerName.push_back("VK_LAYER_KHRONOS_validation");

	// Obtain a list of available validation layers
	uint32_t AvailableLayersCount = 0;
	std::vector<vk::LayerProperties> AvailableLayers;
	vk::Result result;
	result = vk::enumerateInstanceLayerProperties(&AvailableLayersCount, AvailableLayers.data());
	if (result != vk::Result::eSuccess) {
		GLOG(Log::eFatal, "Enum instance layer properties failed.");
		return false;
	}

	AvailableLayers.resize(AvailableLayersCount);
	result = vk::enumerateInstanceLayerProperties(&AvailableLayersCount, AvailableLayers.data());
	ASSERT(result == vk::Result::eSuccess);

	// Verify all required layers are available.
	for (uint32_t i = 0; i < RequiredValidationLayerName.size(); i++) {
		GLOG(Log::eDebug, "Searching for layer: %s...", RequiredValidationLayerName[i]);
		
		bool IsFound = false;
		for (uint32_t j = 0; j < AvailableLayersCount; j++) {
			if (strcmp(RequiredValidationLayerName[i], AvailableLayers[j].layerName.data()) == 0) {
				IsFound = true;
				GLOG(Log::eDebug, "Found.");
				break;
			}
		}

		if (!IsFound) {
			GLOG(Log::eWarn, "Required validation layer is missing: '%s'!", RequiredValidationLayerName[i]);

			// TODO: Remove spec-element.
			RequiredValidationLayerName.clear();
		}
	}

	AvailableLayers.clear();
	std::vector<vk::LayerProperties>().swap(AvailableLayers);
#endif

	// Extensions
	InstanceInfo.setEnabledExtensionCount((uint32_t)RequiredExtensions.size())
		.setPEnabledExtensionNames(RequiredExtensions);
	// Layers
	InstanceInfo.setEnabledLayerCount((uint32_t)RequiredValidationLayerName.size())
		.setPEnabledLayerNames(RequiredValidationLayerName);

#if defined (DPLATFORM_MACOS)
  InstanceInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;    
#endif

	Context.Instance = vk::createInstance(InstanceInfo, Context.Allocator);
	ASSERT(Context.Instance);

#ifdef LEVEL_DEBUG
	GLOG(Log::eInfo, "Create vulkan debugger...");
	
	vk::DebugUtilsMessageSeverityFlagsEXT LogServerity = 
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
	// | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
	// | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

	vk::DebugUtilsMessageTypeFlagsEXT MessageType =
#if !defined(DPLATFORM_MACOS)
		vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding |
#endif
		vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
		vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;

	vk::DebugUtilsMessengerCreateInfoEXT DebugCreateInfo;
	DebugCreateInfo.setMessageSeverity(LogServerity)
		.setMessageType(MessageType)
		.setPUserData(nullptr)
		.setPfnUserCallback(VulkanDebugCallback);

	auto dispatcher = vk::DispatchLoaderDynamic(Context.Instance, vkGetInstanceProcAddr);
	if (Context.Instance.createDebugUtilsMessengerEXT(&DebugCreateInfo,
		Context.Allocator, &Context.DebugMessenger, dispatcher) != vk::Result::eSuccess) {
		GLOG(Log::eFatal, "Create debug utils messenger failed.");
		return false;
	}

	GLOG(Log::eInfo, "Vulkan debugger created.");
#endif

	Context.DynamicLoader = vk::DispatchLoaderDynamic(
		Context.Instance,
		vkGetInstanceProcAddr,
		Context.Device.GetLogicalDevice(),
		vkGetDeviceProcAddr
	);
	GLOG(Log::eInfo, "Vulkan DynamicLoader created.");

	// Surface
	GLOG(Log::eInfo, "Creating vulkan surface...");
	if (!PlatformCreateVulkanSurface(plat_state, &Context)) {
		GLOG(Log::eError, "Create platform surface failed.");
		return false;
	}
	GLOG(Log::eInfo, "Vulkan surface created.");

	// Device
	GLOG(Log::eInfo, "Creating vulkan device...");
	if (!Context.Device.Create(&Context, Context.Surface)) {
		GLOG(Log::eError, "Create vulkan device failed.");
		return false;
	}
	GLOG(Log::eInfo, "Vulkan device created.");

	// Swapchain
	Context.Swapchain.Create(&Context, Context.FrameBufferWidth, Context.FrameBufferHeight);

	CreateCommandBuffer();

	// Save off the number of images we have as the number of render targets needed.
	*out_window_render_target_count = (unsigned char)Context.Swapchain.ImageCount;

	// Sync objects
	Context.ImageAvailableSemaphores.resize(Context.Swapchain.MaxFramesInFlight);
	Context.QueueCompleteSemaphores.resize(Context.Swapchain.MaxFramesInFlight);
	Context.InFlightFences.resize(Context.Swapchain.MaxFramesInFlight);

	for (uint32_t i = 0; i < Context.Swapchain.MaxFramesInFlight; ++i) {
		vk::SemaphoreCreateInfo SeamphoreCreateInfo;
		Context.ImageAvailableSemaphores[i] = Context.Device.GetLogicalDevice().createSemaphore(SeamphoreCreateInfo, Context.Allocator);
		Context.QueueCompleteSemaphores[i] = Context.Device.GetLogicalDevice().createSemaphore(SeamphoreCreateInfo, Context.Allocator);

		// Create the fence in a signaled state, indicating that the first frame has already been rendered
		// This will prevent the application from waiting indefinitely for the first frame to render since it 
		// cannot be rendered until a frame is rendered before it.
		vk::FenceCreateInfo FenceCreateInfo;
		FenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
		Context.InFlightFences[i] = Context.Device.GetLogicalDevice().createFence(FenceCreateInfo, Context.Allocator);
	}

	// In flight fences should not yet exist at this point, so clear the list. These are stored in pointers
	// because the initial state should be 0, and will be 0 when not in use. Actual fences are not owned by this list
	Context.ImagesInFilght.resize(Context.Swapchain.ImageCount);
	for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
		Context.ImagesInFilght[i] = nullptr;
	}

	// Geometry vertex buffer
	const size_t VertexBufferSize = sizeof(Vertex) * 1024 * 1024 * 2;
	Context.ObjectVertexBuffer = NewObject<VulkanBuffer>();
	Context.ObjectVertexBuffer->Type = EGPUBufferType::eRenderbuffer_Type_Vertex;
	Context.ObjectVertexBuffer->TotalSize = VertexBufferSize;
	Context.ObjectVertexBuffer->UseFreelist = true;
	if (!Context.ObjectVertexBuffer->Create()) {
		GLOG(Log::eError, "Error creating vertex buffer.");
		return false;
	}
	Context.ObjectVertexBuffer->Bind(0);
	GLOG(Log::eInfo, "VulkanBackend::CreateRenderbuffer(): Success allocated memory %llu bytes. Enable freelist: %s", VertexBufferSize, "true");

	// Geometry index buffer
	const size_t IndexBufferSize = sizeof(uint32_t) * 1024 * 1024 * 2;
	Context.ObjectIndexBuffer = NewObject<VulkanBuffer>();
	Context.ObjectIndexBuffer->Type = EGPUBufferType::eRenderbuffer_Type_Index;
	Context.ObjectIndexBuffer->TotalSize = IndexBufferSize;
	Context.ObjectIndexBuffer->UseFreelist = true;
	if (!Context.ObjectIndexBuffer->Create()) {
		GLOG(Log::eError, "Error creating index buffer.");
		return false;
	}
	Context.ObjectIndexBuffer->Bind(0);
	GLOG(Log::eInfo, "VulkanBackend::CreateRenderbuffer(): Success allocated memory %llu bytes. Enable freelist: %s", IndexBufferSize, "true");

	// Mark all geometry as invalid.
	for (uint32_t i = 0; i < GEOMETRY_MAX_COUNT; ++i) {
		Geometries[i].id = INVALID_ID;
	}

	GLOG(Log::eInfo, "Create vulkan instance succeed.");
	return true;
}

void VulkanRHI::Shutdown() {
	vk::Device LogicalDevice = Context.Device.GetLogicalDevice();
	LogicalDevice.waitIdle();

	GLOG(Log::eDebug, "Destroying Buffers");
	Context.ObjectVertexBuffer->Destroy();
	Context.ObjectIndexBuffer->Destroy();

	GLOG(Log::eDebug, "Destroying sync objects.");
	for (uint32_t i = 0; i < Context.Swapchain.MaxFramesInFlight; ++i) {
		if (Context.ImageAvailableSemaphores[i]) {
			LogicalDevice.destroySemaphore(Context.ImageAvailableSemaphores[i], Context.Allocator);
		}
		if (Context.QueueCompleteSemaphores[i]) {
			LogicalDevice.destroySemaphore(Context.QueueCompleteSemaphores[i], Context.Allocator);
		}

		if (Context.InFlightFences[i]) {
			LogicalDevice.destroyFence(Context.InFlightFences[i], Context.Allocator);
		}
	}

	Context.ImageAvailableSemaphores.clear();
	Context.QueueCompleteSemaphores.clear();
	Context.InFlightFences.clear();

	GLOG(Log::eDebug, "Destroying command buffers.");
	for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
		if (Context.GraphicsCommandBuffers[i].CommandBuffer) {
			Context.GraphicsCommandBuffers[i].Free(&Context, Context.Device.GetGraphicsCommandPool());
		}
	}

	GLOG(Log::eDebug, "Destroying swapchain.");
	Context.Swapchain.Destroy(&Context);

	GLOG(Log::eDebug, "Destroying vulkan device.");
	Context.Device.Destroy();

	GLOG(Log::eDebug, "Destroying vulkan surface.");
	if (Context.Surface) {
		// NOTE: For now, the surface allocated by default vulkan allocator.
		Context.Instance.destroy(Context.Surface, nullptr);
	}

#ifdef LEVEL_DEBUG
	GLOG(Log::eDebug, "Destroying vulkan debugger...");
	auto dispatcher = vk::DispatchLoaderDynamic(Context.Instance, vkGetInstanceProcAddr);
	if (Context.DebugMessenger) {
		Context.Instance.destroyDebugUtilsMessengerEXT(Context.DebugMessenger, Context.Allocator, dispatcher);
	}
#endif

	GLOG(Log::eDebug, "Destroying vulkan instance...");
	Context.Instance.destroy(Context.Allocator);

	// Destroy the allocator callbacks if set.
	if (Context.Allocator) {
		Memory::Free(Context.Allocator, MemoryType::eMemory_Type_Renderer);
		Context.Allocator = nullptr;
	}
}

bool VulkanRHI::BeginFrame(){
	VulkanDevice* Device = &Context.Device;

	// Check if recreating swap chain and boot out
	if (Context.RecreatingSwapchain) {
		Device->GetLogicalDevice().waitIdle();
		GLOG(Log::eInfo, "Recreating swapchain, booting.");
		return false;
	}

	// Check if the framebuffer has been resized. If so, a new swapchain must be created.
	if (Context.FramebufferSizeGenerate != Context.FramebufferSizeGenerateLast) {
		Device->GetLogicalDevice().waitIdle();

		// If the swap chain recreation failed
		// boot out before unsetting the flag
		if (!RecreateSwapchain()) {
			GLOG(Log::eInfo, "Recreating swapchain, booting.");
			return false;
		}

		GLOG(Log::eInfo, "Resized, booting.");
		return false;
	}

	// Wait for the execution of the current frame to complete. The fence being free will allow this one to move on
	if (Context.Device.GetLogicalDevice().waitForFences(1, &Context.InFlightFences[Context.CurrentFrame], true, UINT64_MAX)
		!= vk::Result::eSuccess) {
		GLOG(Log::eWarn, "In flight fence wait failure!");
		return false;
	}

	// Acquire the next image from the swap chain. Pass along the semaphore that should signaled when this completes.
	// This same semaphore will later be waited on by the queue submission to ensure this image is available.
	Context.ImageIndex = Context.Swapchain.AcquireNextImageIndex(&Context, UINT64_MAX, Context.ImageAvailableSemaphores[Context.CurrentFrame], nullptr);
	if (Context.ImageIndex == INVALID_ID) {
		return false;
	}

	// Begin recording commands
	VulkanCommandBuffer* CommandBuffer = &Context.GraphicsCommandBuffers[Context.ImageIndex];
	CommandBuffer->Reset();
	CommandBuffer->BeginCommand(false, false, false);

	// Dynamic state
	Context.ViewportRect = Vector4(0.0f, (float)Context.FrameBufferHeight, (float)Context.FrameBufferWidth, -(float)Context.FrameBufferHeight);
	SetViewport(Context.ViewportRect);
	Context.ScissorRect = Vector4(0.0f, 0.0f, (float)Context.FrameBufferWidth, (float)Context.FrameBufferHeight);
	SetScissor(Context.ScissorRect);

	return true;
}

bool VulkanRHI::EndFrame() {

	VulkanCommandBuffer* CommandBuffer = &Context.GraphicsCommandBuffers[Context.ImageIndex];
	
	CommandBuffer->EndCommand();

	// Make sure the previous frame is not using this image
	if (Context.ImagesInFilght[Context.ImageIndex] != VK_NULL_HANDLE) {
		if (Context.Device.GetLogicalDevice().waitForFences(1, Context.ImagesInFilght[Context.ImageIndex], true, UINT64_MAX)
			!= vk::Result::eSuccess) {
			GLOG(Log::eWarn, "In flight fence wait failure!");
			return false;
		}
	}

	// Make sure image fence as in use by this frame
	Context.ImagesInFilght[Context.ImageIndex] = &Context.InFlightFences[Context.CurrentFrame];

	// Reset the fence for use on the next frame
	if (Context.Device.GetLogicalDevice().resetFences(1, &Context.InFlightFences[Context.CurrentFrame])
		!= vk::Result::eSuccess) {
		GLOG(Log::eWarn, "In flight fence wait failure!");
		return false;
	}

	// Submit the queue and wait for the operation to complete
	// Begin queue submission
	vk::SubmitInfo SubmitInfo;
	
	// Command buffer(s) to be executed
	SubmitInfo.setCommandBufferCount(1);
	SubmitInfo.setCommandBuffers(CommandBuffer->CommandBuffer);

	// The semaphore(s) to be signaled then the queue is complete
	SubmitInfo.setSignalSemaphoreCount(1);
	SubmitInfo.setSignalSemaphores(Context.QueueCompleteSemaphores[Context.CurrentFrame]);

	// Wait semaphore ensures that the operation cannot begin until the image is available
	SubmitInfo.setWaitSemaphoreCount(1);
	SubmitInfo.setWaitSemaphores(Context.ImageAvailableSemaphores[Context.CurrentFrame]);

	// Each semaphore waits on the corresponding pipeline stage to complete. 1:1 ratio.
	// vk::PipelineStageFlagBits::eColor_Attachment_Ouput prevents subsequent color attachment.
	// Writes from executing until the semaphore signals
	std::array<vk::PipelineStageFlags,1> Flags = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
	SubmitInfo.setWaitDstStageMask(Flags);

	if (Context.Device.GetGraphicsQueue().submit(1, &SubmitInfo, Context.InFlightFences[Context.CurrentFrame]) != vk::Result::eSuccess) {
		GLOG(Log::eError, "Queue submit failed.");
		return false;
	}

	CommandBuffer->UpdateSubmitted();

	// End queue submission
	Context.Swapchain.Presnet(&Context, Context.Device.GetPresentQueue(),
		Context.QueueCompleteSemaphores[Context.CurrentFrame], Context.ImageIndex);

	return true;
}

void VulkanRHI::SetViewport(const Vector4& rect) {
	// Dynamic state
	vk::Viewport Viewport;
	Viewport.x = rect.x;
	Viewport.y = rect.y;
	Viewport.width = rect.z;
	Viewport.height = rect.w;
	Viewport.minDepth = 0.0f;
	Viewport.maxDepth = 1.0f;

	VulkanCommandBuffer* CmdBuffer = &Context.GraphicsCommandBuffers[Context.ImageIndex];
	CmdBuffer->CommandBuffer.setViewport(0, 1, &Viewport);
}

void VulkanRHI::ResetViewport() {
	// Just set the current viewport rect.
	SetViewport(Context.ViewportRect);
}

void VulkanRHI::SetScissor(const Vector4& rect) {
	vk::Rect2D Scissor;
	Scissor.offset.x = (uint32_t)rect.x;
	Scissor.offset.y = (uint32_t)rect.y;
	Scissor.extent.width = (uint32_t)rect.z;
	Scissor.extent.height = (uint32_t)rect.w;

	VulkanCommandBuffer* CmdBuffer = &Context.GraphicsCommandBuffers[Context.ImageIndex];
	CmdBuffer->CommandBuffer.setScissor(0, 1, &Scissor);
}

void VulkanRHI::ResetScissor() {
	// Just set the current scissor rect.
	SetScissor(Context.ScissorRect);
}

void VulkanRHI::Resize(unsigned short width, unsigned short height) {
	// Update the "Framebuffer size generate", a counter which indicates when the
	// framebuffer size has been updated
	Context.FrameBufferWidth = width;
	Context.FrameBufferHeight = height;
	Context.FramebufferSizeGenerate++;

	GLOG(Log::eInfo, "Vulkan renderer backend resize: width/height/generation: %i/%i/%llu", width, height, Context.FramebufferSizeGenerate);
}

void VulkanRHI::CreateCommandBuffer() {
	if (Context.GraphicsCommandBuffers == nullptr) {
		Context.GraphicsCommandBuffers = (VulkanCommandBuffer*)Memory::Allocate(sizeof(VulkanCommandBuffer) * Context.Swapchain.ImageCount, MemoryType::eMemory_Type_Renderer);
		for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
			Memory::Zero(&(Context.GraphicsCommandBuffers[i]), sizeof(VulkanCommandBuffer));
		}
	}

	for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
		if (Context.GraphicsCommandBuffers[i].CommandBuffer) {
			Context.GraphicsCommandBuffers[i].Free(&Context, Context.Device.GetGraphicsCommandPool());
		}

		Memory::Zero(&Context.GraphicsCommandBuffers[i], sizeof(VulkanCommandBuffer));
		Context.GraphicsCommandBuffers[i].Allocate(&Context, Context.Device.GetGraphicsCommandPool(), true);
	}

	GLOG(Log::eInfo, "Vulkan command buffers created.");
}

bool VulkanRHI::RecreateSwapchain() {
	// If already being recreated, do not try again.
	if (Context.RecreatingSwapchain) {
		GLOG(Log::eDebug, "Already called recreate swapchain. Booting.");
		return false;
	}
	
	// Detect if the windows is too small to be drawn to
	if (Context.FrameBufferWidth == 0 || Context.FrameBufferHeight == 0) {
		GLOG(Log::eDebug, "Recreate swapchain called when windows is < 1px. Booting.");
		return false;
	}

	// Mark as recreatring if the dimensions are valid.
	Context.RecreatingSwapchain = true;

	// Wait for any operation to complete
	Context.Device.GetLogicalDevice().waitIdle();

	// Clear these out just in case
	for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
		Context.ImagesInFilght[i] = nullptr;
	}

	// Requery support
	Context.Device.QuerySwapchainSupport(Context.Device.GetPhysicalDevice(), Context.Surface, Context.Device.GetSwapchainSupportInfo());
	Context.Device.DetectDepthFormat();

	Context.Swapchain.Recreate(&Context, Context.FrameBufferWidth, Context.FrameBufferHeight);

	// Update framebuffer size generation.
	Context.FramebufferSizeGenerateLast = Context.FramebufferSizeGenerate;

	// Cleanup swapcahin
	for (uint32_t i = 0; i < Context.Swapchain.ImageCount; ++i) {
		Context.GraphicsCommandBuffers[i].Free(&Context, Context.Device.GetGraphicsCommandPool());
	}

	// Tell the renderer that a refresh is required.
	SEventContext EventContext = SEventContext();
	EngineEvent::Fire(eEventCode::Default_Rendertarget_Refresh_Required, nullptr, EventContext);

	CreateCommandBuffer();

	// Clear the recreating flag.
	Context.RecreatingSwapchain = false;

	return true;
}

UTexture* VulkanRHI::AcquireTexture(const FString& name, bool auto_release) {
	UTexture* tex = NewObject<VulkanTexture>(name);
	if (!tex) {
		return nullptr;
	}

	tex->SetIsAutoRelease(auto_release);
	return tex;
}

bool VulkanRHI::CreateGeometry(UGeometry* geometry, const FGeometryConfig& config) {
	if (!geometry || config.vertex_count == 0 || !config.vertices) {
		return false;
	}

	GeometryData* InternalData = nullptr;
	if (geometry->InternalID != INVALID_ID) {
		if (geometry->InternalID >= GEOMETRY_MAX_COUNT) {
			return false;
		}

		InternalData = &Geometries[geometry->InternalID];

		// Debug log.
		/*GLOG(Log::eDebug,
			"CreateGeometry: geometry=%p InternalID=%u "
			"OLD vertex offset=%llu size=%llu "
			"OLD index offset=%llu size=%llu "
			"NEW vertex size=%llu "
			"NEW index size=%llu",
			geometry,
			geometry->InternalID,
			InternalData->vertext_buffer_offset,
			static_cast<size_t>(InternalData->vertex_element_size) *
			InternalData->vertex_count,
			InternalData->index_buffer_offset,
			static_cast<size_t>(InternalData->index_element_size) *
			InternalData->index_count,
			config.vertex_size * config.vertex_count,
			config.index_size * config.index_count);
		*/
	}
	else {
		// 新资源寻找新的Slot
		for (uint32_t i = 0; i < GEOMETRY_MAX_COUNT; ++i) {
			if (Geometries[i].id == INVALID_ID) {
				geometry->InternalID = i;
				Geometries[i].id = i;
				InternalData = &Geometries[i];
				break;
			}
		}
	}

	if (!InternalData) {
		return false;
	}

	size_t NewVertexOffset = 0;
	const size_t NewVertexSize = static_cast<size_t>(config.vertex_size) * config.vertex_count;
	if (!Context.ObjectVertexBuffer->AllocateMemory(NewVertexSize, &NewVertexOffset)) {
		return false;
	}

	if (!Context.ObjectVertexBuffer->Load(NewVertexOffset, NewVertexSize, config.vertices)) {
		Context.ObjectVertexBuffer->FreeMemory(NewVertexOffset, NewVertexSize);
		return false;
	}

	// 分配缓冲区内存
	size_t NewIndexOffset = 0;
	size_t NewIndexSize = 0;
	if (config.index_count != 0 && config.indices != nullptr) {
		NewIndexSize = static_cast<size_t>(config.index_size) * config.index_count;

		if (!Context.ObjectIndexBuffer->AllocateMemory(NewIndexSize, &NewIndexOffset)) {
			Context.ObjectVertexBuffer->FreeMemory(NewVertexSize, NewVertexOffset);
			return false;
		}

		if (!Context.ObjectIndexBuffer->Load(NewIndexOffset, NewIndexSize, config.indices)) {
			Context.ObjectIndexBuffer->FreeMemory(NewIndexOffset, NewIndexSize);
			Context.ObjectVertexBuffer->FreeMemory(NewVertexSize, NewVertexOffset);
			return false;
		}
	}

	// 释放旧资源
	if (InternalData->vertex_count != 0 && InternalData->vertex_element_size != 0) {
		const size_t OldVertexSize =
			static_cast<size_t>(InternalData->vertex_element_size) * InternalData->vertex_count;
		Context.ObjectVertexBuffer->FreeMemory(OldVertexSize, InternalData->vertext_buffer_offset);
	}

	if (InternalData->index_count != 0 && InternalData->index_element_size != 0) {
		const size_t OldIndexSize = 
			static_cast<size_t>(InternalData->index_element_size) * InternalData->index_count;
		Context.ObjectIndexBuffer->FreeMemory(OldIndexSize, InternalData->index_buffer_offset);
	}

	// Commit new state.
	InternalData->vertex_count = config.vertex_count;
	InternalData->vertex_element_size = config.vertex_size;
	InternalData->vertext_buffer_offset = NewVertexOffset;

	InternalData->index_count = config.index_count;
	InternalData->index_element_size = config.index_count != 0 ? config.index_size : 0;
	InternalData->index_buffer_offset = config.index_count != 0 ? NewIndexOffset : 0;

	if (InternalData->generation == INVALID_ID) {
		InternalData->generation = 0;
	}
	else {
		InternalData->generation++;
	}

	return true;
}

void VulkanRHI::DestroyGeometry(UGeometry* geometry) {
	if (geometry != nullptr && geometry->InternalID != INVALID_ID) {
		Context.Device.GetLogicalDevice().waitIdle();
		GeometryData* InternalData = &Geometries[geometry->InternalID];

		// Free vertex data.
		Context.ObjectVertexBuffer->FreeMemory(InternalData->vertex_element_size * InternalData->vertex_count, InternalData->vertext_buffer_offset);

		// Free index data.
		if (InternalData->index_element_size > 0) {
			Context.ObjectIndexBuffer->FreeMemory(InternalData->index_element_size * InternalData->index_count, InternalData->index_buffer_offset);
		}

		// Clean up date.
		Memory::Zero(InternalData, sizeof(GeometryData));
		InternalData->id = INVALID_ID;
		InternalData->generation = INVALID_ID;
	}
}

void VulkanRHI::DrawGeometry(GeometryRenderData* geometry) {
	// Ignore non-uploaded geometries.
	if (geometry->geometry == nullptr) {
		return;
	}

	if (geometry->geometry->InternalID == INVALID_ID) {
		return;
	}

	GeometryData* BufferData = &Geometries[geometry->geometry->InternalID];
	bool IncludIndexData = BufferData->index_count > 0;
	if (!DrawRenderbuffer(Context.ObjectVertexBuffer, BufferData->vertext_buffer_offset, BufferData->vertex_count, IncludIndexData)) {
		GLOG(Log::eError, "VulkanBackend::DrawGeometry() Failed to draw vertex buffer.");
		return;
	}

	if (IncludIndexData) {
		if (!DrawRenderbuffer(Context.ObjectIndexBuffer, BufferData->index_buffer_offset, BufferData->index_count, !IncludIndexData)) {
			GLOG(Log::eError, "VulkanBackend::DrawGeometry() Failed to draw index buffer.");
			return;
		}
	}
}

void VulkanRHI::ExecuteDrawCalls(const std::vector<DrawCall>& draw_calls,
	size_t frame_number, const FFrameData& data) {
	// 每个DrawCall重置
	UShader* currentShader = nullptr;
	UMaterial* currentMaterial = nullptr;
	UMaterialInstance* currentMaterialInstance = nullptr;

	for (const DrawCall& dc : draw_calls) {
		if (currentShader != dc.shader)
		{
			// 一个Shader只需要绑定一次
			dc.shader->Use();
			currentShader = dc.shader;

			if (!MaterialSystem::Get().ApplyGlobal(currentShader->GetUniqueID(), frame_number, data)) {
				GLOG(Log::eError, "VulkanRHI::ExecuteDrawCalls() Failed to apply global material. Render frame failed.");
				continue;
			}
		}

		// 实例级资源（DescriptorSet / UBO）是按其"材质父材质的 shader"分配、并绑定到该 shader
		// 的 pipeline layout 上的（见 MaterialSystem::ApplyInstance）。当 DrawCall 实际使用的
		// shader 与材质父 shader 不一致时（例如阴影通道复用几何体材质实例、仅把 shader 换成
		// Shader.Builtin.Shadow），绝不能按材质父 shader 去绑定实例资源：那会把别的 shader 的
		// instance 描述符集绑到当前 pipeline layout 上（该 shader 甚至可能没有 set=1），同时也
		// 会错误刷新材质帧号、干扰同一帧内其它通道的正常绑定。此类 DrawCall 只跳过实例资源绑定，
		// local uniform（model 矩阵）仍按实际 shader 应用，DrawCall 本身照常绘制。
		UMaterial* DCMaterial = dc.material->GetParentMaterial();
		const bool ShaderMatchesMaterial = (DCMaterial != nullptr) && (DCMaterial->GetShaderID() == dc.shader->GetUniqueID());

		if (!ShaderMatchesMaterial) {
			if (!MaterialSystem::Get().ApplyLocal(dc.shader, dc.model)) {
				GLOG(Log::eError, "VulkanRHI::ExecuteDrawCalls() Failed to apply local material. Render frame failed.");
				continue;
			}
		}
		else {
			// 绑定材质实例（Uniform等数据）
			if (currentMaterialInstance != dc.material) {
				if (!dc.material->IsNeedUpdate(frame_number)) continue;
				if (!MaterialSystem::Get().ApplyInstance(dc.material, data)) {
					GLOG(Log::eError, "VulkanRHI::ExecuteDrawCalls() Failed to apply instance material. Render frame failed.");
					continue;
				}

				currentMaterial = dc.material->GetParentMaterial();
				currentMaterialInstance = dc.material;
			}

			// local uniform 一律按 DrawCall 实际使用的 shader 取，避免同帧内不同 shader 复用
			// 同一材质实例时用错 uniform 列表。
			if (!MaterialSystem::Get().ApplyLocal(dc.shader, dc.model)) {
				GLOG(Log::eError, "VulkanRHI::ExecuteDrawCalls() Failed to apply local material. Render frame failed.");
				continue;
			}

			dc.material->SetFrameNumber((uint32_t)frame_number);
		}

		// 真正Draw
		GeometryRenderData renderData;
		renderData.geometry = dc.geometry;
		renderData.model_mat = dc.model;
		DrawGeometry(&renderData);
	}
}

bool VulkanRHI::BeginRenderpass(IRenderpass* pass, RenderTarget* target) {
	pass->Begin(target);
	return true;
}

bool VulkanRHI::EndRenderpass(IRenderpass* pass) {
	pass->End();
	return true;
}

/**
 * Shaders
 */
#ifdef LEVEL_DEBUG
bool VulkanRHI::VerifyShaderID(uint32_t shader_id) {
		if (shader_id == INVALID_ID || Context.Shaders[shader_id].ID == INVALID_ID) {
			return false;
		}

		return true;
}   
#else
bool VulkanRHI::VerifyShaderID(uint32_t shader_id) {
	return true;
}
#endif	// LEVEL_DEBUG

bool VulkanRHI::CreateShader(UShader* shader, const FShaderConfig* config, IRenderpass* pass,
	const TArray<FString>& stage_filenames, std::vector<ShaderStage>& stages) {
	// Translate stages.
	vk::ShaderStageFlags VkStages[VULKAN_SHADER_MAX_STAGES];
	for (unsigned short i = 0; i < stages.size(); ++i) {
		switch (stages[i])
		{
		case eShader_Stage_Fragment:
			VkStages[i] = vk::ShaderStageFlagBits::eFragment;
			break;
		case eShader_Stage_Vertex:
			VkStages[i] = vk::ShaderStageFlagBits::eVertex;
			break;
		case eShader_Stage_Geometry:
			VkStages[i] = vk::ShaderStageFlagBits::eGeometry;
			break;
		case eShader_Stage_Compute:
			VkStages[i] = vk::ShaderStageFlagBits::eCompute;
			break;
		default:
			break;
		}
	}

	// TODO: configurable max descriptor allocate count.

	uint32_t MaxDescriptorAllocateCount = 1024;

	// Take a copy of the pointer to the context.
	VulkanShader* OutShader = (VulkanShader*)shader;
	OutShader->Renderpass = (VulkanRenderPass*)pass;
	OutShader->Config.max_descriptor_set_count = (uint16_t)MaxDescriptorAllocateCount;

	// Shader stages. Parse out the flags.
	Memory::Zero(OutShader->Config.stages, sizeof(VulkanShaderStageConfig) * VULKAN_SHADER_MAX_STAGES);
	OutShader->Config.stage_count = 0;
	for (uint32_t i = 0; i < stages.size(); ++i) {
		// Make sure there is room enough to add the stage.
		if (OutShader->Config.stage_count + 1 > VULKAN_SHADER_MAX_STAGES) {
			GLOG(Log::eError, "Shaders may have a maximum of %d stages", VULKAN_SHADER_MAX_STAGES);
			return false;
		}

		// Make sure the stage is a supported one.
		vk::ShaderStageFlagBits StageFlag;
		switch (stages[i])
		{
		case ShaderStage::eShader_Stage_Vertex:
			StageFlag = vk::ShaderStageFlagBits::eVertex;
			break;
		case ShaderStage::eShader_Stage_Fragment:
			StageFlag = vk::ShaderStageFlagBits::eFragment;
			break;
		case ShaderStage::eShader_Stage_Geometry:
			StageFlag = vk::ShaderStageFlagBits::eGeometry;
			break;
		case ShaderStage::eShader_Stage_Compute:
			StageFlag = vk::ShaderStageFlagBits::eCompute;
			break;
		default:
			// Go to the next type.
			GLOG(Log::eError, "vulkan_shader_create: Unsupported shader stage flagged: %d. Stage ignored.", stages[i]);
			continue;
		}

		// Set the stage and bump the counter.
		OutShader->Config.stages[OutShader->Config.stage_count].stage = StageFlag;
		strncpy(OutShader->Config.stages[OutShader->Config.stage_count].filename, stage_filenames[i].CStr(), 255);
		OutShader->Config.stage_count++;
	}

	// Zero out arrays and counts.
	Memory::Zero(OutShader->Config.descriptor_sets, sizeof(VulkanDescriptorSetConfig) * 2);
	OutShader->Config.descriptor_sets[0].sampler_binding_index = INVALID_ID_U8;
	OutShader->Config.descriptor_sets[1].sampler_binding_index = INVALID_ID_U8;

	// Attributes array.
	Memory::Zero(OutShader->Config.attributes, sizeof(vk::VertexInputAttributeDescription) * VULKAN_SHADER_MAX_ATTRIBUTES);

	// Get the uniform count.
	OutShader->GlobalUniformCount = 0;
	OutShader->GlobalUniformSamplerCount = 0;
	OutShader->InstanceUniformCount = 0;
	OutShader->InstanceUniformSamplerCount = 0;
	OutShader->LocalUniformCount = 0;
	uint32_t TotalCount = (uint32_t)config->uniforms.size();
	for (uint32_t i = 0; i < TotalCount; ++i) {
		switch (config->uniforms[i].scope) {
		case ShaderScope::eShader_Scope_Global:
			if (config->uniforms[i].type == ShaderUniformType::eShader_Uniform_Type_Sampler) {
				OutShader->GlobalUniformSamplerCount++;
			}
			else {
				OutShader->GlobalUniformCount++;
			}
			break;
		case ShaderScope::eShader_Scope_Instance:
			if (config->uniforms[i].type == ShaderUniformType::eShader_Uniform_Type_Sampler) {
				OutShader->InstanceUniformSamplerCount++;
			}
			else {
				OutShader->InstanceUniformCount++;
			}
			break;
		case ShaderScope::eShader_Scope_Local:
			OutShader->LocalUniformCount++;
			break;
		}
	}

	// For now, shaders will only ever have these 2 types of descriptor pools.
	OutShader->Config.pool_sizes[0] = vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1024 };
	OutShader->Config.pool_sizes[1] = vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 4096 };

	// Global descriptor set config.
	if (OutShader->GlobalUniformCount > 0 || OutShader->GlobalUniformSamplerCount > 0) {
		// Global descriptor set config.
		VulkanDescriptorSetConfig* SetConfig = &OutShader->Config.descriptor_sets[OutShader->Config.descriptor_set_count];

		// Global UBO binding is first, if present.
		if (OutShader->GlobalUniformCount > 0) {
			unsigned char BindingIndex = (unsigned char)SetConfig->binding_count;
			SetConfig->bindings[BindingIndex].setBinding(BindingIndex);
			SetConfig->bindings[BindingIndex].setDescriptorCount(1);
			SetConfig->bindings[BindingIndex].setDescriptorType(vk::DescriptorType::eUniformBuffer);
			SetConfig->bindings[BindingIndex].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
			SetConfig->binding_count++;
		}

		// Add a binding for samplers if used.
		if (OutShader->GlobalUniformSamplerCount > 0) {
			unsigned char BindingIndex = (unsigned char)SetConfig->binding_count;
			SetConfig->bindings[BindingIndex].setBinding(BindingIndex);
			SetConfig->bindings[BindingIndex].setDescriptorCount(OutShader->GlobalUniformSamplerCount);
			SetConfig->bindings[BindingIndex].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
			SetConfig->bindings[BindingIndex].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
			SetConfig->sampler_binding_index = BindingIndex;
			SetConfig->binding_count++;
		}

		// Increment the set count.
		OutShader->Config.descriptor_set_count++;
	}

	// If using instance uniforms, add a UBO descriptor set.
	if (OutShader->InstanceUniformCount > 0 || OutShader->InstanceUniformSamplerCount > 0) {
		// Global descriptor set config.
		VulkanDescriptorSetConfig* SetConfig = &OutShader->Config.descriptor_sets[OutShader->Config.descriptor_set_count];

		// Global UBO binding is first, if present.
		if (OutShader->InstanceUniformCount > 0) {
			unsigned short BindingIndex = SetConfig->binding_count;
			SetConfig->bindings[BindingIndex].setBinding(BindingIndex);
			SetConfig->bindings[BindingIndex].setDescriptorCount(1);
			SetConfig->bindings[BindingIndex].setDescriptorType(vk::DescriptorType::eUniformBuffer);
			SetConfig->bindings[BindingIndex].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
			SetConfig->binding_count++;
		}

		// Add a binding for samplers if used.
		if (OutShader->InstanceUniformSamplerCount > 0) {
			unsigned short BindingIndex = SetConfig->binding_count;
			SetConfig->bindings[BindingIndex].setBinding(BindingIndex);
			SetConfig->bindings[BindingIndex].setDescriptorCount(OutShader->InstanceUniformSamplerCount);
			SetConfig->bindings[BindingIndex].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
			SetConfig->bindings[BindingIndex].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
			SetConfig->sampler_binding_index = (unsigned char)BindingIndex;
			SetConfig->binding_count++;
		}

		// Increment the set count.
		OutShader->Config.descriptor_set_count++;
	}
	
	// Invalidate all instance states.
	// TODO: Dynamic
	for (uint32_t i = 0; i < 1024; ++i) {
		OutShader->InstanceStates[i].id = INVALID_ID;
		OutShader->InstanceStates[i].offset	= 0;
	}

	// Keep a copy of the cull mode.
	OutShader->Config.cull_mode = config->cull_mode;
	OutShader->Config.pology_mode = config->polygon_mode;
	OutShader->Config.PrimTopo = config->PrimTopo;

	return true;
}

vk::SamplerAddressMode VulkanRHI::ConvertRepeatType(const char* axis, TextureRepeat repeat) {
	switch (repeat)
	{
	case eTexture_Repeat_Repeat:
		return vk::SamplerAddressMode::eRepeat;
	case eTexture_Repeat_Minrrored_Repeat:
		return vk::SamplerAddressMode::eMirroredRepeat;
	case eTexture_Repeat_Clamp_To_Edge:
		return vk::SamplerAddressMode::eClampToEdge;
	case eTexture_Repeat_Clamp_To_Border:
		return vk::SamplerAddressMode::eClampToBorder;
	default:
		GLOG(Log::eWarn, "Convert repeat type (axis='%s'): Type '%x' not supported, defauting to repeat.", axis, repeat);
		return vk::SamplerAddressMode::eRepeat;
	}
}

vk::Filter VulkanRHI::ConvertFilterType(const char* op, TextureFilter filter) {
	switch (filter)
	{
	case eTexture_Filter_Mode_Nearest:
		return vk::Filter::eNearest;
	case eTexture_Filter_Mode_Linear:
		return vk::Filter::eLinear;
	default:
		GLOG(Log::eWarn, "Convert filter type (op='%s'): Filter '%x' not supported, defauting to linear.", op, filter);
		return vk::Filter::eLinear;
	}
}

bool VulkanRHI::AcquireTextureMap(FTextureMap* map) {
	// Create a sampler for the texture.
	vk::SamplerCreateInfo SamplerInfo;
	SamplerInfo.setMinFilter(ConvertFilterType("min", map->filter_minify))
		.setMagFilter(ConvertFilterType("mag", map->filter_magnify))
		.setAddressModeU(ConvertRepeatType("U", map->repeat_u))
		.setAddressModeV(ConvertRepeatType("V", map->repeat_v))
		.setAddressModeW(ConvertRepeatType("W", map->repeat_w));

	// Configurable.
	SamplerInfo.setAnisotropyEnable(VK_TRUE)
		.setMaxAnisotropy(16)
		.setBorderColor(vk::BorderColor::eIntOpaqueBlack)
		.setUnnormalizedCoordinates(VK_FALSE)
		.setCompareEnable(VK_FALSE)
		.setCompareOp(vk::CompareOp::eAlways)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMipLodBias(0.0f)
		.setMinLod(0.0f)
		.setMaxLod(0.0f);

	if (Context.Device.GetLogicalDevice().createSampler(&SamplerInfo, Context.Allocator, (vk::Sampler*)&map->internal_data)
		!= vk::Result::eSuccess) {
		GLOG(Log::eError, "Create sampler failed.");
		return false;
	}

	return true;
}

void VulkanRHI::ReleaseTextureMap(FTextureMap* map) {
	if (map) {
		Context.Device.GetLogicalDevice().waitIdle();
		Context.Device.GetLogicalDevice().destroySampler(*((vk::Sampler*)&map->internal_data), Context.Allocator);
		map->internal_data = nullptr;
	}
}

uint32_t VulkanRHI::AcquireInstanceResource(UShader* shader, std::vector<FTextureMap*>& maps) {
	VulkanShader* VkShader = (VulkanShader*)shader;
	// TODO: Dynamic
	uint32_t OutInstanceID = INVALID_ID;
	for (uint32_t i = 0; i < 1024; ++i) {
		if (VkShader->InstanceStates[i].id == INVALID_ID) {
			VkShader->InstanceStates[i].id = i;
			OutInstanceID = i;
			break;
		}
	}

	if (OutInstanceID == INVALID_ID) {
		GLOG(Log::eError, "vulkan_shader_acquire_instance_resources failed to acquire new id");
		return INVALID_ID;
	}
	
	VulkanShaderInstanceState* InstanceState = &VkShader->InstanceStates[OutInstanceID];
	unsigned char SamplerBindingIndex = VkShader->Config.descriptor_sets[DESC_SET_INDEX_INSTANCE].sampler_binding_index;
	uint32_t InstanceTextureCount = VkShader->Config.descriptor_sets[DESC_SET_INDEX_INSTANCE].bindings[SamplerBindingIndex].descriptorCount;
	
	// Only setup if the shader actually requires it.
	if (shader->InstanceTextureCount > 0) {
		// Wipe out the memory for the entire array, even if it isn't all used.
		UTexture* DefaultTexture = TextureSystem::Get().GetDefaultDiffuseTexture();
		InstanceState->instance_texture_maps = maps;
		// Set unassigned texture pointers to default until assigned.
		for (uint32_t i = 0; i < InstanceTextureCount; ++i) {
			if (maps[i]->texture == nullptr) {
				InstanceState->instance_texture_maps[i]->texture = DefaultTexture;
			}
		}
	}

	// Allocate some space in the UBO - by the stride, not the size.
	uint32_t Size = (uint32_t)shader->UboStride;
	if (Size > 0) {
		if (!VkShader->UniformBuffer.AllocateMemory(Size, &InstanceState->offset)) {
			GLOG(Log::eError, "vulkan_material_shader_acquire_resources failed to acquire ubo space");
			return INVALID_ID;
		}
	}

	VulkanShaderDescriptorSetState* SetState = &InstanceState->descriptor_set_state;

	// Each descriptor binding in the set.
	uint32_t BindingCount = VkShader->Config.descriptor_sets[DESC_SET_INDEX_INSTANCE].binding_count;
	Memory::Zero(SetState->descriptor_states, sizeof(VulkanDescriptorState) * VULKAN_SHADER_MAX_BINDINGS);
	for (uint32_t i = 0; i < BindingCount; ++i) {
		for (uint32_t j = 0; j < 3; ++j) {
			SetState->descriptor_states[i].generations[j] = INVALID_ID;
			SetState->descriptor_states[i].ids[j] = INVALID_ID;
		}
	}

	// Allocate 3 descriptor sets (one per frame).
	std::array<vk::DescriptorSetLayout, 3> Layouts = {
		VkShader->DescriptorSetLayouts[DESC_SET_INDEX_INSTANCE],
		VkShader->DescriptorSetLayouts[DESC_SET_INDEX_INSTANCE],
		VkShader->DescriptorSetLayouts[DESC_SET_INDEX_INSTANCE],
	};

	vk::DescriptorSetAllocateInfo AllocInfo;
	AllocInfo.setDescriptorPool(VkShader->DescriptorPool)
		.setDescriptorSetCount((uint32_t)Layouts.size())
		.setPSetLayouts(Layouts.data());
	if(Context.Device.GetLogicalDevice().allocateDescriptorSets(&AllocInfo, InstanceState->descriptor_set_state.descriptorSets.data())
		!= vk::Result::eSuccess) {
			GLOG(Log::eError, "Allocate descriptor sets failed.");
			return INVALID_ID;
	}
	VkShader->InstanceCount = OutInstanceID;

	return OutInstanceID;
}

bool VulkanRHI::ReleaseInstanceResource(UShader* shader, uint64_t instance_id) {
	if (shader == nullptr) {
		return false;
	}

	VulkanShader* VkShader = (VulkanShader*)shader;
	VulkanShaderInstanceState* InstanceState = &VkShader->InstanceStates[instance_id];

	// Wait for any pending operations using the descriptor set to finish.
	Context.Device.GetLogicalDevice().waitIdle();

	// Free 3 descriptor sets (one per frame)
	Context.Device.GetLogicalDevice().freeDescriptorSets(VkShader->DescriptorPool, 3, InstanceState->descriptor_set_state.descriptorSets.data());

	// Destroy descriptor states.
	Memory::Zero(InstanceState->descriptor_set_state.descriptor_states, sizeof(VulkanDescriptorState) * VULKAN_SHADER_MAX_BINDINGS);

	if (InstanceState->instance_texture_maps.size() > 0) {
		for (uint32_t i = 0; i < InstanceState->instance_texture_maps.size(); ++i) {
			InstanceState->instance_texture_maps[i]->texture = nullptr;
		}
		InstanceState->instance_texture_maps.clear();
	}

	VkShader->UniformBuffer.FreeMemory(shader->UboStride, InstanceState->offset);
	InstanceState->offset = 0;
	InstanceState->id = INVALID_ID;

	return true;
}

bool VulkanRHI::CreateRenderTarget(unsigned char attachment_count, std::vector<RenderTargetAttachment> attachments, IRenderpass* pass, uint32_t width, uint32_t height, RenderTarget* out_target) {
	// Max number of attachments.
	vk::ImageView AttachmentViews[32];
	for (uint32_t i = 0; i < attachment_count; ++i) {
		AttachmentViews[i] = ((VulkanTexture*)attachments[i].texture)->ImageView;
	}

	out_target->attachments.clear();
	for (uint32_t i = 0; i < attachments.size(); ++i) {
		out_target->attachments.push_back(attachments[i]);
	}

	vk::FramebufferCreateInfo FramebufferCreateInfo;
	FramebufferCreateInfo.setRenderPass(((VulkanRenderPass*)pass)->GetRenderPass())
		.setAttachmentCount(attachment_count)
		.setPAttachments(AttachmentViews)
		.setWidth(width)
		.setHeight(height)
		.setLayers(1);

	if (Context.Device.GetLogicalDevice().createFramebuffer(&FramebufferCreateInfo, Context.Allocator,
		(vk::Framebuffer*)&out_target->internal_framebuffer) != vk::Result::eSuccess) {
		GLOG(Log::eError, "VulkanBackend::CreateRenderTarget() Failed to create framebuffers.");
		return false;
	}

	return true;
}

void  VulkanRHI::DestroyRenderTarget(RenderTarget* target, bool free_internal_memory) {
	if (target && target->internal_framebuffer) {
		Context.Device.GetLogicalDevice().destroyFramebuffer(*(vk::Framebuffer*)&target->internal_framebuffer, Context.Allocator);
		target->internal_framebuffer = nullptr;
		if (free_internal_memory) {
			target->attachments.clear();
			Memory::Zero(target, sizeof(RenderTarget));
		}
	}
}

UTexture* VulkanRHI::GetWindowAttachment(unsigned char index) {
	if (index >= Context.Swapchain.ImageCount) {
		GLOG(Log::eFatal, "Attempting to get color attachment index out of range: %d. Attachment count: %d.", index, Context.Swapchain.ImageCount);
		return nullptr;
	}

	return Context.Swapchain.RenderTextures[index];
}

UTexture* VulkanRHI::GetDepthAttachment(unsigned char index) {
	if (index >= Context.Swapchain.ImageCount) {
		GLOG(Log::eFatal, "Attempting to get depth attachment index out of range: %d. Attachment count: %d.", index, Context.Swapchain.ImageCount);
		return nullptr;
	}


	return Context.Swapchain.DepthTexture[index];
}

unsigned char VulkanRHI::GetWindowAttachmentIndex() {
	return (unsigned char)Context.ImageIndex;
}

unsigned char VulkanRHI::GetWindowAttachmentCount() const {
	return (unsigned char)Context.Swapchain.ImageCount;
}

bool VulkanRHI::CreateRenderpass(IRenderpass* out_renderpass, const RenderpassConfig& config) {

	out_renderpass->RenderTargetCount = config.renderTargetCount;
	out_renderpass->Targets.resize(out_renderpass->RenderTargetCount);
	out_renderpass->SetClearColor(config.clear_color);
	out_renderpass->SetClearFlags(config.clear_flags);
	out_renderpass->SetRenderArea(config.render_area);

	// Copy over config for each target.
	for (uint32_t i = 0; i < out_renderpass->RenderTargetCount; ++i) {
		RenderTarget* Target = &out_renderpass->Targets[i];
		Target->attachments.resize(config.target.attachments.size());

		// Each attachment for the target.
		for (uint32_t a = 0; a < Target->attachments.size(); ++a) {
			RenderTargetAttachment* Attachment = &Target->attachments[a];
			const RenderTargetAttachmentConfig* AttachmentConfig = &config.target.attachments[a];

			Attachment->index = AttachmentConfig->index;
			Attachment->source = AttachmentConfig->source;
			Attachment->type = AttachmentConfig->type;
			Attachment->loadOperation = AttachmentConfig->loadOperation;
			Attachment->storeOperation = AttachmentConfig->storeOperation;
			Attachment->texture = nullptr;
		}
	}

	return out_renderpass->Create(&Context, config);
}

void VulkanRHI::DestroyRenderpass(IRenderpass* pass) {
	pass->Destroy();
}

bool VulkanRHI::GetEnabledMultiThread() const {
	return Context.EnableMultithreading;
}

bool VulkanRHI::DrawRenderbuffer(IGPUBuffer* buffer, size_t offset, uint32_t element_count, bool bind_only) {
	if (!buffer) {
		return false;
	}

	VulkanCommandBuffer* CmdBuffer = &Context.GraphicsCommandBuffers[Context.ImageIndex];
	if (!CmdBuffer)
	{
		return false;
	}

	if (buffer->Type == EGPUBufferType::eRenderbuffer_Type_Vertex) {
		// Bind vertex buffer at offset.
		vk::DeviceSize Offsets[1] = { offset };
		CmdBuffer->CommandBuffer.bindVertexBuffers(0, 1, &((VulkanBuffer*)buffer)->Buffer, Offsets);
		if (!bind_only) {
			CmdBuffer->CommandBuffer.draw(element_count, 1, 0, 0);
		}
		return true;
	}
	else if (buffer->Type == EGPUBufferType::eRenderbuffer_Type_Index) {
		// Bind index buffer at offset.
		CmdBuffer->CommandBuffer.bindIndexBuffer(((VulkanBuffer*)buffer)->Buffer, offset, vk::IndexType::eUint32);
		if (!bind_only) {
			CmdBuffer->CommandBuffer.drawIndexed(element_count, 1, 0, 0, 0);
		}
		return true;
	}
	else {
		GLOG(Log::eError, "Can not draw buffer of type: %i.", buffer->Type);
	}

	return false;
}

uint32_t VulkanRHI::QueryInstanceVersion() {
	uint32_t ApiVersion = VK_API_VERSION_1_0;

	// vkEnumerateInstanceVersion 在 1.1+ 才存在
	auto pfnEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
		vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));

	if (pfnEnumerateInstanceVersion) {
		pfnEnumerateInstanceVersion(&ApiVersion);
	}

	GLOG(Log::eInfo, "Instance supported latest Vulkan version: %d.%d.%d",
		VK_API_VERSION_MAJOR(ApiVersion),
		VK_API_VERSION_MINOR(ApiVersion),
		VK_API_VERSION_PATCH(ApiVersion));

	return ApiVersion;
}

uint32_t VulkanRHI::SelectTargetApiVersion(uint32_t instanceVersion) {
	// 按优先级从高到低，选设备能支持的最高版本
	if (instanceVersion >= VK_API_VERSION_1_3) {
		return VK_API_VERSION_1_3;
	}
	else if (instanceVersion >= VK_API_VERSION_1_2) {
		return VK_API_VERSION_1_2;
	}
	else if (instanceVersion >= VK_API_VERSION_1_1) {
		return VK_API_VERSION_1_1;
	}
	return VK_API_VERSION_1_0;
}