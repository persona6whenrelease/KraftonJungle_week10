#pragma once

#include "Core/CoreTypes.h"
#include "Editor/Packaging/EditorPackageSettings.h"

#include <filesystem>

struct FGamePackageValidationResult
{
	bool bSuccess = true;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

class FGamePackageValidator
{
public:
	static FGamePackageValidationResult Validate(
		const std::filesystem::path& PackageRoot,
		const FEditorPackageSettings& Settings);
};
