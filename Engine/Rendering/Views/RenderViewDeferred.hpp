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
	// 阴影贴图（深度纹理，同时作为光照通道的采样输入）；按渲染目标索引各持一份，默认返回第 0 份
	UTexture* GetShadowMapTexture(size_t index = 0) const { return ShadowMapTextures[index % MAX_RENDER_TARGETS]; }
	FTextureMap* GetShadowMap(size_t index = 0) { return &ShadowMapTextureMaps[index % MAX_RENDER_TARGETS]; }
	// 阴影贴图份数即多缓冲份数（MAX_RENDER_TARGETS），渲染时统一以 index % MAX_RENDER_TARGETS 取用

	static const uint32_t SHADOW_MAP_SIZE = 2048;  // 阴影贴图分辨率（规格：2048x2048）


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

	// 方向光参数与光空间矩阵（光源正交相机的 投影 * 视图），逐帧从主方向光 Actor 刷新
	Vector3 LightDirection;
	Matrix4 LightSpaceMatrix;
	// 光照颜色 / 强度（强度按标量打包为 vec4 上传）
	Vector4 LightColor;
	Vector4 LightIntensity;
	// 阴影深度偏置与阴影强度（阴影强度来自光源的 cast_shadow 开关）
	float ShadowBias;
	float ShadowStrength;

	float NearClip;
	float FarClip;
	float Fov;
	Matrix4 ProjectionMatrix;
	ACameraActor* WorldCamera = nullptr;
	Vector4 AmbientColor;

	static const uint32_t MAX_RENDER_TARGETS = 3;  // 多缓冲份数（对应 swapchain 图像数上限）
	GBufferSet GBuffers[MAX_RENDER_TARGETS];       // 多套完整的 G-Buffer，按渲染目标索引取用

	// 阴影贴图：按渲染目标（swapchain 图像）各持一份 2048x2048 深度纹理，
	// 分别作为该渲染目标下阴影通道的深度附件与光照通道的采样输入。
	// MaxFramesInFlight > 1 时若只保留单张，下一帧的阴影通道会在上一帧光照采样完成前覆写它，
	// 造成帧间竞争与阴影闪烁，故与 G-Buffer 一致按 MAX_RENDER_TARGETS 分配多份。
	UTexture* ShadowMapTextures[MAX_RENDER_TARGETS] = { nullptr, nullptr, nullptr };
	FTextureMap ShadowMapTextureMaps[MAX_RENDER_TARGETS];

	uint32_t InstanceID = INVALID_ID;

	// 全屏四边形用于光照计算
	UGeometry* FullscreenQuad = nullptr;

	// 每帧复用的 DrawCall 容器：避免 Render() 逐帧堆分配，每帧仅 clear() + reserve()
	TArray<DrawCall> GBufferDrawCalls;
	TArray<DrawCall> ShadowDrawCalls;
	TArray<DrawCall> LightingDrawCalls;

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