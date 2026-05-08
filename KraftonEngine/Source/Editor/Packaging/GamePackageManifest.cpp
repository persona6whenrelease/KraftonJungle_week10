#include "Editor/Packaging/GamePackageManifest.h"

#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace
{
FString ToPackagePath(const std::filesystem::path& PackageRoot, const std::filesystem::path& FilePath)
{
	return FPaths::ToUtf8(FilePath.lexically_relative(PackageRoot).generic_wstring());
}

FString ToLower(FString Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(),
		[](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
	return Text;
}

bool StartsWith(const FString& Text, const FString& Prefix)
{
	return Text.rfind(Prefix, 0) == 0;
}

FString DetectFileType(const FString& Path)
{
	const FString Lower = ToLower(Path);
	if (Lower == "gameclient.exe")
	{
		return "Executable";
	}
	if (Lower == "d3dcompiler_47.dll" || Lower.ends_with(".dll"))
	{
		return "RuntimeDll";
	}
	if (Lower == "gamepackage.json")
	{
		return "Manifest";
	}
	if (StartsWith(Lower, "settings/"))
	{
		return "Config";
	}
	if (StartsWith(Lower, "shaders/"))
	{
		return "Shader";
	}
	if (StartsWith(Lower, "luascripts/"))
	{
		return "Lua";
	}
	if (StartsWith(Lower, "asset/scene/") || Lower.ends_with(".scene"))
	{
		return "Scene";
	}
	if (Lower.ends_with(".mat"))
	{
		return "Material";
	}
	if (Lower.ends_with(".png") || Lower.ends_with(".jpg") || Lower.ends_with(".jpeg") || Lower.ends_with(".dds") || Lower.ends_with(".bmp") || Lower.ends_with(".tga"))
	{
		return "Texture";
	}
	if (Lower.ends_with(".obj") || Lower.ends_with(".bin") || Lower.ends_with(".mesh"))
	{
		return "Mesh";
	}
	return "Data";
}

FString GetCreatedAtLocalIso8601()
{
	SYSTEMTIME LocalTime;
	GetLocalTime(&LocalTime);

	char Buffer[64];
	snprintf(Buffer, sizeof(Buffer),
		"%04u-%02u-%02uT%02u:%02u:%02u",
		static_cast<unsigned>(LocalTime.wYear),
		static_cast<unsigned>(LocalTime.wMonth),
		static_cast<unsigned>(LocalTime.wDay),
		static_cast<unsigned>(LocalTime.wHour),
		static_cast<unsigned>(LocalTime.wMinute),
		static_cast<unsigned>(LocalTime.wSecond));
	return Buffer;
}

void AppendFileEntry(json::JSON& Files, const FString& Path)
{
	json::JSON Entry = json::Object();
	Entry["Path"] = Path;
	Entry["Type"] = DetectFileType(Path);
	Entry["Required"] = true;
	Entry["Hash"] = "";
	Files.append(Entry);
}
}

bool FGamePackageManifestWriter::Write(
	const std::filesystem::path& PackageRoot,
	const FEditorPackageSettings& Settings,
	FString& OutError)
{
	if (PackageRoot.empty())
	{
		OutError = "Package root is empty.";
		return false;
	}

	json::JSON Root = json::Object();
	Root["PackageVersion"] = 1;
	Root["ProjectName"] = Settings.ProjectName;
	Root["BuildConfiguration"] = Settings.BuildConfiguration;
	Root["BuildPlatform"] = Settings.BuildPlatform;
	Root["SelectedGame"] = Settings.SelectedGame;
	Root["GameProject"] = Settings.GameProjectPath;
	Root["CreatedBy"] = "EditorPackageButton";
	Root["CreatedAt"] = GetCreatedAtLocalIso8601();

	Root["StartScene"] = Settings.StartScenePackagePath;
	Root["GameSettings"] = "Settings/Game.ini";
	Root["ResourceSettings"] = "Settings/Resource.ini";
	Root["ProjectSettings"] = "Settings/ProjectSettings.ini";

	Root["ShaderRoot"] = "Shaders";
	Root["AssetRoot"] = "Asset";
	Root["ScriptRoot"] = "LuaScripts";
	Root["DataRoot"] = "Data";

	json::JSON Files = json::Array();
	AppendFileEntry(Files, "GamePackage.json");

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(PackageRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		const FString PackagePath = ToPackagePath(PackageRoot, Entry.path());
		if (PackagePath == "GamePackage.json")
		{
			continue;
		}

		AppendFileEntry(Files, PackagePath);
	}

	Root["Files"] = Files;

	const std::filesystem::path ManifestPath = PackageRoot / L"GamePackage.json";
	std::ofstream File(ManifestPath, std::ios::binary);
	if (!File.is_open())
	{
		OutError = "Failed to write GamePackage.json.";
		return false;
	}

	File << Root.dump();
	return true;
}
