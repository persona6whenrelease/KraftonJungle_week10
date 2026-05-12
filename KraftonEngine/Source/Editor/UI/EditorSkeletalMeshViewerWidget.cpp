#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/Settings/EditorSettings.h"
#include "Mesh/FBX/FBXManager.h"
#include "Mesh/FBX/FBXSceneAsset.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"

#include <cstdint>

#include "Runtime/Engine.h"
#include "Viewport/Viewport.h"
#include "Component/SkeletalMeshComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Editor/EditorEngine.h"

namespace
{
void RenderBoneTreeNode(const TArray<FBoneInfo>& Bones, int32 BoneIndex, int32& SelectedBoneIndex)
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return;
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (SelectedBoneIndex == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChild = false;
	for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
	{
		if (Bones[ChildIndex].ParentIndex == BoneIndex)
		{
			bHasChild = true;
			break;
		}
	}
	if (!bHasChild)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}

	const bool bOpen = ImGui::TreeNodeEx(
		reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)),
		Flags,
		"%s",
		Bones[BoneIndex].Name.c_str());

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = BoneIndex;
	}

	if (bOpen)
	{
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
		{
			if (Bones[ChildIndex].ParentIndex == BoneIndex)
			{
				RenderBoneTreeNode(Bones, ChildIndex, SelectedBoneIndex);
			}
		}
		ImGui::TreePop();
	}
}
}

FEditorSkeletalMeshViewerWidget::~FEditorSkeletalMeshViewerWidget()
{
	ReleasePreviewScene();
}

void FEditorSkeletalMeshViewerWidget::EnsurePreviewScene()
{
	if (PreviewWorld)
	{
		return;
	}

	PreviewWorld = UObjectManager::Get().CreateObject<UWorld>();
	PreviewWorld->SetWorldType(EWorldType::Editor);
	PreviewWorld->InitWorld();

	PreviewActor = PreviewWorld->SpawnActor<AActor>();

	PreviewMeshComponent = PreviewActor->AddComponent<USkeletalMeshComponent>();
	PreviewActor->SetRootComponent(PreviewMeshComponent);

	PreviewViewportClient = new FSkeletalMeshViewerViewportClient();
	PreviewViewportClient->Initialize();
	
	PreviewViewport = new FViewport();

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	if (Device)
	{
		PreviewViewport->Initialize(Device, 512, 512);
		PreviewViewport->SetClient(PreviewViewportClient);
	}
}

void FEditorSkeletalMeshViewerWidget::ReleasePreviewScene()
{
	if (PreviewViewport)
	{
		PreviewViewport->Release();
		delete PreviewViewport;
		PreviewViewport = nullptr;
	}

	if (PreviewViewportClient)
	{
		PreviewViewportClient->Shutdown();
		delete PreviewViewportClient;
		PreviewViewportClient = nullptr;
	}

	PreviewMeshComponent = nullptr;
	PreviewActor = nullptr;

	if (PreviewWorld)
	{
		PreviewWorld->EndPlay();
		UObjectManager::Get().DestroyObject(PreviewWorld);
		PreviewWorld = nullptr;
	}
}

void FEditorSkeletalMeshViewerWidget::SetPreviewMesh(USkeletalMesh* InMesh, bool bResetCamera)
{
	EnsurePreviewScene();
	PreviewSkeletalMesh = InMesh;

	if (!PreviewMeshComponent)
	{
		return;
	}

	PreviewMeshComponent->SetSkeletalMesh(InMesh);

	FSkeletalMesh* MeshAsset = InMesh ? InMesh->GetSkeletalMeshAsset() : nullptr;
	if (MeshAsset)
	{
		if (!MeshAsset->bBoundsValid)
		{
			MeshAsset->CacheBounds();
		}

		const FVector Center = MeshAsset->BoundsCenter;
		PreviewMeshComponent->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Center.Z));
	}

	if (bResetCamera && PreviewViewportClient)
	{
		PreviewViewportClient->FrameMesh(MeshAsset);
	}
}

void FEditorSkeletalMeshViewerWidget::UpdateInput(float DeltaTime)
{
	if (!bHasPreviewViewportRect || !PreviewViewportClient)
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		return;
	}

	FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());
	const POINT MousePos = InputFrame.GetMousePosition();
	const float MouseX = static_cast<float>(MousePos.x);
	const float MouseY = static_cast<float>(MousePos.y);
	const bool bMouseInPreviewViewport =
		MouseX >= PreviewViewportMin.x && MouseX <= PreviewViewportMax.x &&
		MouseY >= PreviewViewportMin.y && MouseY <= PreviewViewportMax.y;

	const bool bRightMouseDown = InputFrame.GetRawSnapshotForDebug().bRightMouseDown;
	const bool bMiddleMouseDown = InputFrame.GetRawSnapshotForDebug().bMiddleMouseDown;
	const bool bAnyCaptureButtonDown = bRightMouseDown || bMiddleMouseDown;

	if (!bPreviewViewportWantsMouseCapture)
	{
		if (bMouseInPreviewViewport &&
			(InputFrame.GetRawSnapshotForDebug().bRightMousePressed ||
				InputFrame.GetRawSnapshotForDebug().bMiddleMousePressed))
		{
			bPreviewViewportWantsMouseCapture = true;
		}
	}
	else if (!bAnyCaptureButtonDown)
	{
		bPreviewViewportWantsMouseCapture = false;
	}

	bPreviewViewportWantsKeyboardCapture = bPreviewViewportWantsMouseCapture && bRightMouseDown;
}

