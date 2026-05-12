#include "SkeletalMeshViewerViewportClient.h"

#include "Object/Object.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Math/Transform.h"
#include "Collision/RayUtils.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "Render/Scene/FScene.h"
#include "ImGui/imgui.h"
#include "Core/Log.h"

#include <unordered_map>

namespace
{
	// Matrix → relative transform components 로 분해해 USceneComponent에 적용.
	// Scale은 항상 (1,1,1)로 고정 — 원본 매트릭스에 비균일 스케일/시어가 있을 때 ToRotator()의
	// GetEuler() clamp 버그로 회전 추출이 깨지는 것을 차단. (Local/Scale 모드 정확도 보장)
	void ApplyMatrixToRelativeTransform(USceneComponent* Component, const FMatrix& Matrix)
	{
		if (!Component)
		{
			return;
		}

		Component->SetRelativeScale(FVector(1.0f, 1.0f, 1.0f));

		// 각 행을 정규화해 직교 회전 행렬에 가깝게 만든 뒤 quaternion 추출.
		FMatrix Rotation = FMatrix::Identity;
		auto NormalizeRow = [](float A, float B, float C, float& OA, float& OB, float& OC)
		{
			const float Len = std::sqrt(A * A + B * B + C * C);
			if (Len > 1e-6f)
			{
				const float Inv = 1.0f / Len;
				OA = A * Inv; OB = B * Inv; OC = C * Inv;
			}
			else
			{
				OA = 0.0f; OB = 0.0f; OC = 0.0f;
			}
		};
		NormalizeRow(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2], Rotation.M[0][0], Rotation.M[0][1], Rotation.M[0][2]);
		NormalizeRow(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2], Rotation.M[1][0], Rotation.M[1][1], Rotation.M[1][2]);
		NormalizeRow(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2], Rotation.M[2][0], Rotation.M[2][1], Rotation.M[2][2]);
		Component->SetRelativeRotation(Rotation.ToQuat());

		Component->SetRelativeLocation(Matrix.GetLocation());
	}
}

void FSkeletalMeshViewerViewportClient::Initialize()
{
	if (Camera)
	{
		return;
	}

	Camera = UObjectManager::Get().CreateObject<UCameraComponent>();
	Camera->SetOrthographic(false);
	Camera->SetFOV(45.0f);
	Camera->SetNearPlane(0.01f);
	Camera->SetFarPlane(100000.0f);

	RenderOptions.ViewportType = ELevelViewportType::Perspective;
	RenderOptions.ShowFlags.bGrid = false;
	RenderOptions.ShowFlags.bGizmo = true;
	RenderOptions.ShowFlags.bWorldAxis = false;
	RenderOptions.ShowFlags.bBoundingVolume = false;
	RenderOptions.ShowFlags.bCollisionShapes = false;

	Camera->SetWorldLocation(FVector(5.0f, 0.0f, 2.0f));
	Camera->LookAt(FVector::ZeroVector);

	Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
	Gizmo->SetVisibility(false);
}

void FSkeletalMeshViewerViewportClient::Shutdown()
{
	if (Gizmo)
	{
		Gizmo->DestroyRenderState();
		UObjectManager::Get().DestroyObject(Gizmo);
		Gizmo = nullptr;
	}

	if (BoneProxy)
	{
		UObjectManager::Get().DestroyObject(BoneProxy);
		BoneProxy = nullptr;
	}

	if (Camera)
	{
		UObjectManager::Get().DestroyObject(Camera);
		Camera = nullptr;
	}

	TrackedMesh = nullptr;
	GizmoScene = nullptr;
	bGizmoSceneRegistered = false;
	SelectedBoneIndex = -1;
}

void FSkeletalMeshViewerViewportClient::SetGizmoScene(FScene* InScene)
{
	if (!Gizmo || GizmoScene == InScene)
	{
		return;
	}

	if (bGizmoSceneRegistered && GizmoScene)
	{
		Gizmo->DestroyRenderState();
		bGizmoSceneRegistered = false;
	}

	GizmoScene = InScene;
	Gizmo->SetScene(GizmoScene);

	if (GizmoScene)
	{
		Gizmo->CreateRenderState();
		bGizmoSceneRegistered = true;
	}
}

void FSkeletalMeshViewerViewportClient::SetTrackedMesh(USkeletalMeshComponent* InMesh)
{
	if (TrackedMesh == InMesh)
	{
		return;
	}

	TrackedMesh = InMesh;
	SelectBone(-1);

	if (!TrackedMesh)
	{
		return;
	}

	if (!BoneProxy)
	{
		BoneProxy = UObjectManager::Get().CreateObject<USceneComponent>();
	}
	BoneProxy->AttachToComponent(TrackedMesh);
}

void FSkeletalMeshViewerViewportClient::SelectBone(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;

	if (!Gizmo)
	{
		return;
	}

	if (BoneIndex < 0 || !TrackedMesh || !BoneProxy)
	{
		Gizmo->SetTarget(static_cast<AActor*>(nullptr));
		Gizmo->SetVisibility(false);
		return;
	}

	SyncProxyFromBone(BoneIndex);
	Gizmo->SetTarget(BoneProxy);
}

void FSkeletalMeshViewerViewportClient::SyncProxyFromBone(int32 BoneIndex)
{
	if (!BoneProxy || !TrackedMesh || BoneIndex < 0)
	{
		return;
	}

	const TArray<FMatrix>& MeshSpace = TrackedMesh->GetMeshSpaceBoneMatrices();
	if (BoneIndex >= static_cast<int32>(MeshSpace.size()))
	{
		return;
	}

	ApplyMatrixToRelativeTransform(BoneProxy, MeshSpace[BoneIndex]);
}

