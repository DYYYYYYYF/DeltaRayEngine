#include "Shader.hpp"
#include "Platform/File/File.hpp"
#include "Systems/ResourceSystem.h"
#include "Rendering/Renderer.hpp"
#include "../Texture/TextureType.hpp"
#include "Systems/TextureSystem.h"

TArray<uint32_t> UShader::CompileShaderToSPV(const FString& filename, enum ShaderStage shaderStage, bool writeToDisk) {
	size_t PrePathIndex = filename.IndexOf('/');
	size_t SufPathIndex = filename.LastIndexOf('.');
	FString PrePath = filename.SubStr(0, PrePathIndex);
	FString SufPath = filename.SubStr(PrePathIndex, SufPathIndex - PrePathIndex);

	FString ShaderSourceFilename;
	shaderc_source_language SourceLanguage;
	switch (Language)
	{
	case EShaderLanguage::eGLSL:
		ShaderSourceFilename = ResourceSystem::Get().GetRootPath() + FString("/../Shaders/glsl") + SufPath;
		SourceLanguage = shaderc_source_language_glsl;
		break;
	case EShaderLanguage::eHLSL:
		ShaderSourceFilename = ResourceSystem::Get().GetRootPath() + FString("/../Shaders/hlsl") + SufPath + ".hlsl";
		SourceLanguage = shaderc_source_language_hlsl;
		break;
	default:
		GLOG(Log::eError, "Unknown shader language flag.");
		return TArray<uint32_t>();
	}

	shaderc_shader_kind scShadercStage;
	switch (shaderStage)
	{
	case ShaderStage::eShader_Stage_Vertex:
		scShadercStage = shaderc_shader_kind::shaderc_vertex_shader;
		break;
	case ShaderStage::eShader_Stage_Fragment:
		scShadercStage = shaderc_shader_kind::shaderc_fragment_shader;
		break;
	case ShaderStage::eShader_Stage_Geometry:
		scShadercStage = shaderc_shader_kind::shaderc_geometry_shader;
		break;
	case ShaderStage::eShader_Stage_Compute:
		scShadercStage = shaderc_shader_kind::shaderc_compute_shader;
		break;
	default:
		GLOG(Log::eError, "Unknown shader stage flag.");
		return TArray<uint32_t>();
	}

	GLOG(Log::eInfo, "Compile shader file %s...", ShaderSourceFilename.CStr());

	File ShaderSource(ShaderSourceFilename);
	FString Content = ShaderSource.ReadText();
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	SetupCompileOptions(options);
	options.SetOptimizationLevel(shaderc_optimization_level_performance);	// 优化
	options.SetSourceLanguage(SourceLanguage);

	// Like -DMY_DEFINE=1
	//options.AddMacroDefinition("MY_DEFINE", "1");

	shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(Content.CStr(), scShadercStage, Name.CStr(), options);

	if (module.GetCompilationStatus() !=
		shaderc_compilation_status_success) {
		GLOG(Log::eError, "Compile shader %s failed.\n\
			Error msg: %s",
			Name.CStr(),
			module.GetErrorMessage().c_str()
		);
		return TArray<uint32_t>();
	}

	TArray<uint32_t> SPRIV;
	for (auto It = module.cbegin(); It != module.cend(); ++It) {
		SPRIV.Push(*It);
	}

	// 写入文件
	if (writeToDisk && SPRIV.Data()) {
		FString SPRIVFilePath = ResourceSystem::Get().GetRootPath() + FString("/Shaders") + SufPath + ".spv";
		File OutFile(SPRIVFilePath);
		OutFile.WriteBytes(reinterpret_cast<const char*>(SPRIV.Data()), SPRIV.Size() * sizeof(uint32_t), std::ios::trunc | std::ios::binary);
		GLOG(Log::eInfo, "Write shader file into %s...", SPRIVFilePath.CStr());
	}

	return SPRIV;
}

void UShader::ProcessAttributes(const TArray<ShaderAttributeConfig>& attributes) {
	for (uint32_t i = 0; i < attributes.Size(); ++i) {
		AddAttribute(attributes[i]);
	}
}

