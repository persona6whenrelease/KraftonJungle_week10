#include "Editor/Packaging/GamePackageValidator.h"

#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <fstream>
#include <iterator>

namespace
{
FString ToUtf8Path(const std::filesystem::path& Path)
{
	return FPaths::ToUtf8(Path.generic_wstring());
}

void AddError(FGamePackageValidationResult& Result, const FString& Error)
{
	Result.bSuccess = false;
	Result.Errors.push_back(Error);
}

void RequireFile(const std::filesystem::path& PackageRoot, const FString& PackagePath, FGamePackageValidationResult& Result)
{
	const std::filesystem::path FilePath = PackageRoot / FPaths::ToWide(PackagePath);
	if (!std::filesystem::exists(FilePath) || !std::filesystem::is_regular_file(FilePath))
	{
		AddError(Result, "Missing required file: " + PackagePath);
	}
}

void RequireDirectory(const std::filesystem::path& PackageRoot, const FString& PackagePath, FGamePackageValidationResult& Result)
{
	const std::filesystem::path DirectoryPath = PackageRoot / FPaths::ToWide(PackagePath);
	if (!std::filesystem::exists(DirectoryPath) || !std::filesystem::is_directory(DirectoryPath))
	{
		AddError(Result, "Missing required directory: " + PackagePath);
	}
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

void ValidateManifestPaths(const std::filesystem::path& PackageRoot, FGamePackageValidationResult& Result)
{
	const std::filesystem::path ManifestPath = PackageRoot / L"GamePackage.json";
	std::ifstream File(ManifestPath, std::ios::binary);
	if (!File.is_open())
	{
		return;
	}

	const FString Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	json::JSON Root = json::JSON::Load(Text);
	if (!Root.hasKey("Files"))
	{
		AddError(Result, "GamePackage.json has no Files array.");
		return;
	}

	for (auto& Entry : Root["Files"].ArrayRange())
	{
		if (!Entry.hasKey("Path"))
		{
			AddError(Result, "GamePackage.json contains a file entry without Path.");
			continue;
		}

		const FString PackagePathText = Entry["Path"].ToString();
		const std::filesystem::path PackagePath(FPaths::ToWide(PackagePathText));
		if (PackagePath.is_absolute())
		{
			AddError(Result, "Absolute path is forbidden in manifest: " + PackagePathText);
			continue;
		}
		if (ContainsParentTraversal(PackagePath))
		{
			AddError(Result, "../ path is forbidden in manifest: " + PackagePathText);
			continue;
		}

		const std::filesystem::path DiskPath = (PackageRoot / PackagePath).lexically_normal();
		if (!FPaths::IsPathInsideRoot(PackageRoot, DiskPath))
		{
			AddError(Result, "Manifest path escapes package root: " + PackagePathText);
			continue;
		}

		const bool bRequired = Entry.hasKey("Required") ? Entry["Required"].ToBool() : true;
		if (bRequired && !std::filesystem::exists(DiskPath))
		{
			AddError(Result, "Manifest required file is missing: " + PackagePathText);
		}
	}
}
}

FGamePackageValidationResult FGamePackageValidator::Validate(
	const std::filesystem::path& PackageRoot,
	const FEditorPackageSettings& Settings)
{
	FGamePackageValidationResult Result;

	if (PackageRoot.empty() || !std::filesystem::exists(PackageRoot))
	{
		AddError(Result, "Package root does not exist: " + ToUtf8Path(PackageRoot));
		return Result;
	}

	RequireFile(PackageRoot, "GameClient.exe", Result);
	RequireFile(PackageRoot, "GamePackage.json", Result);
	if (Settings.bIncludeD3DCompiler47)
	{
		RequireFile(PackageRoot, "d3dcompiler_47.dll", Result);
	}
	RequireFile(PackageRoot, "lua51.dll", Result);
	RequireFile(PackageRoot, "sfml-audio-3.dll", Result);
	RequireFile(PackageRoot, "sfml-system-3.dll", Result);
	RequireFile(PackageRoot, "sfml-window-3.dll", Result);
	RequireFile(PackageRoot, "OpenAL32.dll", Result);
	RequireFile(PackageRoot, "libvorbis.dll", Result);
	RequireFile(PackageRoot, "libogg-0.dll", Result);
	RequireFile(PackageRoot, "libsndfile-1.dll", Result);
	RequireFile(PackageRoot, "rmlui.dll", Result);
	RequireFile(PackageRoot, "freetype.dll", Result);
	RequireFile(PackageRoot, "z.dll", Result);
	RequireFile(PackageRoot, "libpng16.dll", Result);
	RequireFile(PackageRoot, "bz2.dll", Result);
	RequireFile(PackageRoot, "brotlicommon.dll", Result);
	RequireFile(PackageRoot, "brotlidec.dll", Result);
	RequireFile(PackageRoot, "brotlienc.dll", Result);

	RequireFile(PackageRoot, "Settings/Game.ini", Result);
	RequireFile(PackageRoot, "Settings/Resource.ini", Result);
	RequireFile(PackageRoot, "Settings/ProjectSettings.ini", Result);
	RequireFile(PackageRoot, Settings.StartScenePackagePath, Result);

	RequireDirectory(PackageRoot, "Asset", Result);
	RequireDirectory(PackageRoot, "LuaScripts", Result);
	RequireDirectory(PackageRoot, "Data", Result);
	RequireDirectory(PackageRoot, "Shaders", Result);
	RequireDirectory(PackageRoot, "Asset/UI", Result);
	RequireDirectory(PackageRoot, "Saves/Logs", Result);
	RequireDirectory(PackageRoot, "Saves/Dump", Result);

	RequireFile(PackageRoot, "Shaders/Geometry/Primitive.hlsl", Result);
	RequireFile(PackageRoot, "Shaders/Geometry/UberLit.hlsl", Result);
	RequireFile(PackageRoot, "Shaders/PostProcess/FXAA.hlsl", Result);

	ValidateManifestPaths(PackageRoot, Result);
	return Result;
}
