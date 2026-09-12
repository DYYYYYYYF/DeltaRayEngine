#include "RenderViewUI.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/DMemory.hpp"
#include "Core/Event.hpp"
#include "Math/DMath.hpp"
#include "Framework/Components/TransformComponent.h"
#include "Containers/TArray.hpp"
#include "Systems/MaterialSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/RenderViewSystem.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"
#include "Rendering/Interface/IRendererBackend.hpp"
#include "Framework/Classes/TextActor.h"
#include "Framework/Components/StaticMeshComponent.h"
#include "Rendering/RenderWorld/RenderProxy.h"

static bool RenderViewUIOnEvent(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
	IRenderView* self = (IRenderView*)listenerInst;
	if (self == nullptr) {
		return false;
	}

	switch (code)
	{
	case eEventCode::Default_Rendertarget_Refresh_Required:
		RenderViewSystem::Get().RegenerateRendertargets(self);
		return false;
    default: break;
	}

	return false;
}

RenderViewUI::RenderViewUI(const RenderViewConfig& config) {
	Type = config.type;
	Name = config.name;
	CustomShaderName = config.custom_shader_name;
	RenderpassCount = config.pass_count;
	Passes.Resize(RenderpassCount);
	Renderer = IRenderer::GetRenderer();
}

bool RenderViewUI::OnCreate(const RenderViewConfig& config) {
	// Builtin ui shader.
	const char* ShaderName = "Shader.Builtin.UI";
	UAsset ConfigResource(ShaderName);
	if (!ResourceSystem::Get().Load(ShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin UI shader.");
		return false;
	}

	FShaderConfig* Config = (FShaderConfig*)ConfigResource.Data;
	// NOTE: Assuming the first pass since that's all this view has.
	if (!ShaderSystem::Get().Create(&Passes[0], Config)) {
		GLOG(Log::eError, "Failed to load builtin UI shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);

	UsedShader = ShaderSystem::Get().Get(CustomShaderName.IsEmpty() ? ShaderName : CustomShaderName);
	DiffuseMapLocation = UsedShader->GetUniformIndex("diffuse_texture");
	DiffuseColorLocation = UsedShader->GetUniformIndex("diffuse_color");
	ModelLocation = UsedShader->GetUniformIndex("model");

	// TODO: Set from configurable.
	NearClip = -100.0f;
	FarClip = 100.0f;

	// Default
	ProjectionMatrix = Matrix4::Orthographic(0, config.width, config.height, 0.0f, NearClip, FarClip);
	ViewMatrix = Matrix4::Identity();

	if (!EngineEvent::Register(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewUIOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}

	GLOG(Log::eInfo, "Renderview ui created.");
	return true;
}

void RenderViewUI::OnDestroy() {
	EngineEvent::Unregister(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewUIOnEvent);
}

void RenderViewUI::OnResize(uint32_t width, uint32_t height) {
	// Check if different. If so, regenerate projection matrix.
	if (width == Width && height == Height) {
		return;
	}

	Width = (uint16_t)width;
	Height = (uint16_t)height;
	ProjectionMatrix = Matrix4::Orthographic(0.0f, (float)Width, (float)Height, 0.0f, NearClip, FarClip);

	for (uint32_t i = 0; i < RenderpassCount; ++i) {
		Passes[i].SetRenderArea(Vector4(0, 0, (float)Width, (float)Height));
	}
}

bool RenderViewUI::RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) {
	return true;
}

void RenderViewUI::Render(const TArray<FRenderProxy*>& RenderObejcts) {
	TArray<DrawCall> UIDrawCalls;
	// UI draw calls.
	for (FRenderProxy* RenderProxy : RenderObejcts) {
		FTextRenderProxy* Proxy = Cast<FTextRenderProxy*>(RenderProxy);
		if (!Proxy) continue;

		UGeometry* Geometry = Proxy->GetMesh();
		if (!Geometry) continue;

		DrawCall dc;
		UMaterialInstance* Mat = Geometry->GetMaterialInstance();
		dc.geometry = Geometry;
		dc.model = Proxy->GetModelMatrix();
		dc.material = Mat;
		dc.shader = UsedShader;
		dc.userData = nullptr;
		dc.sortKey = ((uint64_t)dc.shader->ID << 32) | (uint64_t)Mat->GetInternalID();

		UIDrawCalls.Push(dc);
	}

	UIDrawCalls.Sort([](const DrawCall& a, const DrawCall& b) {
			return a.sortKey < b.sortKey;
		});

	FFrameData UIData;
	UIData.projection = ProjectionMatrix;
	UIData.view = ViewMatrix;
	UIData.ambieantColor = Vector4(0.0f);
	UIData.cameraPosition = Vector3(0.0f);
	UIData.renderMode = render_mode;
	UIData.time = 0.0f;

	uint8_t RTIndex = Renderer->GetWindowAttachmentIndex();
	uint64_t FrameNumber = Renderer->GetFrameNum();

	Passes[0].Begin(&Passes[0].Targets[RTIndex]);
	Renderer->ExecuteDrawCalls(UIDrawCalls, FrameNumber, UIData);
	Passes[0].End();
}