void FSkeletalMeshViewerViewportClient::ApplyGizmoEditToBone()
{
	if (!BoneProxy || !TrackedMesh || SelectedBoneIndex < 0)
	{
		return;
	}

	USkeletalMesh* Mesh = TrackedMesh->GetSkeletalMesh();
	const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset || SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
	{
		return;
	}

	const int32 ParentIndex = MeshAsset->Bones[SelectedBoneIndex].ParentIndex;
	const FMatrix NewMeshSpace = BoneProxy->GetRelativeMatrix();

	FMatrix NewLocalPose;
	if (ParentIndex < 0)
	{
		NewLocalPose = NewMeshSpace;
	}
	else
	{
		const TArray<FMatrix>& MeshSpace = TrackedMesh->GetMeshSpaceBoneMatrices();
		if (ParentIndex >= static_cast<int32>(MeshSpace.size()))
		{
			return;
		}
		NewLocalPose = NewMeshSpace * MeshSpace[ParentIndex].GetInverse();
	}

	TrackedMesh->SetBoneLocalPose(SelectedBoneIndex, NewLocalPose);
}

int32 FSkeletalMeshViewerViewportClient::ResolveBoneFromTriangle(const FSkeletalMesh* MeshAsset, int32 FaceIndex) const
{
	if (!MeshAsset || FaceIndex < 0)
	{
		return -1;
	}

	const int32 IndexCount = static_cast<int32>(MeshAsset->Indices.size());
	if (FaceIndex + 2 >= IndexCount)
	{
		return -1;
	}

	const uint32 VIdx[3] = {
		MeshAsset->Indices[FaceIndex],
		MeshAsset->Indices[FaceIndex + 1],
		MeshAsset->Indices[FaceIndex + 2],
	};

	std::unordered_map<uint32, float> WeightByBone;
	for (uint32 VI : VIdx)
	{
		if (VI >= MeshAsset->Vertices.size()) continue;
		const FSkeletalVertex& V = MeshAsset->Vertices[VI];
		for (int32 j = 0; j < 4; ++j)
		{
			if (V.BoneWeights[j] > 0.0f)
			{
				WeightByBone[V.BoneIDs[j]] += V.BoneWeights[j];
			}
		}
	}

	int32 BestBone = -1;
	float BestWeight = -1.0f;
	for (const auto& Pair : WeightByBone)
	{
		if (Pair.second > BestWeight)
		{
			BestWeight = Pair.second;
			BestBone = static_cast<int32>(Pair.first);
		}
	}
	return BestBone;
}

void FSkeletalMeshViewerViewportClient::ApplySnapSettingsToGizmo()
{
	if (!Gizmo)
	{
		return;
	}

	Gizmo->SetSnapSettings(
		SnapSettings.bEnableTranslationSnap, SnapSettings.TranslationSnapSize,
		SnapSettings.bEnableRotationSnap, SnapSettings.RotationSnapSize,
		SnapSettings.bEnableScaleSnap, SnapSettings.ScaleSnapSize);
}

void FSkeletalMeshViewerViewportClient::SetViewportRect(float ScreenX, float ScreenY, float Width, float Height)
{
	ViewportScreenX = ScreenX;
	ViewportScreenY = ScreenY;
	ViewportWidth = Width;
	ViewportHeight = Height;
}

void FSkeletalMeshViewerViewportClient::Resize(uint32 Width, uint32 Height)
{
	if (!Camera)
	{
		return;
	}

	Camera->OnResize(static_cast<int32>(Width), static_cast<int32>(Height));
}

void FSkeletalMeshViewerViewportClient::FrameMesh(const FSkeletalMesh* MeshAsset)
{
	if (!Camera || !MeshAsset)
	{
		return;
	}

	FVector Extent = MeshAsset->BoundsExtent;
	float Radius = Extent.Length();
	Radius = (std::max)(Radius, 1.0f);

	const FVector Target = FVector::ZeroVector;
	const float Distance = Radius * 2.5f;
	Camera->SetWorldLocation(Target + FVector(Distance, 0.0f, Distance * 0.35f));
	Camera->LookAt(Target);
}

void FSkeletalMeshViewerViewportClient::SetViewportType(ELevelViewportType NewType)
{
	if (!Camera)
	{
		return;
	}

	RenderOptions.ViewportType = NewType;

	if (NewType == ELevelViewportType::Perspective)
	{
		Camera->SetOrthographic(false);
		return;
	}

	Camera->SetOrthographic(true);

	if (NewType == ELevelViewportType::FreeOrthographic)
	{
		return;
	}

	constexpr float OrthoDistance = 50.0f;
	auto Position = FVector(0, 0, 0);
	auto Rotation = FVector(0, 0, 0);

	switch (NewType)
	{
	case ELevelViewportType::Top:
		Position = FVector(0, 0, OrthoDistance);
		Rotation = FVector(0, 90.0f, 0);
		break;
	case ELevelViewportType::Bottom:
		Position = FVector(0, 0, -OrthoDistance);
		Rotation = FVector(0, -90.0f, 0);
		break;
	case ELevelViewportType::Front:
		Position = FVector(OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 180.0f);
		break;
	case ELevelViewportType::Back:
		Position = FVector(-OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 0.0f);
		break;
	case ELevelViewportType::Left:
		Position = FVector(0, -OrthoDistance, 0);
		Rotation = FVector(0, 0, 90.0f);
		break;
	case ELevelViewportType::Right:
		Position = FVector(0, OrthoDistance, 0);
		Rotation = FVector(0, 0, -90.0f);
		break;
	default:
		break;
	}

	Camera->SetRelativeLocation(Position);
	Camera->SetRelativeRotation(Rotation);
}

