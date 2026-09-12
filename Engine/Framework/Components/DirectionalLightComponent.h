#pragma once

#include "Framework/Components/SceneComponent.h"
#include "Math/MathTypes.hpp"

/**
 * @brief 方向光组件。
 *
 * 统一承载"光源本身"的数据：光照方向、颜色、强度、环境光以及阴影（光锥正交相机 + 偏置）参数。
 * 该组件只描述数据，不直接参与渲染调用：
 *  - 阴影通道（Shadow Pass）通过 LightSystem 取其中的方向/光锥参数计算 LightSpaceMatrix；
 *  - 延迟光照通道（Lighting Pass）通过帧数据把这些参数上传到 GPU 的全局 Uniform。
 *
 * 所有数值均提供与引擎既有行为一致的默认值，未在场景中放置光源 Actor 时渲染结果不变。
 */
class ENGINE_API UDirectionalLightComponent : public USceneComponent {
	DECLARE_CLASS_TYPE(UDirectionalLightComponent)

public:
	UDirectionalLightComponent(const FString& Name);

	//~ Begin 光照方向
	/** 设置光照方向（光源指向场景的方向），内部归一化后保存 */
	void SetDirection(const Vector3& InDirection);
	/** 取归一化后的光照方向 */
	const Vector3& GetDirection() const { return Direction; }
	//~ End 光照方向

	//~ Begin 颜色与强度
	void SetColor(const Vector4& InColor) { Color = InColor; }
	const Vector4& GetColor() const { return Color; }

	/** 强度（标量），上传时按 (I, I, I, I) 打包为 vec4 供着色器整体缩放 */
	void SetIntensity(float InIntensity);
	float GetIntensity() const { return Intensity; }
	//~ End 颜色与强度

	//~ Begin 环境光
	void SetAmbientColor(const Vector4& InAmbientColor) { AmbientColor = InAmbientColor; }
	const Vector4& GetAmbientColor() const { return AmbientColor; }
	//~ End 环境光

	//~ Begin 阴影配置
	/** 是否投射阴影；关闭时着色器侧的阴影强度为 0（阴影通道参数仍照常计算） */
	void SetCastShadow(bool bInCastShadow) { bCastShadow = bInCastShadow; }
	bool GetCastShadow() const { return bCastShadow; }
	/** 着色器采样阴影时使用的深度偏置，越大越不容易出现自阴影噪点（也更易出现漏光） */
	void SetShadowBias(float InShadowBias);
	float GetShadowBias() const { return ShadowBias; }
	/** 光源沿光照反方向的拉远距离，决定光锥相机到场景的距离 */
	void SetShadowDistance(float InShadowDistance);
	float GetShadowDistance() const { return ShadowDistance; }
	/** 光锥正交相机的半宽/半高，越大覆盖范围越大、阴影精度越低 */
	void SetShadowOrthoExtent(float InShadowOrthoExtent);
	float GetShadowOrthoExtent() const { return ShadowOrthoExtent; }
	/** 光锥正交相机近平面 */
	void SetShadowOrthoNear(float InShadowOrthoNear) { ShadowOrthoNear = InShadowOrthoNear; }
	float GetShadowOrthoNear() const { return ShadowOrthoNear; }
	/** 光锥正交相机远平面 */
	void SetShadowOrthoFar(float InShadowOrthoFar) { ShadowOrthoFar = InShadowOrthoFar; }
	float GetShadowOrthoFar() const { return ShadowOrthoFar; }
	/** 阴影强度：投射阴影为 1，否则为 0（供着色器在采样结果上衰减） */
	float GetShadowStrength() const { return bCastShadow ? 1.0f : 0.0f; }
	//~ End 阴影配置

protected:
	// 光照方向（已归一化，光源指向场景）
	Vector3 Direction;
	// 光源颜色
	Vector4 Color;
	// 强度
	float Intensity;
	// 环境光
	Vector4 AmbientColor;

	// 阴影开关
	bool bCastShadow;
	// 阴影深度偏置
	float ShadowBias;
	// 光锥相机拉远距离
	float ShadowDistance;
	// 光锥正交相机半宽/半高
	float ShadowOrthoExtent;
	// 光锥正交相机近平面
	float ShadowOrthoNear;
	// 光锥正交相机远平面
	float ShadowOrthoFar;
};