void UShader::ProcessUniforms(const TArray<ShaderUniformConfig>& uniforms) {
	for (uint32_t i = 0; i < uniforms.Size(); ++i) {
		if (uniforms[i].type == eShader_Uniform_Type_Sampler) {
			AddSampler(uniforms[i]);
		}
		else {
			AddUniform(uniforms[i]);
		}
	}
}

void UShader::AddAttribute(const ShaderAttributeConfig& config) {
	uint32_t Size = 0;
	switch (config.type) {
	case eShader_Attribute_Type_Int8:
	case eShader_Attribute_Type_UInt8:
		Size = 1;
		break;
	case eShader_Attribute_Type_Int16:
	case eShader_Attribute_Type_UInt16:
		Size = 2;
		break;
	case eShader_Attribute_Type_Float:
	case eShader_Attribute_Type_Int32:
	case eShader_Attribute_Type_UInt32:
		Size = 4;
		break;
	case eShader_Attribute_Type_Float_2:
		Size = 8;
		break;
	case eShader_Attribute_Type_Float_3:
		Size = 12;
		break;
	case eShader_Attribute_Type_Float_4:
		Size = 16;
		break;
	default:
		GLOG(Log::eError, "Unrecognized type %d, defaulting to size of 4. This probably is not what is desired.");
		Size = 4;
		break;
	}

	AttributeStride += (uint16_t)Size;

	// Create/push the attribute.
	ShaderAttribute Attrib = {};
	Attrib.name = config.name;
	Attrib.size = Size;
	Attrib.type = config.type;

	Attributes.Push(Attrib);
}

void UShader::AddSampler(const ShaderUniformConfig& config) {
	// Samples can't be used for push constants.
	if (config.scope == eShader_Scope_Local) {
		GLOG(Log::eError, "add_sampler cannot add a sampler at local scope.");
		return;
	}

	// Verify the name is valid and unique.
	if (!IsUniformNameValid(config.name) || !IsUniformAddStateValid()) {
		return;
	}

	// If global, push into the global list.
	uint32_t Location = 0;
	if (config.scope == eShader_Scope_Global) {
		uint32_t GlobalTextureCount = (uint32_t)GlobalTextureMaps.Size();
		Location = GlobalTextureCount;

		// NOTE: Create a default texture map to be used here. Can always be updated later.
		FTextureMap DefaultMap;
		DefaultMap.filter_magnify = TextureFilter::eTexture_Filter_Mode_Linear;
		DefaultMap.filter_minify = TextureFilter::eTexture_Filter_Mode_Linear;
		DefaultMap.repeat_u = TextureRepeat::eTexture_Repeat_Repeat;
		DefaultMap.repeat_v = TextureRepeat::eTexture_Repeat_Repeat;
		DefaultMap.repeat_w = TextureRepeat::eTexture_Repeat_Repeat;
		DefaultMap.usage = TextureUsage::eTexture_Usage_Unknown;
		if (!Renderer->AcquireTextureMap(&DefaultMap)) {
			GLOG(Log::eError, "Failed to acquire for global texture map during shader creation.");
			return;
		}

		// Allocate a pointer assign the texture, and push into global texture maps.
		// NOTE: This allocation is only done for global texture maps.
		FTextureMap* Map = (FTextureMap*)Memory::Allocate(sizeof(FTextureMap), MemoryType::eMemory_Type_Renderer);
		*Map = DefaultMap;
		Map->texture = TextureSystem::Get().GetDefaultDiffuseTexture();
		GlobalTextureMaps.Push(Map);
	}
	else {
		// Otherwise, it's instance-level, so keep count of how many need to be added during the resource acquisition.
		Location = InstanceTextureCount;
		InstanceTextureCount++;
	}

	// Treat it like a uniform. NOTE: In the case of samplers, out_location is used to determine the
	// hashtable entry's 'location' field value directly, and is then set to the index of the uniform array.
	// This allows location lookups for samplers as if they were uniforms as well (since technically they are).
	// TODO: might need to store this elsewhere
	AddUniform(config.name, 0, config.type, config.scope, config.semantic, Location, true);
}

void UShader::AddUniform(const ShaderUniformConfig& config) {
	if (!IsUniformNameValid(config.name) || !IsUniformAddStateValid()) {
		return;
	}

	AddUniform(config.name, config.size, config.type, config.scope, config.semantic, 0, false);
}

