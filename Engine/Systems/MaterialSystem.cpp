#include "MaterialSystem.h"

#include "Core/EngineLogger.hpp"
#include "Math/MathTypes.hpp"
#include "Rendering/Renderer.hpp"

#include "Systems/TextureSystem.h"
#include "Systems/ResourceSystem.h"
#include "Systems/ShaderSystem.h"

MaterialSystem& MaterialSystem::Get() {
	static MaterialSystem MaterialSystemInstance;
	return MaterialSystemInstance;
}

bool MaterialSystem::Initialize(IRenderer* renderer, SMaterialSystemConfig config) {
	if (config.max_material_count == 0) {
		GLOG(Log::eFatal, "Material system init failed. TextureSystemConfig.max_texture_count should > 0");
		return false;
	}

	if (renderer == nullptr) {
		GLOG(Log::eFatal, "Material system init failed. Renderer is nullptr.");
		return false;
	}

	if (Initilized) {
		return true;
	}

	MaterialSystemConfig = config;
	Renderer = renderer;

	// Invalidate all textures in the array.
	uint32_t Count = MaterialSystemConfig.max_material_count;
	RegisteredMaterials.resize(Count);

	Initilized = true;
	return true;
}

void MaterialSystem::Shutdown() {
	// Destroy all loaded textures.
	for (UMaterial* m : RegisteredMaterials) {
		if (m) {
			DestroyMaterial(m);
			m = nullptr;
		}
	};
	RegisteredMaterials.clear();
	MaterialMap.clear();
}

UMaterial* MaterialSystem::Acquire(const FString& name) {
	// Load the given material configuration from disk.
	UAsset MatResource(name);
	if (!ResourceSystem::Get().Load(name, EAssetType::Material, nullptr, &MatResource)) {
		GLOG(Log::eError, "Failed to load material resource, returning nullptr.");
		return nullptr;
	}

	// Now acquire from loaded config.
	UMaterial* Mat = nullptr;
	if (MatResource.Data) {
		Mat = AcquireFromConfig(*(FMaterialConfig*)MatResource.Data);
	}

	// Clean up
	ResourceSystem::Get().Unload(&MatResource);

	if (!Mat) {
		GLOG(Log::eError, "Failed to load material resource, returning nullptr.");
		return nullptr;
	}

	return Mat;
}

UMaterial* MaterialSystem::AcquireFromConfig(FMaterialConfig config) {
	// 如果找不到材质，则创建一个新的材质。
	if (MaterialMap.find(config.name) == MaterialMap.end()) {
		uint32_t Count = MaterialSystemConfig.max_material_count;
		UMaterial* m = nullptr;
		for (uint32_t i = 0; i < Count; ++i) {
			if (RegisteredMaterials[i] == nullptr) {
				// A free slot has been found. Use it index as the handle.
				RegisteredMaterials[i] = NewObject<UMaterial>(config.name);
				RegisteredMaterials[i]->SetInternalID(i);
				MaterialMap[config.name] = i;
				m = RegisteredMaterials[i];
				break;
			}
		}

		// Make sure an empty slot was actually found.
		if (m == nullptr || m->GetInternalID() == INVALID_ID) {
			GLOG(Log::eFatal, "Material acquire failed. Material system cannot hold anymore materials. Adjust configuration to allow more.");
			return nullptr;
		}

		// Create new material.
		if (!LoadMaterial(config, m)) {
			GLOG(Log::eError, "Load %s material failed.", config.name.CStr());
			return nullptr;
		}

		if (m->Generation == INVALID_ID) {
			m->Generation = 1;
		}
		else {
			m->Generation++;
		}
	}

	uint32_t MaterialID = MaterialMap[config.name];
	UMaterial* Mat = nullptr;
	if (MaterialID != INVALID_ID) {
		Mat = RegisteredMaterials[MaterialID];
	}
	ASSERT(Mat != nullptr);


	// TODO:这里实际上是创建了一个独立的DescriptorSet，后续需要移动到Material中，不应该存在shader中。
	std::vector<FTextureMap*> Maps;
	for (TextureBinding& TexBinding : Mat->TextureBindings) {
		Maps.push_back(&(TexBinding.texture));
	}

	UShader* s = ShaderSystem::Get().Get(config.shader_name);
	Mat->InternalID = Renderer->AcquireInstanceResource(s, Maps);
	if (Mat->InternalID == INVALID_ID) {
		GLOG(Log::eError, "Failed to acquire renderer resources for material '%s'.", Mat->Name.CStr());
		return nullptr;
	}


	// This can only be changed the first time a material is loaded.
	if (Mat->GetReferenceCount() == 0) {
		Mat->SetIsAutoRelease(config.auto_release);
	}

	GLOG(Log::eDebug, "Material '%s' Reference count increased to %i.", config.name.CStr(), Mat->GetReferenceCount());

	// Update the entry.
	return Mat;
}