void FSkeletalMeshViewerViewportClient::Tick(
	float DeltaTime,
	bool bViewportHovered,
	bool bIsCapturing,
	FInputFrame& InputFrame)
{
	if (!Camera)
	{
		return;
	}

	// Fix C — DeprojectScreenToWorld가 사용하는 ScreenWidth/Height와
	// Camera 내부 AspectRatio를 매 프레임 동기화. RT 리사이즈(RequestResize→ApplyPendingResize)는
	// Render 단계로 미뤄지므로, 입력 처리 시점에 projection이 어긋나지 않도록 여기서 미리 갱신한다.
	if (ViewportWidth > 0.0f && ViewportHeight > 0.0f)
	{
		Camera->OnResize(
			static_cast<int32>(ViewportWidth),
			static_cast<int32>(ViewportHeight));
	}

	const float MoveSpeed =
		(ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
			? 35.0f
			: 10.0f;
	const float RotateSpeed = 0.15f;
	const float PanSpeed = 0.015f;
	const float ZoomSpeed = 0.35f;

	// Wheel zoom은 hover만으로 허용 — 직관적인 UX.
	const float ScrollNotches = InputFrame.GetScrollNotches();
	if (bViewportHovered && ScrollNotches != 0.0f)
	{
		Camera->SetWorldLocation(
			Camera->GetWorldLocation() + Camera->GetForwardVector() * ScrollNotches * ZoomSpeed);
		InputFrame.ConsumeScroll("SkeletalMeshViewer", "Preview zoom");
	}

	// Anim ON 동안 gizmo 조작은 무효 — 매 Tick에서 anim이 LocalBonePose를 덮어쓰므로 드래그 결과가 사라진다.
	const bool bAnimOn = TrackedMesh && TrackedMesh->IsDebugRandomBoneAnimEnabled();
	if (bAnimOn && Gizmo)
	{
		// anim이 켜진 직후 진행 중이던 드래그/픽킹 상태를 즉시 정리해 dangling hold 방지.
		if (Gizmo->IsHolding())
		{
			Gizmo->DragEnd();
		}
		Gizmo->SetPressedOnHandle(false);
	}

	// Fix B — 입력 처리 전에 proxy ← bone 동기화 + gizmo location 갱신.
	// 이렇게 해야 RaycastComponent가 사용하는 gizmo의 world location/scale이 최신 상태가 된다.
	// 드래그 중(IsHolding)에는 gizmo가 proxy를 수정 중이므로 동기화를 스킵 — 피드백 루프 방지.
	if (Gizmo && !Gizmo->IsHolding() && SelectedBoneIndex >= 0)
	{
		SyncProxyFromBone(SelectedBoneIndex);
		Gizmo->UpdateGizmoTransform();
	}

	// 카메라 기준 화면 스케일과 axis mask는 매 프레임 갱신 — anim ON/캡쳐 여부와 무관.
	if (Gizmo)
	{
		Gizmo->ApplyScreenSpaceScaling(
			Camera->GetWorldLocation(),
			Camera->IsOrthogonal(),
			Camera->GetOrthoWidth());
		Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(
			RenderOptions.ViewportType, Gizmo->GetMode()));
	}

	// Gizmo interaction (LMB) — RMB/MMB 카메라 캡쳐 중, Anim ON 동안, Corner gizmo 드래그 중에는 비활성.
	// 또한 마우스가 corner gizmo 영역(우상단)에 있으면 viewport gizmo 입력 건너뜀 — corner gizmo가 처리.
	// Gizmo가 hold 중인 경우 그 조건들을 우회해 드래그 종료까지 유지.
	const bool bMouseInCornerArea = IsMouseInCornerGizmoArea();
	const bool bGizmoInputAllowed = !bAnimOn && !IsCornerGizmoHolding()
		&& ((bViewportHovered && !bIsCapturing && !bMouseInCornerArea) || (Gizmo && Gizmo->IsHolding()));
	if (Gizmo && bGizmoInputAllowed)
	{
		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		const float LocalMouseX = MousePos.x - ViewportScreenX;
		const float LocalMouseY = MousePos.y - ViewportScreenY;
		const float VPW = ViewportWidth > 0.0f ? ViewportWidth : 1.0f;
		const float VPH = ViewportHeight > 0.0f ? ViewportHeight : 1.0f;

		const FRay Ray = Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPW, VPH);

		if (InputFrame.WasPressed(VK_LBUTTON))
		{
			// ── Picking 진단 로그 (토글) ─────────────────────────────
			if (bLogPickingDiagnostic)
			{
				const FVector CamLoc = Camera->GetWorldLocation();
				const FVector RayOrigin = Ray.Origin;
				const FVector RayDir = Ray.Direction;
				UE_LOG("[SkelViewerPick] === LMB Press ===");
				UE_LOG("[SkelViewerPick] Mouse(screen)=(%.1f, %.1f) Local=(%.1f, %.1f) Rect=(X=%.1f Y=%.1f W=%.1f H=%.1f)",
					MousePos.x, MousePos.y, LocalMouseX, LocalMouseY,
					ViewportScreenX, ViewportScreenY, ViewportWidth, ViewportHeight);
				UE_LOG("[SkelViewerPick] Camera Loc=(%.3f, %.3f, %.3f) FOV=%.2f Aspect=%.3f Ortho=%d",
					CamLoc.X, CamLoc.Y, CamLoc.Z,
					Camera->GetFOV(),
					(ViewportHeight > 0.0f ? ViewportWidth / ViewportHeight : 0.0f),
					Camera->IsOrthogonal() ? 1 : 0);
				UE_LOG("[SkelViewerPick] Ray Origin=(%.3f, %.3f, %.3f) Dir=(%.3f, %.3f, %.3f)",
					RayOrigin.X, RayOrigin.Y, RayOrigin.Z,
					RayDir.X, RayDir.Y, RayDir.Z);

				if (Gizmo->HasTarget())
				{
					const FVector GizmoLoc = Gizmo->GetWorldLocation();
					const FVector GizmoScale = Gizmo->GetWorldScale();
					UE_LOG("[SkelViewerPick] Gizmo WorldLoc=(%.3f, %.3f, %.3f) WorldScale=(%.3f, %.3f, %.3f) Mode=%d WorldSpace=%d",
						GizmoLoc.X, GizmoLoc.Y, GizmoLoc.Z,
						GizmoScale.X, GizmoScale.Y, GizmoScale.Z,
						static_cast<int32>(Gizmo->GetMode()),
						Gizmo->IsWorldSpace() ? 1 : 0);
				}
				else
				{
					UE_LOG("[SkelViewerPick] Gizmo has no target");
				}

				if (BoneProxy)
				{
					const FVector ProxyLoc = BoneProxy->GetWorldLocation();
					const FVector ProxyRelLoc = BoneProxy->GetRelativeLocation();
					const FVector ProxyRelScale = BoneProxy->GetRelativeScale();
					UE_LOG("[SkelViewerPick] Proxy WorldLoc=(%.3f, %.3f, %.3f) RelLoc=(%.3f, %.3f, %.3f) RelScale=(%.3f, %.3f, %.3f)",
						ProxyLoc.X, ProxyLoc.Y, ProxyLoc.Z,
						ProxyRelLoc.X, ProxyRelLoc.Y, ProxyRelLoc.Z,
						ProxyRelScale.X, ProxyRelScale.Y, ProxyRelScale.Z);
				}

				if (TrackedMesh)
				{
					const FVector MeshLoc = TrackedMesh->GetWorldLocation();
					const FVector MeshRelLoc = TrackedMesh->GetRelativeLocation();
					UE_LOG("[SkelViewerPick] PreviewMesh WorldLoc=(%.3f, %.3f, %.3f) RelLoc=(%.3f, %.3f, %.3f)",
						MeshLoc.X, MeshLoc.Y, MeshLoc.Z,
						MeshRelLoc.X, MeshRelLoc.Y, MeshRelLoc.Z);

					if (SelectedBoneIndex >= 0)
					{
						const TArray<FMatrix>& MeshSpace = TrackedMesh->GetMeshSpaceBoneMatrices();
						if (SelectedBoneIndex < static_cast<int32>(MeshSpace.size()))
						{
							const FVector BoneMS = MeshSpace[SelectedBoneIndex].GetLocation();
							UE_LOG("[SkelViewerPick] SelectedBone Idx=%d MeshSpaceLoc=(%.3f, %.3f, %.3f)",
								SelectedBoneIndex, BoneMS.X, BoneMS.Y, BoneMS.Z);
						}
					}
					else
					{
						UE_LOG("[SkelViewerPick] SelectedBoneIndex=%d (none)", SelectedBoneIndex);
					}
				}
			}
			// ── Picking 진단 로그 끝 ────────────────────────────────

			// Gizmo 핸들 raycast 먼저
			FHitResult GizmoHit;
			if (Gizmo->HasTarget() && FRayUtils::RaycastComponent(Gizmo, Ray, GizmoHit))
			{
				if (bLogPickingDiagnostic)
				{
					UE_LOG("[SkelViewerPick] Gizmo HIT axis=%d dist=%.3f", Gizmo->GetSelectedAxis(), GizmoHit.Distance);
				}
				Gizmo->SetPressedOnHandle(true);
				InputFrame.ConsumeMouseButtons("SkeletalMeshViewerGizmo", "Begin gizmo drag");
			}
			else if (TrackedMesh)
			{
				// Fallback 정책 (Phase 3, A + B 결합):
				// - Bone 미선택 시: 항상 mesh raycast → 초기 bone 선택 진입
				// - Bone 선택 + 클릭이 화면상 gizmo 근처(deadzone): mesh raycast 차단 → 선택 유지
				// - Bone 선택 + 클릭이 멀리: mesh raycast로 다른 bone 명시적 전환 허용
				bool bSuppressFallback = false;
				if (SelectedBoneIndex >= 0 && Gizmo->HasTarget())
				{
					const FMatrix VP = Camera->GetViewMatrix() * Camera->GetProjectionMatrix();
					const FVector Clip = VP.TransformPositionWithW(Gizmo->GetWorldLocation());
					if (Clip.X >= -1.5f && Clip.X <= 1.5f && Clip.Y >= -1.5f && Clip.Y <= 1.5f)
					{
						const float GizmoSx = (Clip.X * 0.5f + 0.5f) * VPW;
						const float GizmoSy = (1.0f - (Clip.Y * 0.5f + 0.5f)) * VPH;
						const float Dx = LocalMouseX - GizmoSx;
						const float Dy = LocalMouseY - GizmoSy;
						constexpr float DeadzonePx = 100.0f;
						if ((Dx * Dx + Dy * Dy) < (DeadzonePx * DeadzonePx))
						{
							bSuppressFallback = true;
							if (bLogPickingDiagnostic)
							{
								UE_LOG("[SkelViewerPick] Gizmo MISS within deadzone (%.1f, %.1f off-center) — suppress fallback",
									Dx, Dy);
							}
						}
					}
				}

				if (!bSuppressFallback)
				{
					if (bLogPickingDiagnostic)
					{
						UE_LOG("[SkelViewerPick] Gizmo MISS - trying mesh raycast");
					}
					USkeletalMesh* SkelMesh = TrackedMesh->GetSkeletalMesh();
					const FSkeletalMesh* MeshAsset = SkelMesh ? SkelMesh->GetSkeletalMeshAsset() : nullptr;
					if (MeshAsset && !MeshAsset->Indices.empty() && !MeshAsset->Vertices.empty())
					{
						FHitResult MeshHit;
						const FMatrix& World = TrackedMesh->GetWorldMatrix();
						const FMatrix& InvWorld = TrackedMesh->GetWorldInverseMatrix();
						const bool bHit = FRayUtils::RaycastTriangles(
							Ray, World, InvWorld,
							&MeshAsset->Vertices[0].pos,
							static_cast<uint32>(sizeof(FSkeletalVertex)),
							MeshAsset->Indices,
							MeshHit);
						if (bHit)
						{
							const int32 BoneIdx = ResolveBoneFromTriangle(MeshAsset, MeshHit.FaceIndex);
							if (bLogPickingDiagnostic)
							{
								UE_LOG("[SkelViewerPick] Mesh HIT FaceIdx=%d dist=%.3f -> BoneIdx=%d",
									MeshHit.FaceIndex, MeshHit.Distance, BoneIdx);
							}
							if (BoneIdx >= 0)
							{
								SelectBone(BoneIdx);
								InputFrame.ConsumeMouseButtons("SkeletalMeshViewerGizmo", "Bone pick via raycast");
							}
						}
						else if (bLogPickingDiagnostic)
						{
							UE_LOG("[SkelViewerPick] Mesh MISS");
						}
					}
				}
			}
			else if (bLogPickingDiagnostic)
			{
				UE_LOG("[SkelViewerPick] Gizmo MISS (no TrackedMesh for fallback)");
			}
		}
		else if (InputFrame.IsLeftDragging())
		{
			if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
			{
				Gizmo->SetHolding(true);
			}
			if (Gizmo->IsHolding())
			{
				Gizmo->UpdateDrag(
					Ray,
					Camera->GetForwardVector(),
					Camera->GetRightVector(),
					Camera->GetUpVector());
				ApplyGizmoEditToBone();
				InputFrame.ConsumeMouse("SkeletalMeshViewerGizmo", "Update gizmo drag");
			}
		}
		else if (InputFrame.WasLeftDragEnded())
		{
			if (Gizmo->IsHolding())
			{
				Gizmo->DragEnd();
				// 드래그 종료 후 proxy ← bone 재동기화 (drift 방지)
				SyncProxyFromBone(SelectedBoneIndex);
				InputFrame.ConsumeMouse("SkeletalMeshViewerGizmo", "End gizmo drag");
			}
		}
		else if (InputFrame.WasReleased(VK_LBUTTON))
		{
			Gizmo->SetPressedOnHandle(false);
			InputFrame.ConsumeKey(VK_LBUTTON, "SkeletalMeshViewerGizmo", "Release LMB");
		}
	}

	// Drag/rotate/pan/WASD는 preview에서 명시적으로 클릭한 capture 상태에서만 동작.
	if (!bIsCapturing)
	{
		return;
	}

	const bool bRightMouseDown =
		InputFrame.IsDown(VK_RBUTTON) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bMiddleMouseDown =
		InputFrame.IsDown(VK_MBUTTON) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);

	if (bRightMouseDown)
	{
		const float DeltaX = static_cast<float>(InputFrame.GetMouseDeltaX());
		const float DeltaY = static_cast<float>(InputFrame.GetMouseDeltaY());

		if (DeltaX != 0.0f || DeltaY != 0.0f)
		{
			Camera->Rotate(DeltaX * RotateSpeed, DeltaY * RotateSpeed);
			InputFrame.ConsumeLook("SkeletalMeshViewer", "Preview camera rotate");
		}
		InputFrame.ConsumeKey(VK_RBUTTON, "SkeletalMeshViewer", "Preview camera rotate");

		FVector MoveDelta = FVector::ZeroVector;

		if (ImGui::IsKeyDown(ImGuiKey_W))
		{
			MoveDelta += Camera->GetForwardVector();
		}
		if (ImGui::IsKeyDown(ImGuiKey_S))
		{
			MoveDelta -= Camera->GetForwardVector();
		}
		if (ImGui::IsKeyDown(ImGuiKey_D))
		{
			MoveDelta += Camera->GetRightVector();
		}
		if (ImGui::IsKeyDown(ImGuiKey_A))
		{
			MoveDelta -= Camera->GetRightVector();
		}
		if (ImGui::IsKeyDown(ImGuiKey_E))
		{
			MoveDelta += FVector::UpVector;
		}
		if (ImGui::IsKeyDown(ImGuiKey_Q))
		{
			MoveDelta -= FVector::UpVector;
		}

		if (!MoveDelta.IsNearlyZero())
		{
			MoveDelta.Normalize();
			Camera->SetWorldLocation(
				Camera->GetWorldLocation() + MoveDelta * MoveSpeed * DeltaTime);
			InputFrame.ConsumeMovement("SkeletalMeshViewer", "Preview camera movement");
		}
	}

	if (bMiddleMouseDown)
	{
		const float DeltaX = static_cast<float>(InputFrame.GetMouseDeltaX());
		const float DeltaY = static_cast<float>(InputFrame.GetMouseDeltaY());

		if (DeltaX != 0.0f || DeltaY != 0.0f)
		{
			const FVector PanDelta =
				Camera->GetRightVector() * (-DeltaX * PanSpeed) +
				Camera->GetUpVector() * (DeltaY * PanSpeed);

			Camera->SetWorldLocation(Camera->GetWorldLocation() + PanDelta);
			InputFrame.ConsumeMouseDelta("SkeletalMeshViewer", "Preview camera pan");
		}
		InputFrame.ConsumeKey(VK_MBUTTON, "SkeletalMeshViewer", "Preview camera pan");
	}
}

