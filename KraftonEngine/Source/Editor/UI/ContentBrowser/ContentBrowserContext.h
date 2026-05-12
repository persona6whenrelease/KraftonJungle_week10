#pragma once
#include "imgui.h" 
#include "Platform/Paths.h"
#include <memory>

class ContentBrowserElement;
class UEditorEngine;

struct ContentBrowserContext final
{
	std::wstring CurrentPath = FPaths::RootDir();
	std::wstring PendingRevealPath;
	ImVec2 ContentSize = ImVec2(50.0f, 50.0f);
	std::shared_ptr<ContentBrowserElement> SelectedElement;

	UEditorEngine* EditorEngine;

	ImVec2 ContentGridStartPos = ImVec2(0.0f, 0.0f);
	int32 ContentGridColumnCount = 1;
	int32 ContentGridSlotIndex = 0;
	float ContentGridGapX = 0.0f;
	float ContentGridGapY = 0.0f;
	float ContentGridMaxBottomY = 0.0f;
	bool bContentGridSlotConsumed = false;

	bool bIsNeedRefresh = false;
	bool bIsRenaming = false;
	bool bRenameFocusNeeded = false;
	char RenameBuffer[512] = {};

	ImVec2 GetContentGridSlotPos() const
	{
		const int32 SafeColumnCount = ContentGridColumnCount > 0 ? ContentGridColumnCount : 1;
		const int32 Column = ContentGridSlotIndex % SafeColumnCount;
		const int32 Row = ContentGridSlotIndex / SafeColumnCount;
		return ImVec2(
			ContentGridStartPos.x + Column * (ContentSize.x + ContentGridGapX),
			ContentGridStartPos.y + Row * (ContentSize.y + ContentGridGapY)
		);
	}

	void MoveToContentGridSlot()
	{
		ImGui::SetCursorPos(GetContentGridSlotPos());
		bContentGridSlotConsumed = false;
	}

	void AdvanceContentGridSlot()
	{
		const ImVec2 SlotPos = GetContentGridSlotPos();
		const float SlotBottomY = SlotPos.y + ContentSize.y;
		if (SlotBottomY > ContentGridMaxBottomY)
		{
			ContentGridMaxBottomY = SlotBottomY;
		}
		++ContentGridSlotIndex;
		bContentGridSlotConsumed = true;
	}
};
