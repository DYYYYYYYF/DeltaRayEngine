#pragma once

#include "Defines.hpp"
#include "Core/Event.hpp"
#include "Containers/FString.hpp"
#include "Rendering/Resources/ResourceTypes.hpp"
#include <functional>
#include <map>

class IRenderer;
class IRenderpass;

class ShaderSystem {
public:
	struct Config {
		unsigned short max_shader_count;
		unsigned short max_uniform_count;
		unsigned short max_global_textures;
		unsigned short max_instance_textures;
	};

public:
	static ShaderSystem& Get();

public:
	/**
	 * @brief Initializes the shader system using the supplied configuration.
	 * NOTE: Call this twice, once to obtain memory requirement (memory = 0) and a second time
	 * including allocated memory.
	 *
	 * @param renderer A pointer to Renderer front-end.
	 * @param config The configuration to be used when initializing the system.
	 * @return b8 True on success; otherwise false.
	 */
	bool Initialize(IRenderer* renderer, ShaderSystem::Config config);

	/**
	 * @brief Shuts down the shader system.
	 */
	void Shutdown();

	/**
	 * @brief Creates a new shader with the given config.
	 *
	 * @return True on success; otherwise false.
	 */
	bool Create(IRenderpass* pass, FShaderConfig* config);

	/**
	 * @brief Gets the identifier of a shader by name.
	 *
	 * @param shader_name The name of the shader.
	 * @return The shader id, if found; otherwise INVALID_ID.
	 */
	unsigned GetID(const FString& shader_name);

	/**
	 * @brief Returns a pointer to a shader with the given identifier.
	 *
	 * @param shader_id The shader identifier.
	 * @return A pointer to a shader, if found; otherwise 0.
	 */
	UShader* GetByID(uint32_t shader_id);

	/**
	 * @brief Returns a pointer to a shader with the given name.
	 *
	 * @param shader_name The name to search for. Case sensitive.
	 * @return A pointer to a shader, if found; otherwise 0.
	 */
	UShader* Get(const FString& shader_name);

	bool ReloadShader(const FString& shader_name, EShaderLanguage language = EShaderLanguage::eGLSL);
	bool ReloadShader(UShader* shader, EShaderLanguage language = EShaderLanguage::eGLSL);
	
public:
	EShaderLanguage GetShaderLanguage() const { return GLOBAL_SHADER_TYPE; }

private:
	bool OnReloadShader(eEventCode code, void* sender, void* listenerInst, SEventContext context);

private:
	uint32_t GetShaderID(const FString& shader_name);

public:
	IRenderer* Renderer = nullptr;
	ShaderSystem::Config ShaderSystemConfig;
	
	TMap<FString, uint32_t> ShaderMap;
	TMap<size_t, UShader*> Shaders;
	
	bool Initilized = false;
	EShaderLanguage GLOBAL_SHADER_TYPE = EShaderLanguage::eGLSL;

};



