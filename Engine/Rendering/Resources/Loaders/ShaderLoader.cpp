#include "ShaderLoader.h"

#include "Core/DMemory.hpp"
#include "Core/EngineLogger.hpp"

#include "Rendering/Resources/Shader/Shader.hpp"
#include "Systems/ResourceSystem.h"
#include "Platform/File/File.hpp"

ShaderLoader::ShaderLoader() {
	Type = EAssetType::Shader;
	TypePath = "Shaders";
}

bool ShaderLoader::Load(const FString& name, void* params, UAsset* resource) {
	(void)params;

	if (name.Length() == 0 || resource == nullptr) {
		return false;
	}

	const char* FormatStr = "%s/%s/%s%s";
	FString FullFilePath = FString::Format(FormatStr, ResourceSystem::Get().Get().GetRootPath(), TypePath.CStr(), name.CStr(), ".scfg");	// shader config

	File AssetFile(FullFilePath.CStr());
	if (!AssetFile.IsExist()) {
		GLOG(Log::eError, "Shader loader load. Unable to open file for binary reading: '%s'.", FullFilePath.CStr());
		return false;
	}
	resource->FullPath = FullFilePath;
	resource->Name = name;

	// Set some defaults, create arrays.
	FShaderConfig* ResourceData = (FShaderConfig*)Memory::Allocate(sizeof(FShaderConfig), MemoryType::eMemory_Type_Resource);
	ResourceData = new(ResourceData)FShaderConfig();
	ASSERT(ResourceData);

	AssetFile.ReadLineByLine([this, ResourceData](size_t index, const FString& line) {
		return ParseLineData(index, line, ResourceData);
	});

	resource->Data = ResourceData;
	resource->DataCount = 1;
	resource->DataSize = sizeof(FShaderConfig);

	return true;
}

