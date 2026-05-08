#include "GameClient/GameClientPackageValidator.h"

#include "GameClient/GameClientSettings.h"
#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
void AppendError(FString& OutErrors, const FString& Error)
{
	if (!OutErrors.empty())
	{
		OutErrors += "\n";
	}
	OutErrors += Error;
}

bool RequireFile(const FString& PackagePath, FString& OutErrors)
{
	std::wstring DiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(PackagePath, DiskPath, &Error))
	{
		AppendError(OutErrors, Error);
		return false;
	}

	if (!std::filesystem::exists(std::filesystem::path(DiskPath)) || !std::filesystem::is_regular_file(std::filesystem::path(DiskPath)))
	{
		AppendError(OutErrors, "Missing required file: " + PackagePath);
		return false;
	}

	return true;
}

bool RequireDirectory(const FString& PackagePath, FString& OutErrors)
{
	std::wstring DiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(PackagePath, DiskPath, &Error))
	{
		AppendError(OutErrors, Error);
		return false;
	}

	if (!std::filesystem::exists(std::filesystem::path(DiskPath)) || !std::filesystem::is_directory(std::filesystem::path(DiskPath)))
	{
		AppendError(OutErrors, "Missing required directory: " + PackagePath);
		return false;
	}

	return true;
}

bool ContainsParentTraversal(const std::filesystem::path& Path)
{
	for (const std::filesystem::path& Part : Path)
	{
		if (Part == L"..")
		{
			return true;
		}
	}
	return false;
}

bool ValidateManifest(FString& OutErrors)
{
	if (!RequireFile("GamePackage.json", OutErrors))
	{
		return false;
	}

	std::ifstream File(std::filesystem::path(FPaths::PackageManifestFilePath()), std::ios::binary);
	if (!File.is_open())
	{
		AppendError(OutErrors, "Failed to open GamePackage.json.");
		return false;
	}

	const FString Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	json::JSON Root = json::JSON::Load(Text);
	if (!Root.hasKey("Files"))
	{
		AppendError(OutErrors, "GamePackage.json has no Files array.");
		return false;
	}

	bool bOk = true;
	for (auto& Entry : Root["Files"].ArrayRange())
	{
		if (!Entry.hasKey("Path"))
		{
			AppendError(OutErrors, "GamePackage.json contains a file entry without Path.");
			bOk = false;
			continue;
		}

		const FString PackagePath = Entry["Path"].ToString();
		const std::filesystem::path Relative(FPaths::ToWide(PackagePath));
		if (Relative.is_absolute())
		{
			AppendError(OutErrors, "Absolute path is forbidden in manifest: " + PackagePath);
			bOk = false;
			continue;
		}
		if (ContainsParentTraversal(Relative))
		{
			AppendError(OutErrors, "../ path is forbidden in manifest: " + PackagePath);
			bOk = false;
			continue;
		}

		const bool bRequired = Entry.hasKey("Required") ? Entry["Required"].ToBool() : true;
		if (bRequired && !RequireFile(PackagePath, OutErrors))
		{
			bOk = false;
		}
	}

	return bOk;
}
}

bool FGameClientPackageValidator::ValidateBeforeEngineInit(const FGameClientSettings& Settings, FString& OutErrors)
{
	bool bOk = true;

	bOk &= ValidateManifest(OutErrors);
	bOk &= RequireFile("Settings/Game.ini", OutErrors);
	bOk &= RequireFile("Settings/Resource.ini", OutErrors);

	if (Settings.RuntimeModules.empty())
	{
		AppendError(OutErrors, "Settings/Game.ini must specify Runtime.Modules or RuntimeModules.");
		bOk = false;
	}

	bOk &= RequireDirectory("Asset", OutErrors);
	bOk &= RequireDirectory("Asset/Scene", OutErrors);
	bOk &= RequireDirectory("LuaScripts", OutErrors);
	bOk &= RequireDirectory("Shaders", OutErrors);
	bOk &= RequireDirectory("Asset/UI", OutErrors);

	bOk &= RequireFile(Settings.StartupScenePackagePath, OutErrors);
	bOk &= RequireFile("Shaders/Geometry/Primitive.hlsl", OutErrors);
	bOk &= RequireFile("Shaders/Geometry/UberLit.hlsl", OutErrors);
	bOk &= RequireFile("Shaders/PostProcess/FXAA.hlsl", OutErrors);

	return bOk;
}