bool FEditorSkeletalMeshViewerWidget::OpenFbxAsset(const FString& FbxPath)
{
	CurrentFbxPath = FbxPath;
	CurrentSceneAsset = FFBXManager::LoadFbxScene(FbxPath);
	SelectedResourceIndex = -1;
	SelectedBoneIndex = -1;

	if (!CurrentSceneAsset)
	{
		StatusMessage = "Failed to load FBX scene";
		return false;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
	if (SkeletalMeshes.empty())
	{
		StatusMessage = "FBX loaded, but no SkeletalMesh was found";
		return false;
	}

	SelectedResourceIndex = 0;
	SetPreviewMesh(GetSelectedSkeletalMesh());
	
	StatusMessage = "FBX loaded";
	return true;
}

void FEditorSkeletalMeshViewerWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	FEditorSettings& Settings = FEditorSettings::Get();
	ImGuiWindowClass ViewerWindowClass;
	ViewerWindowClass.ParentViewportId = 0;
	ViewerWindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
	ImGui::SetNextWindowClass(&ViewerWindowClass);
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("SkeletalMesh Viewer", &Settings.UI.bSkeletalMeshViewer, ImGuiWindowFlags_MenuBar))
	{
		bHasPreviewViewportRect = false;
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		ImGui::End();
		return;
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Asset"))
		{
			ImGui::MenuItem("Open SkeletalMesh...", nullptr, false, false);
			ImGui::MenuItem("Close", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	if (ImGui::BeginTable(
		"##SkeletalMeshViewerLayout",
		3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 260.0f);
		ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 280.0f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		RenderResourcePanel();
		ImGui::Separator();
		RenderBonePanel();

		ImGui::TableSetColumnIndex(1);
		RenderViewportPanel(DeltaTime);

		ImGui::TableSetColumnIndex(2);
		RenderTransformPanel();

		ImGui::EndTable();
	}

	ImGui::End();
}

void FEditorSkeletalMeshViewerWidget::RenderResourcePanel()
{
	if (ImGui::BeginChild("##SkeletalMeshResources", ImVec2(0.0f, 120.0f), false))
	{
		ImGui::TextUnformatted("Resources");
		ImGui::Separator();

		if (!CurrentSceneAsset)
		{
			ImGui::TextDisabled("%s", StatusMessage.c_str());
		}
		else
		{
			const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
			if (SkeletalMeshes.empty())
			{
				ImGui::TextDisabled("No SkeletalMesh in this FBX");
			}

			for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(SkeletalMeshes.size()); ++MeshIndex)
			{
				const USkeletalMesh* Mesh = SkeletalMeshes[MeshIndex];
				const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
				FString Label = "SkeletalMesh " + std::to_string(MeshIndex);
				if (MeshAsset && !MeshAsset->PathFileName.empty())
				{
					Label = MeshAsset->PathFileName;
				}

				const bool bSelected = SelectedResourceIndex == MeshIndex;
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					SelectedResourceIndex = MeshIndex;
					SelectedBoneIndex = -1;
					SetPreviewMesh(GetSelectedSkeletalMesh());
				}
			}
		}
	}
	ImGui::EndChild();
}

