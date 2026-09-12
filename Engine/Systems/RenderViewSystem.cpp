#include "RenderViewSystem.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/DMemory.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"

// TODO: temp
#include "Rendering/Views/RenderViewUI.hpp"
#include "Rendering/Views/RenderViewWorld.hpp"
#include "Rendering/Views/RenderViewDeferred.hpp"
#include "Rendering/Views/RenderViewSkybox.hpp"
#include "Rendering/Views/RenderViewPick.hpp"
#include "Platform/File/JsonObject.h"
#include "ResourceSystem.h"

RenderViewSystem& RenderViewSystem::Get() {
	static RenderViewSystem RenderviewSystemInstance;
	return RenderviewSystemInstance;
}

bool RenderViewSystem::Initialize(IRenderer* renderer, SRenderViewSystemConfig config) {
	if (renderer == nullptr) {
		GLOG(Log::eFatal, "RenderViewSystem::Initialize() Invalid renderer pointer.");
		return false;
	}

	if (config.max_view_count == 0) {
		GLOG(Log::eFatal, "RenderViewSystem::Initialize() Config.max_view_count must be > 0.");
		return false;
	}

	MaxViewCount = config.max_view_count;
	Renderer = renderer;

	// Fill the array with invalid entries.
	RegisteredViews.Resize(MaxViewCount);
	for (uint32_t i = 0; i < MaxViewCount; ++i) {
		RegisteredViews[i] = nullptr;
	}

	// Load render views from app config.
	if (!LoadRenderviewConfig(config.config_path)) {
		GLOG(Log::eFatal, "Failed to create renderview.");
		return false;
	}
	
	Initialized = true;
	return true;
}

void RenderViewSystem::Shutdown() {
	// Renderview
	for (uint32_t i = 0; i < RegisteredViews.Size(); ++i) {
		IRenderView* View = RegisteredViews[i];
		if (View == nullptr) {
			continue;
		}

		// Renderpass & Rendertarget
		for (uint32_t j = 0; j < View->Passes.Size(); ++j) {
			for (uint32_t t = 0; t < View->Passes[j].RenderTargetCount; ++t) {
				Renderer->DestroyRenderTarget(&View->Passes[j].Targets[t], true);
			}
			View->Passes[j].Targets.Clear();
			View->Passes[j].Destroy();
		}
		RegisteredViews[i]->Passes.Clear();
		RegisteredViews[i]->OnDestroy();
	}
	RegisteredViews.Clear();
	RegisteredViews.Empty();
}

bool RenderViewSystem::Create(const RenderViewConfig& config) {
	if (config.pass_count < 1) {
		GLOG(Log::eError, "RenderViewSystem::Create() Renderpass count is zero.");
		return false;
	}

	if (config.name.IsEmpty()) {
		GLOG(Log::eError, "RenderViewSystem::Create() name is required.");
		return false;
	}

	uint16_t ID = INVALID_ID_U16;
	if (RegisteredViewMap.Contains(config.type)){
		GLOG(Log::eError, "RenderViewSystem::Create() A view of type '%d' already exists. A new one will not be created.", config.type);
		return false;
	}

	// Find a new ID.
	for (uint16_t i = 0; i < MaxViewCount; ++i) {
		if (RegisteredViews[i] == nullptr) {
			ID = i;
			break;
		}
	}

	// Make sure id was valid.
	if (ID == INVALID_ID_U16) {
		GLOG(Log::eError, "RenderViewSystem::Create() No available space for a new view. Change system config to account for more.");
		return false;
	}

	// TODO: Assign these function pointers to known functions based on the view type.
	// TODO: Refactor pattern.
	if (RegisteredViews[ID] == nullptr) {
		if (config.type == ERenderViewType::Deferred) {
			RegisteredViews[ID] = NewObject<RenderViewWorldDeferred>(MemoryType::eMemory_Type_Renderer, config);
		}
		else if (config.type == ERenderViewType::UI) {
			RegisteredViews[ID] = NewObject<RenderViewUI>(MemoryType::eMemory_Type_Renderer, config);
		}
		else if (config.type == ERenderViewType::Skybox) {
			RegisteredViews[ID] = NewObject<RenderViewSkybox>(MemoryType::eMemory_Type_Renderer, config);
		}
		else if (config.type == ERenderViewType::Pick) {
			RegisteredViews[ID] = NewObject<RenderViewPick>(MemoryType::eMemory_Type_Renderer, config, Renderer);
		}
		else {
			return true;
		}
	}

	IRenderView* View = RegisteredViews[ID];
	View->ID = ID;

	for (uint32_t i = 0; i < View->RenderpassCount; ++i) {
		if (!Renderer->CreateRenderpass(&View->Passes[i], config.passes[i])) {
			GLOG(Log::eFatal, "RenderViewSystem::Create() Renderpass not found: '%s'.", config.passes[i].name);
			return false;
		}
	}

	View->OnCreate(config);
	RegenerateRendertargets(View);

	// Update the hashtable entry.
	RegisteredViewMap[config.type] = ID;

	return true;
}