bool ShaderLoader::ParseLineData(size_t index, const FString& line, FShaderConfig* resource) {
	// Trim the string.
	FString TrimmedLine = line.Trimmed();

	// Skip blank lines and comments.
	if (TrimmedLine.IsEmpty() || TrimmedLine[0] == '#') {
		// Continue to read next line.
		return true;
	}

	// Split into var-value
	int EqualIndex = TrimmedLine.IndexOf('=');
	if (EqualIndex == -1) {
		// Continue to read next line.
		GLOG(Log::eWarn, "Potential formatting issue found in file '%s': '=' token not found. Skiping line %ui.", resource->name.CStr(), index);
		return true;
	}

	// Value name.
	FString RawVarName = TrimmedLine.SubStr(0, EqualIndex);
	FString TrimmedVarName = RawVarName.Trim();

	// Value.
	FString RawValue = TrimmedLine.SubStr(EqualIndex + 1);
	FString TrimmedValue = RawValue.Trim();

	// Process the variable.
	if (RawVarName.Compare("version") == 0) {
		// TODO: version.
	}
	else if (RawVarName.Compare("name") == 0) {
		resource->name = TrimmedValue;
	}
	else if (RawVarName.Compare("renderpass") == 0) {
		// ResourceData->renderpass_name = StringCopy(TrimmedValue);
	}
	else if (RawVarName.Compare("stages") == 0) {
		// Parse the stages.
		TArray<FString> StageNames = TrimmedValue.Split(',', true, true);
		resource->stage_names = StageNames;

		// Ensue stage name and stage filename count are the same.
		resource->stages.resize(StageNames.Size());

		// Parse each stage and add the right type to the array.
		for (unsigned short i = 0; i < resource->stages.size(); ++i) {
			if (StageNames[i].Compare("frag") == 0 || StageNames[i].Compare("fragment") == 0) {
				resource->stages[i] = ShaderStage::eShader_Stage_Fragment;
			}
			else if (StageNames[i].Compare("vert") == 0 || StageNames[i].Compare("vertex") == 0) {
				resource->stages[i] = ShaderStage::eShader_Stage_Vertex;
			}
			else if (StageNames[i].Compare("geom") == 0 || StageNames[i].Compare("geometry") == 0) {
				resource->stages[i] = ShaderStage::eShader_Stage_Geometry;
			}
			else if (StageNames[i].Compare("comp") == 0 || StageNames[i].Compare("compute") == 0) {
				resource->stages[i] = ShaderStage::eShader_Stage_Compute;
			}
			else {
				GLOG(Log::eError, "shader_loader_load: Invalid file layout. Unrecognized stage '%s'", StageNames[i].CStr());
			}
		}
	}
	else if (TrimmedVarName.Compare("stagefiles") == 0) {
		resource->stage_filenames = TrimmedValue.Split(',', true, true);
		if (resource->stages.size() != resource->stage_filenames.Size()) {
			GLOG(Log::eError, "shader_loader_load: Invalid file layout. Attribute fields must be 'type,name'. Skipping.");
		}
	}
	else if (TrimmedVarName.Compare("cull_mode") == 0) {
		if (TrimmedValue.Compare("front") == 0) {
			resource->cull_mode = FaceCullMode::eFace_Cull_Mode_Front;
		}
		else if (TrimmedValue.Compare("front_and_back") == 0) {
			resource->cull_mode = FaceCullMode::eFace_Cull_Mode_Front_And_Back;
		}
		else if (TrimmedValue.Compare("none") == 0) {
			resource->cull_mode = FaceCullMode::eFace_Cull_Mode_None;
		}
	}
	else if (TrimmedVarName.Compare("polygon_mode") == 0) {
		if (TrimmedValue.Compare("line") == 0) {
			resource->polygon_mode = PolygonMode::ePology_Mode_Line;
		}
		else if (TrimmedValue.Compare("fill") == 0) {
			resource->polygon_mode = PolygonMode::ePology_Mode_Fill;
		}
	}
	else if (TrimmedVarName.Compare("primitive_topology") == 0) {
		if (TrimmedValue.Compare("triangle_list") == 0) {
			resource->PrimTopo = PrimitiveTopology::eTriangleList;
		}
		else if (TrimmedValue.Compare("triangle_strip") == 0) {
			resource->PrimTopo = PrimitiveTopology::eTriangleStrip;
		}
	}
	else if (TrimmedVarName.Compare("depth_test") == 0) {
		resource->depthTest = FString::ToBool(TrimmedValue);
	}
	else if (TrimmedVarName.Compare("depth_write") == 0) {
		resource->depthWrite = FString::ToBool(TrimmedValue);
	}
	else if (TrimmedVarName.Compare("attribute") == 0) {
		// Parse attribute.
		TArray<FString> Fields = TrimmedValue.Split(',', true, true);
		if (Fields.Size() != 2) {
			GLOG(Log::eError, "shader_loader_load: Invalid file layout. Attribute fields must be 'type,name'. Skipping.");
		}
		else {
			ShaderAttributeConfig Attribute;
			// Parse field type.
			if (Fields[0].Compare("float") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Float;
				Attribute.size = 4;
			}
			else if (Fields[0].Compare("vec2") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Float_2;
				Attribute.size = 8;
			}
			else if (Fields[0].Compare("vec3") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Float_3;
				Attribute.size = 12;
			}
			else if (Fields[0].Compare("vec4") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Float_4;
				Attribute.size = 16;
			}
			else if (Fields[0].Compare("u8") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_UInt8;
				Attribute.size = 1;
			}
			else if (Fields[0].Compare("u16") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_UInt16;
				Attribute.size = 2;
			}
			else if (Fields[0].Compare("u32") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_UInt32;
				Attribute.size = 4;
			}
			else if (Fields[0].Compare("i8") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Int8;
				Attribute.size = 1;
			}
			else if (Fields[0].Compare("i16") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Int16;
				Attribute.size = 2;
			}
			else if (Fields[0].Compare("i32") == 0) {
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Int32;
				Attribute.size = 4;
			}
			else {
				GLOG(Log::eError, "shader_loader_load: Invalid file layout. Attribute type must be float, vec2, vec3, vec4, i8, i16, i32, u8, u16, or u32.");
				GLOG(Log::eWarn, "Defaulting to float.");
				Attribute.type = ShaderAttributeType::eShader_Attribute_Type_Float;
				Attribute.size = 4;
			}

			// Take a copy of the attribute name.
			Attribute.name_length = (unsigned short)Fields[1].Length();
			Attribute.name = Fields[1];

			// Add the attribute.
			resource->attributes.push_back(Attribute);
		}

		Fields.Clear();
		// TODO: Free Memory in fields.
	}
	else if (TrimmedVarName.Compare("uniform") == 0) {
		// Parse field type.
		TArray<FString> Fields = TrimmedValue.Split(',', true, true);
		if (Fields.Size() != 3) {
			GLOG(Log::eError, "shader_loader_load: Invalid file layout. Uniform fields must be 'type,scope,name'. Skipping.");
		}
		else {
			ShaderUniformConfig Uniform;
			if (Fields[0].Compare("float") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Float;
				Uniform.size = 4;
			}
			else if (Fields[0].Compare("vec2") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Float_2;
				Uniform.size = 8;
			}
			else if (Fields[0].Compare("vec3") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Float_3;
				Uniform.size = 12;
			}
			else if (Fields[0].Compare("vec4") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Float_4;
				Uniform.size = 16;
			}
			else if (Fields[0].Compare("u8") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_UInt8;
				Uniform.size = 1;
			}
			else if (Fields[0].Compare("u16") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_UInt16;
				Uniform.size = 2;
			}
			else if (Fields[0].Compare("u32") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_UInt32;
				Uniform.size = 4;
			}
			else if (Fields[0].Compare("i8") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Int8;
				Uniform.size = 1;
			}
			else if (Fields[0].Compare("i16") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Int16;
				Uniform.size = 2;
			}
			else if (Fields[0].Compare("i32") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Int32;
				Uniform.size = 4;
			}
			else if (Fields[0].Compare("mat4") == 0 || Fields[0].Compare("matrix") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Matrix;
				Uniform.size = 64;
			}
			else if (Fields[0].Compare("samp") == 0 || Fields[0].Compare("sampler") == 0) {
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Sampler;
				Uniform.size = 0;
			}
			else {
				GLOG(Log::eError, "shader_loader_load: Invalid file layout. Uniform type must be f32, vec2, vec3, vec4, i8, i16, i32, u8, u16, u32 or mat4.");
				GLOG(Log::eWarn, "Defaulting to f32.");
				Uniform.type = ShaderUniformType::eShader_Uniform_Type_Float;
				Uniform.size = 4;
			}

			// Parse the scope.
			if (Fields[1].Compare("0") == 0) {
				Uniform.scope = ShaderScope::eShader_Scope_Global;
			}
			else if (Fields[1].Compare("1") == 0) {
				Uniform.scope = ShaderScope::eShader_Scope_Instance;
			}
			else if (Fields[1].Compare("2") == 0) {
				Uniform.scope = ShaderScope::eShader_Scope_Local;
			}
			else {
				GLOG(Log::eError, "shader_loader_load: Invalid file layout: Uniform scope must be 0 for global, 1 for instance or 2 for local.");
				GLOG(Log::eWarn, "Defaulting to global.");
				Uniform.scope = ShaderScope::eShader_Scope_Global;
			}

			// Parse semantic 方便应用材质参数
			Uniform.semantic = ParseSemantic(Fields[2]);

			// Take a copy of the uniform name.
			Uniform.name_length = (unsigned short)Fields[2].Length();
			Uniform.name = Fields[2];

			// Add the uniform.
			resource->uniforms.push_back(Uniform);
		}

		Fields.Clear();
	}

	// TODO: more fields.

	return true;
}