void FEditorSkeletalMeshViewerWidget::RenderViewportPanel(float DeltaTime)
{
	(void)DeltaTime;

	ImVec2 AvailableSize = ImGui::GetContentRegionAvail();
	if (AvailableSize.x < 1.0f)
	{
		AvailableSize.x = 1.0f;
	}
	if (AvailableSize.y < 1.0f)
	{
		AvailableSize.y = 1.0f;
	}

	ImGui::BeginChild("##SkeletalMeshViewport", AvailableSize, true, ImGuiWindowFlags_NoScrollbar);

	ImVec2 ViewportMin = ImGui::GetCursorScreenPos();
	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	if (ViewportSize.x < 1.0f)
	{
		ViewportSize.x = 1.0f;
	}
	if (ViewportSize.y < 1.0f)
	{
		ViewportSize.y = 1.0f;
	}
	PreviewViewportMin = ViewportMin;
	PreviewViewportMax = ImVec2(ViewportMin.x + ViewportSize.x, ViewportMin.y + ViewportSize.y);
	bHasPreviewViewportRect = true;

	USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
	if (!SelectedMesh && PreviewSkeletalMesh)
	{
		SelectedMesh = PreviewSkeletalMesh;
	}
	if (!SelectedMesh)
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		ImGui::TextDisabled("No SkeletalMesh loaded");
		ImGui::EndChild();
		return;
	}

	EnsurePreviewScene();

	// 에디터 메인 뷰포트에서 액터를 선택하는 등 외부 동작 후 preview의 SceneProxy가
	// 누락되는 케이스 방어 — 컴포넌트 상태가 어긋났으면 매 프레임 자가 복구한다.
	// 사용자의 카메라 조작을 보존하기 위해 복구 경로에서는 FrameMesh를 건너뛴다.
	if (PreviewMeshComponent &&
		(PreviewMeshComponent->GetSkeletalMesh() != SelectedMesh ||
			PreviewMeshComponent->GetSceneProxy() == nullptr))
	{
		SetPreviewMesh(SelectedMesh, /*bResetCamera=*/false);
	}

	if (PreviewSkeletalMesh &&
		PreviewMeshComponent &&
		PreviewMeshComponent->GetSkeletalMesh() != PreviewSkeletalMesh)
	{
		PreviewMeshComponent->SetSkeletalMesh(PreviewSkeletalMesh);
	}
	if (PreviewSkeletalMesh &&
		PreviewMeshComponent &&
		!PreviewMeshComponent->GetSceneProxy())
	{
		PreviewMeshComponent->MarkRenderStateDirty();
	}

	if (PreviewViewport && PreviewViewportClient && EditorEngine)
	{
		const bool bViewportHovered = ImGui::IsWindowHovered();
		const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool bAnyCaptureButtonDown = bRightMouseDown || bMiddleMouseDown;

		if (!bPreviewViewportWantsMouseCapture)
		{
			if (bViewportHovered &&
				(ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
					ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
			{
				bPreviewViewportWantsMouseCapture = true;
			}
		}
		else if (!bAnyCaptureButtonDown)
		{
			bPreviewViewportWantsMouseCapture = false;
		}

		bPreviewViewportWantsKeyboardCapture =
			bPreviewViewportWantsMouseCapture && bRightMouseDown;

		FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());
		PreviewViewportClient->Tick(
			DeltaTime,
			bViewportHovered || bPreviewViewportWantsMouseCapture,
			bPreviewViewportWantsMouseCapture,
			InputFrame);

		PreviewViewport->RequestResize(
			static_cast<uint32>(ViewportSize.x),
			static_cast<uint32>(ViewportSize.y));

		EditorEngine->RenderSkeletalMeshViewerPreview(
			PreviewWorld,
			PreviewViewport,
			PreviewViewportClient);

		if (PreviewViewport->GetSRV())
		{
			ImGui::Image(
				(ImTextureID)PreviewViewport->GetSRV(),
				ViewportSize);
		}
		else
		{
			ImGui::TextDisabled("Preview render target is not ready");
		}
	}
	else
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		ImGui::TextDisabled("Preview scene is not ready");
	}

	ImGui::EndChild();
}

void FEditorSkeletalMeshViewerWidget::RenderBonePanel()
{
	if (ImGui::BeginChild("##SkeletalMeshBoneHierarchy", ImVec2(0.0f, 0.0f), false))
	{
		ImGui::TextUnformatted("Bone Hierarchy");
		ImGui::Separator();

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset)
		{
			ImGui::TextDisabled("No SkeletalMesh selected");
		}
		else if (MeshAsset->Bones.empty())
		{
			ImGui::TextDisabled("No bones found");
		}
		else
		{
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (MeshAsset->Bones[BoneIndex].ParentIndex < 0)
				{
					RenderBoneTreeNode(MeshAsset->Bones, BoneIndex, SelectedBoneIndex);
				}
			}
		}
	}
	ImGui::EndChild();
}

void FEditorSkeletalMeshViewerWidget::RenderTransformPanel()
{
	if (ImGui::BeginChild("##SkeletalMeshDetails", ImVec2(0.0f, 0.0f), false))
	{
		ImGui::TextUnformatted("Transform");
		ImGui::Separator();

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset || SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
		{
			ImGui::TextDisabled("No bone selected");
		}
		else
		{
			const FBoneInfo& Bone = MeshAsset->Bones[SelectedBoneIndex];
			const FVector LocationVector = Bone.LocalBindPose.GetLocation();
			const FVector RotationVector = Bone.LocalBindPose.GetEuler();
			const FVector ScaleVector = Bone.LocalBindPose.GetScale();
			float Location[3] = { LocationVector.X, LocationVector.Y, LocationVector.Z };
			float Rotation[3] = { RotationVector.X, RotationVector.Y, RotationVector.Z };
			float Scale[3] = { ScaleVector.X, ScaleVector.Y, ScaleVector.Z };

			ImGui::TextUnformatted(Bone.Name.c_str());
			ImGui::Separator();
			ImGui::InputFloat3("Location", Location, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Rotation", Rotation, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Scale", Scale, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}
	}
	ImGui::EndChild();
}

USkeletalMesh* FEditorSkeletalMeshViewerWidget::GetSelectedSkeletalMesh() const
{
	if (!CurrentSceneAsset)
	{
		return nullptr;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
	if (SelectedResourceIndex < 0 || SelectedResourceIndex >= static_cast<int32>(SkeletalMeshes.size()))
	{
		return nullptr;
	}

	return SkeletalMeshes[SelectedResourceIndex];
}
