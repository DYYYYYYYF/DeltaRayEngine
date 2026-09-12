#include "DirectionalLightComponent.h"
#include "Core/EngineLogger.hpp"

UDirectionalLightComponent::UDirectionalLightComponent(const FString& Name) : USceneComponent(Name) {
	// 默认值与引擎既有硬编码保持一致，未放置光源 Actor 时渲染结果不变
	Direction = Vector3(-0.57735f, -0.57735f, -0.57735f).Normalized();
	Color = Vector4(0.8f, 0.8f, 0.8f, 1.0f);
	Intensity = 1.0f;
	AmbientColor = Vector4(0.18f, 0.18f, 0.18f, 1.0f);

	bCastShadow = true;
	ShadowBias = 0.005f;
	ShadowDistance = 40.0f;
	ShadowOrthoExtent = 20.0f;
	ShadowOrthoNear = 0.1f;
	ShadowOrthoFar = 100.0f;
}

void UDirectionalLightComponent::SetDirection(const Vector3& InDirection) {
	// 零向量会导致归一化出现 NaN，此处直接忽略并给出告警
	const float LengthSq = InDirection.LengthSquared();
	if (LengthSq <= 0.0f) {
		GLOG(Log::eWarn, "DirectionalLightComponent::SetDirection: zero-length direction is ignored.");
		return;
	}

	Direction = InDirection.Normalized();
}

void UDirectionalLightComponent::SetIntensity(float InIntensity) {
	// 负强度会让光照变为"减光"，此处钳制到非负
	if (InIntensity < 0.0f) {
		GLOG(Log::eWarn, "DirectionalLightComponent::SetIntensity: negative intensity %.3f is clamped to 0.", InIntensity);
		Intensity = 0.0f;
		return;
	}

	Intensity = InIntensity;
}

void UDirectionalLightComponent::SetShadowBias(float InShadowBias) {
	if (InShadowBias < 0.0f) {
		GLOG(Log::eWarn, "DirectionalLightComponent::SetShadowBias: negative bias %.5f is clamped to 0.", InShadowBias);
		ShadowBias = 0.0f;
		return;
	}

	ShadowBias = InShadowBias;
}

void UDirectionalLightComponent::SetShadowDistance(float InShadowDistance) {
	// 拉远距离必须为正，否则光源位置会落到光锥背面、阴影整体失效
	if (InShadowDistance <= 0.0f) {
		GLOG(Log::eWarn, "DirectionalLightComponent::SetShadowDistance: non-positive distance %.3f is ignored.", InShadowDistance);
		return;
	}

	ShadowDistance = InShadowDistance;
}

void UDirectionalLightComponent::SetShadowOrthoExtent(float InShadowOrthoExtent) {
	if (InShadowOrthoExtent <= 0.0f) {
		GLOG(Log::eWarn, "DirectionalLightComponent::SetShadowOrthoExtent: non-positive extent %.3f is ignored.", InShadowOrthoExtent);
		return;
	}

	ShadowOrthoExtent = InShadowOrthoExtent;
}
