#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/Settings/EditorSettings.h"
#include "Mesh/MeshManager.h"
#include "Mesh/FBX/FBXSceneAsset.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "Runtime/Engine.h"
#include "Viewport/Viewport.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/SkeletalGizmoComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "Core/ProjectSettings.h"
#include "Platform/Paths.h"
#include "Engine/UI/ImGui/ImGuiViewportPresenter.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Resource/MeshBufferManager.h"
#include "WICTextureLoader.h"
#include "Debug/DrawDebugHelpers.h"

namespace
{
	void DrawDebugOctahedralBone(UWorld* World, const FVector& Head, const FVector& Tail, const FColor& Color)
	{
		FVector Dir = Tail - Head;
		float Length = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);

		// 뼈의 길이가 너무 짧으면 그리지 않음
		if (Length < 0.001f) return;

		Dir.X /= Length;
		Dir.Y /= Length;
		Dir.Z /= Length;

		// --- [설정값] ---
		// OffsetRatio: 마름모(가장 두꺼운 부분)가 위치할 지점. 
		// 0.8f = 부모에서 출발해 자식 쪽으로 80% 간 지점 (자식 쪽에 무게가 실린 형태)
		// (참고: 블렌더 기본 형태는 0.1f ~ 0.2f로 부모 쪽에 무게가 실려 있습니다.)
		float OffsetRatio = 0.2f;

		// BoneThickness: 뼈의 두께. 길이에 비례하게 설정 (길이의 10%)
		float BoneThickness = Length * 0.05f;
		// --------------

		// 가장 두꺼운 중심점 계산
		FVector MidPos = Head + FVector(Dir.X * Length * OffsetRatio, Dir.Y * Length * OffsetRatio, Dir.Z * Length * OffsetRatio);

		// 직교하는 두 축(Right, Up) 계산을 위한 임의의 Up 벡터
		FVector ArbitraryUp = (std::abs(Dir.Z) > 0.99f) ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, 1.0f);

		// Right = ArbitraryUp x Dir (외적)
		FVector Right(
			ArbitraryUp.Y * Dir.Z - ArbitraryUp.Z * Dir.Y,
			ArbitraryUp.Z * Dir.X - ArbitraryUp.X * Dir.Z,
			ArbitraryUp.X * Dir.Y - ArbitraryUp.Y * Dir.X
		);
		float RightLen = std::sqrt(Right.X * Right.X + Right.Y * Right.Y + Right.Z * Right.Z);
		Right.X /= RightLen; Right.Y /= RightLen; Right.Z /= RightLen;

		// Up = Dir x Right (외적)
		FVector Up(
			Dir.Y * Right.Z - Dir.Z * Right.Y,
			Dir.Z * Right.X - Dir.X * Right.Z,
			Dir.X * Right.Y - Dir.Y * Right.X
		);

		// 마름모의 4개 꼭짓점
		FVector P1 = MidPos + FVector(Right.X * BoneThickness, Right.Y * BoneThickness, Right.Z * BoneThickness);
		FVector P2 = MidPos + FVector(Up.X * BoneThickness, Up.Y * BoneThickness, Up.Z * BoneThickness);
		FVector P3 = MidPos - FVector(Right.X * BoneThickness, Right.Y * BoneThickness, Right.Z * BoneThickness);
		FVector P4 = MidPos - FVector(Up.X * BoneThickness, Up.Y * BoneThickness, Up.Z * BoneThickness);

		// 선 12가닥 그리기
		// 1. 밑면 (마름모 링)
		DrawDebugNodepthLine(World, P1, P2, Color);
		DrawDebugNodepthLine(World, P2, P3, Color);
		DrawDebugNodepthLine(World, P3, P4, Color);
		DrawDebugNodepthLine(World, P4, P1, Color);

		// 2. Head(부모)에서 마름모로 이어지는 선
		DrawDebugNodepthLine(World, Head, P1, Color);
		DrawDebugNodepthLine(World, Head, P2, Color);
		DrawDebugNodepthLine(World, Head, P3, Color);
		DrawDebugNodepthLine(World, Head, P4, Color);

		// 3. Tail(자식)에서 마름모로 이어지는 선
		DrawDebugNodepthLine(World, Tail, P1, Color);
		DrawDebugNodepthLine(World, Tail, P2, Color);
		DrawDebugNodepthLine(World, Tail, P3, Color);
		DrawDebugNodepthLine(World, Tail, P4, Color);
	}