void RenderViewSystem::RegenerateRendertargets(IRenderView* view) {
	// Create render target for each.
	for (size_t r = 0; r < view->RenderpassCount; ++r) {
		IRenderpass* Pass = &view->Passes[r];

		for (unsigned char i = 0; i < Pass->RenderTargetCount; ++i) {
			// 告知视图当前重建的是哪一个渲染目标，供其为多缓冲资源（G-Buffer 套件、阴影贴图）选对下标
			view->RegeneratingTargetIndex = (uint32_t)i;

			RenderTarget* Target = &Pass->Targets[i];
			// Destroy the old if exists.
			// TODO: check if a resize is actually needed for this target.
			Renderer->DestroyRenderTarget(Target, false);

			unsigned char AttachCount = (unsigned char)Target->attachments.Size();
			for (uint32_t a = 0; a < AttachCount; ++a) {
				RenderTargetAttachment* Attachment = &Target->attachments[a];
				if (Attachment->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default) {
					if (Attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color) {
						Attachment->texture = Renderer->GetWindowAttachment(i);
					}
					else if (Attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
						Attachment->texture = Renderer->GetDepthAttachment(i);
					}
					else {
						GLOG(Log::eFatal, "Unsupported attachment type: 0x%x", Attachment->type);
						continue;
					}
				}
				else if (Attachment->source == RenderTargetAttachmentSource::eRender_Target_Attachment_Source_View) {
					if (!view->RegenerateAttachmentTarget((uint32_t)r, Attachment)) {
						GLOG(Log::eError, "View failed to regenerate attachment target for attachment type: 0x%x.", Attachment->type);
					}
				}
			}

			// Create the render target.
			Renderer->CreateRenderTarget(AttachCount, Target->attachments,
				Pass, Target->attachments[0].texture->GetWidth(), 
				Target->attachments[0].texture->GetHeight(),
				&Pass->Targets[i]);
		}
	}
}

void RenderViewSystem::OnWindowResize(uint32_t width, uint32_t height) {
	// Send to all view.
	for (uint32_t i = 0; i < RegisteredViews.Size(); ++i) {
		if (RegisteredViews[i]) {
			RegisteredViews[i]->OnResize(width, height);
		}
	}
}

IRenderView* RenderViewSystem::Get(ERenderViewType Type) {
	if (Initialized) {
		if (!RegisteredViewMap.Contains(Type)){
			GLOG(Log::eDebug, "Can not find render view of type '%d', return nullptr.", Type);
			return nullptr;
		}

		uint16_t ID = RegisteredViewMap[Type];
		if (ID != INVALID_ID_U16) {
			IRenderView* Result = RegisteredViews[ID];
			return Result;
		}
	}

	GLOG(Log::eError, "RenderViewSystem::Get() Acquire renderview beform renderview system initialize.");
	return nullptr;
}

