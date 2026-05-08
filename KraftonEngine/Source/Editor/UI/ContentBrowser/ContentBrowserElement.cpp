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

void MaterialElement::OnLeftClicked(ContentBrowserContext& Context)
{
	MaterialInspector = { ContentItem.Path };
}

void MaterialElement::RenderDetail()
{
	MaterialInspector.Render();
}
