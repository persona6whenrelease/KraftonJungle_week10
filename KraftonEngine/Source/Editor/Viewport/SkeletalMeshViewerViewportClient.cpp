#include "SkeletalMeshViewerViewportClient.h"

#include "Object/Object.h"
#include "Component/CameraComponent.h"
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

void FSkeletalMeshViewerViewportClient::Tick(float DeltaTime, bool bViewportHovered)
{
	if (!Camera || !bViewportHovered)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();

	const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

	const float MoveSpeed = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 35.0f : 10.0f;
	const float RotateSpeed = 0.15f;
	const float PanSpeed = 0.015f;
	const float ZoomSpeed = 0.35f;

	if (bRightMouseDown)
	{
		const float DeltaX = IO.MouseDelta.x;
		const float DeltaY = IO.MouseDelta.y;

		if (DeltaX != 0.0f || DeltaY != 0.0f)
		{
			Camera->Rotate(DeltaX * RotateSpeed, DeltaY * RotateSpeed);
		}

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
		}
	}

	if (bMiddleMouseDown)
	{
		const float DeltaX = IO.MouseDelta.x;
		const float DeltaY = IO.MouseDelta.y;

		if (DeltaX != 0.0f || DeltaY != 0.0f)
		{
			const FVector PanDelta =
				Camera->GetRightVector() * (-DeltaX * PanSpeed) +
				Camera->GetUpVector() * (DeltaY * PanSpeed);

			Camera->SetWorldLocation(Camera->GetWorldLocation() + PanDelta);
		}
	}

	if (IO.MouseWheel != 0.0f)
	{
		Camera->SetWorldLocation(
			Camera->GetWorldLocation() +
			Camera->GetForwardVector() * IO.MouseWheel * ZoomSpeed);
	}
}
