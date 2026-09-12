#include "DirectionalLightActor.h"
#include "Core/EngineLogger.hpp"
#include "Framework/Components/DirectionalLightComponent.h"
#include "Platform/File/JsonObject.h"
#include "Systems/LightSystem.h"

ADirectionalLightActor::ADirectionalLightActor(const FString& Name) : AActor(Name) {
	DirectionalLightComponent = CreateComponent<UDirectionalLightComponent>("DirectionalLightComponent");
	if (DirectionalLightComponent) {
		SetRootComponent(DirectionalLightComponent);
		// 注册进光照系统，渲染通道据此读取光源配置
		LightSystem::Get().RegisterLight(DirectionalLightComponent);
	}
}

ADirectionalLightActor::~ADirectionalLightActor() {
	// 注销以避免渲染通道持有已销毁组件（LightSystem 内部只做指针比较，不会解引用悬空指针）
	LightSystem::Get().UnregisterLight(DirectionalLightComponent);
}

bool ADirectionalLightActor::LoadFromConfig(const JsonObject& Config) {
	if (!DirectionalLightComponent) {
		GLOG(Log::eWarn, "ADirectionalLightActor::LoadFromConfig: light component is null.");
		return false;
	}

	// 逐字段读取，配置中缺失的字段保留组件当前值，保证旧配置文件与新增字段兼容
	DirectionalLightComponent->SetDirection(
		Config.ReadVector3("DirectionalLight.Direction", DirectionalLightComponent->GetDirection()));
	DirectionalLightComponent->SetColor(
		Config.ReadVector4("DirectionalLight.Color", DirectionalLightComponent->GetColor()));
	DirectionalLightComponent->SetIntensity(
		Config.ReadFloat("DirectionalLight.Intensity", DirectionalLightComponent->GetIntensity()));
	DirectionalLightComponent->SetAmbientColor(
		Config.ReadVector4("DirectionalLight.AmbientColor", DirectionalLightComponent->GetAmbientColor()));
	DirectionalLightComponent->SetCastShadow(
		Config.ReadBool("DirectionalLight.CastShadow", DirectionalLightComponent->GetCastShadow()));
	DirectionalLightComponent->SetShadowBias(
		Config.ReadFloat("DirectionalLight.ShadowBias", DirectionalLightComponent->GetShadowBias()));
	DirectionalLightComponent->SetShadowDistance(
		Config.ReadFloat("DirectionalLight.ShadowDistance", DirectionalLightComponent->GetShadowDistance()));
	DirectionalLightComponent->SetShadowOrthoExtent(
		Config.ReadFloat("DirectionalLight.ShadowOrthoExtent", DirectionalLightComponent->GetShadowOrthoExtent()));
	DirectionalLightComponent->SetShadowOrthoNear(
		Config.ReadFloat("DirectionalLight.ShadowOrthoNear", DirectionalLightComponent->GetShadowOrthoNear()));
	DirectionalLightComponent->SetShadowOrthoFar(
		Config.ReadFloat("DirectionalLight.ShadowOrthoFar", DirectionalLightComponent->GetShadowOrthoFar()));

	GLOG(Log::eDebug, "Directional light config loaded (intensity=%.3f, cast_shadow=%d).",
		DirectionalLightComponent->GetIntensity(), DirectionalLightComponent->GetCastShadow() ? 1 : 0);
	return true;
}

void ADirectionalLightActor::SaveToConfig(JsonObject& Config) const {
	if (!DirectionalLightComponent) {
		return;
	}

	// 与 Camera 段一致：退出时把运行时最终值写回配置文件
	Config.WriteVector3("DirectionalLight.Direction", DirectionalLightComponent->GetDirection());
	Config.WriteVector4("DirectionalLight.Color", DirectionalLightComponent->GetColor());
	Config.WriteFloat("DirectionalLight.Intensity", DirectionalLightComponent->GetIntensity());
	Config.WriteVector4("DirectionalLight.AmbientColor", DirectionalLightComponent->GetAmbientColor());
	Config.WriteBool("DirectionalLight.CastShadow", DirectionalLightComponent->GetCastShadow());
	Config.WriteFloat("DirectionalLight.ShadowBias", DirectionalLightComponent->GetShadowBias());
	Config.WriteFloat("DirectionalLight.ShadowDistance", DirectionalLightComponent->GetShadowDistance());
	Config.WriteFloat("DirectionalLight.ShadowOrthoExtent", DirectionalLightComponent->GetShadowOrthoExtent());
	Config.WriteFloat("DirectionalLight.ShadowOrthoNear", DirectionalLightComponent->GetShadowOrthoNear());
	Config.WriteFloat("DirectionalLight.ShadowOrthoFar", DirectionalLightComponent->GetShadowOrthoFar());
}
