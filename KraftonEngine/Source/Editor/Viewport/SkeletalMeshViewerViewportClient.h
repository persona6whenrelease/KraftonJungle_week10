#pragma once

#include "Viewport/ViewportClient.h"
#include "Render/Types/ViewTypes.h"
#include "Selection/BoneSelectionManager.h"
class UCameraComponent;
class USkinnedMeshComponent;
struct FInputFrame;
struct FSkeletalMesh;
class UWorld;

class FSkeletalMeshViewerViewportClient : public FViewportClient
{
public:
	FSkeletalMeshViewerViewportClient() = default;
	~FSkeletalMeshViewerViewportClient() override = default;

	void Initialize();
	void Shutdown();

	void Resize(uint32 Width, uint32 Height);
	void SetViewportRect(float MinX, float MinY, float Width, float Height);
	void FrameMesh(const FSkeletalMesh* MeshAsset);
	void SetViewportType(ELevelViewportType NewType);

	UCameraComponent* GetCamera() const { return Camera; }

	FViewportRenderOptions& GetRenderOptions() { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const { return RenderOptions; }

	void Tick(float DeltaTime, bool bViewportHovered, bool bIsCapturing, FInputFrame& InputFrame);

	FBoneSelectionManager& GetBoneSelectionManager() { return BoneSelectionManager; }

	void SetPreviewWorld(UWorld* InWorld);

	void FocusBone(USkinnedMeshComponent* SkelMeshComp, int32 BoneIndex);

private:
	UCameraComponent* Camera = nullptr;
	FViewportRenderOptions RenderOptions;
	FBoneSelectionManager BoneSelectionManager;

	// Viewport 크기를 저장할 변수 추가 (Ray 계산용)
	float ViewportMinX = 0.0f;
	float ViewportMinY = 0.0f;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;
};