bool MaterialSystem::LoadMaterial(FMaterialConfig config, UMaterial* mat) {
	// name
	mat->Name = config.name;
	mat->ShaderID = ShaderSystem::Get().GetID(config.shader_name);
	UShader* s = ShaderSystem::Get().GetByID(mat->ShaderID);
	if (!s)
	{
		GLOG(Log::eError, "Unable to load material because its shader was not found: '%s'. This is likely a problem with the material asset.", config.shader_name.CStr());
		return false;
	}

	// 由Shader反射具体Property后在config中查找
	const std::vector<ShaderUniform>& Uniforms = s->GetUniformList();
	for (const ShaderUniform& uniform: Uniforms) {
		switch (uniform.scope)
		{
		case eShader_Scope_Global:
		case eShader_Scope_Local:
			continue;

		default:
			break;
		}

		// 查找Config
		auto property = config.Properties.Find(uniform.name);

		// 解析字符数据
		switch (uniform.type)
		{
		case eShader_Uniform_Type_Float:
		{
			float value = property ? property->ToFloat() : 0.0f;

			UniformValue MatValue;
			MatValue.uniform = s->GetUniformHandle(uniform.name);

			// 清零目标区域（保证未使用的字节干净）
			std::memset(MatValue.data, 0, 64);
			std::memcpy(MatValue.data, &value, sizeof(value));
			mat->UniformValues.Push(std::move(MatValue));
		}
		break;

		case eShader_Uniform_Type_Float_4:
		{
			Vector4 vec = property ? Vector4::FromString((*property).CStr()) : Vector4();

			UniformValue MatValue;
			MatValue.uniform = s->GetUniformHandle(uniform.name);

			// 清零目标区域（保证未使用的字节干净）
			std::memset(MatValue.data, 0, 64);
			std::memcpy(MatValue.data, &vec, sizeof(vec));
			mat->UniformValues.Push(std::move(MatValue));
		}
		break;

		case eShader_Uniform_Type_UInt32:
		{
			Vector4 vec = property? Vector4::FromString((*property).CStr()) : Vector4();

			UniformValue MatValue;
			MatValue.uniform = s->GetUniformHandle(uniform.name);

			// 清零目标区域（保证未使用的字节干净）
			std::memset(MatValue.data, 0, 64);
			std::memcpy(MatValue.data, &vec, sizeof(vec));
			mat->UniformValues.Push(std::move(MatValue));
		}
		break;

		case eShader_Uniform_Type_Sampler:
		{
			TextureBinding texValue;
			texValue.uniform = s->GetUniformHandle(uniform.name);

			// 如果材质有指定贴图则使用,如果没有内部指定Default
			TextureUsage usage = GetTextureUsageFromUniformName(uniform.name);
			if (property) CreateTextureMap(texValue.texture, usage, *property);
			else CreateTextureMap(texValue.texture, usage, FString());

			mat->TextureBindings.Push(std::move(texValue));
		}
		break;
		}
	}

	return true;
}

void MaterialSystem::DestroyMaterial(UMaterial* mat) {
	GLOG(Log::eInfo, "Destroying material '%s'...", mat->Name.CStr());

	// Release texture references.
	TextureSystem& TextureSystemInst = TextureSystem::Get();
	for (TextureBinding& TexBinding : mat->TextureBindings) {
		if (!TexBinding.texture.texture) {
			TextureSystemInst.Release(TexBinding.texture.texture->GetName());
		}
		Renderer->ReleaseTextureMap(&TexBinding.texture);
	}

	//Release renderer resources.
	if (mat->ShaderID != INVALID_ID && mat->InternalID != INVALID_ID) {
		UShader* s = ShaderSystem::Get().GetByID(mat->ShaderID);
		Renderer->ReleaseInstanceResource(s, mat->InternalID);
		mat->ShaderID = INVALID_ID;
	}

	// Remove from the registered materials list.
	if (MaterialMap.find(mat->Name) != MaterialMap.end())
	{
		uint32_t MaterialID = MaterialMap[mat->Name];
		RegisteredMaterials[MaterialID] = nullptr;
		GLOG(Log::eInfo, "Released material '%s'. Material unloaded.", mat->Name.CStr());

		// Update the entry.
		MaterialMap.erase(mat->Name);
	}

	// Delete the material object.
	DeleteObject(mat);
}

