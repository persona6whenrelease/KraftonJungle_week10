#pragma once

#include "Editor/UI/EditorWidget.h"

class UFBXSceneAsset;
class USkeletalMesh;

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	void Render(float DeltaTime) override;
	bool OpenFbxAsset(const FString& FbxPath);

private:
	void RenderResourcePanel();
	void RenderViewportPanel();
	void RenderBonePanel();
	void RenderTransformPanel();
	USkeletalMesh* GetSelectedSkeletalMesh() const;

	UFBXSceneAsset* CurrentSceneAsset = nullptr;
	FString CurrentFbxPath;
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;
};
