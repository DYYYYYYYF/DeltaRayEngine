#pragma once

#include "Defines.hpp"
#include "Math/MathTypes.hpp"
#include "Containers/TArray.hpp"
#include "Resources/Shader/Shader.hpp"
#include "Rendering/Interface/IGPUBuffer.hpp"
#include "Framework/Classes/Actor.h"

#include <vector>
#include <functional>

struct RenderpassConfig;
class Mesh;
class USkybox;
class IRenderer;
class IRenderpass;

enum RendererBackendType {
	eRenderer_Backend_Type_Vulkan,
	eRenderer_Backend_Type_OpenGL,
	eRenderer_Backend_Type_DirecX
};

enum RenderTargetAttachmentType {
	eRender_Target_Attachment_Type_Color = 0x01,
	eRender_Target_Attachment_Type_Depth = 0x02,
	eRender_Target_Attachment_Type_Stencil = 0x04
};

enum RenderTargetAttachmentSource {
	eRender_Target_Attachment_Source_Default = 0x1,
	eRender_Target_Attachment_Source_View = 0x2
};

enum RenderTargetAttachmentLoadOperation {
	eRender_Target_Attachment_Load_Operation_DontCare = 0x0,
	eRender_Target_Attachment_Load_Operation_Load = 0x1,
	eRender_Target_Attachment_Load_Operation_Clear = 0x2
};

enum RenderTargetAttachmentStoreOperation {
	eRender_Target_Attachment_Store_Operation_DontCare = 0x0,
	eRender_Target_Attachment_Store_Operation_Store = 0x1,
};

struct RenderTargetAttachmentConfig {
	uint32_t index = 0;
	RenderTargetAttachmentType type = RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color;
	RenderTargetAttachmentSource source = RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default;
	RenderTargetAttachmentLoadOperation loadOperation = RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load;
	RenderTargetAttachmentStoreOperation storeOperation = RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store;
	// 如果当前Attachment是需要渲染到屏幕的则为true
	bool presentAfter = true;
};

struct RenderTargetConfig {
	TArray<RenderTargetAttachmentConfig> attachments;
};

struct RenderTargetAttachment {
	uint32_t index = 0;	// 用于区分多个颜色附件的索引
	RenderTargetAttachmentType type = RenderTargetAttachmentType::eRender_Target_Attachment_Type_Color;
	RenderTargetAttachmentSource source = RenderTargetAttachmentSource::eRender_Target_Attachment_Source_Default;
	RenderTargetAttachmentLoadOperation loadOperation = RenderTargetAttachmentLoadOperation::eRender_Target_Attachment_Load_Operation_Load;
	RenderTargetAttachmentStoreOperation storeOperation = RenderTargetAttachmentStoreOperation::eRender_Target_Attachment_Store_Operation_Store;
	bool presentAfter = true;
	class UTexture* texture = nullptr;
};

struct GeometryRenderData {
	Matrix4 model_mat;
	class UGeometry* geometry = nullptr;
	uint64_t uniqueID = INVALID_ID;
	uint32_t InstanceIndex = 0;		// 用来存Shader
};

struct SRenderViewPassConfig {
	const char* name = nullptr;
};

struct SRenderPacket {
	double delta_time = 0.0;
	unsigned short view_count = 0;
	TArray<struct RenderViewPacket> views;
};

struct RenderTarget {
	bool sync_to_window_size = true;
	TArray<struct RenderTargetAttachment> attachments;
	void* internal_framebuffer = nullptr;
};

// DrawCall
enum class ERenderQueueType {
	Opaque,
	Transparent,
	UI,
	Skybox
};

class UGeometry;
class UMaterialInstance;

// G-Buffer纹理
#include "Resources/Texture/TextureType.hpp"
struct GBufferSet {
	UTexture* AlbedoTexture = nullptr;
	UTexture* NormalTexture = nullptr;
	UTexture* PositionTexture = nullptr;
	UTexture* DepthTexture = nullptr;

	FTextureMap AlbedoTextureMap;
	FTextureMap NormalTextureMap;
	FTextureMap PositionTextureMap;
	FTextureMap DepthTextureMap;
};

struct DrawCall {
	UGeometry* geometry;
	UMaterialInstance* material;
	Matrix4 model;
	UShader* shader;
	uint32_t sortKey;
	void* userData;

	bool indexed;
};

// TODO: 替换成DrawCall
struct IRenderviewPacketData {};
struct WorldPacketData : public IRenderviewPacketData {
public:
	WorldPacketData() {}
	WorldPacketData(const WorldPacketData& data) {
		Meshes = data.Meshes;
	}

	TArray<GeometryRenderData> Meshes;
	float GlobalTime;
};

struct MeshPacketData : public IRenderviewPacketData {
	uint32_t mesh_count = 0;
	AActor** meshes = nullptr;
};

struct UIPacketData : public IRenderviewPacketData {
public:
	UIPacketData() {}
	UIPacketData(const UIPacketData& data) {
		meshData = data.meshData;
		textCount = data.textCount;
		Textes = data.Textes;
	}

	MeshPacketData meshData;
	// TODO: temp
	uint32_t textCount = 0;
	class ATextActor** Textes = nullptr;
};

struct PickPacketData : public IRenderviewPacketData {
public:
	PickPacketData() {}
	PickPacketData(const PickPacketData& data) {
		WorldMeshData = data.WorldMeshData;
		UIMeshData = data.UIMeshData;
		UIGeometryCount = data.UIGeometryCount;
		TextCount = data.TextCount;
		Texts = data.Texts;
	}

	TArray<GeometryRenderData> WorldMeshData;
	MeshPacketData UIMeshData;
	uint32_t UIGeometryCount = 0;
	// TODO: Temp.
	uint32_t TextCount = 0;
	class ATextActor** Texts = nullptr;
};

struct SkyboxPacketData : public IRenderviewPacketData {
public:
	SkyboxPacketData() {}
	SkyboxPacketData(const SkyboxPacketData& data) {
		sb = data.sb;
	}

	USkybox* sb = nullptr;
};

struct RenderBackendConfig {
	std::string application_name;
};
