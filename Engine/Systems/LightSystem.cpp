#include "LightSystem.h"
#include "Core/EngineLogger.hpp"
#include "Framework/Components/DirectionalLightComponent.h"

LightSystem& LightSystem::Get() {
	static LightSystem LightSystemInstance;
	return LightSystemInstance;
}

void LightSystem::RegisterLight(UDirectionalLightComponent* Light) {
	if (!Light) {
		return;
	}

	for (UDirectionalLightComponent* Registered : Lights) {
		if (Registered == Light) {
			return;
		}
	}

	Lights.Push(Light);

	// 首盏注册的方向光作为主光源，后续注册的仅计入列表，不抢占主光源
	if (MainLight == nullptr) {
		MainLight = Light;
	}

	GLOG(Log::eDebug, "Directional light registered. total=%zu, has_main=%d.",
		Lights.Size(), MainLight ? 1 : 0);
}

void LightSystem::UnregisterLight(UDirectionalLightComponent* Light) {
	if (!Light) {
		return;
	}

	for (size_t i = 0; i < Lights.Size(); ++i) {
		if (Lights[i] == Light) {
			Lights.RemoveAt(i);
			break;
		}
	}

	// 主光源被注销时回退到剩余的第一盏，避免渲染通道继续持有已销毁组件
	if (MainLight == Light) {
		MainLight = Lights.IsEmpty() ? nullptr : Lights[0];
	}

	GLOG(Log::eDebug, "Directional light unregistered. total=%zu, has_main=%d.",
		Lights.Size(), MainLight ? 1 : 0);
}
