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
#include "Render/Resource/MeshBufferManager.h"
#include "Component/SkinnedMeshComponent.h"
#include <cfloat>
#include <cmath>
namespace {
	constexpr float ViewerGizmoAxisPickPixels = 1.0f;
	constexpr float ViewerGizmoCenterPickPixels = 1.0f;
	constexpr float ViewerGizmoRotatePickPixels = 1.0f;
	constexpr float ViewerBoneJointRadiusScale = 0.005f;
	constexpr float ViewerBonePickMinPixels = 6.0f;

	float ComputeWorldUnitsPerPixelAtGizmo(
		const UCameraComponent* Camera,
		const UGizmoComponent* Gizmo,
		float ViewportWidth,
		float ViewportHeight)
	{
		if (!Camera || !Gizmo || ViewportHeight <= 0.0f)
		{
			return 0.0f;
		}

		if (Camera->IsOrthogonal())
		{
			const float AspectRatio = ViewportWidth > 0.0f
				? ViewportWidth / ViewportHeight
				: Camera->GetAspectRatio();
			const float OrthoHeight = Camera->GetOrthoWidth() / AspectRatio;
			return OrthoHeight / ViewportHeight;
		}

		float Distance = FVector::Distance(Camera->GetWorldLocation(), Gizmo->GetWorldLocation());
		if (Distance < Camera->GetNearPlane())
		{
			Distance = Camera->GetNearPlane();
		}

		const float ViewHeightAtDepth = 2.0f * Distance * tanf(Camera->GetFOV() * 0.5f);
		return ViewHeightAtDepth / ViewportHeight;
	}

	float ComputeBoneDebugWorldRadius(const USkinnedMeshComponent* SkelMeshComp)
	{
		if (!SkelMeshComp)
		{
			return 0.0f;
		}

		const FBoundingBox Bounds = SkelMeshComp->GetWorldAABB();
		const FVector Diagonal = Bounds.Max - Bounds.Min;
		const float Distance = std::sqrt(Diagonal.Dot(Diagonal));
		return Distance * ViewerBoneJointRadiusScale;
	}