// ============================================================
// Corner Gizmo Overlay — 우상단 2D 스크린-스페이스 gizmo
// 메인 viewport gizmo와 동일한 본을 조작. 카메라 거리 영향 0, 큰 hit 영역.
// ============================================================
namespace
{
	constexpr float CornerGizmoSize = 150.0f;     // 영역 한 변
	constexpr float CornerGizmoPaddingX = 20.0f;
	constexpr float CornerGizmoPaddingY = 20.0f;
	constexpr float CornerAxisLength = 55.0f;     // 핸들 길이
	constexpr float CornerArrowHeadSize = 12.0f;
	constexpr float CornerStemThickness = 3.5f;
	constexpr float CornerHitRadius = 14.0f;      // 픽셀 hit 반경
	constexpr float CornerCenterSize = 8.0f;      // 중앙 정사각형/원 반경
	constexpr float CornerRingRadius = 50.0f;     // Rotate 모드 링 반경
	constexpr float CornerRingHitTol = 8.0f;      // Rotate hit 허용 반경 (±)

	constexpr float TranslateSensitivity = 0.005f; // unit / pixel
	constexpr float RotateSensitivityDeg = 0.5f;   // degree / pixel
	constexpr float ScaleSensitivity = 0.005f;     // factor / pixel

	// 스크린상 축 방향 (Y down). 시각적 구분을 위해 등각풍 배치.
	struct FAxisScreenDir { float x; float y; };
	const FAxisScreenDir AxisScreenDirs[3] = {
		{ 1.0f, 0.0f },                // X(red): 우
		{ -0.7071f, 0.7071f },         // Y(green): 좌하 (depth 느낌)
		{ 0.0f, -1.0f },               // Z(blue): 상
	};

