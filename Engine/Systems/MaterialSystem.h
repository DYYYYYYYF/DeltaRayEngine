#pragma once

#include "Defines.hpp"
#include "Containers/FString.hpp"
#include "Rendering/Resources/ResourceTypes.hpp"
#include <unordered_map>

class IRenderer;

struct SMaterialSystemConfig {
	uint32_t max_material_count = 512;
};

class MaterialSystem {
public:
	static MaterialSystem& Get();

public:
	bool Initialize(IRenderer* renderer, SMaterialSystemConfig config);
	void Shutdown();

	UMaterial* Acquire(const FString& name);
	UMaterial* AcquireFromConfig(FMaterialConfig config);

	bool LoadMaterial(FMaterialConfig config, UMaterial* mat);
	void DestroyMaterial(UMaterial* mat);

	/**
	 * @brief Applies global-level data for the material shader id.
	 *
	 * @param shader_id The identifier of the shader to apply globals for.
	 * @param projection A constant pointer to a projection matrix.
	 * @param view A constant pointer to a view matrix.
	 * @return True on success; otherwise false.
	 */
	bool ApplyGlobal(uint32_t shader_id, size_t renderer_frame_number, const FFrameData& data);

	/**
	 * @brief Applies instance-level material data for the given material.
	 *
	 * @param mat A pointer to the material to be applied.
	 * @param need_update Indicates if the material needs to be update.
	 * @return True on success; otherwise false.
	 */
	bool ApplyInstance(UMaterialInstance* mat, const FFrameData& data);

	/**
	 * @brief Applies local-level material data (typically just model matrix).
	 *
	 * @param m A pointer to the material to be applied.
	 * @param model A constant pointer to the model matrix to be applied.
	 * @return True on success; otherwise false.
	 */
	bool ApplyLocal(UMaterialInstance* mat, const Matrix4& model);

	/**
	 * @brief Applies local-level material data with an explicitly specified shader.
	 *
	 * 当 DrawCall 实际使用的 shader 与材质父材质的 shader 不一致时（例如阴影通道复用几何体
	 * 材质实例、仅把 shader 换成阴影着色器），必须按实际 shader 取 local uniform 列表，
	 * 否则会把材质父 shader 的 local 语义（push constant）写错。
	 *
	 * @param shader The shader actually used by the draw call.
	 * @param model A constant pointer to the model matrix to be applied.
	 * @return True on success; otherwise false.
	 */
	bool ApplyLocal(UShader* shader, const Matrix4& model);

private:
	bool CreateTextureMap(FTextureMap& map, TextureUsage usage, const FString& textureName);

	TextureUsage GetTextureUsageFromUniformName(const FString& name) const;

public:
	SMaterialSystemConfig MaterialSystemConfig;

	// Array of registered materials.
	std::vector<UMaterial*> RegisteredMaterials;
	// Hashtable for material lookups.
	std::unordered_map<FString, uint32_t> MaterialMap;

	// Know locations for the material shader.
	FMaterialShaderUniformLocations MaterialLocations;
	uint32_t MaterialShaderID = INVALID_ID;

	// Know locations for the deferred lighting material shader.
	uint32_t DeferredLightMaterialShaderID = INVALID_ID;

	// Know locations for the ui shader.
	uint32_t UIShaderID = INVALID_ID;

	bool Initilized = false;

	IRenderer* Renderer = nullptr;

};