ShaderSemantic ShaderLoader::ParseSemantic(const FString& semantic) {
	if (semantic.Compare("projection") == 0) return ShaderSemantic::eShaderSemantic_Projection;
	if (semantic.Compare("view") == 0) return ShaderSemantic::eShaderSemantic_View;
	if (semantic.Compare("ambient_color") == 0) return ShaderSemantic::eShaderSemantic_AmbientColor;
	if (semantic.Compare("view_position") == 0) return ShaderSemantic::eShaderSemantic_ViewPosition;
	if (semantic.Compare("render_mode") == 0) return ShaderSemantic::eShaderSemantic_RenderMode;
	if (semantic.Compare("time") == 0) return ShaderSemantic::eShaderSemantic_Time;
	if (semantic.Compare("light_space_matrix") == 0) return ShaderSemantic::eShaderSemantic_LightSpaceMatrix;
	if (semantic.Compare("model") == 0) return ShaderSemantic::eShaderSemantic_Model_Matrix;

	if (semantic.Compare("gbuffer_albedo_texture") == 0) return ShaderSemantic::eSemantic_GBuffer_Albedo;
	if (semantic.Compare("gbuffer_normal_texture") == 0) return ShaderSemantic::eSemantic_GBuffer_Normal;
	if (semantic.Compare("gbuffer_position_texture") == 0) return ShaderSemantic::eSemantic_GBuffer_Position;

	if (semantic.Compare("diffuse_texture") == 0) return ShaderSemantic::eSemantic_Diffuse_Texture;
	if (semantic.Compare("normal_texture") == 0) return ShaderSemantic::eSemantic_Normal_Texture;
	if (semantic.Compare("roughness_metallic_texture") == 0) return ShaderSemantic::eSemantic_Roughness_Metallic_Texture;

	if (semantic.Compare("skybox_texture") == 0) return ShaderSemantic::eSemantic_Skybox_Texture;
	if (semantic.Compare("shadow_map_texture") == 0) return ShaderSemantic::eSemantic_Shadow_Map;

	return ShaderSemantic::eShaderSemantic_None;
}

void ShaderLoader::Unload(UAsset* resource) {
	FShaderConfig* Data = (FShaderConfig*)resource->Data;

	Data->stage_filenames.Clear();
	Data->stage_names.Clear();

	// Clean up attributes.
	Data->attributes.clear();

	// Clean up uniforms.
	Data->uniforms.clear();

	if (resource->Data) {
		Memory::Free(resource->Data, MemoryType::eMemory_Type_Texture);
		resource->Data = nullptr;
		resource->DataSize = 0;
		resource->DataCount = 0;
		resource->LoaderID = INVALID_ID;
	}

	resource = nullptr;
}
