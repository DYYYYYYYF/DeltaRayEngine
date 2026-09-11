#pragma once

#include "MaterialType.hpp"
#include "Rendering/Resources/Texture/Texture.hpp"
#include "Framework/Object.h"

struct ShaderUniform;

struct UniformValue {
	ShaderUniform* uniform = nullptr;
	uint8_t data[64];
};

struct TextureBinding {
	ShaderUniform* uniform = nullptr;
	FTextureMap texture;
};

class UMaterial : public UAsset{
	friend class MaterialSystem;

public:
	UMaterial(const FString& Name);
	virtual ~UMaterial();

public:
	inline uint32_t GetInternalID() const { return InternalID; }
	inline void SetInternalID(uint32_t ID) { InternalID = ID; }

	// 父材质绑定的 shader 在 ShaderSystem 中的注册 ID（与 UShader::GetUniqueID() 同一套编号）。
	// 用于判定 DrawCall 实际使用的 shader 是否与该材质匹配。
	inline uint32_t GetShaderID() const { return ShaderID; }

	inline size_t GetReferenceCount() const { return ReferenceCount; }
	inline void IncreaseReferenceCount(uint32_t Count = 1) { ReferenceCount += Count; }
	void DecreaseReferenceCount(uint32_t Count = 1);

	inline bool IsAutoRelease() const { return AutoRelease; }
	inline void SetIsAutoRelease(bool B) { AutoRelease = B; }

	const TArray<UniformValue>& GetUniformValues() const { return UniformValues; }
	const TArray<TextureBinding>& GetTextureBindings() const { return TextureBindings; }

private:
	void DestroyInstance();

protected:
	// Base
	FString Name;
	uint32_t Generation;
	uint32_t InternalID;
	uint32_t ShaderID;

	// Parameters
	TArray<UniformValue> UniformValues;
	TArray<TextureBinding> TextureBindings;
	
private:
	size_t ReferenceCount = 0;
	bool AutoRelease = true;

};


// Material Instance
class UMaterialInstance : public UObject, public TRequireClassType<UMaterialInstance> {
	DECLARE_CLASS_TYPE(UMaterialInstance)
	friend class MaterialSystem;

public:
	explicit UMaterialInstance(UMaterial* BaseMat);
	virtual ~UMaterialInstance();

public:
	bool IsTextureBindingExist(const FString& UniformName) const;
	bool SetTextureOnBinding(const FString& UniformName, FTextureMap Texture);

public:
	UMaterial* GetParentMaterial() const { return BaseMaterial; }

	uint32_t GetInternalID() const { return InternalID; }
	void SetInternalID(uint32_t ID) { InternalID = ID; }

	void SetFrameNumber(size_t Framenumber) { RenderFrameNumer = Framenumber; }
	bool IsNeedUpdate(size_t CurrentFramenumber) const { return RenderFrameNumer != CurrentFramenumber; }

	const TArray<UniformValue>& GetUniformValues() const { return UniformValues; }
	const TArray<TextureBinding>& GetTextureBindings() const { return TextureBindings; }

	void Release();

protected:
	// Parameters
	TArray<UniformValue> UniformValues;
	TArray<TextureBinding> TextureBindings;

private:
	UMaterial* BaseMaterial;
	uint32_t InternalID = INVALID_ID;
	size_t RenderFrameNumer = 0;

};