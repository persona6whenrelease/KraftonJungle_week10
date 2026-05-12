#pragma once

#include "Editor/UI/EditorWidget.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"

class UFBXSceneAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
class ADirectionalLightActor;
class FViewport;
class FSkeletalMeshViewerViewportClient;

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	~FEditorSkeletalMeshViewerWidget() override;
	
	void UpdateInput(float DeltaTime);
	void Render(float DeltaTime) override;
	bool OpenFbxAsset(const FString& FbxPath);
	bool WantsMouseCapture() const { return bPreviewViewportWantsMouseCapture; }
	bool WantsKeyboardCapture() const { return bPreviewViewportWantsKeyboardCapture; }

private:
	void EnsurePreviewScene();
	void ReleasePreviewScene();
	void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bResetCamera = true);
	void TickPreviewScene(float DeltaTime);
	
	void RenderResourcePanel();
	void RenderViewportPanel(float DeltaTime);
	void RenderBonePanel();
	void RenderTransformPanel();
	USkeletalMesh* GetSelectedSkeletalMesh() const;

	UFBXSceneAsset* CurrentSceneAsset = nullptr;
	USkeletalMesh* PreviewSkeletalMesh = nullptr;
	FString CurrentFbxPath;
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	ADirectionalLightActor* PreviewDirectionalLightActor = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	FViewport* PreviewViewport = nullptr;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = nullptr;
	ImVec2 PreviewViewportMin = ImVec2(0.0f, 0.0f);
	ImVec2 PreviewViewportMax = ImVec2(0.0f, 0.0f);
	bool bHasPreviewViewportRect = false;
	bool bPreviewViewportWantsMouseCapture = false;
	bool bPreviewViewportWantsKeyboardCapture = false;
};
