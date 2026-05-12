#include "SkeletalMeshViewerViewportClient.h"

#include "Object/Object.h"
#include "Component/CameraComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"

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

	Camera->SetWorldLocation(FVector(5.0f, 0.0f, 2.0f));
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
