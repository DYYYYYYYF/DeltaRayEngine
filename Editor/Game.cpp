#include "Game.h"

#include <Core/Controller.hpp>
#include <Core/Event.hpp>
#include <Systems/CameraSystem.h>
#include <Platform/File/JsonObject.h>
#include <Scene/World.h>
#include "UI/Console/GameConsole.h"
#include "GameLogic/UI/WindowDataPanel.h"

// TODO: Temp
#include "UI/Console/Keybinds.h"
#include "UI/Console/GameCommand.h"
#include "GameLogic/LogicActors/RotationCubeActor.h"
#include "Framework/Components/CameraComponent.h"
#include "Framework/Classes/SkyboxActor.h"
#include "Framework/Classes/DirectionalLightActor.h"

bool GameOnEvent(eEventCode code, void* sender, void* listender_inst, SEventContext context) {
	switch (code)
	{
        case eEventCode::Object_Hover_ID_Changed: 
        {
            //GameInst->HoveredObjectID = context.data.u32[0];
            return true;
        }break;
    }

	return false;
}

void LoadScene1(GameInstance* Game);
void LoadScene2(GameInstance* Game);
void LoadScene3(GameInstance* Game);
void LoadScene4(GameInstance* Game);

bool GameOnDebugEvent(eEventCode code, void* sender, void* listener_instance, SEventContext context) {
	GameInstance* GameInst = (GameInstance*)listener_instance;

	if (code == eEventCode::Debug_0) {
		LoadScene1(GameInst);
		return true;
	}
	else if (code == eEventCode::Debug_1) {
		LoadScene2(GameInst);
		return true;
	}
	else if (code == eEventCode::Debug_2) {
		LoadScene3(GameInst);
		return true;
	}
	else if (code == eEventCode::Debug_3) {
		LoadScene4(GameInst);
		return true;
	}

	return false;
}

bool GameInstance::Boot() {
	if (!IGame::Boot()) {
		return false;
	}

	GLOG(Log::eInfo, "Booting...");

	File MaterialAsset(EDITOR_CONFIG_PATH);
	if (!MaterialAsset.IsExist()) {
		return false;
	}

	JsonObject Content = JsonObject(MaterialAsset.ReadText());
	WindowSize.Width = (uint16_t)Content.ReadInt("Window.Width");
	WindowSize.Height = (uint16_t)Content.ReadInt("Window.Height");

	Keybind GameKeybind;
	GameKeybind.Setup(this);
	GameCommand GameCmd;
	GameCmd.Setup();

	// Configure fonts.
	BitmapFontConfig BmpFontConfig;
	BmpFontConfig.name = "Ubuntu Mono 21px";
	BmpFontConfig.resourceName = "UbuntuMono21px";
	BmpFontConfig.size = 21;

	SystemFontConfig SysFontConfig;
	SysFontConfig.defaultSize = 20;
	SysFontConfig.name = "Noto Sans";
	SysFontConfig.resourceName = "NotoSansCJK";

	FontConfig.autoRelease = false;
	FontConfig.defaultBitmapFontCount = 1;
	FontConfig.bitmapFontConfigs = (BitmapFontConfig*)Memory::Allocate(sizeof(BitmapFontConfig) * 1, MemoryType::eMemory_Type_Array);
	new (static_cast<BitmapFontConfig*>(FontConfig.bitmapFontConfigs)) BitmapFontConfig(BmpFontConfig);
	FontConfig.defaultSystemFontCount = 1;
	FontConfig.systemFontConfigs = (SystemFontConfig*)Memory::Allocate(sizeof(SystemFontConfig) * 1, MemoryType::eMemory_Type_Array);
	new (static_cast<SystemFontConfig*>(FontConfig.systemFontConfigs)) SystemFontConfig(SysFontConfig);
	FontConfig.maxBitmapFontCount = 100;
	FontConfig.maxSystemFontCount = 100;

	return true;
}