	ImU32 AxisColor(int32 Axis, bool bActive)
	{
		if (bActive) return IM_COL32(255, 230, 80, 255);
		switch (Axis)
		{
		case 0: return IM_COL32(230, 70, 70, 255);   // X red
		case 1: return IM_COL32(70, 220, 70, 255);   // Y green
		case 2: return IM_COL32(70, 100, 230, 255);  // Z blue
		default: return IM_COL32(200, 200, 200, 255);
		}
	}

	void DrawArrow(ImDrawList* DL, ImVec2 Center, int32 Axis, bool bActive)
	{
		const FAxisScreenDir& D = AxisScreenDirs[Axis];
		ImVec2 Tip = ImVec2(Center.x + D.x * CornerAxisLength, Center.y + D.y * CornerAxisLength);
		const ImU32 Col = AxisColor(Axis, bActive);
		// Stem
		DL->AddLine(Center, Tip, Col, CornerStemThickness);
		// Arrowhead — 삼각형
		const float Px = -D.y; // 수직
		const float Py = D.x;
		const ImVec2 Base = ImVec2(Tip.x - D.x * CornerArrowHeadSize, Tip.y - D.y * CornerArrowHeadSize);
		const ImVec2 L = ImVec2(Base.x + Px * (CornerArrowHeadSize * 0.45f), Base.y + Py * (CornerArrowHeadSize * 0.45f));
		const ImVec2 R = ImVec2(Base.x - Px * (CornerArrowHeadSize * 0.45f), Base.y - Py * (CornerArrowHeadSize * 0.45f));
		DL->AddTriangleFilled(Tip, L, R, Col);
	}

