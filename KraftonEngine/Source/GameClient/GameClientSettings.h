#pragma once

#include "Core/CoreTypes.h"
#include "Render/Types/ViewTypes.h"

struct FGameClientSettings
{
	FString WindowTitle = "GameClient";
	int32 WindowWidth = 1280;
	int32 WindowHeight = 720;
	bool bFullscreen = false;

	FString StartupScenePackagePath = "Asset/Scene/PackagedStart.Scene";
	TArray<FString> RuntimeModules;

	bool bRequireStartupScene = true;
	bool bEnableOverlay = false;
	bool bEnableDebugDraw = false;
	bool bEnableLuaHotReload = false;
	FViewportRenderOptions RenderOptions;

	void Load();
};