bool MaterialSystem::CreateTextureMap(FTextureMap& map, TextureUsage usage, const FString& textureName) {
	map.usage = usage;

	TextureSystem& texSys = TextureSystem::Get();
	if (!textureName.IsEmpty())
	{
		if (usage != TextureUsage::eTexture_Usage_Map_Cubemap) map.texture = texSys.Acquire(textureName, true); 
		else map.texture = texSys.AcquireCube(textureName, true);
	}

	if (!map.texture)
	{
		switch (usage)
		{
		case TextureUsage::eTexture_Usage_Map_Diffuse:
			map.texture = texSys.GetDefaultDiffuseTexture();
			break;

		case TextureUsage::eTexture_Usage_Map_Normal:
			map.texture = texSys.GetDefaultNormalTexture();
			break;

		case TextureUsage::eTexture_Usage_Map_RoughnessMetallic:
			map.texture = texSys.GetDefaultRoughnessMetallicTexture();
			break;
		case TextureUsage::eTexture_Usage_Map_Cubemap:
		{
			// Cubemap需要特殊处理
			map.filter_magnify = TextureFilter::eTexture_Filter_Mode_Linear;
			map.filter_minify = TextureFilter::eTexture_Filter_Mode_Linear;
			map.repeat_u = TextureRepeat::eTexture_Repeat_Clamp_To_Edge;
			map.repeat_v = TextureRepeat::eTexture_Repeat_Clamp_To_Edge;
			map.repeat_w = TextureRepeat::eTexture_Repeat_Clamp_To_Edge;
			map.usage = TextureUsage::eTexture_Usage_Map_Cubemap;
		} break;
		}
	}

	// 创建Sampler
	if (!Renderer->AcquireTextureMap(&map)) {
		return false;
	}

	return true;
}

TextureUsage MaterialSystem::GetTextureUsageFromUniformName(const FString& name) const {
	if (name.Compare("diffuse_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Diffuse;
	}
	else if (name.Compare("normal_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Normal;
	}
	else if (name.Compare("roughnessMetillc_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_RoughnessMetallic;
	}
	else if (name.Compare("cubemap_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Cubemap;
	}
	else if (name.Compare("specular_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Specular;
	}
	else if (name.Compare("skybox_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Cubemap;
	}
	else if (name.Compare("shadow_map_texture") == 0) {
		return TextureUsage::eTexture_Usage_Map_Shadow;
	}

	return TextureUsage::eTexture_Usage_Unknown;
}

#ifdef LEVEL_DEBUG
#define MATERIAL_APPLY_OR_FAIL(expr)                  \
    if (!expr) {                                      \
        GLOG(Log::eError, "Failed to apply material: %s", #expr); \
        return false;                                 \
    }
#else
#define MATERIAL_APPLY_OR_FAIL(expr) expr
#endif

bool MaterialSystem::ApplyGlobal(uint32_t shader_id, size_t renderer_frame_number, const FFrameData& data) {

	UShader* UsedShader = ShaderSystem::Get().GetByID(shader_id);
	if (UsedShader == nullptr) {
		return false;
	}

	if (UsedShader->RenderFrameNumber == renderer_frame_number) {
		return true;
	}

	const std::vector<ShaderUniform>& uniforms = UsedShader->GetUniformList();
	for (const ShaderUniform& uniform : uniforms) {
		if (uniform.scope != eShader_Scope_Global) continue;

		switch (uniform.semantic)
		{
		case ShaderSemantic::eShaderSemantic_Projection:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.projection));
			break;

		case ShaderSemantic::eShaderSemantic_View:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.view));
			break;

		case ShaderSemantic::eShaderSemantic_ViewPosition:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.cameraPosition));
			break;

		case ShaderSemantic::eShaderSemantic_AmbientColor:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.ambieantColor));
			break;

		case ShaderSemantic::eShaderSemantic_Time:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.time));
			break;

		case ShaderSemantic::eShaderSemantic_RenderMode:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.renderMode));
			break;

		case ShaderSemantic::eShaderSemantic_LightSpaceMatrix:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(&uniform, &data.lightSpaceMatrix));
			break;

		default:
			break;
		}
	}

	MATERIAL_APPLY_OR_FAIL(UsedShader->ApplyGlobal());

	// Sync
	UsedShader->RenderFrameNumber = renderer_frame_number;

	return true;
}

