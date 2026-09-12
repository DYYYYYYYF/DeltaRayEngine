#pragma once

#include "RenderTypes.hpp"
#include "Resources/ResourceTypes.hpp"
#include "Math/Color.hpp"

struct SStaticMeshData;
struct SPlatformState;
struct ShaderUniform;

class RHI;
class RHI;
class IGPUBuffer;
class IRenderpass;
class UGeometry;
class UShader;
class Camera;
class UWorld;

class IRenderer {
public:
	IRenderer();
	IRenderer(RendererBackendType type, struct SPlatformState* plat_state);
	virtual ~IRenderer();

public:
	DAPI static IRenderer* GetRenderer();

public:
	virtual bool Initialize(const std::string& application_name, Vector2 window_size, struct SPlatformState* plat_state);
	virtual void Shutdown();

	virtual void OnResize(unsigned short width, unsigned short height);
	virtual bool DrawFrame(UWorld* World);
	virtual void ExecuteDrawCalls(const TArray<DrawCall>& draw_calls, size_t frame_number, const FFrameData& data);

public:
	virtual UTexture* AcquireTexture(const FString& name, bool auto_release = true);

	virtual bool CreateGeometry(UGeometry* geometry, const FGeometryConfig& config);
	virtual void DestroyGeometry(UGeometry* geometry);

	virtual bool GetEnabledMutiThread() const;

	/**
	 * @beief Draws the given geometry. Should only be called inside a renderpas, within a frame.
	 * 
	 * @param data The render data of the geometry to be drawn.
	 */
	virtual void DrawGeometry(GeometryRenderData* data);

	/**
	 * @brief Begins the given renderpass.
	 * 
	 * @param pass A pointer to the renderpass to begin.
	 * @param target A pointer to the render target to be used.
	 * @return True on success.
	 */
	virtual bool BeginRenderpass(IRenderpass* pass, RenderTarget* target);

	/**
	 * @beief End the given renderpass.
	 * 
	 * @param pass A pointer to the renderpass to begin.
	 * @return True on success.
	 */
	virtual bool EndRenderpass(IRenderpass* pass);

	/**
	 * @brief Creates internal shader resources using the provided parameters.
	 *
	 * @param shader A pointer to the shader.
	 * @param pass The pointer of the renderpass to be associated with the shader.
	 * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
	 * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
	 * @return True on success; otherwise false.
	 */
	virtual bool CreateRenderShader(UShader* shader, const FShaderConfig* config, IRenderpass* pass, const TArray<FString>& stage_filenames, TArray<ShaderStage> stages);

	/**
	 * @brief Destroys the given shader and releases any resources held by it.
	 * @param shader A pointer to the shader to be destroyed.
	 */
	virtual void DestroyRenderShader(UShader* shader);

	/**
	 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
	 * Must be done after vulkan_shader_create().
	 *
	 * @param shader A pointer to the shader to be initialized.
	 * @return True on success; otherwise false.
	 */
	virtual bool InitializeRenderShader(UShader* shader);

	
	/**
	 * @brief Acquires internal instance-level resources and provides an instance id.
	 *
	 * @param shader A pointer to the shader to acquire resources from.
	 * @param maps Array to hold the texture maps.
	 * @return INVALID_ID on false; otherwise return the instance id.
	 */
	virtual uint32_t AcquireInstanceResource(UShader* shader, TArray<FTextureMap*> maps);

	/**
	 * @brief Releases internal instance-level resources for the given instance id.
	 *
	 * @param shader A pointer to the shader to release resources from.
	 * @param instance_id The instance identifier whose resources are to be released.
	 * @return True on success; otherwise false.
	 */
	virtual bool ReleaseInstanceResource(UShader* shader, uint32_t instance_id);

	
	/**
	 * @brief Acquires internal resource for the given texture map.
	 * 
	 * @param map A pointer to texture map to obtain resources for.
	 * @return True on success.
	 */
	virtual bool AcquireTextureMap(FTextureMap* map);
	
	/**
	 * @brief Release internal resource for the given texture map.
	 *
	 * @param map A pointer to texture map to obtain resources for.
	 */
	virtual void ReleaseTextureMap(FTextureMap* map);

	// Renderbuffer
	virtual bool DrawRenderbuffer(IGPUBuffer* buffer, size_t offset, uint32_t element_count, bool bind_only);
	
	// Render target
	virtual void SetViewport(Vector4 rect);
	virtual void ResetViewport();
	virtual void SetScissor(Vector4 rect);
	virtual void ResetScissor();

	// Renderpass
	virtual bool CreateRenderTarget(unsigned char attachment_count, TArray<RenderTargetAttachment> attachments, IRenderpass* pass, uint32_t width, uint32_t height, RenderTarget* out_target);
	virtual void DestroyRenderTarget(RenderTarget* target, bool free_internal_memory) ;
	virtual bool CreateRenderpass(IRenderpass* out_renderpass, const RenderpassConfig& config);
	virtual void DestroyRenderpass(IRenderpass* pass) ;
	virtual RHI* GetRenderBackend() { return RHI_; }

	virtual UTexture* GetWindowAttachment(unsigned char index);
	virtual unsigned char GetWindowAttachmentCount() const;
	virtual UTexture* GetDepthAttachment(unsigned char index);
	virtual unsigned char GetWindowAttachmentIndex();

	size_t GetFrameNum() const;

public:
	RendererBackendType GetBackendType() const { return BackendType; }
	uint32_t GetWidth() const { return FramebufferWidth; }
	uint32_t GetHeight() const { return FramebufferHeight; }

protected:
	static IRenderer* Renderer;

	RHI* RHI_;
	RendererBackendType BackendType;

	unsigned char WindowRenderTargetCount;
	uint32_t FramebufferWidth;
	uint32_t FramebufferHeight;

	bool Resizing;
	unsigned char FrameSinceResize;
};
