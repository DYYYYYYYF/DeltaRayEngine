#include "ShaderSystem.h"

#include "Rendering/Renderer.hpp"
#include "Systems/TextureSystem.h"

#include "Core/DMemory.hpp"
#include "Core/EngineLogger.hpp"
#include "Platform/File/JsonObject.h"
#include "Rendering/Vulkan/VulkanShader.hpp"

ShaderSystem& ShaderSystem::Get() {
	static ShaderSystem ShaderSystemInstance;
	return ShaderSystemInstance;
}

bool ShaderSystem::Initialize(IRenderer* renderer, ShaderSystem::Config config) {
	if (renderer == nullptr) {
		return false;
	}

	// Read current config
	File MaterialAsset(ENGINE_CONFIG_PATH);
	if (!MaterialAsset.IsExist()) {
		return false;
	}

	JsonObject Content = JsonObject(MaterialAsset);
	GLOBAL_SHADER_TYPE = Content.ReadString("renderer.shader_language")
		.compare("glsl") == 0 ? EShaderLanguage::eGLSL : EShaderLanguage::eHLSL;

	Renderer = renderer;
	if (config.max_shader_count < 512) {
		if (config.max_shader_count == 0) {
			GLOG(Log::eError, "shader_system_initialize - config.max_shader_count must be greater than 0");
			return false;
		}
		else {
			// This is to help avoid hashtable collisions.
			GLOG(Log::eError, "shader_system_initialize - config.max_shader_count is recommended to be at least 512.");
		}
	}
	
	ShaderSystemConfig = config;

	if (!EngineEvent::Register(eEventCode::Reload_Shader_Module, nullptr, 
		std::bind(&ShaderSystem::OnReloadShader, this, std::placeholders::_1, std::placeholders::_2,
			std::placeholders::_3, std::placeholders::_4))) {
		GLOG(Log::eError, "Unable to listen for refresh required event, creation failed.");
		return false;
	}

	Initilized = true;
	return true;
}

void ShaderSystem::Shutdown() {
	if (Initilized) {
		for (auto& Pair : Shaders) {
			UShader* Val = Pair.Second();
			if (Val) {
				Val->Destroy();
				DeleteObject(Val);
			}
		}
		
		Shaders.Clear();

		EngineEvent::Unregister(eEventCode::Reload_Shader_Module, nullptr, 
			std::bind(&ShaderSystem::OnReloadShader, this, std::placeholders::_1, std::placeholders::_2,
				std::placeholders::_3, std::placeholders::_4));

		ShaderMap.Clear();

		// Save current config
		File MaterialAsset(ENGINE_CONFIG_PATH);
		if (!MaterialAsset.IsExist()) {
			return;
		}

		JsonObject Content = JsonObject(MaterialAsset);
		const char* Lan = GLOBAL_SHADER_TYPE == EShaderLanguage::eGLSL ? "glsl" : "hlsl";
		Content.WriteString("renderer.shader_language", std::string(Lan));
		Content.SaveToFile(MaterialAsset);
	}
}

bool ShaderSystem::ReloadShader(const FString& shader_name, EShaderLanguage language) {
	UShader* s = Get(shader_name);
	if (s == nullptr) {
		return false;
	}

	return ReloadShader(s, language);
}

bool ShaderSystem::ReloadShader(UShader* shader, EShaderLanguage language) {
	// Change shader status.
	shader->Status = EShaderStatus::eShader_State_Reloading;
	GLOBAL_SHADER_TYPE = language;

	if (!shader->Reload()) {
		GLOG(Log::eError, "shader_system_create: reload shader failed for shader '%s'.", shader->Name.CStr());
		// NOTE: initialize automatically destroys the shader if it fails.
		shader->Destroy();
		return false;
	}


	shader->Status = EShaderStatus::eShader_State_Initialized;
	return true;
}

bool ShaderSystem::OnReloadShader(eEventCode code, void* sender, void* listenerInst, SEventContext context) {
	FString ShaderName = context.data.c;
	if (!ReloadShader(ShaderName)) {
		GLOG(Log::eError, "Failed to reload shader %s.", ShaderName.CStr());
		return false;
	}

	return true;
}

