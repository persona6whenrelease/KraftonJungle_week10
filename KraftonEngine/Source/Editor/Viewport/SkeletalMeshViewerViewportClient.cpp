#include "SkeletalMeshViewerViewportClient.h"

#include "Object/Object.h"
#include "Component/CameraComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Mesh/SkeletalMeshAsset.h"

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
	RenderOptions.ShowFlags.bGizmo = false;
	RenderOptions.ShowFlags.bWorldAxis = false;
	RenderOptions.ShowFlags.bBoundingVolume = false;
	RenderOptions.ShowFlags.bCollisionShapes = false;

	Camera->SetWorldLocation(FVector(-5.0f, -5.0f, 3.0f));
	Camera->LookAt(FVector::ZeroVector);
}

void FSkeletalMeshViewerViewportClient::Shutdown()
{
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

	const float MoveSpeed = InputFrame.IsDown(VK_SHIFT) ? 35.0f : 10.0f;
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

	const bool bRightMouseDown = InputFrame.IsDown(VK_RBUTTON);
	const bool bMiddleMouseDown = InputFrame.IsDown(VK_MBUTTON);

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

		if (InputFrame.IsDown('W'))
		{
			MoveDelta += Camera->GetForwardVector();
		}
		if (InputFrame.IsDown('S'))
		{
			MoveDelta -= Camera->GetForwardVector();
		}
		if (InputFrame.IsDown('D'))
		{
			MoveDelta += Camera->GetRightVector();
		}
		if (InputFrame.IsDown('A'))
		{
			MoveDelta -= Camera->GetRightVector();
		}
		if (InputFrame.IsDown('E'))
		{
			MoveDelta += FVector::UpVector;
		}
		if (InputFrame.IsDown('Q'))
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