bool MaterialSystem::ApplyInstance(UMaterialInstance* mat, const FFrameData& data) {
	if (!mat || mat->InternalID == INVALID_ID) {
		return false;
	}

	UMaterial* OriginMaterial = mat->GetParentMaterial();
	if (!OriginMaterial) {
		return false;
	}

	UShader* UsedShader = ShaderSystem::Get().GetByID(OriginMaterial->ShaderID);
	if (!UsedShader) {
		return false;
	}

	// Apply instance-level uniforms.
	MATERIAL_APPLY_OR_FAIL(UsedShader->BindInstance(mat->InternalID));
	for (const auto& value : mat->UniformValues)
	{
		MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(value.uniform, value.data));
	}

	for (const auto& tex : mat->TextureBindings)
	{
		switch (tex.uniform->semantic)
		{
		case ShaderSemantic::eSemantic_GBuffer_Albedo:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &data.gBuffer->AlbedoTextureMap));
			break;

		case ShaderSemantic::eSemantic_GBuffer_Normal:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &data.gBuffer->NormalTextureMap));
			break;

		case ShaderSemantic::eSemantic_GBuffer_Position:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &data.gBuffer->PositionTextureMap));
			break;

		case ShaderSemantic::eSemantic_Diffuse_Texture:
		{
			if (tex.texture.texture) {
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
			else {
				TextureSystem::Get().GetDefaultDiffuseTexture();
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
		} break;

		case ShaderSemantic::eSemantic_Normal_Texture:
		{
			if (tex.texture.texture) {
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
			else {
				TextureSystem::Get().GetDefaultNormalTexture();
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
		} break;

		case ShaderSemantic::eSemantic_Roughness_Metallic_Texture:
		{
			if (tex.texture.texture) {
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
			else {
				TextureSystem::Get().GetDefaultRoughnessMetallicTexture();
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			}
		} break;
		case ShaderSemantic::eSemantic_Skybox_Texture:
			MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, &tex.texture));
			break;

		case ShaderSemantic::eSemantic_Shadow_Map:
		{
			// 阴影贴图由渲染流程逐帧提供；未提供时直接跳过，禁止回落到默认漫反射纹理
			if (data.shadowMap) {
				MATERIAL_APPLY_OR_FAIL(UsedShader->SetUniform(tex.uniform, data.shadowMap));
			}
		} break;

		default:
			GLOG(Log::Level::eError, "MaterialSystem::ApplyInstance() Unknow texture binding semantic.");
			break;
		}
	}

	MATERIAL_APPLY_OR_FAIL(UsedShader->ApplyInstance());
	return true;
}

bool MaterialSystem::ApplyLocal(UMaterialInstance* mat, const Matrix4& model) {
	if (!mat) {
		return false;
	}

	UMaterial* OriginMaterial = mat->GetParentMaterial();
	if (!OriginMaterial) {
		return false;
	}
	UShader* UsedShader = ShaderSystem::Get().GetByID(OriginMaterial->ShaderID);
	if (!UsedShader) {
		return false;
	}

	// 默认按材质父材质的 shader 应用 local uniform。
	return ApplyLocal(UsedShader, model);
}

bool MaterialSystem::ApplyLocal(UShader* shader, const Matrix4& model) {
	if (!shader) {
		return false;
	}

	// 使用传入shader的本地uniform列表来设置模型矩阵
	const std::vector<ShaderUniform>& uniforms = shader->GetUniformList();
	for (const ShaderUniform& uniform : uniforms)
	{
		if (uniform.scope != eShader_Scope_Local) continue;

		switch (uniform.semantic)
		{
		case ShaderSemantic::eShaderSemantic_Model_Matrix:
			return shader->SetUniform(&uniform, &model);
		}
	}

	return true;
}
