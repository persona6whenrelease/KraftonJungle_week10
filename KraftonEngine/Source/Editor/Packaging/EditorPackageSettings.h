#pragma once

#include "Core/CoreTypes.h"

struct FEditorPackageGameDefinition
{
	FString DisplayName;
	FString ModuleName;
	FString ProjectPath;
};

struct FEditorPackageSettings
{
	FString ProjectName = "KraftonGame";

	FString OutputDirectory = "Dist/GameClient";
	FString ClientExecutablePath = "Bin/GameClient/KraftonEngine.exe";

	// Game selection. The selected game resolves to a runtime module and, when present,
	// a game static-library project that the solution build must compile before packaging.
	FString SelectedGame = "Crossy";
	FString GameProjectPath = "CrossyGame.vcxproj";

	// Packaging builds the client instead of trusting a stale copied executable.
	bool bBuildBeforePackage = true;
	bool bAutoFindBuildTool = true;
	FString BuildToolPath;
	FString BuildSolutionPath = "KraftonEngine.sln";
	FString BuildConfiguration = "GameClient";
	FString BuildPlatform = "x64";

	FString StartSceneName = "PackagedStart";
	FString StartScenePackagePath = "Asset/Scene/PackagedStart.Scene";

	int32 WindowWidth = 1280;
	int32 WindowHeight = 720;
	bool bFullscreen = false;

	bool bRequireStartupScene = true;
	bool bEnableOverlay = false;
	bool bEnableDebugDraw = false;
	bool bEnableLuaHotReload = false;
	TArray<FString> RuntimeModules = { "CrossyGame" };

	// d3dcompiler_47.dll may be required on older Windows/runtime environments,
	// but some deployments prefer to rely on the system copy. Keep it explicit.
	bool bIncludeD3DCompiler47 = false;

	// Explicit package file set. Entries are project-root-relative package paths.
	// Directories may be written as "Asset" or "Asset/**". Wildcards support '*' and '?'.
	TArray<FString> IncludePackagePaths = {
		"Asset/**",
		"LuaScripts/**",
		"Data/**",
		"Shaders/**",
		"Settings/Resource.ini",
		"Settings/ProjectSettings.ini"
	};
	TArray<FString> ExcludePackagePaths = {
		"Asset/Scene/PackagedStart.Scene"
	};

	bool bRunSmokeTest = false;
};

struct FGamePackageBuildResult
{
	bool bSuccess = false;
	FString ErrorMessage;
	FString OutputDirectory;
	TArray<FString> Warnings;
};
