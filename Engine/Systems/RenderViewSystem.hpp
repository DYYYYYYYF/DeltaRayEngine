#pragma once

#include "Defines.hpp"
#include "Math/MathTypes.hpp"
#include "Containers/TArray.hpp"
#include "Rendering/Interface/IRenderView.hpp"

class IRenderer;

struct SRenderViewSystemConfig {
	unsigned short max_view_count;
	FString config_path;
};

class RenderViewSystem {
public:
	static DAPI RenderViewSystem& Get();

public:
	IRenderView* Get(ERenderViewType Type);

	bool Initialize(IRenderer* renderer, SRenderViewSystemConfig config);
	void Shutdown();

	bool Create(const RenderViewConfig& config);
	void OnWindowResize(uint32_t width, uint32_t height);
	void RegenerateRendertargets(IRenderView* view);

private:
	bool LoadRenderviewConfig(const FString& path);

private:
	bool Initialized;
	IRenderer* Renderer;
	uint16_t MaxViewCount;

	TArray<IRenderView*> RegisteredViews;
	TMap<ERenderViewType, uint16_t> RegisteredViewMap;
};