void UShader::AddUniform(const FString& uniform_name, uint32_t size, ShaderUniformType type, 
	ShaderScope scope, ShaderSemantic semantic, uint32_t set_location, bool is_sampler){
	uint16_t UniformCount = (uint16_t)Uniforms.Size();
	ShaderUniform Entry;
	Entry.name = uniform_name;
	Entry.index = UniformCount;
	Entry.scope = scope;
	Entry.type = type;
	Entry.semantic = semantic;

	bool IsGlobal = (scope == eShader_Scope_Global);
	if (is_sampler) {
		Entry.location = set_location;
	}
	else {
		Entry.location = Entry.index;
	}

	if (scope != eShader_Scope_Local) {
		Entry.set_index = (uint32_t)scope;
		Entry.offset = is_sampler ? 0 : IsGlobal ? GlobalUboSize : UboSize;
		Entry.size = is_sampler ? 0 : size;
	}
	else {
		// Push a new aligned range (align to 4, as required by Vulkan spec)
		Entry.set_index = INVALID_ID_U16;
		Range r = PaddingAligned(PushConstantsSize, size, 4);
		// utilize the aligned offset/range
		Entry.offset = r.offset;
		Entry.size = (unsigned short)r.size;

		// Track in configuration for use in initialization.
		PushConstantsRanges[PushConstantsRangeCount] = r;
		PushConstantsRangeCount++;

		// Increase the push constant's size by the total value.
		PushConstantsSize += r.size;
	}

	HashMap[uniform_name] = (uint16_t)Entry.index;
	Uniforms.Push(Entry);

	if (!is_sampler) {
		if (Entry.scope == eShader_Scope_Global) {
			GlobalUboSize += Entry.size;
		}
		else if (Entry.scope == eShader_Scope_Instance) {
			UboSize += Entry.size;
		}
	}
}

uint32_t UShader::GetUniformIndex(const FString& name) const {
	if (Status == EShaderStatus::eShader_State_Uninitialized) {
		GLOG(Log::eError, "Shader::GetUniformIndex — Shader '%s' is not init.", Name.CStr());
		return INVALID_ID;
	}

	const uint32_t* It = HashMap.Find(name);
	if (It == nullptr) {
		GLOG(Log::eError, "Shader '%s' not found uniform '%s'.",
			Name.CStr(), name.CStr());
		return INVALID_ID;
	}

	uint32_t ArrayIndex = *It;
	if (ArrayIndex >= (uint32_t)Uniforms.Size()) {
		return INVALID_ID;
	}

	// HashMap 存的是 Uniforms 数组下标，Uniforms[i].index 才是真正的 uniform index
	return Uniforms[ArrayIndex].index;
}

ShaderUniform* UShader::GetUniformHandle(const FString& name) {
	if (Status == EShaderStatus::eShader_State_Uninitialized) {
		GLOG(Log::eError, "Shader::GetUniformIndex — Shader '%s' is not init.", Name.CStr());
		return nullptr;
	}

	uint32_t* It = HashMap.Find(name);
	if (It == nullptr) {
		GLOG(Log::eError, "Shader '%s' not found uniform '%s'.",
			Name.CStr(), name.CStr());
		return nullptr;
	}

	uint32_t ArrayIndex = *It;
	if (ArrayIndex >= (uint32_t)Uniforms.Size()) {
		return nullptr;
	}

	// HashMap 存的是 Uniforms 数组下标，Uniforms[i].index 才是真正的 uniform index
	return &Uniforms[ArrayIndex];
}

bool UShader::IsUniformNameValid(const FString& uniform_name) {
	if (uniform_name.IsEmpty()) {
		GLOG(Log::eError, "Uniform name must exist.");
		return false;
	}

	uint32_t* it = HashMap.Find(uniform_name);
	if (it != nullptr) {
		GLOG(Log::eError, "A uniform by the name '%s' already exists on shader '%s'.", uniform_name.CStr(), Name.CStr());
		return false;
	}

	return true;
}

bool UShader::IsUniformAddStateValid() {
	if (Status != EShaderStatus::eShader_State_Uninitialized) {
		GLOG(Log::eError, "Uniforms may only be added to shaders before initialization.");
		return false;
	}
	return true;
}