FVector GetRotationEulerNoScale(const FMatrix& Matrix)
{
	FMatrix RotationMatrix = Matrix;
	const FVector Scale = Matrix.GetScale();
	if (std::abs(Scale.X) > 0.0001f)
	{
		RotationMatrix.M[0][0] /= Scale.X;
		RotationMatrix.M[0][1] /= Scale.X;
		RotationMatrix.M[0][2] /= Scale.X;
	}
	if (std::abs(Scale.Y) > 0.0001f)
	{
		RotationMatrix.M[1][0] /= Scale.Y;
		RotationMatrix.M[1][1] /= Scale.Y;
		RotationMatrix.M[1][2] /= Scale.Y;
	}
	if (std::abs(Scale.Z) > 0.0001f)
	{
		RotationMatrix.M[2][0] /= Scale.Z;
		RotationMatrix.M[2][1] /= Scale.Z;
		RotationMatrix.M[2][2] /= Scale.Z;
	}
	RotationMatrix.SetLocation(FVector(0.0f, 0.0f, 0.0f));
	return RotationMatrix.GetEuler();
}

FMatrix MakeLocalPoseMatrix(const FVector& Location, const FVector& Rotation, const FVector& Scale)
{
	return FMatrix::MakeScaleMatrix(Scale) *
		FMatrix::MakeRotationEuler(Rotation) *
		FMatrix::MakeTranslationMatrix(Location);
}

enum class EViewerToolbarIcon : int32
{
	Setting = 0,
	ShowFlag,
	Translate,
	Rotate,
	Scale,
	WorldSpace,
	LocalSpace,
	TranslateSnap,
	RotateSnap,
	ScaleSnap,
	Count
};

const wchar_t* GetViewerToolbarIconFileName(EViewerToolbarIcon Icon)
{
	switch (Icon)
	{
	case EViewerToolbarIcon::Setting: return L"Setting.png";
	case EViewerToolbarIcon::ShowFlag: return L"Show_Flag.png";
	case EViewerToolbarIcon::Translate: return L"Translate.png";
	case EViewerToolbarIcon::Rotate: return L"Rotate.png";
	case EViewerToolbarIcon::Scale: return L"Scale.png";
	case EViewerToolbarIcon::WorldSpace: return L"WorldSpace.png";
	case EViewerToolbarIcon::LocalSpace: return L"LocalSpace.png";
	case EViewerToolbarIcon::TranslateSnap: return L"Translate_Snap.png";
	case EViewerToolbarIcon::RotateSnap: return L"Rotate_Snap.png";
	case EViewerToolbarIcon::ScaleSnap: return L"Scale_Snap.png";
	default: return L"";
	}
}

ID3D11ShaderResourceView** GetViewerToolbarIconTable()
{
	static ID3D11ShaderResourceView* Icons[static_cast<int32>(EViewerToolbarIcon::Count)] = {};
	return Icons;
}

bool bViewerToolbarIconsLoaded = false;

void EnsureViewerToolbarIconsLoaded()
{
	if (bViewerToolbarIconsLoaded || !GEngine)
	{
		return;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!Device)
	{
		return;
	}

	ID3D11ShaderResourceView** Icons = GetViewerToolbarIconTable();
	const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/ToolIcons/");
	for (int32 i = 0; i < static_cast<int32>(EViewerToolbarIcon::Count); ++i)
	{
		const std::wstring FilePath = IconDir + GetViewerToolbarIconFileName(static_cast<EViewerToolbarIcon>(i));
		DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &Icons[i]);
	}

	bViewerToolbarIconsLoaded = true;
}

bool DrawViewerToolbarIconButton(const char* Id, EViewerToolbarIcon Icon, const char* FallbackLabel)
{
	constexpr float IconSize = 16.0f;
	ID3D11ShaderResourceView* IconSRV = GetViewerToolbarIconTable()[static_cast<int32>(Icon)];
	if (!IconSRV)
	{
		return ImGui::Button(FallbackLabel);
	}

	return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(IconSRV), ImVec2(IconSize, IconSize));
}

float CalcViewerIconButtonWidth()
{
	return 16.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float CalcViewerTextButtonWidth(const char* Label)
{
	return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

FString FormatViewerStatCount(size_t Value)
{
	std::string Text = std::to_string(Value);
	for (int32 InsertPos = static_cast<int32>(Text.length()) - 3; InsertPos > 0; InsertPos -= 3)
	{
		Text.insert(static_cast<size_t>(InsertPos), ",");
	}
	return Text;
}

void DrawViewerMeshStatsOverlay(const FSkeletalMesh* MeshAsset, const ImVec2& ViewportMin)
{
	if (!MeshAsset)
	{
		return;
	}

	const FString VerticesText = "Vertices: " + FormatViewerStatCount(MeshAsset->Vertices.size());
	const FString TrianglesText = "Triangles: " + FormatViewerStatCount(MeshAsset->Indices.size() / 3);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 ShadowColor = IM_COL32(0, 0, 0, 220);
	const ImU32 TextColor = IM_COL32(230, 230, 230, 255);
	const float LineHeight = ImGui::GetTextLineHeight();
	ImVec2 TextPos(ViewportMin.x + 8.0f, ViewportMin.y + 8.0f);

	auto DrawLine = [&](const FString& Text)
	{
		DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), ShadowColor, Text.c_str());
		DrawList->AddText(TextPos, TextColor, Text.c_str());
		TextPos.y += LineHeight;
	};

	DrawLine(VerticesText);
	DrawLine(TrianglesText);
}

