#pragma once

#include "Viewport/ViewportClient.h"
#include "Render/Types/ViewTypes.h"
#include "Math/Matrix.h"

class UCameraComponent;
class UGizmoComponent;
class USceneComponent;
class USkeletalMeshComponent;
class FScene;
struct FInputFrame;
struct FSkeletalMesh;

struct FViewerSnapSettings
{
	bool  bEnableTranslationSnap = false;
	float TranslationSnapSize    = 0.1f;
	bool  bEnableRotationSnap    = false;
	float RotationSnapSize       = 15.0f; // degrees
	bool  bEnableScaleSnap       = false;
	float ScaleSnapSize          = 0.1f;
};

class FSkeletalMeshViewerViewportClient : public FViewportClient
{
public:
	FSkeletalMeshViewerViewportClient() = default;
	~FSkeletalMeshViewerViewportClient() override = default;

	void Initialize();
	void Shutdown();

	void Resize(uint32 Width, uint32 Height);
	void FrameMesh(const FSkeletalMesh* MeshAsset);
	void SetViewportType(ELevelViewportType NewType);

	UCameraComponent* GetCamera() const { return Camera; }
	UGizmoComponent* GetGizmo() const { return Gizmo; }

	FViewportRenderOptions& GetRenderOptions() { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const { return RenderOptions; }

	FViewerSnapSettings& GetSnapSettings() { return SnapSettings; }
	const FViewerSnapSettings& GetSnapSettings() const { return SnapSettings; }
	void ApplySnapSettingsToGizmo();

	// Gizmo가 등록될 Scene을 외부에서 지정 (PreviewWorld의 FScene)
	void SetGizmoScene(FScene* InScene);

	// 미리보기 메시를 트래킹 — bone proxy를 mesh component 자식으로 부착
	void SetTrackedMesh(USkeletalMeshComponent* InMesh);
	USkeletalMeshComponent* GetTrackedMesh() const { return TrackedMesh; }

	// Bone 선택 — proxy 동기화 + gizmo target 설정. -1이면 선택 해제.
	void SelectBone(int32 BoneIndex);
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }

	// 뷰포트 클릭 좌표 변환용 — 매 프레임 위젯이 갱신
	void SetViewportRect(float ScreenX, float ScreenY, float Width, float Height);

	// Picking 진단 로그 토글 — toolbar 버튼에서 제어
	bool IsLogPickingDiagnosticEnabled() const { return bLogPickingDiagnostic; }
	void SetLogPickingDiagnosticEnabled(bool bEnabled) { bLogPickingDiagnostic = bEnabled; }

	// Corner gizmo (우상단 오버레이) — viewport gizmo와 동일 본을 조작하는 2D 보조 컨트롤
	bool IsCornerGizmoHolding() const { return CornerActiveAxis >= 0; }
	void RenderCornerGizmoAndHandleInput();

	void Tick(float DeltaTime, bool bViewportHovered, bool bIsCapturing, FInputFrame& InputFrame);

private:
	void SyncProxyFromBone(int32 BoneIndex);
	void ApplyGizmoEditToBone();
	int32 ResolveBoneFromTriangle(const FSkeletalMesh* MeshAsset, int32 FaceIndex) const;

	bool IsMouseInCornerGizmoArea() const;
	void ApplyCornerGizmoDelta(int32 Axis, float Dx, float Dy);

private:
	UCameraComponent* Camera = nullptr;
	UGizmoComponent* Gizmo = nullptr;
	USceneComponent* BoneProxy = nullptr;
	USkeletalMeshComponent* TrackedMesh = nullptr;
	FScene* GizmoScene = nullptr;
	int32 SelectedBoneIndex = -1;

	FViewportRenderOptions RenderOptions;
	FViewerSnapSettings SnapSettings;

	float ViewportScreenX = 0.0f;
	float ViewportScreenY = 0.0f;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;

	bool bGizmoSceneRegistered = false;
	bool bLogPickingDiagnostic = false;

	// Corner gizmo state
	int32 CornerActiveAxis = -1;       // -1=none, 0=X, 1=Y, 2=Z, 3=Center(uniform)
	float CornerLastMouseX = 0.0f;
	float CornerLastMouseY = 0.0f;
};
