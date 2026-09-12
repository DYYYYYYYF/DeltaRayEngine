#pragma once

#include <vulkan/vulkan.hpp>
#include "Rendering/RenderTypes.hpp"

class VulkanContext;
class VulkanCommandBuffer;

enum VulkanRenderPassState {
	eRenderPass_State_Ready,
	eRenderPass_State_Recording,
	eRenderPass_State_In_Renderpass,
	eRenderPass_State_Recording_Ended,
	eRenderPass_State_Submited,
	eRenderPass_State_Not_Allocated
};

enum RenderpassClearFlags {
	eRenderpass_Clear_None = 0x00,
	eRenderpass_Clear_Color_Buffer = 0x01,
	eRenderpass_Clear_Depth_Buffer = 0x02,
	eRenderpass_Clear_Stencil_Buffer = 0x04
};

struct RenderpassConfig {
	const char* name = nullptr;

	float depth = 1.0f;
	uint32_t stencil = 1;

	Vector4 render_area;
	Vector4 clear_color;

	unsigned char clear_flags = 0;

	unsigned char renderTargetCount = 0;
	struct RenderTargetConfig target;
};

class IRenderpass {
public:
	virtual bool Create(VulkanContext* context, const RenderpassConfig& config) = 0;
	virtual void Destroy() = 0;

	virtual void Begin(struct RenderTarget* target) = 0;
	virtual void End() = 0;

public:
	void SetRenderArea(const Vector4& area) { RenderArea = area; }
	const Vector4& GetRenderArea() const { return RenderArea; }

	void SetClearColor(const Vector4& col) { ClearColor = col; }
	const Vector4& GetClearColor() const { return ClearColor; }

	void SetClearFlags(unsigned char val) { ClearFlags = val; }
	unsigned char GetClearFlags() const { return ClearFlags; }

	void SetID(unsigned short i) { ID = i; }
	unsigned short GetID() const { return ID; }

public:
	unsigned char RenderTargetCount;
	TArray<struct RenderTarget> Targets; 
	void* Renderpass = nullptr;

protected:
	unsigned short ID = INVALID_ID_U16;
	Vector4 RenderArea;
	Vector4 ClearColor;
	unsigned char ClearFlags = 0;

};