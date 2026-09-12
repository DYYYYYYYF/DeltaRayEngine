#include "Renderer.hpp"
#include "Vulkan/VulkanBackend.hpp"
#include "Interface/IGPUBuffer.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/DMemory.hpp"
#include "Core/Event.hpp"

#include "Math/MathTypes.hpp"
#include "Systems/MaterialSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/TextureSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/RenderViewSystem.hpp"
#include "Scene/World.h"
#include "RenderWorld/RenderWorld.h"

IRenderer* IRenderer::Renderer = nullptr;

IRenderer::IRenderer() {
	RHI_ = nullptr;
	BackendType = RendererBackendType::eRenderer_Backend_Type_Vulkan;
	WindowRenderTargetCount = 0;
	FramebufferWidth = 1920;
	FramebufferHeight = 1080;
	Resizing = false;
	FrameSinceResize = 0;
}

IRenderer::IRenderer(RendererBackendType type, struct SPlatformState* plat_state) : RHI_(nullptr){
	BackendType = RendererBackendType::eRenderer_Backend_Type_Vulkan;
	WindowRenderTargetCount = 0;
	FramebufferWidth = 1920;
	FramebufferHeight = 1080;
	Resizing = false;
	FrameSinceResize = 0;

	if (plat_state == nullptr) {
		return ;
	}

	BackendType = type;
	if (type == eRenderer_Backend_Type_Vulkan) {
		// TODO: fill
		void* TempBackend = (VulkanRHI*)Memory::Allocate(sizeof(VulkanRHI), MemoryType::eMemory_Type_Renderer);
		RHI_ = new(TempBackend)VulkanRHI();

		// TODO: make this configurable
		RHI_->SetFrameNum(0);
	}
}

IRenderer::~IRenderer() {
	Shutdown();
}

IRenderer* IRenderer::GetRenderer() {
	if (!Renderer) {
		GLOG(Log::Level::eError, "Function IRenderer::GetRenderer() should called after initialization. Return nullptr.");
		return nullptr;
	}
	return Renderer;
}

bool IRenderer::Initialize(const std::string& application_name, Vector2 window_size, struct SPlatformState* plat_state) {
	if (RHI_ == nullptr) {
		return false;
	}

	Renderer = this;

	// Default framebuffer size. Overriden when window is created.
	FramebufferWidth = (uint32_t)window_size.x;
	FramebufferHeight = (uint32_t)window_size.y;
	Resizing = false;
	FrameSinceResize = 0;

	RenderBackendConfig RendererConfig;
	RendererConfig.application_name = application_name;

	if (!RHI_->Initialize(&RendererConfig, &WindowRenderTargetCount, plat_state)) {
		GLOG(Log::eFatal, "Renderer backend init failed.");
		return false;
	}

	return true;
}

void IRenderer::Shutdown() {
	if (RHI_ != nullptr) {
		RHI_->Shutdown();
		Memory::Free(RHI_, eMemory_Type_Renderer);
	}

	RHI_ = nullptr;
}

void IRenderer::OnResize(unsigned short width, unsigned short height) {
	if (RHI_ != nullptr) {
		Resizing = true;
		FramebufferWidth = width;
		FramebufferHeight = height;
		FrameSinceResize = 0;
	}
	else {
		GLOG(Log::eWarn, "Renderer backend does not exist to accept resize: %i %i", width, height);
	}
}

bool IRenderer::DrawFrame(UWorld* World) {
	RHI_->IncreaseFrameNum();

	// Make sure the window is not currently being resized by waiting a designated
	// number of frames after the last resize operation before performing the backend updates.
	if (Resizing) {
		FrameSinceResize++;

		// If the required number of frames have passed since the resize, go ahead and perform the actual update.
		if (FrameSinceResize >= 30) {
			float Width = (float)FramebufferWidth;
			float Height = (float)FramebufferHeight;

			RHI_->Resize(
				static_cast<unsigned short>(Width), 
				static_cast<unsigned short>(Height)
			);

			RenderViewSystem::Get().OnWindowResize(FramebufferWidth, FramebufferHeight);

			FrameSinceResize = 0;
			Resizing = false;
		}
		else {
			// Skip rendering the frame and try again next time.
			//Platform::PlatformSleep(16);
			return true;
		}
	}

	if (RHI_->BeginFrame()) {
		// Renderer Tick
		URenderWorld* RWorld = World->GetRenderWorld();
		if (RWorld) {
			RWorld->Record();
		}

		// End frame
		bool result = RHI_->EndFrame();

		if (!result) {
			GLOG(Log::eError, "Renderer end frame failed.");
			return false;
		}

	}

	return true;
}

