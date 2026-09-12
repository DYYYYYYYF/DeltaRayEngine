#pragma once

#include "Defines.hpp"
#include "Rendering/Interface/IRenderView.hpp"
#include "Rendering/Resources/Texture/Texture.hpp"

class UShader;
class IREnderer;

struct RenderviewPickShaderInfo {
	UShader* UsedShader = nullptr;
	VulkanRenderPass* Pass = nullptr;
	float NearClip = 0.1f;
	float FarClip = 10000.0f;
	float Fov = Deg2Rad(60.0f);
	Matrix4 ProjectionMatrix;
	Matrix4 ViewMatrix;

	uint32_t IDColorLocation = 0;
	uint32_t ViewLocation = 0;
	uint32_t ModelLocation = 0;
	uint32_t ProjectionLocation = 0;
};

class RenderViewPick : public IRenderView {
public:
	RenderViewPick(const RenderViewConfig& config, IRenderer* renderer);
	virtual bool OnCreate(const RenderViewConfig& config) override;
	virtual void OnDestroy() override;
	virtual void OnResize(uint32_t width, uint32_t height) override;
	virtual bool RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) override;

public:
	void AcquireShaderInstance();
	void ReleaseShaderInstance();

public:
	short GetMouseX() const { return MouseX; }
	void SetMouseX(short x) { MouseX = x; }
	short GetMouseY() const { return MouseY; }
	void SetMouseY(short y) { MouseY = y; }

private:
	IRenderer* Renderer = nullptr;
	RenderviewPickShaderInfo UIShaderInfo;
	RenderviewPickShaderInfo WorldShaderInfo;

	// Used as the color attachment for both renderpass.
	UTexture* ColorTargetAttachment;
	UTexture* DepthTargetAttachment;

	int InstanceCount = 0;
	TArray<bool> InstanceUpdated;

	short MouseX = 0, MouseY = 0;

};