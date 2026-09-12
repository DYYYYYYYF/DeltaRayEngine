#pragma once

#include "Rendering/RenderTypes.hpp"
#include "Systems/GeometrySystem.h"

enum ShaderStage;
struct SPlatformState;
struct ShaderUniform;
struct GeometryRenderData;
struct RenderTarget;
struct FTextureMap;
struct RenderBackendConfig;
struct FShaderConfig;

class Texture;
class UMaterial;
class UGeometry;
class UShader;
class IRenderpass;
class IGPUBuffer;

enum BuiltinRenderpass : unsigned char{
	eButilin_Renderpass_World = 0x01,
	eButilin_Renderpass_UI = 0x02
};

class RHI {
public:
	// Generic
	virtual bool Initialize(const RenderBackendConfig* config, unsigned char* out_window_render_target_count, SPlatformState* plat_state) = 0;
	virtual void Shutdown() = 0;
	virtual bool BeginFrame() = 0;
	virtual bool EndFrame() = 0;
	virtual void Resize(unsigned short width, unsigned short height) = 0;
	virtual void DrawGeometry(GeometryRenderData* geometry) = 0;
	virtual void ExecuteDrawCalls(const TArray<DrawCall>& draw_calls, size_t frame_number, const FFrameData& data) = 0;

	// Texture
	virtual UTexture* AcquireTexture(const FString& name, bool auto_release) = 0;

	// Geometry
	virtual bool CreateGeometry(UGeometry* geometry, const FGeometryConfig& config) = 0;
	virtual void DestroyGeometry(UGeometry* geometry) = 0;
	
	// Renderpass
	virtual bool BeginRenderpass(IRenderpass* pass, RenderTarget* target) = 0;
	virtual bool EndRenderpass(IRenderpass* pass) = 0;
	virtual bool CreateRenderTarget(unsigned char attachment_count, TArray<RenderTargetAttachment> attachments, IRenderpass* pass, uint32_t width, uint32_t height, RenderTarget* out_target) = 0;
	virtual void DestroyRenderTarget(RenderTarget* target, bool free_internal_memory) = 0;
	virtual bool CreateRenderpass(IRenderpass* out_renderpass,const RenderpassConfig& config) = 0;
	virtual void DestroyRenderpass(IRenderpass* pass) = 0;
	virtual UTexture* GetWindowAttachment(unsigned char index) = 0;
	virtual unsigned char GetWindowAttachmentCount() const = 0;
	virtual UTexture* GetDepthAttachment(unsigned char index) = 0;
	virtual unsigned char GetWindowAttachmentIndex() = 0;

	// Renderbuffer
	virtual bool DrawRenderbuffer(IGPUBuffer* buffer, size_t offset, uint32_t element_count, bool bind_only) = 0;

	// Render target
	virtual void SetViewport(const Vector4& rect) = 0;
	virtual void ResetViewport() = 0;
	virtual void SetScissor(const Vector4& rect) = 0;
	virtual void ResetScissor() = 0;

	// Shader
	virtual bool CreateShader(UShader* shader, const FShaderConfig* config, IRenderpass* pass, const TArray<FString>& stage_filenames, TArray<ShaderStage>& stages) = 0;
	virtual uint32_t AcquireInstanceResource(UShader* shader, TArray<FTextureMap*>& maps) = 0;
	virtual bool ReleaseInstanceResource(UShader* shader, uint64_t instance_id) = 0;

	virtual bool AcquireTextureMap(FTextureMap* map) = 0;
	virtual void ReleaseTextureMap(FTextureMap* map) = 0;
	virtual bool GetEnabledMultiThread() const { return false; }

public:
	SPlatformState* GetPlatformState() { return PlatformState; }

	size_t GetFrameNum() const { return FrameNum; }
	void SetFrameNum(size_t num) { FrameNum = num; }
	void IncreaseFrameNum() { FrameNum++; }

public:
	// TODO: Make dynamic
	GeometryData Geometries[4096];

protected:
	RendererBackendType BackendType = RendererBackendType::eRenderer_Backend_Type_Vulkan;
	SPlatformState* PlatformState = nullptr;
	size_t FrameNum = 0;

};

