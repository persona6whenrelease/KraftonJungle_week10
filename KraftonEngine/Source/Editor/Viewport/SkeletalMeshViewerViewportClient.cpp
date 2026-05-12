#include "SkeletalMeshViewerViewportClient.h"

#include "Object/Object.h"
#include "Component/CameraComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"
#include "Component/SkeletalGizmoComponent.h" // 기즈모 헤더 추가
#include "GameFramework/World.h"

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

	Camera->SetWorldLocation(FVector(-5.0f, -5.0f, 3.0f));
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

void FSkeletalMeshViewerViewportClient::FrameMesh(const FSkeletalMesh* MeshAsset)
{
	if (!Camera || !MeshAsset)
	{
		return;
	}

	FVector Extent = MeshAsset->BoundsExtent;
	float Radius = Extent.Length();
	Radius = (std::max)(Radius, 1.0f);

	const float Distance = Radius * 2.5f;
	Camera->SetWorldLocation(FVector(-Distance, -Distance, Distance * 0.65f));
	Camera->LookAt(FVector::ZeroVector);
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

	// 1. Bone Manager 업데이트 (기즈모 뼈대 위치 동기화 등)
	BoneSelectionManager.Tick();

	// 2. 기즈모 조작 상태 플래그
	bool bGizmoHandledInput = false;

	// 3. 기즈모 입력 처리
	USkeletalGizmoComponent* Gizmo = BoneSelectionManager.GetGizmo();
	if (Gizmo && Gizmo->IsActive() && ViewportWidth > 0 && ViewportHeight > 0)
	{
		// ImGui의 윈도우 내 기준 상대 마우스 좌표를 구합니다. 
		// (InputFrame.GetMouseX()가 이미 로컬 좌표라면 그것을 사용하세요)
		ImVec2 MousePos = ImGui::GetMousePos();
		ImVec2 WindowPos = ImGui::GetWindowPos();
		float LocalMouseX = MousePos.x - WindowPos.x;
		float LocalMouseY = MousePos.y - WindowPos.y;

		FRay MouseRay = CalculateMouseRay(Camera, LocalMouseX, LocalMouseY, ViewportWidth, ViewportHeight);

		bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		bool bLeftMouseJustPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		bool bLeftMouseJustReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

		if (Gizmo->IsHolding())
		{
			bGizmoHandledInput = true; // 기즈모를 잡고 있을 때는 다른 조작 무시

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
		else if (bViewportHovered)
		{
			// 잡고 있지 않을 때는 레이캐스트를 통해 Hover된 축을 찾음
			FHitResult HitResult;
			if (Gizmo->LineTraceComponent(MouseRay, HitResult))
			{
				if (bLeftMouseJustPressed)
				{
					Gizmo->SetHolding(true);
					bGizmoHandledInput = true;
				}
			}
			else
			{
				// 기즈모 외부 클릭 시 선택 해제 등을 원한다면 여기에 추가 가능
			}
		}
	}

	// 4. 카메라 제어 조작
	// 만약 마우스가 기즈모 드래그에 사용 중이라면, 화면 회전/이동을 막습니다.
	if (bGizmoHandledInput)
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

void FSkeletalMeshViewerViewportClient::SetPreviewWorld(UWorld* InWorld)
{
	BoneSelectionManager.SetScene(InWorld ? &InWorld->GetScene() : nullptr);
}

