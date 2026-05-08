#pragma once

#include "Editor/Packaging/EditorPackageSettings.h"

class UEditorEngine;

class FGamePackageBuilder
{
public:
	FGamePackageBuildResult Build(
		UEditorEngine* Editor,
		const FEditorPackageSettings& Settings);

private:
	bool PrepareOutputDirectory(const FEditorPackageSettings& Settings, FString& OutError);
	bool ExportCurrentWorld(UEditorEngine* Editor, const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyClientExecutable(const FEditorPackageSettings& Settings, FString& OutError);
	bool WriteGameIni(const FEditorPackageSettings& Settings, FString& OutError);
	bool CopySettingsFiles(const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyContentDirectories(const FEditorPackageSettings& Settings, FString& OutError);
	bool CopyRuntimeDependencies(const FEditorPackageSettings& Settings, FString& OutError);
	bool CreateRuntimeWritableDirectories(const FEditorPackageSettings& Settings, FString& OutError);
	bool WriteManifest(const FEditorPackageSettings& Settings, FString& OutError);
	bool ValidatePackage(const FEditorPackageSettings& Settings, FString& OutError);
	bool RunSmokeTest(const FEditorPackageSettings& Settings, FString& OutError);
};