bool GameInstance::Initialize() {
	GLOG(Log::eDebug, "GameInitialize() called.");
	File MaterialAsset(EDITOR_CONFIG_PATH);
	if (!MaterialAsset.IsExist()) {
		return false;
	}

	// Get transform
	JsonObject Content = JsonObject(MaterialAsset);
	Matrix4 Mat = Content.ReadMatrix4("Camera.Transform");
	
	// Load python script
	TestPython.SetPythonFile("recompile_shader");

	Vector3 Position = Content.ReadVector3("Camera.Position");
	Vector3 Rotation = Content.ReadVector3("Camera.Rotation");

	WorldCamera = CameraSystem::Get().GetMainCamera();
	if (WorldCamera) {
		UCameraComponent* CameraComp = WorldCamera->GetCameraComponent();
		if (CameraComp) {
			CameraComp->SetPosition(Position);
			CameraComp->SetEulerAngles(Rotation);
		}
	}
	

	// Create test ui text objects.
	ATextActor* TestText = NewObject<AWindowDataPanel>("Render information window.");
	if (TestText) {
		FString TestTextName = "Ubuntu Mono 21px";
		FString TestTextContent = "Test! \n Yooo!";
		TestText->SetFontSize(21);
		TestText->SetTextFont(TestTextName, UITextType::eUI_Text_Type_Bitmap);
		TestText->SetActorLocation(Vector3(350, 600, 0));
		World->AddActor(TestText);
	}

	FString TestSystemName = "Noto Sans CJK JP";
	FString TestSystemContent = "Keyboard map:\
		\nLoad models:\
		\n\tO: Scene1 P: Scene2\
		\n\tK: Scene3 L: Scene4\
		\nM: Watch memory usage.\
		\nF1: Default view.\
		\nF2: Normal view.\
		\nF3: Material view.\
		\nF4: Depth view.";
	ATextActor* TestSysText = NewObject<ATextActor>("Keyboard map texts.", UITextType::eUI_Text_Type_System, TestSystemName, 25, TestSystemContent);

	if (TestSysText) {
		TestSysText->SetActorLocation(Vector3(100, 600, 0));
		World->AddActor(TestSysText);
	}

	// Load console
	GameConsole = NewObject<DebugConsoleActor>("Debug console panel");
	if (GameConsole) {
		World->AddActor(GameConsole);
	}

	// Skybox
	ASkyboxActor* SkyboxActor = NewObject<ASkyboxActor>("SkyboxActor");
	if (SkyboxActor) {
		World->AddActor(SkyboxActor);
	}

	// 方向光 Actor：阴影通道与延迟光照通道的光照参数统一由它配置
	// （方向/颜色/强度/环境光/阴影偏置与光锥参数，构造时自动注册进 LightSystem）
	DirectionalLightActor = NewObject<ADirectionalLightActor>("DirectionalLight");
	if (DirectionalLightActor) {
		DirectionalLightActor->LoadFromConfig(Content);
		World->AddActor(DirectionalLightActor);
	}

	// World meshes
	ARotationCubeActor* CubeMesh = NewObject<ARotationCubeActor>("TestCube");
	CubeMesh->SetActorLocation(Vector(0.0f, 0.0f, 0.0f));
	CubeMesh->SetActorRotation(Vector(0.0f));
	World->AddActor(CubeMesh);

	ARotationCubeActor* CubeMesh2 = NewObject<ARotationCubeActor>("TestCube2");
	CubeMesh2->SetActorLocation(Vector(10.0f, 0.0f, 0.0f));
	CubeMesh2->SetActorRotation(Vector(0.0f));
	CubeMesh2->SetWorldScale(Vector(0.5f));
	CubeMesh2->AttachTo(CubeMesh);
	World->AddActor(CubeMesh2);

	ARotationCubeActor* CubeMesh3 = NewObject<ARotationCubeActor>("TestCube3");
	CubeMesh3->SetActorLocation(Vector(15.0f, 0.0f, 0.0f));
	CubeMesh3->SetActorRotation(Vector(0.0f));
	CubeMesh3->SetWorldScale(Vector(0.3f));
	CubeMesh3->AttachTo(CubeMesh2);
	World->AddActor(CubeMesh3);

	// TODO: TEMP
	EngineEvent::Register(eEventCode::Debug_0, this, GameOnDebugEvent);
	EngineEvent::Register(eEventCode::Debug_1, this, GameOnDebugEvent);
	EngineEvent::Register(eEventCode::Debug_2, this, GameOnDebugEvent);
	EngineEvent::Register(eEventCode::Debug_3, this, GameOnDebugEvent);
	EngineEvent::Register(eEventCode::Object_Hover_ID_Changed, this, GameOnEvent);
	// TEMP

	// 优先初始化World
	// World->Actor->Component
	// Component中可能会初始化Geometry，后续在RegisterToWorld时需要这些数据
	World->Initialize();

	return true;
}

