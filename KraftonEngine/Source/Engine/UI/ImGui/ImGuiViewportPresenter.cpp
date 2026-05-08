#include "Engine/UI/ImGui/ImGuiViewportPresenter.h"

#include "Viewport/Viewport.h"

void FImGuiViewportPresenter::DrawInCurrentWindow(
	FViewport* Viewport,
	const FViewportPresentationRect& Rect,
	const FImGuiViewportPresentOptions& Options)
{
	Draw(ImGui::GetWindowDrawList(), Viewport, Rect, Options);
}

void FImGuiViewportPresenter::DrawInMainBackground(
	FViewport* Viewport,
	const FViewportPresentationRect& Rect,
	const FImGuiViewportPresentOptions& Options)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	Draw(ImGui::GetBackgroundDrawList(MainViewport), Viewport, Rect, Options);
}

void FImGuiViewportPresenter::Draw(
	ImDrawList* DrawList,
	FViewport* Viewport,
	const FViewportPresentationRect& Rect,
	const FImGuiViewportPresentOptions& Options)
{
	if (!DrawList || !Viewport || !Viewport->GetSRV() || !Rect.IsValid())
	{
		return;
	}

	const ImVec2 Min(Rect.X, Rect.Y);
	const ImVec2 Max(Rect.Right(), Rect.Bottom());

	DrawList->AddImage(
		reinterpret_cast<ImTextureID>(Viewport->GetSRV()),
		Min,
		Max);

	if (Options.bDrawActiveBorder)
	{
		DrawList->AddRect(
			Min,
			Max,
			Options.ActiveBorderColor,
			0.0f,
			0,
			Options.ActiveBorderThickness);
	}
}
