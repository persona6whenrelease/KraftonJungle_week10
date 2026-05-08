#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Engine/UI/Rml/RmlUiConfig.h"
#if WITH_RMLUI
#include "Engine/UI/Rml/RmlUiWindowsMacroGuard.h"
#include <RmlUi/Core/SystemInterface.h>
#endif

#if WITH_RMLUI

class FRmlWin32SystemInterface final : public Rml::SystemInterface
{
public:
	FRmlWin32SystemInterface();
	~FRmlWin32SystemInterface() override = default;

	double GetElapsedTime() override;
	void JoinPath(Rml::String& TranslatedPath, const Rml::String& DocumentPath, const Rml::String& Path) override;
	bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override;
	void SetMouseCursor(const Rml::String& CursorName) override;
	void SetClipboardText(const Rml::String& Text) override;
	void GetClipboardText(Rml::String& Text) override;

private:
	LARGE_INTEGER Frequency = {};
	LARGE_INTEGER StartCounter = {};
};

#else

class FRmlWin32SystemInterface
{
};

#endif