// 임시 기즈모 디버그 라인
void DrawViewerGizmoDebugLines(
	FSkeletalMeshViewerViewportClient* PreviewClient,
	const ImVec2& ViewportMin,
	const ImVec2& ViewportSize)
{
	if (!PreviewClient || ViewportSize.x <= 0.0f || ViewportSize.y <= 0.0f)
	{
		return;
	}

	UCameraComponent* Camera = PreviewClient->GetCamera();
	UGizmoComponent* Gizmo = PreviewClient->GetBoneSelectionManager().GetGizmo();
	if (!Camera || !Gizmo || !Gizmo->IsActive())
	{
		return;
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

	auto ProjectLocalToScreen = [&](const FVector& LocalPosition, ImVec2& OutScreen) -> bool
	{
		const FVector ClipSpace = LocalToClip.TransformPositionWithW(LocalPosition);
		if (!std::isfinite(ClipSpace.X) || !std::isfinite(ClipSpace.Y) || ClipSpace.Z < 0.0f)
		{
			return false;
		}

		OutScreen.x = ViewportMin.x + (ClipSpace.X * 0.5f + 0.5f) * ViewportSize.x;
		OutScreen.y = ViewportMin.y + (1.0f - (ClipSpace.Y * 0.5f + 0.5f)) * ViewportSize.y;
		return true;
	};

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const FMeshData& MeshData = FMeshBufferManager::Get().GetMeshData(EMeshShape::TransGizmo);
	const ImU32 AxisColors[4] =
	{
		IM_COL32(255, 60, 60, 220),
		IM_COL32(60, 255, 60, 220),
		IM_COL32(80, 120, 255, 220),
		IM_COL32(255, 255, 255, 220)
	};
	bool bBoundsValid[4] = {};
	ImVec2 BoundsMin[4] = {};
	ImVec2 BoundsMax[4] = {};

	auto UpdateBounds = [&](int32 SubID, const ImVec2& Point)
	{
		if (SubID < 0 || SubID > 3)
		{
			return;
		}

		if (!bBoundsValid[SubID])
		{
			BoundsMin[SubID] = Point;
			BoundsMax[SubID] = Point;
			bBoundsValid[SubID] = true;
			return;
		}

		BoundsMin[SubID].x = (std::min)(BoundsMin[SubID].x, Point.x);
		BoundsMin[SubID].y = (std::min)(BoundsMin[SubID].y, Point.y);
		BoundsMax[SubID].x = (std::max)(BoundsMax[SubID].x, Point.x);
		BoundsMax[SubID].y = (std::max)(BoundsMax[SubID].y, Point.y);
	};

	const uint32 AxisMask = Gizmo->GetAxisMask();
	for (uint32 IndexOffset = 0; IndexOffset + 2 < static_cast<uint32>(MeshData.Indices.size()); IndexOffset += 3)
	{
		const uint32 Index0 = MeshData.Indices[IndexOffset + 0];
		const uint32 Index1 = MeshData.Indices[IndexOffset + 1];
		const uint32 Index2 = MeshData.Indices[IndexOffset + 2];
		if (Index0 >= MeshData.Vertices.size() || Index1 >= MeshData.Vertices.size() || Index2 >= MeshData.Vertices.size())
		{
			continue;
		}

		const FVertex& Vertex0 = MeshData.Vertices[Index0];
		const FVertex& Vertex1 = MeshData.Vertices[Index1];
		const FVertex& Vertex2 = MeshData.Vertices[Index2];
		const int32 SubID = Vertex0.SubID;
		if (SubID < 3 && (AxisMask & (1u << SubID)) == 0)
		{
			continue;
		}

		ImVec2 Screen0, Screen1, Screen2;
		if (!ProjectLocalToScreen(Vertex0.Position, Screen0) ||
			!ProjectLocalToScreen(Vertex1.Position, Screen1) ||
			!ProjectLocalToScreen(Vertex2.Position, Screen2))
		{
			continue;
		}

		const ImU32 Color = AxisColors[(SubID >= 0 && SubID <= 3) ? SubID : 3];
		DrawList->AddLine(Screen0, Screen1, Color, 1.0f);
		DrawList->AddLine(Screen1, Screen2, Color, 1.0f);
		DrawList->AddLine(Screen2, Screen0, Color, 1.0f);

		UpdateBounds(SubID, Screen0);
		UpdateBounds(SubID, Screen1);
		UpdateBounds(SubID, Screen2);
	}

	ImVec2 CenterScreen;
	if (ProjectLocalToScreen(FVector::ZeroVector, CenterScreen))
	{
		DrawList->AddCircleFilled(CenterScreen, 4.0f, IM_COL32(255, 255, 255, 255));
	}

	for (int32 SubID = 0; SubID < 4; ++SubID)
	{
		if (bBoundsValid[SubID])
		{
			DrawList->AddRect(BoundsMin[SubID], BoundsMax[SubID], AxisColors[SubID], 0.0f, 0, 1.5f);
		}
	}
}
// 임시 기즈모 디버그 라인

void DrawViewerShowFlagsControls(FViewportRenderOptions& Opts, const char* TableId)
{
	ImGui::Text("Show");
	if (ImGui::BeginTable(TableId, 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Checkbox("Primitives", &Opts.ShowFlags.bPrimitives);
		ImGui::TableNextColumn();
		ImGui::Checkbox("BillboardText", &Opts.ShowFlags.bBillboardText);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Grid", &Opts.ShowFlags.bGrid);
		ImGui::TableNextColumn();
		ImGui::Checkbox("World Axis", &Opts.ShowFlags.bWorldAxis);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Gizmo", &Opts.ShowFlags.bGizmo);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Checkbox("Bounding Volume", &Opts.ShowFlags.bBoundingVolume);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Collision", &Opts.ShowFlags.bCollisionShapes);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Debug Draw", &Opts.ShowFlags.bDebugDraw);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Octree", &Opts.ShowFlags.bOctree);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Fog", &Opts.ShowFlags.bFog);
		ImGui::TableNextColumn();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Checkbox("FXAA", &Opts.ShowFlags.bFXAA);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Visualize2.5D", &Opts.ShowFlags.bVisualize25DCulling);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Shadows", &FProjectSettings::Get().Shadow.bEnabled);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Shadow Frustum", &Opts.ShowFlags.bShowShadowFrustum);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Picking BVH", &Opts.ShowFlags.bPickingBVH);
		ImGui::Checkbox("Collision BVH", &Opts.ShowFlags.bCollisionBVH);

		ImGui::EndTable();
	}
}

