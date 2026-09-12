#pragma once

#include <Defines.hpp>
#include <IGame.hpp>
#include <Math/MathTypes.hpp>
#include <Core/CPython.hpp>
#include <Core/Keymap.hpp>
#include <Containers/TArray.hpp>
#include "Framework/Classes/StaticMeshActor.h"
#include "Framework/Classes/TextActor.h"

#ifndef EIDTOR_MODE
#define EIDTOR_MODE
#endif

#define EDITOR_CONFIG_PATH FString::Format("%s%s", ROOT_PATH, "/Editor/Config.json")

class DebugConsoleActor;
class ACameraActor;
class ADirectionalLightActor;

class GameInstance : public IGame {
public:
	GameInstance() :WorldCamera(nullptr), ConsoleKeymap(nullptr){}
	virtual ~GameInstance() {};

public:
	virtual bool Boot() override;
	virtual void Shutdown() override;
	virtual bool Initialize() override;
	virtual void BeginPlay() override;
	virtual bool Update(float delta_time) override;
	virtual void OnResize(unsigned int width, unsigned int height) override;

public:
	ACameraActor* WorldCamera;
	DebugConsoleActor* GameConsole;
	// 方向光 Actor：Initialize 中创建，Shutdown 时把光照配置写回 Editor/Config.json
	ADirectionalLightActor* DirectionalLightActor = nullptr;

	// TODO: temp
	Keymap* ConsoleKeymap;
	CPythonModule TestPython;
	// TODO: end temp

};