void IRenderer::ExecuteDrawCalls(
	const TArray<DrawCall>& draw_calls, size_t frame_number, const FFrameData& data) {
	RHI_->ExecuteDrawCalls(draw_calls, frame_number, data);
}

void IRenderer::SetViewport(Vector4 rect) {
	RHI_->SetViewport(rect);
}

void IRenderer::ResetViewport() {
	RHI_->ResetViewport();
}

void IRenderer::SetScissor(Vector4 rect) {
	RHI_->SetScissor(rect);
}

void IRenderer::ResetScissor() {
	RHI_->ResetScissor();
}

UTexture* IRenderer::AcquireTexture(const FString& name, bool auto_release) {
	return RHI_->AcquireTexture(name, auto_release);
}

bool IRenderer::CreateGeometry(UGeometry* geometry, const FGeometryConfig& config) {
	return RHI_->CreateGeometry(geometry, config);
}

void IRenderer::DestroyGeometry(UGeometry* geometry) {
	RHI_->DestroyGeometry(geometry);
}

void IRenderer::DrawGeometry(GeometryRenderData* data) {
	RHI_->DrawGeometry(data);
}

bool IRenderer::BeginRenderpass(IRenderpass* pass, RenderTarget* target) {
	return RHI_->BeginRenderpass(pass, target);
}

bool IRenderer::EndRenderpass(IRenderpass* pass) {
	return RHI_->EndRenderpass(pass);
}

bool IRenderer::CreateRenderShader(UShader* shader, const FShaderConfig* config, IRenderpass* pass, const TArray<FString>& stage_filenames, TArray<ShaderStage> stages) {
	return RHI_->CreateShader(shader, config, pass, stage_filenames, stages);
}

void IRenderer::DestroyRenderShader(UShader* shader) {
	shader->Destroy();
}

bool IRenderer::InitializeRenderShader(UShader* shader) {
	return shader->Initialize();
}

uint32_t IRenderer::AcquireInstanceResource(UShader* shader, TArray<FTextureMap*> maps) {
	return RHI_->AcquireInstanceResource(shader, maps);
}

bool IRenderer::ReleaseInstanceResource(UShader* shader, uint32_t instance_id) {
	return RHI_->ReleaseInstanceResource(shader, instance_id);
}

bool IRenderer::AcquireTextureMap(FTextureMap* map) {
	return RHI_->AcquireTextureMap(map);
}

void IRenderer::ReleaseTextureMap(FTextureMap* map) {
	RHI_->ReleaseTextureMap(map);
}

bool IRenderer::CreateRenderTarget(unsigned char attachment_count, TArray<RenderTargetAttachment> attachments, IRenderpass* pass, uint32_t width, uint32_t height, RenderTarget* out_target) {
	return RHI_->CreateRenderTarget(attachment_count, attachments, pass, width, height, out_target);
}

void IRenderer::DestroyRenderTarget(RenderTarget* target, bool free_internal_memory) {
	RHI_->DestroyRenderTarget(target, free_internal_memory);
}

UTexture* IRenderer::GetWindowAttachment(unsigned char index) {
	return RHI_->GetWindowAttachment(index);
}

unsigned char IRenderer::GetWindowAttachmentCount() const {
	return RHI_->GetWindowAttachmentCount();
}

UTexture* IRenderer::GetDepthAttachment(unsigned char index) {
	return RHI_->GetDepthAttachment(index);
}

unsigned char IRenderer::GetWindowAttachmentIndex() {
	return RHI_->GetWindowAttachmentIndex();
}

size_t IRenderer::GetFrameNum() const {
	return RHI_->GetFrameNum();
}

bool IRenderer::CreateRenderpass(IRenderpass* out_renderpass, const RenderpassConfig& config) {
	if (config.renderTargetCount == 0) {
		GLOG(Log::eError, "Can not have a renderpass target count of0.");
		return false;
	}

	return RHI_->CreateRenderpass(out_renderpass, config);
}

void IRenderer::DestroyRenderpass(IRenderpass* pass) {
	RHI_->DestroyRenderpass(pass);
}

bool IRenderer::GetEnabledMutiThread() const {
	return RHI_->GetEnabledMultiThread();
}

/**
 * Renderbuffer
 */
bool IRenderer::DrawRenderbuffer(IGPUBuffer* buffer, size_t offset, uint32_t element_count, bool bind_only) {
	return RHI_->DrawRenderbuffer(buffer, offset, element_count, bind_only);
}