void RenderViewerTransformToolbar(FSkeletalMeshViewerViewportClient* PreviewClient, UEditorEngine* EditorEngine)
{
	constexpr float ButtonSpacing = 4.0f;
	constexpr float GroupSpacing = 12.0f;

	UGizmoComponent* Gizmo = PreviewClient ? PreviewClient->GetBoneSelectionManager().GetGizmo() : nullptr;

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

	auto DrawGizmoIcon = [&](const char* Id, EViewerToolbarIcon Icon, EGizmoMode TargetMode, const char* FallbackLabel) -> bool
	{
		const bool bSelected = Gizmo && Gizmo->GetMode() == TargetMode;
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
		}
		const bool bClicked = DrawViewerToolbarIconButton(Id, Icon, FallbackLabel);
		if (bSelected)
		{
			ImGui::PopStyleColor();
		}
		return bClicked;
	};

	if (!Gizmo)
	{
		ImGui::BeginDisabled();
	}
	if (DrawGizmoIcon("##ViewerTranslateToolIcon", EViewerToolbarIcon::Translate, EGizmoMode::Translate, "Translate") && Gizmo)
	{
		Gizmo->SetTranslateMode();
	}
	ImGui::SameLine(0.0f, ButtonSpacing);
	if (DrawGizmoIcon("##ViewerRotateToolIcon", EViewerToolbarIcon::Rotate, EGizmoMode::Rotate, "Rotate") && Gizmo)
	{
		Gizmo->SetRotateMode();
	}
	ImGui::SameLine(0.0f, ButtonSpacing);
	if (DrawGizmoIcon("##ViewerScaleToolIcon", EViewerToolbarIcon::Scale, EGizmoMode::Scale, "Scale") && Gizmo)
	{
		Gizmo->SetScaleMode();
	}
	if (!Gizmo)
	{
		ImGui::EndDisabled();
	}

	ImGui::PopStyleColor(3);

	FEditorSettings& Settings = FEditorSettings::Get();

	ImGui::SameLine(0.0f, GroupSpacing);
	const bool bWorldCoord = Settings.CoordSystem == EEditorCoordSystem::World;
	if (bWorldCoord)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
	}
	if (DrawViewerToolbarIconButton(
		"##ViewerCoordSystemIcon",
		bWorldCoord ? EViewerToolbarIcon::WorldSpace : EViewerToolbarIcon::LocalSpace,
		bWorldCoord ? "World" : "Local"))
	{
		if (EditorEngine)
		{
			EditorEngine->ToggleCoordSystem();
		}
		else
		{
			Settings.CoordSystem = bWorldCoord ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
		}
		if (EditorEngine)
		{
			EditorEngine->ApplyTransformSettingsToGizmo(Gizmo);
		}
	}
	if (bWorldCoord)
	{
		ImGui::PopStyleColor();
	}

	bool bSnapChanged = false;
	auto DrawSnapControl = [&](const char* Id, EViewerToolbarIcon Icon, const char* FallbackLabel, bool& bEnabled, float& Value, float MinValue)
	{
		ImGui::SameLine(0.0f, 6.0f);
		ImGui::PushID(Id);
		const bool bWasEnabled = bEnabled;
		if (bWasEnabled)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.58f, 0.88f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.42f, 0.72f, 1.0f));
		}
		if (DrawViewerToolbarIconButton("##SnapToggle", Icon, FallbackLabel))
		{
			bEnabled = !bEnabled;
			bSnapChanged = true;
		}
		if (bWasEnabled)
		{
			ImGui::PopStyleColor(3);
		}
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::SetNextItemWidth(48.0f);
		if (ImGui::InputFloat("##Value", &Value, 0.0f, 0.0f, "%.2f"))
		{
			if (Value < MinValue)
			{
				Value = MinValue;
			}
			bSnapChanged = true;
		}
		ImGui::PopID();
	};

	DrawSnapControl("ViewerTranslateSnap", EViewerToolbarIcon::TranslateSnap, "T", Settings.bEnableTranslationSnap, Settings.TranslationSnapSize, 0.001f);
	DrawSnapControl("ViewerRotateSnap", EViewerToolbarIcon::RotateSnap, "R", Settings.bEnableRotationSnap, Settings.RotationSnapSize, 0.001f);
	DrawSnapControl("ViewerScaleSnap", EViewerToolbarIcon::ScaleSnap, "S", Settings.bEnableScaleSnap, Settings.ScaleSnapSize, 0.001f);

	if (EditorEngine && (bSnapChanged || Gizmo))
	{
		EditorEngine->ApplyTransformSettingsToGizmo(Gizmo);
	}
}

