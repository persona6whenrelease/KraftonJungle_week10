#pragma once

#include "Editor/UI/EditorWidget.h"
#include "GameFramework/World.h"

class UFBXSceneAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
class FViewport;

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	~FEditorSkeletalMeshViewerWidget() override;
	
	void Render(float DeltaTime) override;
	bool OpenFbxAsset(const FString& FbxPath);

private:
	void EnsurePreviewScene();
	void ReleasePreviewScene();
	void SetPreviewMesh(USkeletalMesh* PreviewMesh);
	
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

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	FViewport* PreviewViewport = nullptr;
};
