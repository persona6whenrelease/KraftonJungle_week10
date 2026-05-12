#include "ContentBrowserElement.h"
#include "Platform/Paths.h"

bool ContentBrowserElement::RenderSelectSpace(ContentBrowserContext& Context)
{
	FString Name = FPaths::ToUtf8(ContentItem.Name);
	ImGui::PushID(Name.c_str());

	bIsSelected = Context.SelectedElement.get() == this;

	bool bIsClicked = ImGui::Selectable("##Element", bIsSelected, 0, Context.ContentSize);

	ImVec2 Min = ImGui::GetItemRectMin();
	ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();
	Max.y -= fontSize;
	Max.x -= fontSize * 0.5f;
	Min.x += fontSize * 0.5f;
	DrawList->AddImage(Icon, Min, Max);

	ImVec2 TextPos(Min.x, Max.y);

	if (bIsSelected && Context.bIsRenaming)
	{
		ImVec2 SavedScreenPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(TextPos);
		ImGui::PushItemWidth(Context.ContentSize.x);
		if (Context.bRenameFocusNeeded)
		{
			ImGui::SetKeyboardFocusHere();
			Context.bRenameFocusNeeded = false;
		}
		bool bConfirmed = ImGui::InputText("##RenameInput", Context.RenameBuffer, sizeof(Context.RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		bool bDeactivated = ImGui::IsItemDeactivated();
		ImGui::PopItemWidth();
		ImGui::SetCursorScreenPos(SavedScreenPos);

		if (bConfirmed)
		{
			std::wstring NewName = FPaths::ToWide(FString(Context.RenameBuffer));
			if (!NewName.empty() && NewName != ContentItem.Path.filename().wstring())
			{
				std::filesystem::path NewPath = ContentItem.Path.parent_path() / NewName;
				std::error_code ec;
				std::filesystem::rename(ContentItem.Path, NewPath, ec);
				if (!ec)
				{
					ContentItem.Path = NewPath;
					ContentItem.Name = NewName;
					Context.bIsNeedRefresh = true;
				}
			}
			Context.bIsRenaming = false;
		}
		else if (bDeactivated)
		{
			Context.bIsRenaming = false;
		}
	}
	else
	{
		FString Text = EllipsisText(FPaths::ToUtf8(ContentItem.Name), Context.ContentSize.x);
		DrawList->AddText(TextPos, ImGui::GetColorU32(ImGuiCol_Text), Text.c_str());
	}

	ImGui::PopID();

	return bIsClicked;
}

void ContentBrowserElement::Render(ContentBrowserContext& Context)
{
	if (RenderSelectSpace(Context))
	{
		Context.SelectedElement = shared_from_this();
		bIsSelected = true;
		OnLeftClicked(Context);
	}

	bool bDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (bDoubleClicked && !Context.bIsRenaming)
	{
		OnDoubleLeftClicked(Context);
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Rename"))
		{
			Context.SelectedElement = shared_from_this();
			bIsSelected = true;
			StartRename(Context);
		}
		ImGui::EndPopup();
	}

	if (bIsSelected && ImGui::IsKeyPressed(ImGuiKey_F2) && !Context.bIsRenaming)
	{
		StartRename(Context);
	}

	if (!Context.bIsRenaming && ImGui::BeginDragDropSource())
	{
		RenderSelectSpace(Context);
		ImGui::SetDragDropPayload(GetDragItemType(), &ContentItem, sizeof(ContentItem));
		OnDrag(Context);
		ImGui::EndDragDropSource();
	}
}

void ContentBrowserElement::StartRename(ContentBrowserContext& Context)
{
	Context.bIsRenaming = true;
	Context.bRenameFocusNeeded = true;
	FString CurrentName = FPaths::ToUtf8(ContentItem.Path.filename().wstring());
	strncpy_s(Context.RenameBuffer, sizeof(Context.RenameBuffer), CurrentName.c_str(), _TRUNCATE);
}

FString ContentBrowserElement::EllipsisText(const FString& text, float maxWidth)
{
	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();

	if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
		return text;

	const char* ellipsis = "...";
	float ellipsisWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ellipsis).x;

	std::string result = text;

	while (!result.empty())
	{
		result.pop_back();

		float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, result.c_str()).x;
		if (w + ellipsisWidth <= maxWidth)
		{
			result += ellipsis;
			break;
		}
	}

	return result;
}

void DirectoryElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	Context.CurrentPath = ContentItem.Path;
	Context.PendingRevealPath = ContentItem.Path;
	Context.bIsNeedRefresh = true;
}

#include "Serialization/SceneSaveManager.h"
#include "Editor/EditorEngine.h"
void SceneElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	std::filesystem::path ScenePath = ContentItem.Path;
	FString FilePath = FPaths::ToUtf8(ScenePath.wstring());
	UEditorEngine* EditorEngine = Context.EditorEngine;
	EditorEngine->LoadSceneFromPath(FilePath);
}

void FBXElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		return;
	}

	Context.EditorEngine->OpenSkeletalMeshViewerAsset(FPaths::ToUtf8(ContentItem.Path.wstring()));
}

void MaterialElement::OnLeftClicked(ContentBrowserContext& Context)
{
	MaterialInspector = { ContentItem.Path };
}

void MaterialElement::RenderDetail()
{
	MaterialInspector.Render();
}

void CurveElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	ContentBrowserElement::OnDoubleLeftClicked(Context);

	if (Context.EditorEngine)
	{
		Context.EditorEngine->OpenCurveAsset(FPaths::ToUtf8(ContentItem.Path.wstring()));
	}
}

void ExpandableElement::Render(ContentBrowserContext& Context)
{
	ContentBrowserElement::Render(Context);

	DrawExpandButton(Context);

	if (bExpanded)
	{
		DrawExpandedPanel(Context);
	}
}

void ExpandableElement::DrawExpandButton(ContentBrowserContext& Context)
{
	ImVec2 TileMin = ImGui::GetItemRectMin();
	ImVec2 TileMax = ImGui::GetItemRectMax();

	const float ButtonSize = 18.0f;

	ImGui::SetCursorScreenPos(ImVec2(
		TileMax.x - ButtonSize - 2.0f,
		TileMin.y + 2.0f
	));

	FString Name = FPaths::ToUtf8(ContentItem.Name) + "Expand";
	ImGui::PushID(Name.c_str());

	if (ImGui::SmallButton(bExpanded ? "v" : ">"))
	{
		bExpanded = !bExpanded;

		//if (bExpanded)
		//{
		//	OnExpanded(Context);
		//}
		//else
		//{
		//	OnCollapsed(Context);
		//}
	}

	ImGui::PopID();
}

void ExpandableElement::DrawExpandedPanel(ContentBrowserContext& Context)
{
	if (InternalElements.empty())
	{
		return;
	}

	ImVec2 TileMin = ImGui::GetItemRectMin();
	ImVec2 TileMax = ImGui::GetItemRectMax();

	const float PanelWidth = 420.0f;
	const float PanelHeight = 260.0f;

	ImVec2 PanelPos(
		TileMin.x,
		TileMax.y + 4.0f
	);

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();

	if (PanelPos.x + PanelWidth > Viewport->WorkPos.x + Viewport->WorkSize.x)
	{
		PanelPos.x = Viewport->WorkPos.x + Viewport->WorkSize.x - PanelWidth - 8.0f;
	}

	if (PanelPos.y + PanelHeight > Viewport->WorkPos.y + Viewport->WorkSize.y)
	{
		PanelPos.y = TileMin.y - PanelHeight - 4.0f;
	}

	FString WindowId = "##ExpandablePanel";

	ImGui::SetNextWindowPos(PanelPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(PanelWidth, PanelHeight), ImGuiCond_Always);

	ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

	bool bOpen = true;
	if (ImGui::Begin(WindowId.c_str(), &bOpen, Flags))
	{
		DrawInternalElements(Context, PanelWidth);
	}
	ImGui::End();

	ImGui::PopStyleVar();

	if (!bOpen)
	{
		bExpanded = false;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const bool bPanelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
		const bool bTileHovered = ImGui::IsItemHovered();

		if (!bPanelHovered && !bTileHovered)
		{
			bExpanded = false;
		}
	}
}

void ExpandableElement::DrawInternalElements(ContentBrowserContext& Context, float PanelWidth)
{
	const float ItemWidth = Context.ContentSize.x;
	const float ItemHeight = Context.ContentSize.y;
	const float Gap = 8.0f;

	int ColumnCount = static_cast<int>(PanelWidth / (ItemWidth + Gap));
	if (ColumnCount < 1)
	{
		ColumnCount = 1;
	}

	ImVec2 StartPos = ImGui::GetCursorPos();

	for (int32 Index = 0; Index < static_cast<int32>(InternalElements.size()); ++Index)
	{
		const int32 Column = Index % ColumnCount;
		const int32 Row = Index / ColumnCount;

		const float X = StartPos.x + Column * (ItemWidth + Gap);
		const float Y = StartPos.y + Row * (ItemHeight + Gap);

		ImGui::SetCursorPos(ImVec2(X, Y));
		InternalElements[Index]->Render(Context);
	}

	const int32 RowCount =
		(static_cast<int32>(InternalElements.size()) + ColumnCount - 1) / ColumnCount;

	ImGui::SetCursorPos(ImVec2(
		StartPos.x,
		StartPos.y + RowCount * (ItemHeight + Gap)
	));
}