void RenderViewerViewportToolbar(FSkeletalMeshViewerViewportClient* PreviewClient)
{
	if (!PreviewClient)
	{
		return;
	}

	EnsureViewerToolbarIconsLoaded();
	FViewportRenderOptions& Opts = PreviewClient->GetRenderOptions();

	static const char* ViewportTypeNames[] = {
		"Perspective", "Top", "Bottom", "Left", "Right", "Front", "Back", "Free Orthographic"
	};
	constexpr int32 ViewportTypeCount = sizeof(ViewportTypeNames) / sizeof(ViewportTypeNames[0]);
	int32 CurrentTypeIdx = static_cast<int32>(Opts.ViewportType);
	const char* CurrentTypeName =
		(CurrentTypeIdx >= 0 && CurrentTypeIdx < ViewportTypeCount)
		? ViewportTypeNames[CurrentTypeIdx]
		: ViewportTypeNames[0];

	static const char* ViewModeNames[] = { "Phong", "Unlit", "Gouraud", "Lambert", "Wireframe", "SceneDepth", "WorldNormal", "LightCulling" };
	const int32 ViewModeIndex = static_cast<int32>(Opts.ViewMode);
	const char* CurrentViewModeName = (ViewModeIndex >= 0 && ViewModeIndex < static_cast<int32>(EViewMode::Count))
		? ViewModeNames[ViewModeIndex]
		: ViewModeNames[static_cast<int32>(EViewMode::Lit_Phong)];

	const float RowStartX = ImGui::GetCursorPosX();
	const float RowRightX = RowStartX + ImGui::GetContentRegionAvail().x;
	RenderViewerTransformToolbar(PreviewClient, Cast<UEditorEngine>(GEngine));

	const float RightToolbarWidth =
		CalcViewerTextButtonWidth(CurrentTypeName) +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerTextButtonWidth(CurrentViewModeName) +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerIconButtonWidth() +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerIconButtonWidth();
	const float RightToolbarStartX = RowRightX - RightToolbarWidth;

	if (ImGui::GetCursorPosX() < RightToolbarStartX)
	{
		ImGui::SameLine();
		ImGui::SetCursorPosX(RightToolbarStartX);
	}
	else
	{
		ImGui::SameLine();
	}

	if (ImGui::Button(CurrentTypeName))
	{
		ImGui::OpenPopup("ViewerViewportTypePopup");
	}
	if (ImGui::BeginPopup("ViewerViewportTypePopup"))
	{
		for (int32 TypeIndex = 0; TypeIndex < ViewportTypeCount; ++TypeIndex)
		{
			const bool bSelected = TypeIndex == CurrentTypeIdx;
			if (ImGui::Selectable(ViewportTypeNames[TypeIndex], bSelected))
			{
				PreviewClient->SetViewportType(static_cast<ELevelViewportType>(TypeIndex));
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button(CurrentViewModeName))
	{
		ImGui::OpenPopup("ViewerViewModePopup");
	}
	if (ImGui::BeginPopup("ViewerViewModePopup"))
	{
		int32 CurrentMode = (ViewModeIndex >= 0 && ViewModeIndex < static_cast<int32>(EViewMode::Count))
			? ViewModeIndex
			: static_cast<int32>(EViewMode::Lit_Phong);

		if (ImGui::BeginTable("ViewerViewModeTable", 3, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("Unlit", &CurrentMode, static_cast<int32>(EViewMode::Unlit));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Phong", &CurrentMode, static_cast<int32>(EViewMode::Lit_Phong));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Gouraud", &CurrentMode, static_cast<int32>(EViewMode::Lit_Gouraud));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("Lambert", &CurrentMode, static_cast<int32>(EViewMode::Lit_Lambert));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Wireframe", &CurrentMode, static_cast<int32>(EViewMode::Wireframe));
			ImGui::TableNextColumn();
			ImGui::RadioButton("SceneDepth", &CurrentMode, static_cast<int32>(EViewMode::SceneDepth));
			ImGui::TableNextColumn();
			ImGui::RadioButton("WorldNormal", &CurrentMode, static_cast<int32>(EViewMode::WorldNormal));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("LightCulling", &CurrentMode, static_cast<int32>(EViewMode::LightCulling));
			ImGui::EndTable();
		}

		Opts.ViewMode = static_cast<EViewMode>(CurrentMode);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (DrawViewerToolbarIconButton("##ViewerShowFlagsIcon", EViewerToolbarIcon::ShowFlag, "Show"))
	{
		ImGui::OpenPopup("ViewerShowFlagsPopup");
	}
	if (ImGui::BeginPopup("ViewerShowFlagsPopup"))
	{
		DrawViewerShowFlagsControls(Opts, "ViewerShowFlagsTable");
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (DrawViewerToolbarIconButton("##ViewerSettingsIcon", EViewerToolbarIcon::Setting, "Settings"))
	{
		ImGui::OpenPopup("ViewerSettingsPopup");
	}
	if (ImGui::BeginPopup("ViewerSettingsPopup"))
	{
		if (ImGui::CollapsingHeader("Viewport Utility Settings (Grid , Camera , SceneDepth , FXAA)"))
		{
			ImGui::Text("Grid");
			ImGui::SliderFloat("Spacing", &Opts.GridSpacing, 0.1f, 10.0f, "%.1f");
			ImGui::SliderInt("Half Line Count", &Opts.GridHalfLineCount, 10, 500);

			ImGui::Separator();
			ImGui::Text("Camera");
			ImGui::SliderFloat("Move Sensitivity", &Opts.CameraMoveSensitivity, 0.1f, 5.0f, "%.1f");
			ImGui::SliderFloat("Rotate Sensitivity", &Opts.CameraRotateSensitivity, 0.1f, 5.0f, "%.1f");

			ImGui::Separator();
			ImGui::Text("SceneDepth");
			ImGui::SliderFloat("Exponent", &Opts.Exponent, 1.0f, 512.0f, "%.0f");
			ImGui::Combo("Mode", &Opts.SceneDepthVisMode, "Power\0Linear\0");

			ImGui::Text("FXAA");
			ImGui::SliderFloat("EdgeThreshold", &Opts.EdgeThreshold, 0.06f, 0.333f, "%.3f");
			ImGui::SliderFloat("EdgeThresholdMin", &Opts.EdgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
		}

		if (ImGui::CollapsingHeader("Light Culling Settings"))
		{
			int32 CullingMode = static_cast<int32>(Opts.LightCullingMode);
			ImGui::RadioButton("Off", &CullingMode, static_cast<int32>(ELightCullingMode::Off));
			ImGui::SameLine();
			ImGui::RadioButton("Tile", &CullingMode, static_cast<int32>(ELightCullingMode::Tile));
			ImGui::SameLine();
			ImGui::RadioButton("Cluster", &CullingMode, static_cast<int32>(ELightCullingMode::Cluster));
			Opts.LightCullingMode = static_cast<ELightCullingMode>(CullingMode);
			ImGui::SliderFloat("HeatMapMax", &Opts.HeatMapMax, 1.0f, 100.0f, "%.0f");
			ImGui::Checkbox("Enable2.5DCulling", &Opts.Enable25DCulling);
			ImGui::Checkbox("Visualize2.5DCulling", &Opts.ShowFlags.bVisualize25DCulling);
		}

		ImGui::EndPopup();
	}
}

void RenderBoneTreeNode(const TArray<FBoneInfo>& Bones, int32 BoneIndex, int32& SelectedBoneIndex, int32& OutDoubleClickedBoneIndex)
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

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		OutDoubleClickedBoneIndex = BoneIndex;
	}

	if (bOpen)
	{
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
		{
			if (Bones[ChildIndex].ParentIndex == BoneIndex)
			{
				RenderBoneTreeNode(Bones, ChildIndex, SelectedBoneIndex, OutDoubleClickedBoneIndex);
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
	PreviewActor->bTickInEditor = true;

	PreviewMeshComponent = PreviewActor->AddComponent<USkeletalMeshComponent>();
	PreviewActor->SetRootComponent(PreviewMeshComponent);

	PreviewDirectionalLightActor = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	if (PreviewDirectionalLightActor)
	{
		PreviewDirectionalLightActor->InitDefaultComponents();
		PreviewDirectionalLightActor->bTickInEditor = true;
		PreviewDirectionalLightActor->SetActorLocation(FVector(5.0f, 0.0f, 5.0f));
		PreviewDirectionalLightActor->SetActorRotation(FRotator(15.0f, 180.0f, 0.0f));
	}

	PreviewViewportClient = new FSkeletalMeshViewerViewportClient();
	PreviewViewportClient->Initialize();
	PreviewViewportClient->SetPreviewWorld(PreviewWorld);

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
	PreviewDirectionalLightActor = nullptr;
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

	// [추가] 뷰포트 클라이언트의 본 셀렉션 매니저에 타겟 컴포넌트 전달
	if (PreviewViewportClient)
	{
		PreviewViewportClient->GetBoneSelectionManager().SetTargetSkeletalMesh(PreviewMeshComponent);
	}

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

void FEditorSkeletalMeshViewerWidget::TickPreviewScene(float DeltaTime)
{
	if (!PreviewWorld)
	{
		return;
	}

	PreviewWorld->Tick(DeltaTime, DeltaTime, LEVELTICK_ViewportsOnly);

	UpdateBoneDebugLines();
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
	CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);
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
					SelectedBoneIndex = -1; // UI 선택 초기화

					// [추가] 매니저의 선택 상태도 초기화 (기즈모 숨김 처리 등)
					if (PreviewViewportClient)
					{
						PreviewViewportClient->GetBoneSelectionManager().ClearSelection();
					}

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
	RenderViewerViewportToolbar(PreviewViewportClient);
	ImGui::Separator();

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
	ImGui::InvisibleButton(
		"##SkeletalMeshPreviewViewportInput",
		ViewportSize,
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle);
	const bool bViewportInputHovered = ImGui::IsItemHovered();
	const bool bViewportInputActive = ImGui::IsItemActive();

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
		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		const bool bViewportHovered =
			bViewportInputHovered ||
			bViewportInputActive ||
			(MousePos.x >= PreviewViewportMin.x && MousePos.x <= PreviewViewportMax.x &&
				MousePos.y >= PreviewViewportMin.y && MousePos.y <= PreviewViewportMax.y);
		const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool bAnyCaptureButtonDown = bRightMouseDown || bMiddleMouseDown;


		const uint32 NewWidth = static_cast<uint32>(ViewportSize.x);
		const uint32 NewHeight = static_cast<uint32>(ViewportSize.y);

		PreviewViewport->RequestResize(NewWidth, NewHeight);
		PreviewViewportClient->SetViewportRect(ViewportMin.x, ViewportMin.y, ViewportSize.x, ViewportSize.y);

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

		TickPreviewScene(DeltaTime);

		FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());
		PreviewViewportClient->Tick(
			DeltaTime,
			bViewportHovered || bPreviewViewportWantsMouseCapture,
			bPreviewViewportWantsMouseCapture,
			InputFrame);

		EditorEngine->RenderSkeletalMeshViewerPreview(
			PreviewWorld,
			PreviewViewport,
			PreviewViewportClient);

		if (PreviewViewport->GetSRV())
		{
			FImGuiViewportPresenter::DrawInCurrentWindow(
				PreviewViewport,
				FViewportPresentationRect(ViewportMin.x, ViewportMin.y, ViewportSize.x, ViewportSize.y));
			//// 임시 기즈모 디버그 라인
			//DrawViewerGizmoDebugLines(
			//	PreviewViewportClient,
			//	ViewportMin,
			//	ViewportSize);
			//// 임시 기즈모 디버그 라인
			//DrawViewerMeshStatsOverlay(
			//	SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr,
			//	ViewportMin);
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

		if (PreviewViewportClient)
		{
			SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
		}

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
			// [추가] 렌더링 전 기존 선택 인덱스 캐싱
			int32 PrevSelectedBoneIndex = SelectedBoneIndex;
			int32 DoubleClickedBoneIndex = -1;

			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{

				if (MeshAsset->Bones[BoneIndex].ParentIndex < 0)
				{
					RenderBoneTreeNode(MeshAsset->Bones, BoneIndex, SelectedBoneIndex, DoubleClickedBoneIndex);
				}
			}

			if (DoubleClickedBoneIndex != -1 && PreviewViewportClient)
			{
				PreviewViewportClient->FocusBone(PreviewMeshComponent, DoubleClickedBoneIndex);
			}

			// [추가] 클릭으로 인해 인덱스가 변했다면 매니저에 선택 명령 전달
			if (PrevSelectedBoneIndex != SelectedBoneIndex && PreviewViewportClient)
			{
				PreviewViewportClient->GetBoneSelectionManager().SelectBone(SelectedBoneIndex);
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

		if (PreviewViewportClient)
		{
			SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
		}

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset ||
			!PreviewMeshComponent ||
			SelectedBoneIndex < 0 ||
			SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
		{
			ImGui::TextDisabled("No bone selected");
		}
		else
		{
			const TArray<FMatrix>& LocalPoses = PreviewMeshComponent->GetLocalBonePoseMatrices();
			if (SelectedBoneIndex >= static_cast<int32>(LocalPoses.size()))
			{
				ImGui::TextDisabled("No bone pose available");
				ImGui::EndChild();
				return;
			}

			const FBoneInfo& Bone = MeshAsset->Bones[SelectedBoneIndex];
			const FMatrix& LocalPose = LocalPoses[SelectedBoneIndex];
			const FVector LocationVector = LocalPose.GetLocation();
			const FVector RotationVector = GetRotationEulerNoScale(LocalPose);
			const FVector ScaleVector = LocalPose.GetScale();
			float Location[3] = { LocationVector.X, LocationVector.Y, LocationVector.Z };
			float Rotation[3] = { RotationVector.X, RotationVector.Y, RotationVector.Z };
			float Scale[3] = { ScaleVector.X, ScaleVector.Y, ScaleVector.Z };

			ImGui::TextUnformatted(Bone.Name.c_str());
			ImGui::Separator();
			bool bChanged = false;
			bChanged |= ImGui::DragFloat3("Location", Location, 0.1f, -100000.0f, 100000.0f, "%.3f");
			bChanged |= ImGui::DragFloat3("Rotation", Rotation, 0.1f, -360.0f, 360.0f, "%.3f");
			bChanged |= ImGui::DragFloat3("Scale", Scale, 0.01f, 0.001f, 100.0f, "%.3f");

			if (bChanged)
			{
				FVector NewLocation(Location[0], Location[1], Location[2]);
				FVector NewRotation(Rotation[0], Rotation[1], Rotation[2]);
				FVector NewScale(
					(std::max)(Scale[0], 0.001f),
					(std::max)(Scale[1], 0.001f),
					(std::max)(Scale[2], 0.001f));
				PreviewMeshComponent->SetBoneLocalPose(
					SelectedBoneIndex,
					MakeLocalPoseMatrix(NewLocation, NewRotation, NewScale));
			}
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


void FEditorSkeletalMeshViewerWidget::UpdateBoneDebugLines()
{
	if (!PreviewSkeletalMesh || !PreviewMeshComponent)
	{
		return;
	}

	const auto& Bones = PreviewSkeletalMesh->GetSkeletalMeshAsset()->Bones;

	// 연산이 완료된 MeshSpace 배열을 가져옵니다.
	const auto& BoneMatrices = PreviewMeshComponent->GetMeshSpaceBoneMatrices();

	// 본 위치 디버그 스피어 크기 계산을 위한 대략적인 거리 측정
	FVector Subtract = PreviewMeshComponent->GetWorldAABB().Max - PreviewMeshComponent->GetWorldAABB().Min; // [주의] GetMeshSpaceBoneMatrices()의 결과가 최신이 되도록 강제 업데이트 트리거
	float distance = Subtract.Dot(Subtract);

	// 1. 컴포넌트 전체의 월드 변환 행렬
	FMatrix ComponentWorldTransform = PreviewMeshComponent->GetWorldMatrix();

	// 관절에 구체(Sphere) 그리기
	for (int i = 0; i < Bones.size(); ++i)
	{
		// bone sphere 그리기
		FMatrix WorldMat = BoneMatrices[i] * ComponentWorldTransform;
		FVector WorldPos = WorldMat.GetLocation();
		DrawDebugNodepthSphere(PreviewWorld, WorldPos, 0.001f * distance, 8, FColor::Yellow());
	}

	// 뼈다귀(Octahedral) 그리기
	for (int i = 0; i < Bones.size(); ++i)
	{
		const auto& Bone = Bones[i];

		if (Bone.ParentIndex < 0 || Bone.ParentIndex >= BoneMatrices.size())
		{
			continue;
		}

		// Component Space(Mesh Space) * Component World Transform = 최종 World Space
		FMatrix ParentWorldMat = BoneMatrices[Bone.ParentIndex] * ComponentWorldTransform;
		FVector ParentPos = ParentWorldMat.GetLocation();

		FMatrix ChildWorldMat = BoneMatrices[i] * ComponentWorldTransform;
		FVector ChildPos = ChildWorldMat.GetLocation();

		DrawDebugOctahedralBone(PreviewWorld, ParentPos, ChildPos, FColor::Green());

	}

}