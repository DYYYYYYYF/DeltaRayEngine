#include "RenderViewPick.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/DMemory.hpp"
#include "Core/Event.hpp"
#include "Core/ID/UID.hpp"
#include "Math/DMath.hpp"

#include "Containers/TArray.hpp"
#include "Systems/MaterialSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/RenderViewSystem.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"
#include "Rendering/Interface/IRendererBackend.hpp"
#include "Framework/Classes/TextActor.h"
#include "Framework/Components/StaticMeshComponent.h"
#include "Framework/Components/CameraComponent.h"

static bool RenderViewPickOnEvent(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
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

static bool OnMouseMoved(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
	if (code == eEventCode::Mouse_Moved) {
		RenderViewPick* self = (RenderViewPick*)listenerInst;
		
		// Update position and regenerate the projection matrix.
		short x = context.data.i16[0];
		short y = context.data.i16[1];
		self->SetMouseX(x);
		self->SetMouseY(y);

		return true;
	}

	return false;
}

void RenderViewPick::AcquireShaderInstance() {
	// UI Shader.
	uint32_t Instance = Renderer->AcquireInstanceResource(UIShaderInfo.UsedShader, TArray<FTextureMap*>());
	if (Instance == INVALID_ID) {
		GLOG(Log::eError, "Failed to acquire shader resource.");
		return;
	}

	// World Shader.
	Instance = Renderer->AcquireInstanceResource(WorldShaderInfo.UsedShader, TArray<FTextureMap*>());
	if (Instance == INVALID_ID) {
		GLOG(Log::eError, "Failed to acquire shader resource.");
		return;
	}

	InstanceCount++;
	InstanceUpdated.Push(false);
}

void RenderViewPick::ReleaseShaderInstance() {
	for (int i = 0; i < InstanceCount; ++i) {
		// UI Shader
		if (!Renderer->ReleaseInstanceResource(UIShaderInfo.UsedShader, i)) {
			GLOG(Log::eError, "Failed to release shader resource.");
		}

		// UI Shader
		if (!Renderer->ReleaseInstanceResource(WorldShaderInfo.UsedShader, i)) {
			GLOG(Log::eError, "Failed to release shader resource.");
		}
	}

	InstanceUpdated.Clear();
}

RenderViewPick::RenderViewPick(const RenderViewConfig& config, IRenderer* renderer) {
	Type = config.type;
	Name = config.name;
	CustomShaderName = config.custom_shader_name;
	RenderpassCount = config.pass_count;
	Passes.Resize(RenderpassCount);
	Renderer = renderer;
}

bool RenderViewPick::OnCreate(const RenderViewConfig& config) {
	// NOTE: In this heavily-customized view, the exact number of passes is known, so
	// these index assumptions are fine.
	WorldShaderInfo.Pass = &Passes[0];
	UIShaderInfo.Pass = &Passes[1];

	// Builtin UI Pick shader.
	const char* UIShaderName = "Shader.Builtin.UIPick";
	UAsset ConfigResource(UIShaderName);
	if (!ResourceSystem::Get().Load(UIShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin UI Pick shader.");
		return false;
	}
	FShaderConfig* Config = (FShaderConfig*)ConfigResource.Data;
	if (!ShaderSystem::Get().Create(UIShaderInfo.Pass, Config)) {
		GLOG(Log::eError, "Failed to load builtin UI Pick shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);
	UIShaderInfo.UsedShader = ShaderSystem::Get().Get(UIShaderName);

	// Extract uniform locations.
	UIShaderInfo.IDColorLocation = UIShaderInfo.UsedShader->GetUniformIndex("id_color");
	UIShaderInfo.ModelLocation = UIShaderInfo.UsedShader->GetUniformIndex("model");
	UIShaderInfo.ProjectionLocation = UIShaderInfo.UsedShader->GetUniformIndex("projection");
	UIShaderInfo.ViewLocation = UIShaderInfo.UsedShader->GetUniformIndex("view");

	// Default UI properties.
	UIShaderInfo.NearClip = -100.0f;
	UIShaderInfo.FarClip = 100.0f;
	UIShaderInfo.Fov = 0;
	UIShaderInfo.ProjectionMatrix = Matrix4::Orthographic(0.0f, config.width, config.height, 0.0f, UIShaderInfo.NearClip, UIShaderInfo.FarClip);
	UIShaderInfo.ViewMatrix = Matrix4::Identity();

	// Builtin World pick shader.
	const char* WorldShaderName = "Shader.Builtin.WorldPick";
	if (!ResourceSystem::Get().Load(WorldShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin UI Pick shader.");
		return false;
	}
	Config = (FShaderConfig*)ConfigResource.Data;
	if (!ShaderSystem::Get().Create(WorldShaderInfo.Pass, Config)) {
		GLOG(Log::eError, "Failed to load builtin UI Pick shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);
	WorldShaderInfo.UsedShader = ShaderSystem::Get().Get(WorldShaderName);

	// Extract uniform locations.
	WorldShaderInfo.IDColorLocation = UIShaderInfo.UsedShader->GetUniformIndex("id_color");
	WorldShaderInfo.ModelLocation = UIShaderInfo.UsedShader->GetUniformIndex("model");
	WorldShaderInfo.ProjectionLocation = UIShaderInfo.UsedShader->GetUniformIndex("projection");
	WorldShaderInfo.ViewLocation = UIShaderInfo.UsedShader->GetUniformIndex("view");

	// Default World properties.
	WorldShaderInfo.NearClip = 0.1f;
	WorldShaderInfo.FarClip = 1000.0f;
	WorldShaderInfo.Fov = Deg2Rad(45.0f);
	WorldShaderInfo.ProjectionMatrix = Matrix4::Perspective(WorldShaderInfo.Fov, 
		(float)config.width/config.height, WorldShaderInfo.NearClip, WorldShaderInfo.FarClip);
	WorldShaderInfo.ViewMatrix = Matrix4::Identity();

	InstanceCount = 0;
	ColorTargetAttachment = Renderer->AcquireTexture("RenderviewPick_ColorTargetAttachment");
	DepthTargetAttachment = Renderer->AcquireTexture("RenderviewPick_DepthTargetAttachment");

	if (!EngineEvent::Register(eEventCode::Mouse_Moved, this, OnMouseMoved)) {
		GLOG(Log::eError, "Unable to listen for mouse moved event, creation failed.");
		return false;
	}

	if (!EngineEvent::Register(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewPickOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}

	GLOG(Log::eInfo, "Renderview pick created.");
	return true;
}

void RenderViewPick::OnDestroy() {
	EngineEvent::Unregister(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewPickOnEvent);
	EngineEvent::Unregister(eEventCode::Mouse_Moved, this, RenderViewPickOnEvent);

	ReleaseShaderInstance();
	ColorTargetAttachment->Destroy();
	DepthTargetAttachment->Destroy();
}

void RenderViewPick::OnResize(uint32_t width, uint32_t height) {
	// Check if different. If so, regenerate projection matrix.
	if (width == Width && height == Height) {
		return;
	}

	Width = (uint16_t)width;
	Height = (uint16_t)height;

	// UI
	UIShaderInfo.ProjectionMatrix = Matrix4::Orthographic(0.0f, (float)Width, (float)Height, 0.0f, UIShaderInfo.NearClip, UIShaderInfo.FarClip);

	// World
	float Aspect = (float)Width / Height;
	WorldShaderInfo.ProjectionMatrix = Matrix4::Perspective(WorldShaderInfo.Fov, Aspect, WorldShaderInfo.NearClip, WorldShaderInfo.FarClip);

	for (uint32_t i = 0; i < RenderpassCount; ++i) {
		Passes[i].SetRenderArea(Vector4(0, 0, (float)Width, (float)Height));
	}
}

bool RenderViewPick::RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) {
	if (attachment->type == eRender_Target_Attachment_Type_Color) {
		attachment->texture = ColorTargetAttachment;
	}
	else if (attachment->type == eRender_Target_Attachment_Type_Depth) {
		attachment->texture = DepthTargetAttachment;
	}
	else {
		GLOG(Log::eError, "Unsupported attachment type 0x%x.", attachment->type);
		return false;
	}

	if (passIndex == 1) {
		// Do not need to regenerate for both passes since they both use the same attachment.
		// Just attach it and move on.
		return true;
	}

	// Destroy current attachment if it exists.
	if (attachment->texture){
		attachment->texture->Destroy();
	}

	// Setup a new texture.
	// Generate a UUID to act as the texture name.
	uint32_t RenderAreaWidth = (uint32_t)Passes[passIndex].GetRenderArea().z;
	uint32_t RenderAreaHeight = (uint32_t)Passes[passIndex].GetRenderArea().w;
	bool HasTransparency = false;

	attachment->texture->SetWidth(RenderAreaWidth);
	attachment->texture->SetHeight(RenderAreaHeight);
	attachment->texture->SetChannelCount(4);
	attachment->texture->AddFlag(HasTransparency ? TextureFlagBits::eTexture_Flag_Has_Transparency : 0);
	attachment->texture->AddFlag(TextureFlagBits::eTexture_Flag_Is_Writeable);
	if (attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
		attachment->texture->AddFlag(TextureFlagBits::eTexture_Flag_Depth);
	}

	attachment->texture->LoadWriteable();

	return true;
}

//bool RenderViewPick::OnRender(struct RenderViewPacket* packet, RHI* back_renderer, size_t frame_number, size_t render_target_index) {
//	uint32_t p = 0;
//	IRenderpass* Pass = (IRenderpass*)&Passes[p];	// first
//
//	if (render_target_index == 0) {
//		// Reset.
//		size_t Count = InstanceUpdated.Size();
//		for (uint32_t i = 0; i < Count; ++i) {
//			InstanceUpdated[i] = false;
//		}
//
//		Pass->Begin(&Pass->Targets[render_target_index]);
//		PickPacketData* PacketData = (PickPacketData*)packet->extended_data;
//
//		uint64_t CurrentInstanceID = 0;
//		UShader* WorldShader = WorldShaderInfo.UsedShader;
//		if (!WorldShader) {
//			GLOG(Log::eError, "Failed to use world pick shader. WorldShader is nullptr.");
//			return false;
//		}
//
//		// World
//		if (!WorldShader->Use()) {
//			GLOG(Log::eError, "Failed to use world pick shader. Render frame failed.");
//			return false;
//		}
//
//		// Apply globals
//		if (!WorldShader->SetUniformByIndex(WorldShaderInfo.ProjectionLocation, &WorldShaderInfo.ProjectionMatrix)) {
//			GLOG(Log::eError, "Failed to apply projection matrix");
//		}
//		if (!WorldShader->SetUniformByIndex(WorldShaderInfo.ViewLocation, &WorldShaderInfo.ViewMatrix)) {
//			GLOG(Log::eError, "Failed to apply view matrix");
//		}
//		WorldShader->ApplyGlobal();
//
//		// Draw geometries. Start from 0 since world geometries are added first, and stop at the world geometry count.
//		uint32_t WorldGeometryCount = (uint32_t)PacketData->WorldMeshData.size();
//		for (uint32_t i = 0; i < WorldGeometryCount; ++i) {
//			GeometryRenderData* Geo = &packet->geometries[i];
//			CurrentInstanceID = Geo->uniqueID;
//
//			WorldShader->BindInstance(CurrentInstanceID);
//
//			// Get color based on id
//			Vector3 IDColor;
//			uint32_t R, G, B;
//			UInt2RGB(Geo->uniqueID, &R, &G, &B);
//			RGB2Vec(R, G, B, &IDColor);
//			if (!WorldShader->SetUniformByIndex(WorldShaderInfo.IDColorLocation, &IDColor)) {
//				GLOG(Log::eError, "Failed to apply id colour uniform.");
//				return false;
//			}
//
//			WorldShader->ApplyInstance();
//			InstanceUpdated[CurrentInstanceID] = true;
//
//			// Apply the locals.
//			if (!WorldShader->SetUniformByIndex(WorldShaderInfo.ModelLocation, &Geo->model_mat)) {
//				GLOG(Log::eError, "Failed to apply model matrix for world geometry.");
//			}
//
//			// Draw
//			Renderer->DrawGeometry(&packet->geometries[i]);
//		}
//
//		Pass->End();
//
//		p++;
//		Pass = &Passes[p];
//
//		// Second pass
//		Pass->Begin(&Pass->Targets[render_target_index]);
//
//		// UI
//		UShader* UIShader = UIShaderInfo.UsedShader;
//		if (!UIShader) {
//			return false;
//		}
//
//		if (!UIShader->Use()) {
//			GLOG(Log::eError, "Failed to use material shader. Render frame failed.");
//			return false;
//		}
//
//		// Apply globals.
//		if (!UIShader->SetUniformByIndex(UIShaderInfo.ProjectionLocation, &UIShaderInfo.ProjectionMatrix)) {
//			GLOG(Log::eError, "Failed to apply projection matrix");
//		}
//		if (!UIShader->SetUniformByIndex(UIShaderInfo.ViewLocation, &UIShaderInfo.ViewMatrix)) {
//			GLOG(Log::eError, "Failed to apply view matrix");
//		}
//		UIShader->ApplyGlobal();
//
//		// Draw geometries. Start off where world geometries left off.
//		for (uint32_t i = WorldGeometryCount; i < packet->geometries.size(); ++i) {
//			GeometryRenderData* Geo = &packet->geometries[i];
//			CurrentInstanceID = Geo->uniqueID;
//
//			UIShader->BindInstance(CurrentInstanceID);
//
//			// Get color based on id
//			Vector3 IDColor;
//			uint32_t R, G, B;
//			UInt2RGB(Geo->uniqueID, &R, &G, &B);
//			RGB2Vec(R, G, B, &IDColor);
//			if (!UIShader->SetUniformByIndex(WorldShaderInfo.IDColorLocation, &IDColor)) {
//				GLOG(Log::eError, "Failed to apply id colour uniform.");
//				return false;
//			}
//
//			UIShader->ApplyInstance();
//			InstanceUpdated[CurrentInstanceID] = true;
//
//			// Apply the locals.
//			if (!UIShader->SetUniformByIndex(WorldShaderInfo.ModelLocation, &Geo->model_mat)) {
//				GLOG(Log::eError, "Failed to apply model matrix for world geometry.");
//			}
//
//			// Draw
//			Renderer->DrawGeometry(&packet->geometries[i]);
//		}
//
//		// Draw bitmap text.
// 		for (uint32_t i = 0; i < PacketData->TextCount; ++i) {
//			ATextActor* Text = PacketData->Texts[i];
//			CurrentInstanceID = Text->GetUniqueID();
//			UIShader->BindInstance(CurrentInstanceID);
//
//			// Get color based on id
//			Vector3 IDColor;
//			uint32_t R, G, B;
//			UInt2RGB(Text->GetUniqueID(), &R, &G, &B);
//			RGB2Vec(R, G, B, &IDColor);
//			if (!UIShader->SetUniformByIndex(UIShaderInfo.IDColorLocation, &IDColor)) {
//				GLOG(Log::eError, "Failed to apply id colour uniform.");
//				return false;
//			}
//
//			UIShader->ApplyInstance();
//
//			// Apply the locals.
//			Matrix4 Model = Text->GetLocalTransform();
//			if (!UIShader->SetUniformByIndex(UIShaderInfo.ModelLocation, &Model)) {
//				GLOG(Log::eError, "Failde to apply model matrix for text.");
//			}
//
//			UTextComponent* TextComp = Text->GetTextComponent();
//			if (!TextComp) {
//				return false;
//			}
//
//			TextComp->Draw();
//		}
//
//		Pass->End();
//	}
//
//	// Read pixel data.
//	UTexture* t = ColorTargetAttachment;
//
//	// Clamp to image size.
//	unsigned short CoordX = CLAMP(MouseX, 0, Width - 1);
//	unsigned short CoordY = CLAMP(MouseY, 0, Height - 1);
//	FColor Pixel = t->ReadTexturePixel(CoordX, CoordY);
//
//	// Extract the id from the sampled color.
//	uint32_t ObjID = INVALID_ID;
//	RGB2Uint(Pixel.R, Pixel.G, Pixel.B, &ObjID);
//	if (ObjID == 0x00FFFFFF) {
//		// This is pure white.
//		ObjID = INVALID_ID;
//	}
//
//	SEventContext Context;
//	Context.data.u32[0] = ObjID;
//	EngineEvent::Fire(eEventCode::Object_Hover_ID_Changed, 0, Context);
//
//	return true;
//}
