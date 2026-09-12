#pragma once

#include "Defines.hpp"
#include "Containers/TArray.hpp"

class UDirectionalLightComponent;

/**
 * @brief 光照系统。
 *
 * 维护场景中已注册的方向光组件，为渲染通道提供"当前主光源"的统一取用入口：
 * 阴影通道与延迟光照通道均不再各自持有硬编码光照参数，而是逐帧从这里读取光源 Actor 的配置。
 * 组件在挂载/卸载时自行注册与注销，因此不存在渲染侧持有失效指针的情况。
 */
class ENGINE_API LightSystem {
public:
	static LightSystem& Get();

	/** 组件挂载时注册；若当前尚无主光源则同时置为主光源 */
	void RegisterLight(UDirectionalLightComponent* Light);
	/** 组件卸载时注销；若被注销者正是主光源则自动切换到剩余的第一盏，没有则置空 */
	void UnregisterLight(UDirectionalLightComponent* Light);

	/** 取当前主方向光（无光源时返回 nullptr，调用方需回退到默认值） */
	UDirectionalLightComponent* GetMainLight() const { return MainLight; }
	/** 已注册的方向光数量 */
	size_t GetLightCount() const { return Lights.Size(); }

private:
	LightSystem() = default;

	TArray<UDirectionalLightComponent*> Lights;
	UDirectionalLightComponent* MainLight = nullptr;
};
