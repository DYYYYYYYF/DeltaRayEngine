#include "RenderViewWorld.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/Event.hpp"
#include "Core/DMemory.hpp"
#include "Math/DMath.hpp"

#include "Containers/TArray.hpp"

#include "Systems/MaterialSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/RenderViewSystem.hpp"

#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"
#include "Rendering/Interface/IRendererBackend.hpp"
#include "Framework/Components/CameraComponent.h"

struct GeometryDistance {
	GeometryRenderData g;
	float distance;
};

static void QuickSort(TArray<GeometryDistance>& arr, int low_index, int high_index, bool ascending);

static bool RenderViewWorldOnEvent(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
	IRenderView* self = (IRenderView*)listenerInst;
	if (self == nullptr) {
		return false;
	}

	switch ((eEventCode)code)
	{
	case eEventCode::Default_Rendertarget_Refresh_Required: 
	{
		RenderViewSystem::Get().RegenerateRendertargets(self);
		return true;
	}

	case eEventCode::Set_Render_Mode:
	{
		EShaderRenderMode RenderMode = EShaderRenderMode(context.data.i32[0]);
		switch (RenderMode)
		{
		case EShaderRenderMode::eShader_Render_Mode_Default:
			self->render_mode = EShaderRenderMode::eShader_Render_Mode_Default;
			GLOG(Log::eDebug, "Change render mode: eShader_Render_Mode_Default.");
			break;

		case EShaderRenderMode::eShader_Render_Mode_Lighting:
			self->render_mode = EShaderRenderMode::eShader_Render_Mode_Lighting;
			GLOG(Log::eDebug, "Change render mode: eShader_Render_Mode_Lighting.");
			break;

		case EShaderRenderMode::eShader_Render_Mode_Normals:
			self->render_mode = EShaderRenderMode::eShader_Render_Mode_Normals;
			GLOG(Log::eDebug, "Change render mode: eShader_Render_Mode_Normals.");
			break;

		case EShaderRenderMode::eShader_Render_Mode_Depth:
			self->render_mode = EShaderRenderMode::eShader_Render_Mode_Depth;
			GLOG(Log::eDebug, "Change render mode: eShader_Render_Mode_Depth.");
			break;
		}

		return true;
	}
	}	// switch


	return false;
}

RenderViewWorld::RenderViewWorld() {
	
}

RenderViewWorld::RenderViewWorld(const RenderViewConfig& config) {
	Type = config.type;
	Name = config.name;
	CustomShaderName = config.custom_shader_name;
	RenderpassCount = config.pass_count;
	Passes.Resize(RenderpassCount);
}

bool RenderViewWorld::OnCreate(const RenderViewConfig& config) {
	// Builtin world shader.
	const char* ShaderName = "Shader.Builtin.World";
	UAsset ConfigResource("Shader.Builtin.World");
	if (!ResourceSystem::Get().Load(ShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin skybox shader.");
		return false;
	}

	FShaderConfig* Config = (FShaderConfig*)ConfigResource.Data;
	// NOTE: Assuming the first pass since that's all this view has.
	if (!ShaderSystem::Get().Create(&Passes[0], Config)) {
		GLOG(Log::eError, "Failed to load builtin world shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);

	UsedShader = ShaderSystem::Get().Get(CustomShaderName.IsEmpty() ? ShaderName : CustomShaderName);

	// TODO: Set from configurable.
	NearClip = 0.1f;
	FarClip = 1000.0f;
	Fov = Deg2Rad(45.0f);

	// Default
	WorldCamera = CameraSystem::Get().GetMainCamera();
	WorldCamera->SetFOV(Fov);
	WorldCamera->SetAspectRatio((float)config.width / config.height);
	WorldCamera->SetNearPlane(NearClip);
	WorldCamera->SetFarPlane(FarClip);
	ProjectionMatrix = WorldCamera->GetProjectionMatrix(ECameraProjectionMode::Perspective);

	// TODO: Obtain from scene.
	AmbientColor = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

	if (!EngineEvent::Register(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewWorldOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}
	if (!EngineEvent::Register(eEventCode::Set_Render_Mode, this, RenderViewWorldOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}
	
	GLOG(Log::eInfo, "Renderview world created.");
	return true;
}

void RenderViewWorld::OnDestroy() {
	EngineEvent::Unregister(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewWorldOnEvent);
	EngineEvent::Unregister(eEventCode::Set_Render_Mode, this, RenderViewWorldOnEvent);
}

void RenderViewWorld::OnResize(uint32_t width, uint32_t height) {
	// Check if different. If so, regenerate projection matrix.
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

bool RenderViewWorld::RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) {
	return true;
}

// Quick sort geometry distance.
static void Swap(GeometryDistance* a, GeometryDistance* b) {
	GeometryDistance temp = *a;
	*a = *b;
	*b = temp;
}

static int Partition(TArray<GeometryDistance>& arr, int low_index, int high_index, bool ascending) {
	GeometryDistance Privot = arr[high_index];
	int i = (low_index - 1);

	for (int j = low_index; j <= high_index; j++) {
		if (ascending) {
			if (arr[j].distance < Privot.distance) {
				++i;
				Swap(&arr[i], &arr[j]);
			}
		}
		else {
			if (arr[j].distance > Privot.distance) {
				++i;
				Swap(&arr[i], &arr[j]);
			}
		}
	}

	Swap(&arr[i + 1], &arr[high_index]);
	return i + 1;
}

static void QuickSort(TArray<GeometryDistance>& arr, int low_index, int high_index, bool ascending) {
	if (low_index < high_index) {
		int PartitionIndex = Partition(arr, low_index, high_index, ascending);

		// Independently sort elements before and after the partition index.
		QuickSort(arr, low_index, PartitionIndex - 1, ascending);
		QuickSort(arr, PartitionIndex + 1, high_index, ascending);
	}
}