	float Cross2D(const ImVec2& A, const ImVec2& B, const ImVec2& C)
	{
		return (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
	}

	bool IsPointInsideTriangle2D(const ImVec2& Point, const ImVec2& A, const ImVec2& B, const ImVec2& C)
	{
		const float Area = Cross2D(A, B, C);
		if (std::abs(Area) <= 1.0e-4f)
		{
			return false;
		}

		const float Edge0 = Cross2D(A, B, Point);
		const float Edge1 = Cross2D(B, C, Point);
		const float Edge2 = Cross2D(C, A, Point);
		const bool bHasNegative = Edge0 < 0.0f || Edge1 < 0.0f || Edge2 < 0.0f;
		const bool bHasPositive = Edge0 > 0.0f || Edge1 > 0.0f || Edge2 > 0.0f;
		return !(bHasNegative && bHasPositive);
	}

	bool ProjectGizmoLocalToViewport(
		const FMatrix& LocalToClip,
		const FVector& LocalPosition,
		float ViewportWidth,
		float ViewportHeight,
		ImVec2& OutScreen,
		float& OutDepth)
	{
		const FVector ClipSpace = LocalToClip.TransformPositionWithW(LocalPosition);
		if (!std::isfinite(ClipSpace.X) || !std::isfinite(ClipSpace.Y) || !std::isfinite(ClipSpace.Z) || ClipSpace.Z < 0.0f)
		{
			return false;
		}

		OutScreen.x = (ClipSpace.X * 0.5f + 0.5f) * ViewportWidth;
		OutScreen.y = (1.0f - (ClipSpace.Y * 0.5f + 0.5f)) * ViewportHeight;
		OutDepth = ClipSpace.Z;
		return true;
	}

	bool ProjectWorldToViewport(
		const FMatrix& WorldToClip,
		const FVector& WorldPosition,
		float ViewportWidth,
		float ViewportHeight,
		ImVec2& OutScreen,
		float& OutDepth)
	{
		const FVector ClipSpace = WorldToClip.TransformPositionWithW(WorldPosition);
		if (!std::isfinite(ClipSpace.X) || !std::isfinite(ClipSpace.Y) || !std::isfinite(ClipSpace.Z) || ClipSpace.Z < 0.0f)
		{
			return false;
		}

		OutScreen.x = (ClipSpace.X * 0.5f + 0.5f) * ViewportWidth;
		OutScreen.y = (1.0f - (ClipSpace.Y * 0.5f + 0.5f)) * ViewportHeight;
		OutDepth = ClipSpace.Z;
		return true;
	}

	float DistancePointToPoint2D(const ImVec2& A, const ImVec2& B)
	{
		const float DX = A.x - B.x;
		const float DY = A.y - B.y;
		return std::sqrt(DX * DX + DY * DY);
	}

	float ComputeWorldUnitsPerPixelAtWorldPosition(
		UCameraComponent* Camera,
		const FVector& WorldPosition,
		float ViewportWidth,
		float ViewportHeight)
	{
		if (!Camera || ViewportHeight <= 0.0f)
		{
			return 0.0f;
		}

		if (Camera->IsOrthogonal())
		{
			const float AspectRatio = ViewportWidth > 0.0f
				? ViewportWidth / ViewportHeight
				: Camera->GetAspectRatio();
			const float OrthoHeight = Camera->GetOrthoWidth() / AspectRatio;
			return OrthoHeight / ViewportHeight;
		}

		float Distance = FVector::Distance(Camera->GetWorldLocation(), WorldPosition);
		if (Distance < Camera->GetNearPlane())
		{
			Distance = Camera->GetNearPlane();
		}

		const float ViewHeightAtDepth = 2.0f * Distance * tanf(Camera->GetFOV() * 0.5f);
		return ViewHeightAtDepth / ViewportHeight;
	}

	float ProjectWorldRadiusToPixelsApprox(
		UCameraComponent* Camera,
		const FVector& WorldPosition,
		float WorldRadius,
		float ViewportWidth,
		float ViewportHeight)
	{
		const float WorldUnitsPerPixel = ComputeWorldUnitsPerPixelAtWorldPosition(
			Camera,
			WorldPosition,
			ViewportWidth,
			ViewportHeight);
		if (WorldUnitsPerPixel <= 0.0f || WorldRadius <= 0.0f)
		{
			return ViewerBonePickMinPixels;
		}

		return (std::max)(ViewerBonePickMinPixels, WorldRadius / WorldUnitsPerPixel);
	}

	int32 PickBoneScreenSpace(
		UCameraComponent* Camera,
		USkinnedMeshComponent* SkelMeshComp,
		float MouseX,
		float MouseY,
		float ViewportWidth,
		float ViewportHeight)
	{
		if (!Camera || !SkelMeshComp || !SkelMeshComp->GetSkeletalMesh() || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
		{
			return -1;
		}

		const FSkeletalMesh* Asset = SkelMeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset();
		if (!Asset || Asset->Bones.empty())
		{
			return -1;
		}

		const TArray<FMatrix>& MeshSpaceBones = SkelMeshComp->GetMeshSpaceBoneMatrices();
		if (MeshSpaceBones.size() < Asset->Bones.size())
		{
			return -1;
		}

		const FMatrix ComponentWorld = SkelMeshComp->GetWorldMatrix();
		const FMatrix WorldToClip = Camera->GetViewProjectionMatrix();
		const ImVec2 MousePoint(MouseX, MouseY);
		const float WorldPickRadius = ComputeBoneDebugWorldRadius(SkelMeshComp);

		int32 BestBoneIndex = -1;
		float BestDistancePixels = FLT_MAX;
		float BestDepth = -FLT_MAX;

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			const FMatrix BoneWorldMatrix = MeshSpaceBones[BoneIndex] * ComponentWorld;
			const FVector BoneWorldPosition = BoneWorldMatrix.GetLocation();

			ImVec2 BoneScreen;
			float BoneDepth = 0.0f;
			if (!ProjectWorldToViewport(WorldToClip, BoneWorldPosition, ViewportWidth, ViewportHeight, BoneScreen, BoneDepth))
			{
				continue;
			}

			const float PickRadiusPixels = ProjectWorldRadiusToPixelsApprox(Camera, BoneWorldPosition, WorldPickRadius, ViewportWidth, ViewportHeight);
			const float DistancePixels = DistancePointToPoint2D(MousePoint, BoneScreen);
			if (DistancePixels > PickRadiusPixels)
			{
				continue;
			}

			if (BestBoneIndex == -1 ||
				DistancePixels < BestDistancePixels ||
				(std::abs(DistancePixels - BestDistancePixels) <= 0.01f && BoneDepth > BestDepth))
			{
				BestBoneIndex = BoneIndex;
				BestDistancePixels = DistancePixels;
				BestDepth = BoneDepth;
			}
		}

		return BestBoneIndex;
	}

	bool PickGizmoScreenSpace(
		UCameraComponent* Camera,
		UGizmoComponent* Gizmo,
		float MouseX,
		float MouseY,
		float ViewportWidth,
		float ViewportHeight)
	{
		if (!Camera || !Gizmo || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
		{
			if (Gizmo)
			{
				Gizmo->UpdateHoveredAxis(-1);
			}
			return false;
		}

		const float PerViewScale = Gizmo->GetScreenSpaceScaleForRender(
			Camera->GetWorldLocation(),
			Camera->IsOrthogonal(),
			Camera->GetOrthoWidth());
		const FMatrix RenderModel =
			FMatrix::MakeScaleMatrix(FVector(PerViewScale, PerViewScale, PerViewScale)) *
			FMatrix::MakeRotationEuler(Gizmo->GetRelativeRotation().ToVector()) *
			FMatrix::MakeTranslationMatrix(Gizmo->GetWorldLocation());
		const FMatrix LocalToClip = RenderModel * Camera->GetViewProjectionMatrix();
		const FMeshDataView MeshData = Gizmo->GetMeshDataView();
		if (!MeshData.IsValid())
		{
			Gizmo->UpdateHoveredAxis(-1);
			return false;
		}

		const uint32 AxisMask = Gizmo->GetAxisMask();
		const ImVec2 MousePoint(MouseX, MouseY);

		uint32 BestIndexOffset = UINT32_MAX;
		float BestDepth = -FLT_MAX;
		bool bBestIsCenter = false;

		for (uint32 IndexOffset = 0; IndexOffset + 2 < MeshData.IndexCount; IndexOffset += 3)
		{
			const uint32 Index0 = MeshData.IndexData[IndexOffset + 0];
			const uint32 Index1 = MeshData.IndexData[IndexOffset + 1];
			const uint32 Index2 = MeshData.IndexData[IndexOffset + 2];
			if (Index0 >= MeshData.VertexCount || Index1 >= MeshData.VertexCount || Index2 >= MeshData.VertexCount)
			{
				continue;
			}

			const FVertex& Vertex0 = MeshData.GetVertex<FVertex>(Index0);
			const FVertex& Vertex1 = MeshData.GetVertex<FVertex>(Index1);
			const FVertex& Vertex2 = MeshData.GetVertex<FVertex>(Index2);
			const int32 SubID = Vertex0.SubID;
			if (SubID < 3 && (AxisMask & (1u << SubID)) == 0)
			{
				continue;
			}

			ImVec2 Screen0, Screen1, Screen2;
			float Depth0 = 0.0f;
			float Depth1 = 0.0f;
			float Depth2 = 0.0f;
			if (!ProjectGizmoLocalToViewport(LocalToClip, Vertex0.Position, ViewportWidth, ViewportHeight, Screen0, Depth0) ||
				!ProjectGizmoLocalToViewport(LocalToClip, Vertex1.Position, ViewportWidth, ViewportHeight, Screen1, Depth1) ||
				!ProjectGizmoLocalToViewport(LocalToClip, Vertex2.Position, ViewportWidth, ViewportHeight, Screen2, Depth2))
			{
				continue;
			}

			if (!IsPointInsideTriangle2D(MousePoint, Screen0, Screen1, Screen2))
			{
				continue;
			}

			const float TriangleDepth = (Depth0 + Depth1 + Depth2) / 3.0f;
			const bool bIsCenter = SubID == 3;
			if (BestIndexOffset == UINT32_MAX ||
				(bIsCenter && !bBestIsCenter) ||
				(bIsCenter == bBestIsCenter && TriangleDepth > BestDepth))
			{
				BestIndexOffset = IndexOffset;
				BestDepth = TriangleDepth;
				bBestIsCenter = bIsCenter;
			}
		}

		if (BestIndexOffset == UINT32_MAX)
		{
			Gizmo->UpdateHoveredAxis(-1);
			return false;
		}

		Gizmo->UpdateHoveredAxis(static_cast<int32>(BestIndexOffset));
		return true;
	}

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

		float ScrollNotches = InputFrame.GetScrollNotches();
		if (ScrollNotches == 0.0f)
		{
			ScrollNotches = ImGui::GetIO().MouseWheel;
		}
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
	ViewportWidth = static_cast<float>(Width);
	ViewportHeight = static_cast<float>(Height);
	Camera->OnResize(static_cast<int32>(Width), static_cast<int32>(Height));
}

void FSkeletalMeshViewerViewportClient::SetViewportRect(float MinX, float MinY, float Width, float Height)
{
	if (!Camera)
	{
		return;
	}

	ViewportMinX = MinX;
	ViewportMinY = MinY;
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

#pragma region Skeltal Gizmo Interaction

	USkeletalGizmoComponent* Gizmo = BoneSelectionManager.GetGizmo();
	const bool bGizmoHolding = Gizmo && Gizmo->IsHolding();
	TickPreviewCameraInput(Camera, DeltaTime, bViewportHovered, bIsCapturing, bGizmoHolding, InputFrame);

	BoneSelectionManager.Tick();
	Gizmo = BoneSelectionManager.GetGizmo();
	const bool bCanInteractWithViewport = bViewportHovered && !bIsCapturing && ViewportWidth > 0.0f && ViewportHeight > 0.0f;
	const bool bGizmoActive = Gizmo && Gizmo->IsActive() && ViewportWidth > 0.0f && ViewportHeight > 0.0f;
	if (bGizmoActive)
	{
		Gizmo->ClearScreenSpaceScaleOverride();
		Gizmo->ApplyScreenSpaceScaling(
			Camera->GetWorldLocation(),
			Camera->IsOrthogonal(),
			Camera->GetOrthoWidth());
		const float WorldUnitsPerPixel = ComputeWorldUnitsPerPixelAtGizmo(
			Camera,
			Gizmo,
			ViewportWidth,
			ViewportHeight);
		Gizmo->SetScreenSpacePickingRadii(
			WorldUnitsPerPixel * ViewerGizmoAxisPickPixels,
			WorldUnitsPerPixel * ViewerGizmoCenterPickPixels,
			WorldUnitsPerPixel * ViewerGizmoRotatePickPixels);
		Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(RenderOptions.ViewportType, Gizmo->GetMode()));

		if (bViewportHovered && !Gizmo->IsHolding())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Space))
			{
				EGizmoMode CurrentMode = Gizmo->GetMode();

				if (CurrentMode == EGizmoMode::Translate)
				{
					Gizmo->SetRotateMode();
				}
				else if (CurrentMode == EGizmoMode::Rotate)
				{
					Gizmo->SetScaleMode();
				}
				else // Scale 모드이거나 기타 모드일 경우 다시 Translate로
				{
					Gizmo->SetTranslateMode();
				}
			}
		}

		// ImGui의 윈도우 내 기준 상대 마우스 좌표를 구합니다. 
		// (InputFrame.GetMouseX()가 이미 로컬 좌표라면 그것을 사용하세요)
		ImVec2 MousePos = ImGui::GetIO().MousePos;
		float LocalMouseX = MousePos.x - ViewportMinX;
		float LocalMouseY = MousePos.y - ViewportMinY;

		FRay MouseRay = Camera->DeprojectScreenToWorld(
			LocalMouseX,
			LocalMouseY,
			ViewportWidth,
			ViewportHeight);

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
				if (Gizmo->GetMode() == EGizmoMode::Rotate)
				{
					Gizmo->UpdateScreenSpaceRotateDrag(
						LocalMouseX,
						LocalMouseY,
						ViewportWidth,
						ViewportHeight,
						Camera->GetViewProjectionMatrix(),
						Camera->GetForwardVector());
				}
				else
				{
					Gizmo->UpdateDrag(MouseRay, Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector());
				}
			}
		}
		else
		{
			// 마우스 우클릭/휠클릭 등으로 카메라 뷰포트를 회전(Capturing) 중이 아닐 때만 호버링 허용
			if (bViewportHovered && !bIsCapturing)
			{
				FHitResult HitResult;

				const bool bGizmoHit = PickGizmoScreenSpace(
					Camera,
					Gizmo,
					LocalMouseX,
					LocalMouseY,
					ViewportWidth,
					ViewportHeight);

				// 실제 렌더 mesh를 screen-space로 project해서 SelectedAxis를 세팅합니다.
				if (bGizmoHit)
				{
					if (bLeftMouseJustPressed)
					{
						// 축을 클릭하면 드래그 홀딩 시작
						Gizmo->SetHolding(true);
					}
				}
				else
				{
					if (bLeftMouseJustPressed && !InputFrame.IsDown(VK_CONTROL))
					{
						const int32 HitBoneIndex = PickBoneScreenSpace(
							Camera,
							BoneSelectionManager.GetTargetSkeletalMesh(),
							LocalMouseX,
							LocalMouseY,
							ViewportWidth,
							ViewportHeight);
						if (HitBoneIndex >= 0)
						{
							BoneSelectionManager.SelectBone(HitBoneIndex);
							InputFrame.ConsumeMouseButtons("SkeletalMeshViewer", "Select bone");
						}
						else
						{
							// [보너스 기능] 빈 공간을 좌클릭하면 본 선택 해제 및 기즈모 숨김
							BoneSelectionManager.ClearSelection();
							InputFrame.ConsumeMouseButtons("SkeletalMeshViewer", "Clear bone selection");
						}
					}
				}
			}
			else
			{
				// 마우스가 뷰포트 밖으로 나갔거나 카메라를 이리저리 돌리는 중이라면 호버링(노란색) 초기화
				Gizmo->UpdateHoveredAxis(-1);
			}
		}
	}
	else if (bCanInteractWithViewport)
	{
		ImVec2 MousePos = ImGui::GetIO().MousePos;
		const float LocalMouseX = MousePos.x - ViewportMinX;
		const float LocalMouseY = MousePos.y - ViewportMinY;
		const bool bLeftMouseJustPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		if (bLeftMouseJustPressed && !InputFrame.IsDown(VK_CONTROL))
		{
			const int32 HitBoneIndex = PickBoneScreenSpace(
				Camera,
				BoneSelectionManager.GetTargetSkeletalMesh(),
				LocalMouseX,
				LocalMouseY,
				ViewportWidth,
				ViewportHeight);
			if (HitBoneIndex >= 0)
			{
				BoneSelectionManager.SelectBone(HitBoneIndex);
				InputFrame.ConsumeMouseButtons("SkeletalMeshViewer", "Select bone");
			}
			else
			{
				BoneSelectionManager.ClearSelection();
				InputFrame.ConsumeMouseButtons("SkeletalMeshViewer", "Clear bone selection");
			}
		}
	}