	void DrawScaleHandle(ImDrawList* DL, ImVec2 Center, int32 Axis, bool bActive)
	{
		const FAxisScreenDir& D = AxisScreenDirs[Axis];
		ImVec2 Tip = ImVec2(Center.x + D.x * CornerAxisLength, Center.y + D.y * CornerAxisLength);
		const ImU32 Col = AxisColor(Axis, bActive);
		DL->AddLine(Center, Tip, Col, CornerStemThickness);
		// Square cap
		const float Half = CornerArrowHeadSize * 0.4f;
		DL->AddRectFilled(
			ImVec2(Tip.x - Half, Tip.y - Half),
			ImVec2(Tip.x + Half, Tip.y + Half),
			Col);
	}

	void DrawRotateRing(ImDrawList* DL, ImVec2 Center, int32 Axis, bool bActive)
	{
		const ImU32 Col = AxisColor(Axis, bActive);
		// 3색 링을 같은 위치에 중첩하지 않도록 살짝 다른 반경 사용
		const float R = CornerRingRadius - Axis * 4.0f;
		DL->AddCircle(Center, R, Col, 64, 2.5f);
	}

	float ScreenProjection(float Dx, float Dy, int32 Axis)
	{
		const FAxisScreenDir& D = AxisScreenDirs[Axis];
		return Dx * D.x + Dy * D.y;
	}

