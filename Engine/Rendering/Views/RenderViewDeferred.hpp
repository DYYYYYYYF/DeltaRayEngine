#pragma once

#include "Defines.hpp"
#include "Rendering/Resources/Texture/Texture.hpp"
#include "Rendering/Interface/IRenderView.hpp"

class UShader;
class ACameraActor;

class RenderViewWorldDeferred : public IRenderView {
public:
	RenderViewWorldDeferred();
	RenderViewWorldDeferred(const RenderViewConfig& config);
	virtual bool OnCreate(const RenderViewConfig& config) override;
	virtual void OnDestroy() override;
	virtual void OnResize(uint32_t width, uint32_t height) override;
	virtual bool RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) override;

	virtual void Render(const TArray<FRenderProxy*>& RenderProxies) override;

public:
	const char* GetShaderName() const {
		if (GBufferShader->Name.IsEmpty()) {
			return nullptr;
		}
		return GBufferShader->Name.CStr();
	}

	void SetGBufferShader(UShader* shader) { GBufferShader = shader; }
	void SetLightingShader(UShader* shader) { LightingShader = shader; }
	UShader* GetGBufferShader() const { return GBufferShader; }
	UShader* GetLightingShader() const { return LightingShader; }

	// 阴影通道（pass 0）
	void SetShadowShader(UShader* shader) { ShadowShader = shader; }
	UShader* GetShadowShader() const { return ShadowShader; }
	// 光空间矩阵（投影 * 视图），逐帧写入帧数据供着色器使用
	const Matrix4& GetLightSpaceMatrix() const { return LightSpaceMatrix; }
	// 阴影贴图（深度纹理，同时作为光照通道的采样输入）
	UTexture* GetShadowMapTexture() const { return ShadowMapTexture; }
	FTextureMap* GetShadowMap() { return &ShadowMapTextureMap; }

	static const uint32_t SHADOW_MAP_SIZE = 2048;  // 阴影贴图分辨率（规格：2048x2048）

	// 方向光与光空间正交相机参数（阴影通道与延迟光照通道共用，避免字面量散落）
	static constexpr float SHADOW_LIGHT_DIR_X = -0.57735f;   // 光照方向 X 分量（-1/sqrt(3)）
	static constexpr float SHADOW_LIGHT_DIR_Y = -0.57735f;   // 光照方向 Y 分量
	static constexpr float SHADOW_LIGHT_DIR_Z = -0.57735f;   // 光照方向 Z 分量
	static constexpr float SHADOW_LIGHT_DISTANCE = 40.0f;    // 光源沿光照反方向的拉远距离
	static constexpr float SHADOW_ORTHO_HALF_EXTENT = 20.0f; // 光锥正交相机半宽/半高
	static constexpr float SHADOW_ORTHO_NEAR = 0.1f;         // 光锥正交相机近平面
	static constexpr float SHADOW_ORTHO_FAR = 100.0f;        // 光锥正交相机远平面

private:
	GBufferSet* GetCurrentGBufferSet(size_t render_target_index) {
		return &GBuffers[render_target_index % MAX_RENDER_TARGETS];
	}

private:
	IRenderer* Renderer;

	// G-Buffer渲染着色器
	UShader* GBufferShader = nullptr;
	// 光照计算着色器
	UShader* LightingShader = nullptr;
	// 阴影（深度预通道）着色器
	UShader* ShadowShader = nullptr;

	// 方向光参数与光空间矩阵（光源正交相机的 投影 * 视图）
	Vector3 LightDirection;
	Matrix4 LightSpaceMatrix;

	// 阴影贴图：2048x2048 深度纹理，作为阴影通道的深度附件 + 光照通道的采样输入
	UTexture* ShadowMapTexture = nullptr;
	FTextureMap ShadowMapTextureMap;

	float NearClip;
	float FarClip;
	float Fov;
	Matrix4 ProjectionMatrix;
	ACameraActor* WorldCamera = nullptr;
	Vector4 AmbientColor;

	static const uint32_t MAX_RENDER_TARGETS = 3;  // 双缓冲
	GBufferSet GBuffers[MAX_RENDER_TARGETS];        // 两套完整的G-Buffer

	uint32_t InstanceID = INVALID_ID;

	// 全屏四边形用于光照计算
	UGeometry* FullscreenQuad = nullptr;

	// 每帧复用的 DrawCall 容器：避免 Render() 逐帧堆分配，每帧仅 clear() + reserve()
	std::vector<DrawCall> GBufferDrawCalls;
	std::vector<DrawCall> ShadowDrawCalls;
	std::vector<DrawCall> LightingDrawCalls;

	// 着色器uniform位置
	struct GBufferUniforms {
		unsigned short ModelLocation;
		unsigned short ViewLocation;
		unsigned short ProjectionLocation;
		// 材质属性
		unsigned short DiffuseColorLocation;
		unsigned short MetallicLocation;
		unsigned short RoughnessLocation;
		unsigned short AmbientOcclusionLocation;
		unsigned short NormalIntensityLocation;
		// 纹理采样器
		unsigned short DiffuseMapLocation;
		unsigned short NormalMapLocation;
		unsigned short MetallicRoughnessMapLocation;
	} GBufferUniforms;

	struct LightingUniforms {
		unsigned short AlbedoMapLocation;
		unsigned short NormalMapLocation;
		unsigned short PositionMapLocation;
		unsigned short ViewPositionLocation;
		unsigned short AmbientColorLocation;
		unsigned short ModeLocation;
		unsigned short TimeLocation;
		unsigned short LightIntensityLocation;
		unsigned short DebugModeLocation;
	} LightingUniforms;

	// 辅助方法
	bool CreateGBufferTextures(uint32_t width, uint32_t height);
	void DestroyGBufferTextures();
	bool CreateFullscreenQuad();
	void DestroyFullscreenQuad();

	// 阴影辅助方法
	bool CreateShadowResources();
	void DestroyShadowResources();
	void UpdateLightSpaceMatrix();
};