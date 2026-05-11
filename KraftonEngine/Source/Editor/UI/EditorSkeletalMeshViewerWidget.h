#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	void Render(float DeltaTime) override;

private:
	void RenderResourcePanel();
	void RenderViewportPanel();
	void RenderBonePanel();
	void RenderTransformPanel();

	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;
};
