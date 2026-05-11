#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/Settings/EditorSettings.h"
#include "ImGui/imgui.h"

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
		RenderViewportPanel();

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

		const bool bSelected = SelectedResourceIndex == 0;
		if (ImGui::Selectable("Empty Preview Slot", bSelected))
		{
			SelectedResourceIndex = 0;
			SelectedBoneIndex = -1;
		}
	}
	ImGui::EndChild();
}

void FEditorSkeletalMeshViewerWidget::RenderViewportPanel()
{
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

	const ImVec2 ViewportMin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	const ImVec2 ViewportMax(ViewportMin.x + ViewportSize.x, ViewportMin.y + ViewportSize.y);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportMin, ViewportMax, IM_COL32(42, 42, 42, 255));
	DrawList->AddRect(ViewportMin, ViewportMax, IM_COL32(95, 95, 95, 255));

	const ImVec2 Center((ViewportMin.x + ViewportMax.x) * 0.5f, (ViewportMin.y + ViewportMax.y) * 0.5f);
	DrawList->AddLine(ImVec2(ViewportMin.x + 20.0f, Center.y), ImVec2(ViewportMax.x - 20.0f, Center.y), IM_COL32(70, 120, 70, 255));
	DrawList->AddLine(ImVec2(Center.x, ViewportMin.y + 20.0f), ImVec2(Center.x, ViewportMax.y - 20.0f), IM_COL32(90, 90, 150, 255));

	const char* Message = "No SkeletalMesh loaded";
	const ImVec2 TextSize = ImGui::CalcTextSize(Message);
	DrawList->AddText(
		ImVec2(Center.x - TextSize.x * 0.5f, Center.y - TextSize.y * 0.5f),
		IM_COL32(210, 210, 210, 255),
		Message);

	ImGui::Dummy(ViewportSize);
	ImGui::EndChild();
}

void FEditorSkeletalMeshViewerWidget::RenderBonePanel()
{
	if (ImGui::BeginChild("##SkeletalMeshBoneHierarchy", ImVec2(0.0f, 0.0f), false))
	{
		ImGui::TextUnformatted("Bone Hierarchy");
		ImGui::Separator();

		const bool bRootSelected = SelectedBoneIndex == 0;
		if (ImGui::TreeNodeEx("root", ImGuiTreeNodeFlags_DefaultOpen | (bRootSelected ? ImGuiTreeNodeFlags_Selected : 0)))
		{
			if (ImGui::IsItemClicked())
			{
				SelectedBoneIndex = 0;
			}

			const bool bPreviewSelected = SelectedBoneIndex == 1;
			if (ImGui::Selectable("preview_bone", bPreviewSelected))
			{
				SelectedBoneIndex = 1;
			}

			ImGui::TreePop();
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

		if (SelectedBoneIndex < 0)
		{
			ImGui::TextDisabled("No bone selected");
		}
		else
		{
			float Location[3] = { 0.0f, 0.0f, 0.0f };
			float Rotation[3] = { 0.0f, 0.0f, 0.0f };
			float Scale[3] = { 1.0f, 1.0f, 1.0f };

			ImGui::InputFloat3("Location", Location, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Rotation", Rotation, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Scale", Scale, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}
	}
	ImGui::EndChild();
}