bool ShaderSystem::Create(IRenderpass* pass, FShaderConfig* config) {
	uint32_t ID = GetShaderID(config->name);

	UShader* OutShader = nullptr;
	if (ID == INVALID_ID) {
		RendererBackendType BackendAPI = Renderer->GetBackendType();
		switch (BackendAPI)
		{
		case eRenderer_Backend_Type_Vulkan:
			OutShader = NewObject<VulkanShader>(config->name);
			break;
			// TODO
		case eRenderer_Backend_Type_OpenGL:
			break;
		case eRenderer_Backend_Type_DirecX:
			break;
		}

		ID = OutShader->GetUniqueID();
		Shaders[ID] = OutShader;
		ShaderMap[config->name] = ID;
	}
	else {
		GLOG(Log::eWarn, "Shader named '%s' already create. It will be covered.", config->name.CStr());
		OutShader = Shaders[ID];
	}

	if (!OutShader) {
		GLOG(Log::eError, "ShaderSystem::Create create shader failed.");
		return false;
	}

	OutShader->ID = ID;
	if (OutShader->ID == INVALID_ID) {
		GLOG(Log::eError, "Unable to find free slot to create new shader. Aborting.");
		return false;
	}
	
	OutShader->Name = config->name;
	OutShader->Language = GLOBAL_SHADER_TYPE;
	Memory::Zero(OutShader->PushConstantsRanges, sizeof(Range) * 32);

	// A running total of the actual global uniform buffer object size.
	OutShader->GlobalUboSize = 0;
	// A running total of the actual instance uniform buffer object size.
	OutShader->UboSize = 0;
	// NOTE: UBO alignment requirement set in renderer backend.

	// This is hard-coded because the Vulkan spec only guarantees that a _minimum_ 128 bytes of space are available,
	// and it's up to the driver to determine how much is available. Therefore, to avoid complexity, only the
	// lowest common denominator of 128B will be used.
	OutShader->PushConstantsStride = 128;
	OutShader->PushConstantsSize = 0;

	// Process flags.
	OutShader->Flags = ShaderFlags::eShader_Flag_None;
	if (config->depthTest) {
		OutShader->Flags |= ShaderFlags::eShader_Flag_DepthTest;
	}
	if (config->depthWrite) {
		OutShader->Flags |= ShaderFlags::eShader_Flag_DepthWrite;
	}


	if (!Renderer->CreateRenderShader(OutShader, config, pass, config->stage_filenames, config->stages)) {
		GLOG(Log::eError, "Error creating shader.");
		return false;
	}

	// Process attributes.
	OutShader->ProcessAttributes(config->attributes);

	// Process uniforms.
	OutShader->ProcessUniforms(config->uniforms);

	// Initialize the shader.
	if (!OutShader->Initialize()) {
		GLOG(Log::eError, "shader_system_create: initialization failed for shader '%s'.", config->name.CStr());
		// NOTE: initialize automatically destroys the shader if it fails.
		return false;
	}

	OutShader->Status = EShaderStatus::eShader_State_Initialized;
	return true;
}

unsigned ShaderSystem::GetID(const FString& shader_name) {
	return GetShaderID(shader_name);
}

UShader* ShaderSystem::GetByID(uint32_t shader_id) {
	if (shader_id >= ShaderSystemConfig.max_shader_count || Shaders[shader_id]->ID == INVALID_ID) {
		return nullptr;
	}

	return Shaders[shader_id];
}

UShader* ShaderSystem::Get(const FString& shader_name) {
	uint32_t ShaderID = GetShaderID(shader_name);
	if (ShaderID != INVALID_ID) {
		return GetByID(ShaderID);
	}

	return nullptr;
}

uint32_t ShaderSystem::GetShaderID(const FString& shader_name) {
	uint32_t* it = ShaderMap.Find(shader_name);
	if (it == nullptr){
		return INVALID_ID;
	}

	return *it;
}
