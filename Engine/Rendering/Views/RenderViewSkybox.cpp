#include "RenderViewSkybox.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/DMemory.hpp"
#include "Core/Event.hpp"
#include "Math/DMath.hpp"

#include "Containers/TArray.hpp"
#include "Rendering/Resources/Skybox/Skybox.hpp"

#include "Systems/MaterialSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/RenderViewSystem.hpp"

#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"
#include "Rendering/Interface/IRendererBackend.hpp"
#include "Framework/Components/CameraComponent.h"
#include "Rendering/RenderWorld/RenderProxy.h"

static bool RenderViewSkyboxOnEvent(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
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

RenderViewSkybox::RenderViewSkybox(const RenderViewConfig& config) {
	Type = config.type;
	Name = config.name;
	CustomShaderName = config.custom_shader_name;
	RenderpassCount = config.pass_count;
	Passes.Resize(RenderpassCount);
	Renderer = IRenderer::GetRenderer();
}

bool RenderViewSkybox::OnCreate(const RenderViewConfig& config) {
	FString ShaderName = "Shader.Builtin.Skybox";
	UAsset ConfigResource(ShaderName);
	if (!ResourceSystem::Get().Load(ShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin skybox shader.");
		return false;
	}

	FShaderConfig* Config = (FShaderConfig*)ConfigResource.Data;
	// NOTE: Assuming the first pass since that's all this view has.
	if (!ShaderSystem::Get().Create(&Passes[0], Config)) {
		GLOG(Log::eError, "Failed to load builtin skybox shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);

	// Get either the custom shader override or the defined default.
	UsedShader = ShaderSystem::Get().Get(CustomShaderName.IsEmpty() ? ShaderName : CustomShaderName);
	
	// TODO: Set from configurable.
	NearClip = 0.1f;
	FarClip = 1000.0f;
	Fov = Deg2Rad(45.0f);

	// Default
	ProjectionMatrix = Matrix4::Perspective(Fov, (float)config.width / config.height, NearClip, FarClip);
	WorldCamera = CameraSystem::Get().GetMainCamera();

	if (!EngineEvent::Register(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewSkyboxOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}

	GLOG(Log::eInfo, "Renderview skybox created.");
	return true;
}

void RenderViewSkybox::OnDestroy() {
	EngineEvent::Unregister(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewSkyboxOnEvent);
}

void RenderViewSkybox::OnResize(uint32_t width, uint32_t height) {
	// Check if different.
	if (width == Width && height == Height) {
		return;
	}

	Width = (uint16_t)width;
	Height = (uint16_t)height;
	ProjectionMatrix = Matrix4::Perspective(Fov, (float)Width / (float)Height, NearClip, FarClip);

	for (uint32_t i = 0; i < RenderpassCount; ++i) {
		Passes[i].SetRenderArea(Vector4(0, 0, (float)Width, (float)Height));
	}
}

bool RenderViewSkybox::RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) {
	return false;
}

void RenderViewSkybox::Render(const TArray<FRenderProxy*>& RenderObejcts) {
	if (RenderObejcts.IsEmpty()) return;

	UGeometry* SkyboxGeometry = nullptr;
	size_t ObjectCount = RenderObejcts.Size();
	if (ObjectCount > 1) {
		GLOG(Log::Level::eWarn, "");
	}

	// 使用最后一个Skybox对象
	FSkyboxRenderProxy* Proxy = Cast<FSkyboxRenderProxy*>(RenderObejcts[ObjectCount-1]);
	if (!Proxy) return;

	SkyboxGeometry = Proxy->GetMesh();
	if (!SkyboxGeometry) {
		return;
	}

	UCameraComponent* CameraComp = WorldCamera->GetCameraComponent();
	if (!CameraComp) {
		return;
	}

	// Skybox需要让它始终和摄像机原点一致（玩家永远不能到达）
	Matrix4 ViewMatrix = CameraComp->GetViewMatrix();
	ViewMatrix.data[12] = 0.0f;
	ViewMatrix.data[13] = 0.0f;
	ViewMatrix.data[14] = 0.0f;

	// Record
	FFrameData Data;
	Data.projection = ProjectionMatrix;
	Data.view = ViewMatrix;
	Data.time = 0.0f;

	DrawCall DC;
	DC.geometry = SkyboxGeometry;
	DC.model = Matrix4::Identity();
	DC.material = SkyboxGeometry->GetMaterialInstance();
	DC.shader = UsedShader;
	DC.sortKey = ((uint64_t)UsedShader->ID << 32) | (uint64_t)DC.material->GetInternalID();

	TArray<DrawCall> DrawCalls;
	DrawCalls.Push(DC);

	// Execute pass
	IRenderpass* SkyboxPass = (IRenderpass*)&Passes[0];
	if (!SkyboxPass) return;

	uint8_t RTIndex = Renderer->GetWindowAttachmentIndex();
	uint64_t FrameNumber = Renderer->GetFrameNum();

	SkyboxPass->Begin(&SkyboxPass->Targets[RTIndex]);
	Renderer->ExecuteDrawCalls(DrawCalls, FrameNumber, Data);
	SkyboxPass->End();
}
