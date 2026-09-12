#include "RenderViewDeferred.hpp"

#include "Core/EngineLogger.hpp"
#include "Core/Event.hpp"
#include "Core/DMemory.hpp"
#include "Math/DMath.hpp"

#include "Containers/TArray.hpp"
#include "Containers/FString.hpp"

#include "Systems/MaterialSystem.h"
#include "Systems/ShaderSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/RenderViewSystem.hpp"
#include "Systems/TextureSystem.h"
#include "Systems/GeometrySystem.h"

#include "Rendering/Renderer.hpp"
#include "Rendering/Interface/IRenderpass.hpp"
#include "Rendering/Interface/IRendererBackend.hpp"

#include "Framework/Components/CameraComponent.h"
#include "Framework/Components/DirectionalLightComponent.h"
#include "Rendering/RenderWorld/RenderProxy.h"

static bool RenderViewWorldDeferredOnEvent(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
	IRenderView* self = (IRenderView*)listenerInst;
	if (self == nullptr) {
		return false;
	}

	switch ((eEventCode)code) {
	case eEventCode::Default_Rendertarget_Refresh_Required: {
		RenderViewSystem::Get().RegenerateRendertargets(self);
		return true;
	}

	case eEventCode::Set_Render_Mode: {
		EShaderRenderMode RenderMode = EShaderRenderMode(context.data.i32[0]);
		switch (RenderMode) {
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
	}

	return false;
}

RenderViewWorldDeferred::RenderViewWorldDeferred() {
	FullscreenQuad = nullptr;
	Renderer = nullptr;
}

RenderViewWorldDeferred::RenderViewWorldDeferred(const RenderViewConfig& config) {
	Type = config.type;
	Name = config.name;
	CustomShaderName = config.custom_shader_name;
	RenderpassCount = config.pass_count; // 3个通道：0=阴影(深度预通道)、1=G-Buffer、2=延迟光照
	Passes.Resize(RenderpassCount);
	FullscreenQuad = nullptr;
	Renderer = IRenderer::GetRenderer();
}

bool RenderViewWorldDeferred::OnCreate(const RenderViewConfig& config) {
	// 加载阴影通道着色器 (pass 0：深度预通道)
	const char* ShadowShaderName = "Shader.Builtin.Shadow";
	UAsset ShadowConfigResource(ShadowShaderName);
	if (!ResourceSystem::Get().Load(ShadowShaderName, EAssetType::Shader, nullptr, &ShadowConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin shadow shader.");
		return false;
	}

	FShaderConfig* ShadowConfig = (FShaderConfig*)ShadowConfigResource.Data;
	// 第 0 个通道用于阴影深度渲染
	if (!ShaderSystem::Get().Create(&Passes[0], ShadowConfig)) {
		GLOG(Log::eError, "Failed to create shadow shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ShadowConfigResource);

	ShadowShader = ShaderSystem::Get().Get(ShadowShaderName);

	// 加载G-Buffer着色器
	const char* GBufferShaderName = "Shader.Builtin.GBuffer";
	UAsset ConfigResource(GBufferShaderName);
	if (!ResourceSystem::Get().Load(GBufferShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin G-Buffer shader.");
		return false;
	}

	FShaderConfig* Config = (FShaderConfig*)ConfigResource.Data;
	// 第 1 个通道用于G-Buffer渲染
	if (!ShaderSystem::Get().Create(&Passes[1], Config)) {
		GLOG(Log::eError, "Failed to create G-Buffer shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);

	GBufferShader = ShaderSystem::Get().Get(GBufferShaderName);

	// 加载延迟光照着色器
	const char* LightingShaderName = "Shader.Builtin.DeferredLighting";
	if (!ResourceSystem::Get().Load(LightingShaderName, EAssetType::Shader, nullptr, &ConfigResource)) {
		GLOG(Log::eError, "Failed to load builtin deferred lighting shader.");
		return false;
	}

	Config = (FShaderConfig*)ConfigResource.Data;
	// 第 2 个通道用于光照计算
	if (!ShaderSystem::Get().Create(&Passes[2], Config)) {
		GLOG(Log::eError, "Failed to create deferred lighting shader.");
		return false;
	}
	ResourceSystem::Get().Unload(&ConfigResource);

	LightingShader = ShaderSystem::Get().Get(LightingShaderName);

	// 设置渲染参数 (与原World渲染保持一致)
	NearClip = 0.1f;
	FarClip = 1000.0f;
	Fov = Deg2Rad(45.0f);

	ProjectionMatrix = Matrix4::Perspective(Fov, (float)config.width / config.height, NearClip, FarClip);
	WorldCamera = CameraSystem::Get().GetMainCamera();

	// 环境光与方向光参数统一由场景中的方向光 Actor 提供，
	// 每次计算光空间矩阵时从 LightSystem 刷新（见 UpdateLightSpaceMatrix）

	// 创建G-Buffer纹理
	if (!CreateGBufferTextures(config.width, config.height)) {
		GLOG(Log::eError, "Failed to create G-Buffer textures.");
		return false;
	}

	// 创建阴影贴图与光空间矩阵（分辨率固定，不随窗口变化）
	if (!CreateShadowResources()) {
		GLOG(Log::eError, "Failed to create shadow resources.");
		return false;
	}

	// 阴影通道的渲染区域固定为阴影贴图尺寸（配置里的默认渲染区域是窗口尺寸，会只渲染到贴图的一部分）
	Passes[0].SetRenderArea(Vector4(0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE));

	if (!EngineEvent::Register(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewWorldDeferredOnEvent)) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}
	if (!EngineEvent::Register(eEventCode::Set_Render_Mode, this, RenderViewWorldDeferredOnEvent)) {
		GLOG(Log::eError, "Unable to listen for render mode event, creation failed.");
		return false;
	}

	GLOG(Log::eInfo, "Deferred world render view created successfully.");
	return true;
}

void RenderViewWorldDeferred::OnDestroy() {
	EngineEvent::Unregister(eEventCode::Default_Rendertarget_Refresh_Required, this, RenderViewWorldDeferredOnEvent);
	EngineEvent::Unregister(eEventCode::Set_Render_Mode, this, RenderViewWorldDeferredOnEvent);

	DestroyGBufferTextures();
	DestroyShadowResources();
	DestroyFullscreenQuad();
}

void RenderViewWorldDeferred::OnResize(uint32_t width, uint32_t height) {
	if (width == Width && height == Height) {
		return;
	}

	Width = (uint16_t)width;
	Height = (uint16_t)height;
	ProjectionMatrix = Matrix4::Perspective(Fov, (float)Width / (float)Height, NearClip, FarClip);

	// 重新创建G-Buffer纹理
	DestroyGBufferTextures();
	CreateGBufferTextures(width, height);

	for (uint32_t i = 0; i < RenderpassCount; ++i) {
		// pass 0 = 阴影通道：渲染区域固定为阴影贴图尺寸；其余通道跟随窗口尺寸
		if (i == 0) {
			Passes[i].SetRenderArea(Vector4(0, 0, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE));
		}
		else {
			Passes[i].SetRenderArea(Vector4(0, 0, (float)Width, (float)Height));
		}

		// 更新所有渲染目标的纹理引用
		for (uint32_t targetIndex = 0; targetIndex < Passes[i].Targets.Size(); ++targetIndex) {
			RenderTarget* target = &Passes[i].Targets[targetIndex];

			if (i == 0) {
				// 阴影通道：深度附件指向该渲染目标对应的阴影贴图（分辨率固定，重建渲染目标时保持引用正确）
				UTexture* ShadowMap = ShadowMapTextures[targetIndex % MAX_RENDER_TARGETS];
				for (uint32_t attachIndex = 0; attachIndex < target->attachments.Size(); ++attachIndex) {
					RenderTargetAttachment* attachment = &target->attachments[attachIndex];

					if (attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
						attachment->texture = ShadowMap;
					}
				}
			}
			else if (i == 1) {
				// 更新G-Buffer纹理引用
				uint32_t bufferIndex = targetIndex % MAX_RENDER_TARGETS;
				for (uint32_t attachIndex = 0; attachIndex < target->attachments.Size(); ++attachIndex) {
					RenderTargetAttachment* attachment = &target->attachments[attachIndex];

					if (attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color) {
						switch (attachment->index) {
						case 0:
							attachment->texture = GBuffers[bufferIndex].AlbedoTexture;
							break;
						case 1:
							attachment->texture = GBuffers[bufferIndex].NormalTexture;
							break;
						case 2:
							attachment->texture = GBuffers[bufferIndex].PositionTexture;
							break;
						}
					}
					else if (attachment->type == RenderTargetAttachmentType::eRender_Target_Attachment_Type_Depth) {
						attachment->texture = GBuffers[bufferIndex].DepthTexture;
					}
				}
			}
			// 光照通道使用swapchain，无需更新纹理引用
		}
	}
}

bool RenderViewWorldDeferred::RegenerateAttachmentTarget(uint32_t passIndex, RenderTargetAttachment* attachment) {
	if (passIndex == 0) {
		// 阴影通道 (pass 0) - 仅深度附件，使用 SHADOW_MAP_SIZE x SHADOW_MAP_SIZE 阴影贴图
		if (attachment->type & eRender_Target_Attachment_Type_Depth) {
			// 每个渲染目标绑定各自的那份阴影贴图（按 targets 下标取模），实现帧间隔离；
			// 渲染期阴影通道与光照采样端使用同一取模规则定位贴图，保证写入与读取一致。
			UTexture* ShadowMap = ShadowMapTextures[RegeneratingTargetIndex % MAX_RENDER_TARGETS];
			if (ShadowMap == nullptr) {
				GLOG(Log::eError, "Shadow: Shadow map texture is not ready.");
				return false;
			}
			attachment->texture = ShadowMap;
		}
		else {
			GLOG(Log::eError, "Shadow: Unsupported attachment type %d", attachment->type);
			return false;
		}
	}
	else if (passIndex == 1) {
		// G-Buffer通道 - 多个渲染目标
		// 按当前重建的渲染目标索引选取对应的 G-Buffer 套件，使每个 swapchain 图像写各自的
		// 反照率/法线/位置/深度纹理；此前固定取第 0 套会让在飞的多个帧共用同一批资源，
		// 与 MaxFramesInFlight>1 叠加后产生帧间覆写竞争。
		const uint32_t bufferIndex = RegeneratingTargetIndex % MAX_RENDER_TARGETS;

		if (attachment->type & eRender_Target_Attachment_Type_Color) {
			// 根据attachment的索引来决定使用哪个G-Buffer纹理
			switch (attachment->index) {
			case 0:
				attachment->texture = GBuffers[bufferIndex].AlbedoTexture;
				break;
			case 1:
				attachment->texture = GBuffers[bufferIndex].NormalTexture;
				break;
			case 2:
				attachment->texture = GBuffers[bufferIndex].PositionTexture;
				break;
			default:
				return false;
			}
		}
		else if (attachment->type & eRender_Target_Attachment_Type_Depth) {
			attachment->texture = GBuffers[bufferIndex].DepthTexture;
		}
		else {
			GLOG(Log::eError, "G-Buffer: Unsupported attachment type %d", attachment->type);
			return false;
		}
	}
	else if (passIndex == 2) {
		// 光照通道 - 使用默认颜色缓冲或者swapchain
		// 这里通常不需要重新生成attachment，直接使用默认的
		if ((attachment->type & eRender_Target_Attachment_Type_Color) && attachment->index == 0) {
			// 主颜色输出，通常是swapchain图像
			return true;
		}
	}
	else {
		GLOG(Log::eError, "Unsupported pass index %d for deferred rendering", passIndex);
		return false;
	}

	return true;
}

/**
 * 取得指定通道中可安全使用的渲染目标。
 * preferredIndex 为该通道期望使用的索引（通常为当前 swapchain 图像索引）；
 * 当 Targets 为空或该索引越界时回退到索引 0 并记录日志，避免 &Targets[index] 越界访问。
 */
static RenderTarget* AcquirePassTarget(IRenderpass* pass, uint8_t preferredIndex, const char* passName) {
	if (pass == nullptr || pass->Targets.IsEmpty()) {
		GLOG(Log::eError, "%s: render target list is empty.", passName);
		return nullptr;
	}

	if ((size_t)preferredIndex >= pass->Targets.Size()) {
		GLOG(Log::eWarn, "%s: preferred target index %u is out of range (target count = %zu), fallback to index 0.",
			passName, (uint32_t)preferredIndex, pass->Targets.Size());
		return &pass->Targets[0];
	}

	return &pass->Targets[preferredIndex];
}

void RenderViewWorldDeferred::Render(const TArray<FRenderProxy*>& RenderProxies) {
	// 确保全屏四边形存在
	if (!FullscreenQuad) {
		if (!CreateFullscreenQuad()) {
			GLOG(Log::eError, "Failed to create fullscreen quad.");
			return;
		}
	}

	// 逐帧刷新光空间矩阵（阴影通道与延迟光照通道都依赖它）
	UpdateLightSpaceMatrix();

	// 注意：本帧要用的 G-Buffer 套件、阴影贴图下标都要等下面拿到窗口附件索引（RTIndex）后才能确定（见阶段二），
	// 此处不再固定取第 0 套，避免"光照采样读到的 G-Buffer"与"G-Buffer 通道实际写入的套件"错位。

	// 阶段一：DRAWCALL 生成、收集与状态排序
	// 复用成员容器，避免逐帧堆分配；每帧先整体清空，杜绝跨帧残留与跨通道串数据
	GBufferDrawCalls.Clear();
	ShadowDrawCalls.Clear();
	LightingDrawCalls.Clear();

	// 仅做容量提示（不改变元素数量），push_back 仍会按需增长
	GBufferDrawCalls.Reserve(RenderProxies.Size());
	LightingDrawCalls.Reserve(1);

	// --- 收集 GBuffer 几何体的 DrawCall ---
	for (uint32_t i = 0; i < RenderProxies.Size(); ++i) {
		FStaticMeshRenderProxy* RenderProxy = Cast<FStaticMeshRenderProxy*>(RenderProxies[i]);
		if (!RenderProxy || !RenderProxy->IsVisible()) continue;

		TArray<UGeometry*> Geometries = RenderProxy->GetMesh();
		for (UGeometry* Geometry : Geometries) {
			if (!Geometry || !Geometry->IsVisible()) continue;

			DrawCall dc;
			UMaterialInstance* Mat = Geometry->GetMaterialInstance();
			dc.geometry = Geometry;
			dc.model = RenderProxy->GetModelMatrix();
			dc.material = Mat;
			dc.shader = GBufferShader;
			dc.userData = nullptr;
			dc.sortKey = ((uint64_t)dc.shader->ID << 32) | (uint64_t)Mat->GetInternalID();
			GBufferDrawCalls.Push(dc);
		}
	}

	// 状态排序
	GBufferDrawCalls.Sort([](const DrawCall& a, const DrawCall& b) {
		return a.sortKey < b.sortKey;
		});

	if (ShadowShader == nullptr) {
		GLOG(Log::eError, "Shadow shader is not ready, skip shadow pass.");
		return;
	}

	// --- 收集 阴影通道 的 DrawCall：几何体/材质与 G-Buffer 完全一致，仅把 shader 换成阴影着色器 ---
	ShadowDrawCalls.Reserve(GBufferDrawCalls.Size());
	for (const DrawCall& GBufferDC : GBufferDrawCalls) {
		DrawCall ShadowDC = GBufferDC;
		ShadowDC.shader = ShadowShader;
		ShadowDC.sortKey = ((uint64_t)ShadowDC.shader->ID << 32) | (uint64_t)ShadowDC.material->GetInternalID();
		ShadowDrawCalls.Push(ShadowDC);
	}

	// 状态排序
	ShadowDrawCalls.Sort([](const DrawCall& a, const DrawCall& b) {
		return a.sortKey < b.sortKey;
		});

	// --- 收集 延迟光照 的 DrawCall ---
	DrawCall LightingDC;
	UMaterialInstance* DeferredLightingMat = FullscreenQuad->GetMaterialInstance();
	LightingDC.geometry = FullscreenQuad;
	LightingDC.model = Matrix4::Identity();
	LightingDC.material = DeferredLightingMat;
	LightingDC.shader = LightingShader;
	// 此处（阶段一）尚未确定本帧的多缓冲下标，先置空占位，待阶段二算出 BufferIndex 后回填
	LightingDC.userData = nullptr;
	LightingDC.sortKey = ((uint64_t)LightingDC.shader->ID << 32) | (uint64_t)DeferredLightingMat->GetInternalID();

	LightingDrawCalls.Push(LightingDC);


	// 阶段二：逐帧不变量（窗口附件索引 + 帧号）
	uint8_t RTIndex = Renderer->GetWindowAttachmentIndex();
	uint64_t FrameNumber = Renderer->GetFrameNum();

	// 本帧使用的多缓冲资源下标：G-Buffer 套件与阴影贴图共用同一取模规则，
	// 与 RegenerateAttachmentTarget 里渲染目标的附件绑定规则（targets 下标 % MAX_RENDER_TARGETS）保持一致。
	// RTIndex 必然小于渲染目标数量，因此该下标一定落在有效范围内，不会触发回退。
	const uint32_t BufferIndex = (uint32_t)RTIndex % MAX_RENDER_TARGETS;
	GBufferSet* CurrentGBuffer = GetCurrentGBufferSet(BufferIndex);

	// DrawCall 收集（阶段一）早于本阶段，此处回填延迟光照通道需要采样的 G-Buffer 套件
	if (!LightingDrawCalls.IsEmpty()) {
		LightingDrawCalls[0].userData = CurrentGBuffer;
	}

	// 三个通道共用同一帧号：阴影通道的 DrawCall 其实际 shader 与材质父 shader 不一致
	// （dc.shader 为 Shader.Builtin.Shadow），后端 ExecuteDrawCalls 对这类 DrawCall 会跳过
	// 实例资源绑定、也不刷新材质帧号，因此不会再出现"后执行通道因材质本帧已应用被整体跳过"
	// 的问题，无需再为阴影通道单独分配帧号。
	const uint64_t SharedFrameNumber = FrameNumber;

	// 阶段三：第 0 通道 —— 阴影深度通道（先于 G-Buffer 通道执行，产出阴影贴图）
	// 阴影通道的附件 source=View，但每个渲染目标绑定的是各自那份阴影贴图（见 RegenerateAttachmentTarget），
	// 因此这里用与附件绑定完全相同的取模下标选 target，保证「本帧写入的贴图」与「本帧延迟光照采样的贴图」一致。
	IRenderpass* ShadowPass = (IRenderpass*)&Passes[0];
	RenderTarget* ShadowTarget = AcquirePassTarget(ShadowPass, (uint8_t)BufferIndex, "Shadow");
	if (ShadowTarget == nullptr) {
		GLOG(Log::eError, "Shadow pass has no valid render target, shadow pass skipped.");
		return;
	}

	FFrameData ShadowData;
	ShadowData.time = 0.0f;
	ShadowData.lightSpaceMatrix = LightSpaceMatrix;

	// 阴影贴图固定为 SHADOW_MAP_SIZE²，但后端的动态视口/裁剪是在每帧 BeginFrame 时按交换链
	// （窗口）尺寸设置的，RenderPass::Begin 只用 RenderArea 限制 vkCmdBeginRenderPass 的渲染区域，
	// 不会改写动态视口。若不覆盖，光栅化只会落在贴图左下角一块窗口大小的范围内、且 v 方向被
	// 负高度视口翻转，与延迟光照采样端 proj.xy*0.5+0.5 的贴图 uv 约定不一致，阴影随即错位/丢失。
	// 这里显式覆盖为整张阴影贴图，并使用正高度视口：使光空间 NDC 到贴图行号的映射与采样端一致。
	Renderer->SetViewport(Vector4(0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE));
	Renderer->SetScissor(Vector4(0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE));

	ShadowPass->Begin(ShadowTarget);
	Renderer->ExecuteDrawCalls(ShadowDrawCalls, SharedFrameNumber, ShadowData);
	ShadowPass->End();

	// 还原后端默认视口/裁剪，避免影响后续 G-Buffer 与延迟光照通道
	Renderer->ResetViewport();
	Renderer->ResetScissor();

	// 阶段四：第 1 通道 —— G-BUFFER 通道绘制（直接呼叫后端执行）
	IRenderpass* GBufferPass = (IRenderpass*)&Passes[1];

	// 绑定 Pass 全局不变量
	FFrameData GBufferData;
	GBufferData.projection = ProjectionMatrix;
	GBufferData.view = WorldCamera->GetViewMatrix();
	GBufferData.cameraPosition = WorldCamera->GetActorLocation();
	GBufferData.renderMode = render_mode;
	GBufferData.time = 0.0f;
	GBufferData.lightSpaceMatrix = LightSpaceMatrix;
	// 环境光同样来自光照 Actor（GBuffer 着色器按 ambient_color 语义接收）
	GBufferData.ambieantColor = AmbientColor;

	// G-Buffer 通道的颜色与深度附件均为 source=View、按渲染目标索引绑定到对应的 G-Buffer 套件，
	// 因此这里使用与 CurrentGBuffer 相同的取模下标，确保"写入的套件"就是"光照要采样的套件"
	RenderTarget* GBufferTarget = AcquirePassTarget(GBufferPass, (uint8_t)BufferIndex, "GBuffer");
	if (GBufferTarget == nullptr) {
		GLOG(Log::eError, "G-Buffer pass has no valid render target, G-Buffer pass skipped.");
		return;
	}

	GBufferPass->Begin(GBufferTarget);
	Renderer->ExecuteDrawCalls(GBufferDrawCalls, SharedFrameNumber, GBufferData);
	GBufferPass->End();

	// 阶段五：第 2 通道 —— 延迟光照通道绘制（直接呼叫后端执行，采样阴影贴图）
	IRenderpass* LightingPass = (IRenderpass*)&Passes[2];

	// 绑定 Pass 全局不变量
	FFrameData LightingData;
	LightingData.time = 0.0f;
	LightingData.gBuffer = CurrentGBuffer;
	LightingData.lightSpaceMatrix = LightSpaceMatrix;
	// 采样本帧阴影通道写入的那份贴图（与 RenderTarget 附件绑定的取模下标同源）
	LightingData.shadowMap = &ShadowMapTextureMaps[BufferIndex];

	// 方向光参数：全部来自光照 Actor，经全局 Uniform 上传 GPU，
	// 着色器不再使用任何硬编码的光照方向/颜色/强度/阴影偏置常量
	LightingData.ambieantColor = AmbientColor;
	LightingData.lightDirection = Vector4(LightDirection.x, LightDirection.y, LightDirection.z, 0.0f);
	LightingData.lightColor = LightColor;
	LightingData.lightIntensity = LightIntensity;
	LightingData.shadowBias = ShadowBias;
	LightingData.shadowStrength = ShadowStrength;

	// 延迟光照通道输出到 source=Default 的窗口颜色附件，必须使用 RTIndex 与 swapchain 图像对齐
	RenderTarget* LightingTarget = AcquirePassTarget(LightingPass, RTIndex, "DeferredLighting");
	if (LightingTarget == nullptr) {
		GLOG(Log::eError, "Deferred lighting pass has no valid render target, lighting pass skipped.");
		return;
	}

	LightingPass->Begin(LightingTarget);
	Renderer->ExecuteDrawCalls(LightingDrawCalls, SharedFrameNumber, LightingData);
	LightingPass->End();
}

bool RenderViewWorldDeferred::CreateGBufferTextures(uint32_t width, uint32_t height) {
	TextureSystem& TextureSystemInst = TextureSystem::Get();
	for (uint32_t bufferIndex = 0; bufferIndex < MAX_RENDER_TARGETS; ++bufferIndex) {
		// 创建反照率+金属度纹理 (RGBA8)
		FString ALbedoTextureName("GBuffer_Albedo_%d", bufferIndex);
		GBuffers[bufferIndex].AlbedoTexture = TextureSystemInst.AcquireWriteable(ALbedoTextureName.CStr(), width, height, 4, false);
		GBuffers[bufferIndex].AlbedoTextureMap.texture = GBuffers[bufferIndex].AlbedoTexture;
		Renderer->AcquireTextureMap(&GBuffers[bufferIndex].AlbedoTextureMap);

		// 创建法线+粗糙度纹理 (RGBA8)
		FString NormalTextureName("GBuffer_Normal_%d", bufferIndex);
		GBuffers[bufferIndex].NormalTexture = TextureSystemInst.AcquireWriteable(NormalTextureName.CStr(), width, height, 4, false);
		GBuffers[bufferIndex].NormalTextureMap.texture = GBuffers[bufferIndex].NormalTexture;
		Renderer->AcquireTextureMap(&GBuffers[bufferIndex].NormalTextureMap);

		// 创建位置纹理 (RGBA8)
		FString PositionTextureName("GBuffer_Position_%d", bufferIndex);
		GBuffers[bufferIndex].PositionTexture = TextureSystemInst.AcquireWriteable(PositionTextureName.CStr(), width, height, 4, false);
		GBuffers[bufferIndex].PositionTextureMap.texture = GBuffers[bufferIndex].PositionTexture;
		Renderer->AcquireTextureMap(&GBuffers[bufferIndex].PositionTextureMap);

		// 创建深度纹理（必须显式传 has_depth=true：否则会按 1 通道颜色格式创建，
		// 与它作为深度附件时的深度格式不匹配，深度写入与深度测试行为未定义）
		FString DepthTextureName("GBuffer_Depth_%d", bufferIndex);
		GBuffers[bufferIndex].DepthTexture = TextureSystemInst.AcquireWriteable(DepthTextureName.CStr(), width, height, 1, false, true);
		GBuffers[bufferIndex].DepthTextureMap.texture = GBuffers[bufferIndex].DepthTexture;
		Renderer->AcquireTextureMap(&GBuffers[bufferIndex].DepthTextureMap);
	}

	return true;
}

void RenderViewWorldDeferred::DestroyGBufferTextures() {
	// 销毁G-Buffer纹理
	for (uint32_t bufferIndex = 0; bufferIndex < MAX_RENDER_TARGETS; ++bufferIndex) {
		if (GBuffers[bufferIndex].AlbedoTexture) GBuffers[bufferIndex].AlbedoTexture->Destroy();
		if (GBuffers[bufferIndex].NormalTexture) GBuffers[bufferIndex].NormalTexture->Destroy();
		if (GBuffers[bufferIndex].PositionTexture) GBuffers[bufferIndex].PositionTexture->Destroy();
		if (GBuffers[bufferIndex].DepthTexture) GBuffers[bufferIndex].DepthTexture->Destroy();

		Renderer->ReleaseTextureMap(&GBuffers[bufferIndex].AlbedoTextureMap);
		Renderer->ReleaseTextureMap(&GBuffers[bufferIndex].NormalTextureMap);
		Renderer->ReleaseTextureMap(&GBuffers[bufferIndex].PositionTextureMap);
		Renderer->ReleaseTextureMap(&GBuffers[bufferIndex].DepthTextureMap);
	}
}

bool RenderViewWorldDeferred::CreateShadowResources() {
	TextureSystem& TextureSystemInst = TextureSystem::Get();

	// 阴影贴图：每个渲染目标各一份 SHADOW_MAP_SIZE x SHADOW_MAP_SIZE 深度语义纹理
	// （分辨率固定，不随窗口变化）。参数参照 CreateGBufferTextures 的深度纹理写法
	// （1 通道、无透明），并显式打开深度标志。
	// 每个渲染目标各持一份的原因：MaxFramesInFlight > 1 时多个帧同时在飞，单张贴图会被后续帧的
	// 阴影通道覆写，而前一帧的延迟光照通道可能仍在采样它，造成帧间竞争与阴影闪烁。
	for (uint32_t bufferIndex = 0; bufferIndex < MAX_RENDER_TARGETS; ++bufferIndex) {
		FString ShadowMapName("ShadowMap_%d", bufferIndex);
		ShadowMapTextures[bufferIndex] = TextureSystemInst.AcquireWriteable(ShadowMapName.CStr(), SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, false, true);
		if (ShadowMapTextures[bufferIndex] == nullptr) {
			GLOG(Log::eError, "CreateShadowResources: Failed to acquire shadow map texture %d.", bufferIndex);
			DestroyShadowResources();
			return false;
		}

		ShadowMapTextureMaps[bufferIndex].texture = ShadowMapTextures[bufferIndex];
		if (!Renderer->AcquireTextureMap(&ShadowMapTextureMaps[bufferIndex])) {
			// 回滚：注册 TextureMap 失败时销毁刚创建的纹理并清空两侧指针，不残留悬空引用
			GLOG(Log::eError, "CreateShadowResources: Failed to acquire shadow map texture map %d.", bufferIndex);
			ShadowMapTextures[bufferIndex]->Destroy();
			ShadowMapTextures[bufferIndex] = nullptr;
			ShadowMapTextureMaps[bufferIndex].texture = nullptr;
			DestroyShadowResources();
			return false;
		}
	}

	// 阴影贴图分辨率固定、不随窗口变化，光空间矩阵也仅与光源参数有关，创建时计算一次即可
	UpdateLightSpaceMatrix();
	return true;
}

void RenderViewWorldDeferred::DestroyShadowResources() {
	// 幂等：可安全重复调用，空指针安全。
	// 纹理与 TextureMap 都在释放后置空，二次调用既不会重复销毁纹理，
	// 也不会对从未成功注册（或已释放）的 TextureMap 重复释放采样器。
	for (uint32_t bufferIndex = 0; bufferIndex < MAX_RENDER_TARGETS; ++bufferIndex) {
		if (ShadowMapTextures[bufferIndex]) {
			ShadowMapTextures[bufferIndex]->Destroy();
			ShadowMapTextures[bufferIndex] = nullptr;
		}

		// 仅当 TextureMap 仍绑定纹理（即已成功 AcquireTextureMap）时才释放，避免空注册释放
		if (Renderer && ShadowMapTextureMaps[bufferIndex].texture) {
			Renderer->ReleaseTextureMap(&ShadowMapTextureMaps[bufferIndex]);
		}
		ShadowMapTextureMaps[bufferIndex].texture = nullptr;
	}
}

void RenderViewWorldDeferred::UpdateLightSpaceMatrix() {
	// 光照参数唯一来源：场景中的方向光 Actor（UDirectionalLightComponent）。
	// 未放置光源 Actor 时使用与组件一致的默认值，保证渲染结果与改造前相同。
	Vector3 Direction = Vector3(-0.57735f, -0.57735f, -0.57735f).Normalized();
	Vector4 Color = Vector4(0.8f, 0.8f, 0.8f, 1.0f);
	float Intensity = 1.0f;
	Vector4 Ambient = Vector4(0.18f, 0.18f, 0.18f, 1.0f);
	float Bias = 0.005f;
	float Strength = 1.0f;
	float Distance = 40.0f;
	float HalfExtent = 20.0f;
	float OrthoNear = 0.1f;
	float OrthoFar = 100.0f;

	UDirectionalLightComponent* MainLight = LightSystem::Get().GetMainLight();
	if (MainLight) {
		Direction = MainLight->GetDirection();
		Color = MainLight->GetColor();
		Intensity = MainLight->GetIntensity();
		Ambient = MainLight->GetAmbientColor();
		Bias = MainLight->GetShadowBias();
		Strength = MainLight->GetShadowStrength();
		Distance = MainLight->GetShadowDistance();
		HalfExtent = MainLight->GetShadowOrthoExtent();
		OrthoNear = MainLight->GetShadowOrthoNear();
		OrthoFar = MainLight->GetShadowOrthoFar();
	}

	// 逐帧刷新供 GPU 上传的帧数据（Render() 中写入 FFrameData）
	LightDirection = Direction;
	LightColor = Color;
	LightIntensity = Vector4(Intensity, Intensity, Intensity, Intensity);
	AmbientColor = Ambient;
	ShadowBias = Bias;
	ShadowStrength = Strength;

	// 光源位置：沿光照方向的反方向拉远，保证正交相机能覆盖场景
	Vector3 LightPosition = -LightDirection * Distance;

	// 光空间矩阵 = 正交投影 * 光源视图矩阵
	// 最后一个参数 zero_to_one = true：阴影贴图是直接与光栅化深度比较的渲染目标，
	// 必须让近/远平面映射到深度 0/1（Vulkan 裁剪体约定），否则场景整体落在裁剪体之外。
	Matrix4 LightProjection = Matrix4::Orthographic(
		-HalfExtent, HalfExtent,
		-HalfExtent, HalfExtent,
		OrthoNear, OrthoFar, false, true);
	Matrix4 LightView = Matrix4::LookAt(LightPosition, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
	LightSpaceMatrix = LightProjection * LightView;
}

bool RenderViewWorldDeferred::CreateFullscreenQuad() {
	// 创建全屏四边形几何体
	FullscreenQuad = GeometrySystem::Get().GenerateQuad("DFFullScreenQuad", "Material.Builtin.DeferredLighting");
	return FullscreenQuad != nullptr;
}

void RenderViewWorldDeferred::DestroyFullscreenQuad() {
	if (FullscreenQuad) {
		DeleteObject(FullscreenQuad);
	}
}
