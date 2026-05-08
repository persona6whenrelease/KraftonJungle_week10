#pragma once

#include "Editor/Packaging/EditorPackageSettings.h"

#include <filesystem>

class FGamePackageManifestWriter
{
public:
	static bool Write(
		const std::filesystem::path& PackageRoot,
		const FEditorPackageSettings& Settings,
		FString& OutError);
};
