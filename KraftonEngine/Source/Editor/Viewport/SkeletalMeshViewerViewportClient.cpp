#include "SkeletalMeshViewerViewportClient.h"
#include "Object/Object.h"
#include "Editor/Settings/EditorSettings.h"
#include "Component/CameraComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"
#include "Component/SkeletalGizmoComponent.h" // 기즈모 헤더 추가
#include "GameFramework/World.h"
#include "Collision/RayUtils.h"
namespace {
	FRay CalculateMouseRay(UCameraComponent* Camera, float MouseX, float MouseY, uint32 Width, uint32 Height)
	{
		if (!Camera || Width == 0 || Height == 0) return FRay();

		// 1. 화면 픽셀 좌표를 NDC(Normalized Device Coordinates) [-1, 1] 범위로 변환
		float NdcX = (2.0f * MouseX) / static_cast<float>(Width) - 1.0f;
		float NdcY = 1.0f - (2.0f * MouseY) / static_cast<float>(Height); // Y축 반전

		// 2. View Projection 역행렬 계산
		FMatrix ViewProj = Camera->GetViewMatrix() * Camera->GetProjectionMatrix();
		FMatrix InvViewProj = ViewProj.GetInverse();

		// 3. Near 평면과 Far 평면의 점을 월드 좌표로 변환
		FVector NearPoint = InvViewProj.TransformPositionWithW(FVector(NdcX, NdcY, 0.0f));
		FVector FarPoint = InvViewProj.TransformPositionWithW(FVector(NdcX, NdcY, 1.0f));

		// 4. 방향 벡터 계산
		FVector RayDir = FarPoint - NearPoint;
		RayDir.Normalize();

		return FRay{ NearPoint, RayDir }; // Ray Origin은 카메라 위치(또는 NearPoint), 방향은 RayDir
	}

	void TickPreviewCameraInput(
		UCameraComponent* Camera,
		float DeltaTime,
		bool bViewportHovered,
		bool bIsCapturing,
		bool bGizmoHolding,
		FInputFrame& InputFrame)
	{
		if (!Camera || bGizmoHolding)
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

		const float ScrollNotches = InputFrame.GetScrollNotches();
		if (bViewportHovered && ScrollNotches != 0.0f)
		{
			Camera->SetWorldLocation(
				Camera->GetWorldLocation() + Camera->GetForwardVector() * ScrollNotches * ZoomSpeed);
			InputFrame.ConsumeScroll("SkeletalMeshViewer", "Preview zoom");
		}

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
}

void FSkeletalMeshViewerViewportClient::Initialize()
{
	if (Camera)
	{
		return;
	}

	Camera = UObjectManager::Get().CreateObject<UCameraComponent>();
	Camera->SetOrthographic(false);
	Camera->SetFOV(FEditorSettings::Get().PerspCamFOV * DEG_TO_RAD);
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

	BoneSelectionManager.Init();
}

void FSkeletalMeshViewerViewportClient::Shutdown()
{
	BoneSelectionManager.Shutdown(); // 매니저 해제

	if (Camera)
	{
		UObjectManager::Get().DestroyObject(Camera);
		Camera = nullptr;
	}
}

void FSkeletalMeshViewerViewportClient::Resize(uint32 Width, uint32 Height)
{
	if (!Camera)
	{
		return;
	}
	ViewportWidth = Width;
	ViewportHeight = Height;
	Camera->OnResize(static_cast<int32>(Width), static_cast<int32>(Height));
}

void FSkeletalMeshViewerViewportClient::SetViewportRect(float MinX, float MinY, uint32 Width, uint32 Height)
{
	ViewportMinX = MinX;
	ViewportMinY = MinY;
	Resize(Width, Height);
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

	USkeletalGizmoComponent* Gizmo = BoneSelectionManager.GetGizmo();
	const bool bGizmoHolding = Gizmo && Gizmo->IsHolding();
	TickPreviewCameraInput(Camera, DeltaTime, bViewportHovered, bIsCapturing, bGizmoHolding, InputFrame);

	BoneSelectionManager.Tick();
	Gizmo = BoneSelectionManager.GetGizmo();
	if (Gizmo && Gizmo->IsActive() && ViewportWidth > 0 && ViewportHeight > 0)
	{
		Gizmo->ClearScreenSpaceScaleOverride();
		Gizmo->ApplyScreenSpaceScaling(
			Camera->GetWorldLocation(),
			Camera->IsOrthogonal(),
			Camera->GetOrthoWidth());
		Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(RenderOptions.ViewportType, Gizmo->GetMode()));

		if (bViewportHovered && !bIsCapturing && !Gizmo->IsHolding())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_W)) Gizmo->SetTranslateMode();
			if (ImGui::IsKeyPressed(ImGuiKey_E)) Gizmo->SetRotateMode();
			if (ImGui::IsKeyPressed(ImGuiKey_R)) Gizmo->SetScaleMode();
		}

		// ImGui의 윈도우 내 기준 상대 마우스 좌표를 구합니다. 
		// (InputFrame.GetMouseX()가 이미 로컬 좌표라면 그것을 사용하세요)
		ImVec2 MousePos = ImGui::GetIO().MousePos;
		float LocalMouseX = MousePos.x - ViewportMinX;
		float LocalMouseY = MousePos.y - ViewportMinY;

		FRay MouseRay = Camera->DeprojectScreenToWorld(
			LocalMouseX,
			LocalMouseY,
			static_cast<float>(ViewportWidth),
			static_cast<float>(ViewportHeight));

		bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		bool bLeftMouseJustPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		bool bLeftMouseJustReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

		if (Gizmo->IsHolding())
		{
			if (bLeftMouseJustReleased)
			{
				Gizmo->DragEnd();
			}
			else if (bLeftMouseDown)
			{
				// 드래그 업데이트 (실제 뼈대 트랜스폼 연산 발생)
				Gizmo->UpdateDrag(MouseRay, Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector());
			}
		}
		else
		{
			// 마우스 우클릭/휠클릭 등으로 카메라 뷰포트를 회전(Capturing) 중이 아닐 때만 호버링 허용
			if (bViewportHovered && !bIsCapturing)
			{
				FHitResult HitResult;

				// LineTraceComponent 내부에서 충돌 시 SelectedAxis를 세팅하므로 자동으로 노란색 하이라이트가 됨
				if (FRayUtils::RaycastComponent(Gizmo, MouseRay, HitResult))
				{
					if (bLeftMouseJustPressed)
					{
						// 축을 클릭하면 드래그 홀딩 시작
						Gizmo->SetHolding(true);
					}
				}
				else
				{
					// [보너스 기능] 빈 공간을 좌클릭하면 본 선택 해제 및 기즈모 숨김
					//if (bLeftMouseJustPressed)
					//{
					//	BoneSelectionManager.ClearSelection();
					//}
				}
			}
			else
			{
				// 마우스가 뷰포트 밖으로 나갔거나 카메라를 이리저리 돌리는 중이라면 호버링(노란색) 초기화
				Gizmo->UpdateHoveredAxis(-1);
			}
		}
	}

}

void FSkeletalMeshViewerViewportClient::SetPreviewWorld(UWorld* InWorld)
{
	BoneSelectionManager.SetScene(InWorld ? &InWorld->GetScene() : nullptr);
}