bool RenderViewSystem::LoadRenderviewConfig(const FString& path) {
	TArray<RenderViewConfig> RenderviewConfigs;

	FString ConfigFilePath = path.IsEmpty() ?
		FString(ROOT_PATH) + FString("/Configs/RenderViews.json") : path;

	// ---- 加载配置文件 ----
	File ConfigFile(ConfigFilePath);
	if (!ConfigFile.IsExist()) {
		GLOG(Log::eError, "RenderViews.json not found.");
		return false;
	}
	JsonObject Root(ConfigFile);

	// ---- 字符串 → 枚举的辅助 lambda ----
	auto ParseType = [](const FString& s) {
		if (s == "Skybox")   return ERenderViewType::Skybox;
		if (s == "Deferred") return ERenderViewType::Deferred;
		if (s == "UI")       return ERenderViewType::UI;
		if (s == "Pick")     return ERenderViewType::Pick;
		return ERenderViewType::Unknown;
		};

	auto ParseAttachType = [](const FString& s) {
		if (s == "Color") return RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color;
		if (s == "Depth") return RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth;
		return RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color;
		};

	auto ParseSource = [](const FString& s) {
		if (s == "Default") return RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default;
		if (s == "View")    return RenderTargetAttachmentSource::eRender_Target_Attachment_Source_View;
		return RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default;
		};

	auto ParseLoadOp = [](const FString& s) {
		if (s == "DontCare") return RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_DontCare;
		if (s == "Load")     return RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load;
		if (s == "Clear")    return RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Clear;
		return RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_DontCare;
		};

	auto ParseStoreOp = [](const FString& s) {
		if (s == "Store")   return RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store;
		if (s == "DontCare")return RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_DontCare;
		return RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store;
		};

	auto ParseClearFlags = [](const JsonObject& flagsArr) {
		uint8_t result = RenderpassClearFlags::eRenderpass_Clear_None;
		for (size_t i = 0; i < flagsArr.Size(); ++i) {
			FString f = FString(flagsArr.ArrayItemAt(i).ReadString().c_str());
			if (f == "Color")   result = result | RenderpassClearFlags::eRenderpass_Clear_Color_Buffer;
			if (f == "Depth")   result = result | RenderpassClearFlags::eRenderpass_Clear_Depth_Buffer;
			if (f == "Stencil") result = result | RenderpassClearFlags::eRenderpass_Clear_Stencil_Buffer;
		}
		return result;
		};

	uint32_t FramebufferWidth = Renderer->GetWidth();
	uint32_t FramebufferHeight = Renderer->GetHeight();

	// ---- 遍历 renderviews 数组 ----
	JsonObject ViewsArr = Root.Read("renderviews");
	size_t ViewCount = ViewsArr.Size();

	for (size_t vi = 0; vi < ViewCount; ++vi) {
		JsonObject ViewJson = ViewsArr.ArrayItemAt(vi);

		RenderViewConfig ViewConfig;
		ViewConfig.name = ViewJson.ReadString("name").c_str();
		ViewConfig.type = ParseType(FString(ViewJson.ReadString("type").c_str()));
		ViewConfig.width = Cast<unsigned short>(FramebufferWidth);
		ViewConfig.height = Cast<unsigned short>(FramebufferHeight);
		ViewConfig.view_matrix_source = RenderViewViewMatrixtSource::eRender_View_View_Matrix_Source_Scene_Camera;

		JsonObject PassesArr = ViewJson.Read("passes");
		size_t PassCount = PassesArr.Size();
		TArray<RenderpassConfig> Passes(PassCount);

		for (size_t pi = 0; pi < PassCount; ++pi) {
			JsonObject PassJson = PassesArr.ArrayItemAt(pi);
			RenderpassConfig& Pass = Passes[pi];

			Pass.name = PassJson.ReadString("name").c_str();
			Pass.render_area = Vector4(0, 0, Cast<float>(FramebufferWidth), Cast<float>(FramebufferHeight));
			Pass.depth = PassJson.ReadFloat("depth", 1.0f);
			Pass.stencil = (uint32_t)PassJson.ReadInt("stencil", 0);

			Vector4 CC = PassJson.ReadVector4("clear_color", Vector4(0, 0, 0, 1));
			Pass.clear_color = CC;
			Pass.clear_flags = ParseClearFlags(PassJson.Read("clear_flags"));

			// render_target_count：配置文件里显式写了就用，否则用 swapchain 数量
			int ExplicitRTC = PassJson.ReadInt("render_target_count", -1);
			Pass.renderTargetCount = (ExplicitRTC >= 0)
				? Cast<unsigned char>(ExplicitRTC)
				: Renderer->GetWindowAttachmentCount();

			// 附件
			JsonObject AttachArr = PassJson.Read("attachments");
			for (size_t ai = 0; ai < AttachArr.Size(); ++ai) {
				JsonObject AJson = AttachArr.ArrayItemAt(ai);
				RenderTargetAttachmentConfig Attach;
				Memory::Zero(&Attach, sizeof(Attach));

				Attach.type = ParseAttachType(FString(AJson.ReadString("type").c_str()));
				Attach.source = ParseSource(FString(AJson.ReadString("source").c_str()));
				Attach.loadOperation = ParseLoadOp(FString(AJson.ReadString("load_op").c_str()));
				Attach.storeOperation = ParseStoreOp(FString(AJson.ReadString("store_op").c_str()));
				Attach.presentAfter = AJson.ReadBool("present_after", false);
				Attach.index = (uint32_t)AJson.ReadInt("index", 0);

				Pass.target.attachments.Push(Attach);
			}
		}

		ViewConfig.passes = Passes;
		ViewConfig.pass_count = (unsigned char)PassCount;

		if (!RenderViewSystem::Get().Create(ViewConfig)) {
			GLOG(Log::eFatal, "Failed to create view '%s'.", ViewConfig.name.CStr());
			return false;
		}
	}

	return true;
}