#pragma endregion

}

void FSkeletalMeshViewerViewportClient::SetPreviewWorld(UWorld* InWorld)
{
	BoneSelectionManager.SetScene(InWorld ? &InWorld->GetScene() : nullptr);
}

void FSkeletalMeshViewerViewportClient::FocusBone(USkinnedMeshComponent* SkelMeshComp, int32 BoneIndex)
{
	if (!SkelMeshComp || !SkelMeshComp->GetSkeletalMesh() || !Camera)
	{
		return;
	}

	const FSkeletalMesh* Asset = SkelMeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset();
	if (!Asset || BoneIndex < 0 || BoneIndex >= Asset->Bones.size())
	{
		return;
	}

	const TArray<FMatrix>& MeshSpaceBones = SkelMeshComp->GetMeshSpaceBoneMatrices();
	FMatrix CompWorld = SkelMeshComp->GetWorldMatrix();

	// 1. 타겟 본(클릭한 본)의 월드 위치 계산
	FMatrix BoneWorldMatrix = MeshSpaceBones[BoneIndex] * CompWorld;
	FVector BoneWorldLocation = BoneWorldMatrix.GetLocation();

	// 2. 카메라 포커스 거리(Zoom) 동적 계산
	// 기본값 2.0f 대신 부모 뼈와의 거리를 측정해 뼈 크기에 맞게 줌인/줌아웃 되도록 합니다.
	float FocusDistance = 2.0f;
	int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;

	if (ParentIndex >= 0 && ParentIndex < MeshSpaceBones.size())
	{
		FMatrix ParentWorldMatrix = MeshSpaceBones[ParentIndex] * CompWorld;
		FVector ParentWorldLocation = ParentWorldMatrix.GetLocation();

		// 부모 본과 현재 본 사이의 거리 계산 (자체 엔진 벡터 클래스에 맞게 수정)
		FVector Diff = ParentWorldLocation - BoneWorldLocation;
		float BoneLength = std::sqrt(Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z);

		if (BoneLength > 0.001f)
		{
			// 뼈 길이의 약 3배 정도 뒤에서 바라보게 설정 (원하는 수치로 튜닝하세요)
			FocusDistance = BoneLength * 3.0f;
		}
	}

	// 3. 카메라 이동 및 방향 설정
	FVector ViewDir = Camera->GetWorldLocation() - BoneWorldLocation;
	float DirLength = std::sqrt(ViewDir.X * ViewDir.X + ViewDir.Y * ViewDir.Y + ViewDir.Z * ViewDir.Z);

	// 카메라가 본과 완전히 겹쳐있는 상태라면 예외 처리
	if (DirLength < 0.001f)
	{
		ViewDir = FVector(1.0f, 1.0f, 1.0f);
		DirLength = std::sqrt(3.0f);
	}

	ViewDir.X /= DirLength; ViewDir.Y /= DirLength; ViewDir.Z /= DirLength;

	// 최종 카메라 위치 및 회전 세팅
	Camera->SetWorldLocation(BoneWorldLocation + ViewDir * FocusDistance);
	Camera->LookAt(BoneWorldLocation);
}