void GameInstance::BeginPlay() {
	if (!World) return;
	World->BeginPlay();
}

void GameInstance::Shutdown() {
	// 序列化数据（必须在 World 销毁前完成：相机与方向光 Actor 的组件此时仍然有效）
	File MaterialAsset(EDITOR_CONFIG_PATH);
	if (MaterialAsset.IsExist()) {
		UCameraComponent* CameraComp = WorldCamera ? WorldCamera->GetCameraComponent() : nullptr;
		if (!CameraComp) {
			GLOG(Log::eError, "GameInstance::Shutdown: camera component is nullptr.");
		}

		JsonObject Content = JsonObject(MaterialAsset);
		Content.WriteInt("Window.Width", (int)WindowSize.Width);
		Content.WriteInt("Window.Height", (int)WindowSize.Height);
		if (CameraComp) {
			Content.WriteVector3("Camera.Position", CameraComp->GetPosition());
			Content.WriteVector3("Camera.Rotation", CameraComp->GetEulerAngles());
		}

		// 方向光配置（方向/颜色/强度/环境光/阴影）写回 Editor/Config.json，下次启动即可复用
		if (DirectionalLightActor) {
			DirectionalLightActor->SaveToConfig(Content);
		}

		Content.SaveToFile(MaterialAsset);
	}

	if (World) {
		// Actor示例由World清空
		World->Destroy();
	}

	// TODO: TEMP
	EngineEvent::Unregister(eEventCode::Debug_0, this, GameOnDebugEvent);
	EngineEvent::Unregister(eEventCode::Debug_1, this, GameOnDebugEvent);
	EngineEvent::Unregister(eEventCode::Debug_2, this, GameOnDebugEvent);
	EngineEvent::Unregister(eEventCode::Debug_3, this, GameOnDebugEvent);
	EngineEvent::Unregister(eEventCode::Object_Hover_ID_Changed, this, GameOnEvent);
	// TEMP
}

bool GameInstance::Update(float delta_time) {
	if (World) {
		World->Tick(delta_time);
	}

	// Controller
	int px, py, cx, cy;
	Controller::GetMousePosition(cx, cy);
	Controller::GetPreviousMousePosition(px, py);
	float MouseMoveSpeed = 0.005f;
	UCameraComponent* CameraComp = WorldCamera->GetCameraComponent();
	if (!CameraComp) {
		return false;
	}

	if (Controller::IsButtonDown(eButtons::Right)) {
		if (cx != px) {
			CameraComp->RotateYaw((px - cx) * MouseMoveSpeed);
		}

		if (cy != py) {
			CameraComp->RotatePitch((py - cy) * MouseMoveSpeed);
		}
	}

	return true;
}

void GameInstance::OnResize(unsigned int width, unsigned int height) {
	WindowSize = { (uint16_t)width, (uint16_t)height };
}

void LoadScene1(GameInstance* GameInst) {
	
}

void LoadScene2(GameInstance* GameInst) {
	AStaticMeshActor* Model1 = NewObject<AStaticMeshActor>("sponza");
	Model1->LoadFromResource("sponza");
	Model1->SetActorLocation(Vector(0.0f, -10.0f, 0.0f));
	Model1->SetActorRotation(Vector(0.0f, 90.0f, 0.0f));
	Model1->SetWorldScale(Vector(0.1f));
	GameInst->GetWorld()->AddActor(Model1);

	AStaticMeshActor* Model2 = NewObject<AStaticMeshActor>("bunny");
	Model2->LoadFromResource("bunny");
	Model2->SetActorLocation(Vector(30.0f, 0.0f, 0.0f));
	Model2->SetActorRotation(Vector(0.0f, 90.0f, 0.0f));
	Model2->SetWorldScale(Vector(5.0f));
	GameInst->GetWorld()->AddActor(Model2);

	AStaticMeshActor* Model3 = NewObject<AStaticMeshActor>("falcon");
	Model3->LoadFromResource("falcon");
	Model3->SetActorLocation(Vector(-30.0f, 0.0f, 0.0f));
	GameInst->GetWorld()->AddActor(Model3);
}

void LoadScene3(GameInstance* GameInst) {
	
}

void LoadScene4(GameInstance* GameInst) {
	
}