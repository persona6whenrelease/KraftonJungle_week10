#pragma once

#include "Core/CoreTypes.h"
#include "Viewport/ViewportPresentationTypes.h"
#include "ImGui/imgui.h"

class FViewport;

struct FImGuiViewportPresentOptions
{
	bool bDrawActiveBorder = false;
	ImU32 ActiveBorderColor = IM_COL32(255, 165, 0, 220);
	float ActiveBorderThickness = 2.0f;
};

class FImGuiViewportPresenter
{
public:
	static void DrawInCurrentWindow(
		FViewport* Viewport,
		const FViewportPresentationRect& Rect,
		const FImGuiViewportPresentOptions& Options = FImGuiViewportPresentOptions());

	static void DrawInMainBackground(
		FViewport* Viewport,
		const FViewportPresentationRect& Rect,
		const FImGuiViewportPresentOptions& Options = FImGuiViewportPresentOptions());

private:
	static void Draw(
		ImDrawList* DrawList,
		FViewport* Viewport,
		const FViewportPresentationRect& Rect,
		const FImGuiViewportPresentOptions& Options);
};
