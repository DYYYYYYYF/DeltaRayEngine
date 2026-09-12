#pragma once

#include "Actor.h"

class UDirectionalLightComponent;
class JsonObject;

/**
 * @brief 方向光 Actor。
 *
 * 场景中的光照配置入口：持有一个方向光组件，构造时把组件注册进 LightSystem，
 * 销毁时注销，保证渲染通道随时能取到"当前主光源"的配置。
 * 光照方向/颜色/强度/阴影参数均可通过 GetDirectionalLightComponent() 读写。
 *
 * 序列化：LoadFromConfig/SaveToConfig 负责与 JSON 配置文件（Editor/Config.json 的
 * DirectionalLight 段）之间读写上述参数，字段缺失时保留组件默认值。
 */
class ENGINE_API ADirectionalLightActor : public AActor {
	DECLARE_CLASS_TYPE(ADirectionalLightActor)

public:
	ADirectionalLightActor(const FString& Name);
	virtual ~ADirectionalLightActor() override;

	UDirectionalLightComponent* GetDirectionalLightComponent() const { return DirectionalLightComponent; }

	/** 从 JSON 配置读取光照参数并应用到组件；缺失字段保留组件当前值，无组件时返回 false */
	bool LoadFromConfig(const JsonObject& Config);
	/** 把组件当前的光照参数写回 JSON 配置对象；无组件时直接返回 */
	void SaveToConfig(JsonObject& Config) const;

protected:
	UDirectionalLightComponent* DirectionalLightComponent;
};
