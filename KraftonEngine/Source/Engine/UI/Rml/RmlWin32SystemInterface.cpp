#include "Engine/UI/Rml/RmlWin32SystemInterface.h"

#if WITH_RMLUI

#include <cstring>
#include <filesystem>
#include <iostream>

FRmlWin32SystemInterface::FRmlWin32SystemInterface()
{
	::QueryPerformanceFrequency(&Frequency);
	::QueryPerformanceCounter(&StartCounter);
}

double FRmlWin32SystemInterface::GetElapsedTime()
{
	LARGE_INTEGER Now = {};
	::QueryPerformanceCounter(&Now);
	return static_cast<double>(Now.QuadPart - StartCounter.QuadPart) / static_cast<double>(Frequency.QuadPart);
}

void FRmlWin32SystemInterface::JoinPath(
	Rml::String& TranslatedPath,
	const Rml::String& DocumentPath,
	const Rml::String& Path)
{
	std::filesystem::path Requested = std::filesystem::path(Path);
	if (Requested.is_absolute())
	{
		TranslatedPath = Requested.generic_string();
		return;
	}

	std::filesystem::path Base = std::filesystem::path(DocumentPath).parent_path();
	TranslatedPath = (Base / Requested).lexically_normal().generic_string();
}

bool FRmlWin32SystemInterface::LogMessage(Rml::Log::Type Type, const Rml::String& Message)
{
	(void)Type;
	::OutputDebugStringA(Message.c_str());
	::OutputDebugStringA("\n");
	return true;
}

void FRmlWin32SystemInterface::SetMouseCursor(const Rml::String& CursorName)
{
	LPCWSTR Cursor = IDC_ARROW;
	if (CursorName == "pointer")
	{
		Cursor = IDC_HAND;
	}
	else if (CursorName == "move")
	{
		Cursor = IDC_SIZEALL;
	}
	else if (CursorName == "resize")
	{
		Cursor = IDC_SIZEWE;
	}
	else if (CursorName == "text")
	{
		Cursor = IDC_IBEAM;
	}
	::SetCursor(::LoadCursorW(nullptr, Cursor));
}

void FRmlWin32SystemInterface::SetClipboardText(const Rml::String& Text)
{
	if (!::OpenClipboard(nullptr))
	{
		return;
	}

	::EmptyClipboard();
	const SIZE_T Size = Text.size() + 1;
	HGLOBAL Memory = ::GlobalAlloc(GMEM_MOVEABLE, Size);
	if (Memory)
	{
		void* Data = ::GlobalLock(Memory);
		if (Data)
		{
			std::memcpy(Data, Text.c_str(), Size);
			::GlobalUnlock(Memory);
			::SetClipboardData(CF_TEXT, Memory);
		}
	}

	::CloseClipboard();
}

void FRmlWin32SystemInterface::GetClipboardText(Rml::String& Text)
{
	Text.clear();
	if (!::OpenClipboard(nullptr))
	{
		return;
	}

	HANDLE DataHandle = ::GetClipboardData(CF_TEXT);
	if (DataHandle)
	{
		const char* Data = static_cast<const char*>(::GlobalLock(DataHandle));
		if (Data)
		{
			Text = Data;
			::GlobalUnlock(DataHandle);
		}
	}

	::CloseClipboard();
}

#endif
