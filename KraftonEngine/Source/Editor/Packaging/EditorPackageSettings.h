#pragma once

#include "Core/CoreTypes.h"

struct FEditorPackageSettings
{
	FString ProjectName = "KraftonGame";

	FString OutputDirectory = "Dist/GameClient";
	FString ClientExecutablePath = "Bin/GameClient/KraftonEngine.exe";

	FString StartSceneName = "PackagedStart";
	FString StartScenePackagePath = "Asset/Scene/PackagedStart.Scene";

	int32 WindowWidth = 1280;
	int32 WindowHeight = 720;
	bool bFullscreen = false;

	bool bRequireStartupScene = true;
	bool bEnableOverlay = false;
	bool bEnableDebugDraw = false;
	bool bEnableLuaHotReload = false;

	bool bRunSmokeTest = false;

	FString BuildConfiguration = "GameClientDevelopment";
};

struct FGamePackageBuildResult
{
	bool bSuccess = false;
	FString ErrorMessage;
	FString OutputDirectory;
	TArray<FString> Warnings;
};
