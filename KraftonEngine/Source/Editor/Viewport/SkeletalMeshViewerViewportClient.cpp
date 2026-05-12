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

#include <unordered_map>

namespace
{
	// Matrix → relative transform components 로 분해해 USceneComponent에 적용.
	void ApplyMatrixToRelativeTransform(USceneComponent* Component, const FMatrix& Matrix)
	{
		if (!Component)
		{
			return;
		}

		const FVector Scale = Matrix.GetScale();
		Component->SetRelativeScale(Scale);

		FMatrix Rotation = Matrix;
		auto SafeNormalize = [](float& A, float& B, float& C)
		{
			const float Len = std::sqrt(A * A + B * B + C * C);
			if (Len > 1e-6f)
			{
				const float Inv = 1.0f / Len;
				A *= Inv; B *= Inv; C *= Inv;
			}
		};
		SafeNormalize(Rotation.M[0][0], Rotation.M[0][1], Rotation.M[0][2]);
		SafeNormalize(Rotation.M[1][0], Rotation.M[1][1], Rotation.M[1][2]);
		SafeNormalize(Rotation.M[2][0], Rotation.M[2][1], Rotation.M[2][2]);
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

	// Gizmo interaction (LMB) — RMB/MMB 카메라 캡쳐 중에는 비활성.
	// Gizmo가 hold 중인 경우 마우스가 뷰포트를 벗어나거나 캡쳐 상태가 바뀌어도 드래그 종료까지는 유효.
	const bool bGizmoInputAllowed = (bViewportHovered && !bIsCapturing) || (Gizmo && Gizmo->IsHolding());
	if (Gizmo && bGizmoInputAllowed)
	{
		Gizmo->ApplyScreenSpaceScaling(
			Camera->GetWorldLocation(),
			Camera->IsOrthogonal(),
			Camera->GetOrthoWidth());
		Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(
			RenderOptions.ViewportType, Gizmo->GetMode()));

		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		const float LocalMouseX = MousePos.x - ViewportScreenX;
		const float LocalMouseY = MousePos.y - ViewportScreenY;
		const float VPW = ViewportWidth > 0.0f ? ViewportWidth : 1.0f;
		const float VPH = ViewportHeight > 0.0f ? ViewportHeight : 1.0f;

		const FRay Ray = Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPW, VPH);

		if (InputFrame.WasPressed(VK_LBUTTON))
		{
			FHitResult GizmoHit;
			if (Gizmo->HasTarget() && FRayUtils::RaycastComponent(Gizmo, Ray, GizmoHit))
			{
				Gizmo->SetPressedOnHandle(true);
				InputFrame.ConsumeMouseButtons("SkeletalMeshViewerGizmo", "Begin gizmo drag");
			}
			else if (TrackedMesh)
			{
				// 메시 raycast로 bone 픽킹
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
						if (BoneIdx >= 0)
						{
							SelectBone(BoneIdx);
							InputFrame.ConsumeMouseButtons("SkeletalMeshViewerGizmo", "Bone pick via raycast");
						}
					}
				}
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

		// 드래그가 진행 중이 아니라면, 매 프레임 proxy를 bone에 재동기화 (외부 변경 반영).
		if (!Gizmo->IsHolding() && SelectedBoneIndex >= 0)
		{
			SyncProxyFromBone(SelectedBoneIndex);
			Gizmo->UpdateGizmoTransform();
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
