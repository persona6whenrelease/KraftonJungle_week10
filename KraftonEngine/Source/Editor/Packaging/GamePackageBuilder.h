#pragma once

#include "Editor/Packaging/EditorPackageSettings.h"

#include <functional>

class UEditorEngine;

struct FGamePackageProgress
{
	float Percent = 0.0f;
	FString Stage;
};

using FGamePackageProgressCallback = std::function<void(const FGamePackageProgress&)>;

class FGamePackageBuilder
{
public:
	FGamePackageBuildResult Build(
		UEditorEngine* Editor,
		const FEditorPackageSettings& Settings,
		FGamePackageProgressCallback ProgressCallback = nullptr);

private:
	bool BuildGameClient(const FEditorPackageSettings& Settings, const FGamePackageProgressCallback& ProgressCallback, FString& OutError);
	bool PrepareOutputDirectory(const FEditorPackageSettings& Settings, FString& OutError);
	bool ExportCurrentWorld(UEditorEngine* Editor, const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyClientExecutable(const FEditorPackageSettings& Settings, FString& OutError);
	bool WriteGameIni(const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyExplicitPackageInputs(const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyRuntimeDependencies(const FEditorPackageSettings& Settings, FString& OutError);
	bool CreateRuntimeWritableDirectories(const FEditorPackageSettings& Settings, FString& OutError);
	bool WriteManifest(const FEditorPackageSettings& Settings, FString& OutError);
	bool ValidatePackage(const FEditorPackageSettings& Settings, FString& OutError);
	bool RunSmokeTest(const FEditorPackageSettings& Settings, FString& OutError);
};