	int32 HitTestAxisLine(ImVec2 MouseLocal, ImVec2 Center, int32 Axis)
	{
		const FAxisScreenDir& D = AxisScreenDirs[Axis];
		ImVec2 Tip = ImVec2(Center.x + D.x * CornerAxisLength, Center.y + D.y * CornerAxisLength);
		// 점-선분 거리 — clamp t∈[0,1]
		const float Vx = Tip.x - Center.x;
		const float Vy = Tip.y - Center.y;
		const float Wx = MouseLocal.x - Center.x;
		const float Wy = MouseLocal.y - Center.y;
		const float Len2 = Vx * Vx + Vy * Vy;
		float T = Len2 > 0.0f ? (Wx * Vx + Wy * Vy) / Len2 : 0.0f;
		if (T < 0.0f) T = 0.0f; else if (T > 1.0f) T = 1.0f;
		const float Cx = Center.x + Vx * T;
		const float Cy = Center.y + Vy * T;
		const float Dist = std::sqrt((MouseLocal.x - Cx) * (MouseLocal.x - Cx) + (MouseLocal.y - Cy) * (MouseLocal.y - Cy));
		return (Dist < CornerHitRadius) ? Axis : -1;
	}

	bool HitTestCenter(ImVec2 MouseLocal, ImVec2 Center)
	{
		const float Dx = MouseLocal.x - Center.x;
		const float Dy = MouseLocal.y - Center.y;
		return (Dx * Dx + Dy * Dy) < (CornerCenterSize * CornerCenterSize * 4.0f);
	}

	int32 HitTestRing(ImVec2 MouseLocal, ImVec2 Center, int32 Axis)
	{
		const float Dx = MouseLocal.x - Center.x;
		const float Dy = MouseLocal.y - Center.y;
		const float Dist = std::sqrt(Dx * Dx + Dy * Dy);
		const float R = CornerRingRadius - Axis * 4.0f;
		return (std::abs(Dist - R) < CornerRingHitTol) ? Axis : -1;
	}
}

bool FSkeletalMeshViewerViewportClient::IsMouseInCornerGizmoArea() const
{
	if (ViewportWidth <= 0.0f || ViewportHeight <= 0.0f) return false;
	const float Right = ViewportScreenX + ViewportWidth - CornerGizmoPaddingX;
	const float Top = ViewportScreenY + CornerGizmoPaddingY;
	const float Left = Right - CornerGizmoSize;
	const float Bottom = Top + CornerGizmoSize;
	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	return MousePos.x >= Left && MousePos.x <= Right
		&& MousePos.y >= Top && MousePos.y <= Bottom;
}

void FSkeletalMeshViewerViewportClient::RenderCornerGizmoAndHandleInput()
{
	if (!Gizmo || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
	{
		return;
	}

	const bool bAnimOn = TrackedMesh && TrackedMesh->IsDebugRandomBoneAnimEnabled();
	const bool bHasTarget = Gizmo->HasTarget() && SelectedBoneIndex >= 0;

	const float Right = ViewportScreenX + ViewportWidth - CornerGizmoPaddingX;
	const float Top = ViewportScreenY + CornerGizmoPaddingY;
	const ImVec2 Center = ImVec2(Right - CornerGizmoSize * 0.5f, Top + CornerGizmoSize * 0.5f);

	ImDrawList* DL = ImGui::GetForegroundDrawList();

	// 배경 패널 (반투명)
	const ImU32 BgCol = bHasTarget && !bAnimOn ? IM_COL32(20, 20, 20, 140) : IM_COL32(50, 50, 50, 100);
	DL->AddRectFilled(
		ImVec2(Right - CornerGizmoSize, Top),
		ImVec2(Right, Top + CornerGizmoSize),
		BgCol, 6.0f);
	DL->AddRect(
		ImVec2(Right - CornerGizmoSize, Top),
		ImVec2(Right, Top + CornerGizmoSize),
		IM_COL32(160, 160, 160, 180), 6.0f, 0, 1.5f);

	const EGizmoMode Mode = Gizmo->GetMode();

	// Gizmo 그리기
	if (Mode == EGizmoMode::Rotate)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			DrawRotateRing(DL, Center, Axis, CornerActiveAxis == Axis);
		}
	}
	else
	{
		// Translate / Scale
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Mode == EGizmoMode::Scale)
			{
				DrawScaleHandle(DL, Center, Axis, CornerActiveAxis == Axis);
			}
			else
			{
				DrawArrow(DL, Center, Axis, CornerActiveAxis == Axis);
			}
		}
		// 중앙 사각형 (uniform / screen-space)
		const ImU32 CenterCol = (CornerActiveAxis == 3) ? IM_COL32(255, 230, 80, 255) : IM_COL32(230, 230, 230, 255);
		DL->AddRectFilled(
			ImVec2(Center.x - CornerCenterSize, Center.y - CornerCenterSize),
			ImVec2(Center.x + CornerCenterSize, Center.y + CornerCenterSize),
			CenterCol);
	}

	// 라벨
	const char* ModeLabel = (Mode == EGizmoMode::Translate) ? "Move" :
		(Mode == EGizmoMode::Rotate) ? "Rotate" :
		(Mode == EGizmoMode::Scale) ? "Scale" : "?";
	DL->AddText(ImVec2(Right - CornerGizmoSize + 8.0f, Top + 4.0f), IM_COL32(220, 220, 220, 220), ModeLabel);
	if (!bHasTarget)
	{
		DL->AddText(ImVec2(Right - CornerGizmoSize + 8.0f, Top + CornerGizmoSize - 18.0f),
			IM_COL32(180, 180, 180, 200), "No bone");
	}

	// 입력 처리
	if (bAnimOn || !bHasTarget)
	{
		CornerActiveAxis = -1;
		return;
	}

	// Viewport gizmo가 드래그 중이면 corner 입력 차단
	if (Gizmo->IsHolding())
	{
		CornerActiveAxis = -1;
		return;
	}

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const bool bLeftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bLeftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool bRightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bMiddleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

	if (CornerActiveAxis >= 0)
	{
		if (!bLeftDown)
		{
			CornerActiveAxis = -1;
			return;
		}
		const float Dx = MousePos.x - CornerLastMouseX;
		const float Dy = MousePos.y - CornerLastMouseY;
		if (Dx != 0.0f || Dy != 0.0f)
		{
			ApplyCornerGizmoDelta(CornerActiveAxis, Dx, Dy);
			CornerLastMouseX = MousePos.x;
			CornerLastMouseY = MousePos.y;
		}
		return;
	}

	// 드래그 시작 — 우/중 클릭 캡쳐 중에는 X
	if (bRightDown || bMiddleDown) return;
	if (!bLeftClicked) return;
	if (!IsMouseInCornerGizmoArea()) return;

	// Hit-test
	int32 HitAxis = -1;
	if (Mode == EGizmoMode::Rotate)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			HitAxis = HitTestRing(MousePos, Center, Axis);
			if (HitAxis >= 0) break;
		}
	}
	else
	{
		if (HitTestCenter(MousePos, Center))
		{
			HitAxis = 3;
		}
		else
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				HitAxis = HitTestAxisLine(MousePos, Center, Axis);
				if (HitAxis >= 0) break;
			}
		}
	}

	if (HitAxis >= 0)
	{
		CornerActiveAxis = HitAxis;
		CornerLastMouseX = MousePos.x;
		CornerLastMouseY = MousePos.y;
	}
}

void FSkeletalMeshViewerViewportClient::ApplyCornerGizmoDelta(int32 Axis, float Dx, float Dy)
{
	if (!BoneProxy || !TrackedMesh || SelectedBoneIndex < 0 || !Gizmo)
	{
		return;
	}

	const EGizmoMode Mode = Gizmo->GetMode();
	const bool bWorldSpace = Gizmo->IsWorldSpace();

	// 축 별 world(or local) 방향 결정
	auto GetAxisWorldDir = [&](int32 A) -> FVector
	{
		if (bWorldSpace)
		{
			if (A == 0) return FVector(1, 0, 0);
			if (A == 1) return FVector(0, 1, 0);
			return FVector(0, 0, 1);
		}
		else
		{
			// Local 모드: proxy의 world matrix에서 축 벡터 추출
			const FMatrix& M = BoneProxy->GetWorldMatrix();
			if (A == 0) return FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Normalized();
			if (A == 1) return FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Normalized();
			return FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Normalized();
		}
	};

	if (Mode == EGizmoMode::Translate)
	{
		float Pixels = 0.0f;
		FVector WorldDir;
		if (Axis == 3)
		{
			// Center: 화면 (Dx, Dy) → 카메라 right/up 평면으로
			Pixels = std::sqrt(Dx * Dx + Dy * Dy) * ((Dx + (-Dy) >= 0.0f) ? 1.0f : -1.0f);
			WorldDir = Camera ? Camera->GetRightVector() : FVector(1, 0, 0);
			const FVector WorldDir2 = Camera ? Camera->GetUpVector() : FVector(0, 0, 1);
			const FVector WorldDelta = WorldDir * (Dx * TranslateSensitivity) + WorldDir2 * (-Dy * TranslateSensitivity);
			BoneProxy->AddWorldOffset(WorldDelta);
			ApplyGizmoEditToBone();
			return;
		}
		Pixels = ScreenProjection(Dx, Dy, Axis);
		WorldDir = GetAxisWorldDir(Axis);
		const FVector WorldDelta = WorldDir * (Pixels * TranslateSensitivity);
		BoneProxy->AddWorldOffset(WorldDelta);
		ApplyGizmoEditToBone();
		return;
	}

	if (Mode == EGizmoMode::Rotate)
	{
		// 회전 각도 — 단순화: tangential drag amount ≈ |delta| with sign
		// Ring 위의 tangent 방향은 (-Dy_to_center, Dx_to_center) 회전 — 여기선 단순히 Dx 부호 사용
		const float Pixels = Dx - Dy; // 우측+ / 위쪽-
		const float AngleDeg = Pixels * RotateSensitivityDeg;
		const float AngleRad = AngleDeg * (3.14159265358979f / 180.0f);
		const FVector RotAxis = GetAxisWorldDir(Axis);
		FQuat DeltaQuat = FQuat::FromAxisAngle(RotAxis, AngleRad);
		const FQuat& CurQuat = BoneProxy->GetRelativeQuat();
		FQuat NewQuat = bWorldSpace ? (DeltaQuat * CurQuat) : (CurQuat * DeltaQuat);
		NewQuat = NewQuat.GetNormalized();
		BoneProxy->SetRelativeRotation(NewQuat);
		ApplyGizmoEditToBone();
		return;
	}

	if (Mode == EGizmoMode::Scale)
	{
		const float Pixels = (Axis == 3) ? (Dx - Dy) : ScreenProjection(Dx, Dy, Axis);
		const float Factor = Pixels * ScaleSensitivity;
		FVector NewScale = BoneProxy->GetRelativeScale();
		if (Axis == 0) NewScale.X += Factor;
		else if (Axis == 1) NewScale.Y += Factor;
		else if (Axis == 2) NewScale.Z += Factor;
		else { NewScale.X += Factor; NewScale.Y += Factor; NewScale.Z += Factor; }
		if (NewScale.X < 0.001f) NewScale.X = 0.001f;
		if (NewScale.Y < 0.001f) NewScale.Y = 0.001f;
		if (NewScale.Z < 0.001f) NewScale.Z = 0.001f;
		BoneProxy->SetRelativeScale(NewScale);
		ApplyGizmoEditToBone();
		return;
	}
}
