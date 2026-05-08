#include "Editor/Packaging/GamePackageBuilder.h"

#include "Editor/EditorEngine.h"
#include "Editor/Packaging/GamePackageAssetCollector.h"
#include "Editor/Packaging/GamePackageManifest.h"
#include "Editor/Packaging/GamePackageSmokeTester.h"
#include "Editor/Packaging/GamePackageValidator.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "GameFramework/WorldContext.h"
#include "SimpleJSON/json.hpp"

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace
{
std::filesystem::path ProjectPath(const FString& Path)
{
	std::filesystem::path Result(FPaths::ToWide(Path));
	if (Result.is_relative())
	{
		Result = std::filesystem::path(FPaths::RootDir()) / Result;
	}
	return Result.lexically_normal();
}

std::filesystem::path PackageRootPath(const FEditorPackageSettings& Settings)
{
	return ProjectPath(Settings.OutputDirectory);
}

FString ToUtf8Path(const std::filesystem::path& Path)
{
	return FPaths::ToUtf8(Path.generic_wstring());
}

bool CopyFileChecked(const std::filesystem::path& Source, const std::filesystem::path& Destination, FString& OutError)
{
	if (!std::filesystem::exists(Source) || !std::filesystem::is_regular_file(Source))
	{
		OutError = "Missing file: " + ToUtf8Path(Source);
		return false;
	}

	if (Destination.has_parent_path())
	{
		std::filesystem::create_directories(Destination.parent_path());
	}

	std::filesystem::copy_file(Source, Destination, std::filesystem::copy_options::overwrite_existing);
	return true;
}

bool ShouldSkipPackagePath(const FString& PackagePath, const TArray<FString>& SkipPackagePaths)
{
	for (const FString& SkipPath : SkipPackagePaths)
	{
		if (PackagePath == SkipPath)
		{
			return true;
		}
	}
	return false;
}

bool CopyDirectoryContents(
	const FString& PackageRootName,
	const std::filesystem::path& Source,
	const std::filesystem::path& Destination,
	const TArray<FString>& SkipPackagePaths,
	FString& OutError)
{
	if (!std::filesystem::exists(Source) || !std::filesystem::is_directory(Source))
	{
		OutError = "Missing content directory: " + ToUtf8Path(Source);
		return false;
	}

	std::filesystem::create_directories(Destination);

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(Source))
	{
		const std::filesystem::path Relative = Entry.path().lexically_relative(Source);
		const std::filesystem::path Target = Destination / Relative;
		const FString PackagePath = PackageRootName + "/" + FPaths::ToUtf8(Relative.generic_wstring());

		if (ShouldSkipPackagePath(PackagePath, SkipPackagePaths))
		{
			continue;
		}

		if (Entry.is_directory())
		{
			std::filesystem::create_directories(Target);
			continue;
		}

		if (!Entry.is_regular_file())
		{
			continue;
		}

		if (Target.has_parent_path())
		{
			std::filesystem::create_directories(Target.parent_path());
		}
		std::filesystem::copy_file(Entry.path(), Target, std::filesystem::copy_options::overwrite_existing);
	}

	return true;
}

std::filesystem::path FindRuntimeDll(const wchar_t* DllName, const TArray<std::filesystem::path>& SearchDirectories = {})
{
	for (const std::filesystem::path& Directory : SearchDirectories)
	{
		const std::filesystem::path Candidate = Directory / DllName;
		if (std::filesystem::exists(Candidate))
		{
			return Candidate;
		}
	}

	const std::filesystem::path ProjectCandidate = std::filesystem::path(FPaths::RootDir()) / DllName;
	if (std::filesystem::exists(ProjectCandidate))
	{
		return ProjectCandidate;
	}

	WCHAR SystemDir[MAX_PATH];
	const UINT Length = GetSystemDirectoryW(SystemDir, MAX_PATH);
	if (Length > 0)
	{
		const std::filesystem::path SystemCandidate = std::filesystem::path(SystemDir) / DllName;
		if (std::filesystem::exists(SystemCandidate))
		{
			return SystemCandidate;
		}
	}

	return {};
}

std::filesystem::path FindVcpkgRuntimeDll(const wchar_t* DllName)
{
	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	const TArray<std::filesystem::path> Candidates = {
		ProjectRoot.parent_path() / L"vcpkg_installed" / L"x64-windows" / L"bin" / DllName,
		ProjectRoot / L"..\\vcpkg_installed\\x64-windows\\bin" / DllName
	};

	for (const std::filesystem::path& Candidate : Candidates)
	{
		const std::filesystem::path Normalized = Candidate.lexically_normal();
		if (std::filesystem::exists(Normalized))
		{
			return Normalized;
		}
	}

	return {};
}

void AppendValidationErrors(const FGamePackageValidationResult& Validation, FString& OutError)
{
	OutError.clear();
	for (const FString& Error : Validation.Errors)
	{
		if (!OutError.empty())
		{
			OutError += "\n";
		}
		OutError += Error;
	}
}
}

FGamePackageBuildResult FGamePackageBuilder::Build(
	UEditorEngine* Editor,
	const FEditorPackageSettings& Settings)
{
	FGamePackageBuildResult Result;
	Result.OutputDirectory = Settings.OutputDirectory;

	FString Error;
	if (!PrepareOutputDirectory(Settings, Error)
		|| !ExportCurrentWorld(Editor, Settings, Error)
		|| !CopyClientExecutable(Settings, Error)
		|| !WriteGameIni(Settings, Error)
		|| !CopySettingsFiles(Settings, Error)
		|| !CopyContentDirectories(Settings, Error)
		|| !CopyRuntimeDependencies(Settings, Error)
		|| !CreateRuntimeWritableDirectories(Settings, Error)
		|| !WriteManifest(Settings, Error)
		|| !ValidatePackage(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	if (Settings.bRunSmokeTest && !RunSmokeTest(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}

bool FGamePackageBuilder::PrepareOutputDirectory(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
	const std::filesystem::path OutputRoot = PackageRootPath(Settings);

	if (!FPaths::IsPathInsideRoot(Root, OutputRoot))
	{
		OutError = "Package output must be inside project root: " + ToUtf8Path(OutputRoot);
		return false;
	}

	std::filesystem::remove_all(OutputRoot);
	std::filesystem::create_directories(OutputRoot);
	return true;
}

bool FGamePackageBuilder::ExportCurrentWorld(UEditorEngine* Editor, const FEditorPackageSettings& Settings, FString& OutError)
{
	if (!Editor)
	{
		OutError = "Editor is null.";
		return false;
	}

	Editor->StopPlayInEditorImmediate();

	FWorldContext* Context = Editor->GetWorldContextFromHandle(Editor->GetActiveWorldHandle());
	if (!Context || !Context->World)
	{
		OutError = "No active editor world.";
		return false;
	}

	const std::filesystem::path ScenePath = PackageRootPath(Settings) / FPaths::ToWide(Settings.StartScenePackagePath);
	if (!FSceneSaveManager::SaveWorldToJSONFile(ScenePath.wstring(), *Context, Editor->GetCamera()))
	{
		OutError = "Failed to export current editor world.";
		return false;
	}

	return true;
}

bool FGamePackageBuilder::CopyClientExecutable(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path Source = ProjectPath(Settings.ClientExecutablePath);
	const std::filesystem::path Destination = PackageRootPath(Settings) / L"GameClient.exe";
	return CopyFileChecked(Source, Destination, OutError);
}

bool FGamePackageBuilder::WriteGameIni(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path GameIniPath = PackageRootPath(Settings) / L"Settings" / L"Game.ini";
	std::filesystem::create_directories(GameIniPath.parent_path());

	json::JSON Root = json::Object();
	Root["Window"]["Title"] = Settings.ProjectName;
	Root["Window"]["Width"] = Settings.WindowWidth;
	Root["Window"]["Height"] = Settings.WindowHeight;
	Root["Window"]["Fullscreen"] = Settings.bFullscreen;

	Root["Paths"]["StartupScene"] = Settings.StartScenePackagePath;

	Root["Runtime"]["RequireStartupScene"] = Settings.bRequireStartupScene;
	Root["Runtime"]["EnableOverlay"] = Settings.bEnableOverlay;
	Root["Runtime"]["EnableDebugDraw"] = Settings.bEnableDebugDraw;
	Root["Runtime"]["EnableLuaHotReload"] = Settings.bEnableLuaHotReload;

	Root["Render"]["ViewMode"] = "Lit_Phong";
	Root["Render"]["FXAA"] = true;
	Root["Render"]["Fog"] = true;

	std::ofstream File(GameIniPath, std::ios::binary);
	if (!File.is_open())
	{
		OutError = "Failed to write Settings/Game.ini.";
		return false;
	}

	File << Root.dump();
	return true;
}

bool FGamePackageBuilder::CopySettingsFiles(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path OutputRoot = PackageRootPath(Settings);
	if (!CopyFileChecked(ProjectPath("Settings/Resource.ini"), OutputRoot / L"Settings" / L"Resource.ini", OutError))
	{
		return false;
	}

	return CopyFileChecked(ProjectPath("Settings/ProjectSettings.ini"), OutputRoot / L"Settings" / L"ProjectSettings.ini", OutError);
}

bool FGamePackageBuilder::CopyContentDirectories(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path OutputRoot = PackageRootPath(Settings);
	const TArray<FString> ContentDirectories = FGamePackageAssetCollector::GetDefaultContentDirectories();
	const TArray<FString> SkipPackagePaths = { Settings.StartScenePackagePath };

	for (const FString& Directory : ContentDirectories)
	{
		if (!CopyDirectoryContents(
			Directory,
			ProjectPath(Directory),
			OutputRoot / FPaths::ToWide(Directory),
			SkipPackagePaths,
			OutError))
		{
			return false;
		}
	}

	return true;
}

bool FGamePackageBuilder::CopyRuntimeDependencies(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path ClientExecutable = ProjectPath(Settings.ClientExecutablePath);
	const std::filesystem::path SfmlBinDir =
		(std::filesystem::path(FPaths::RootDir()) / L"ThirdParty" / L"SFML" / L"bin" / L"Release").lexically_normal();
	const TArray<std::filesystem::path> ClientSearchDirectories = {
		ClientExecutable.parent_path(),
		SfmlBinDir
	};

	const std::filesystem::path D3DCompiler = FindRuntimeDll(L"d3dcompiler_47.dll", ClientSearchDirectories);
	if (D3DCompiler.empty())
	{
		OutError = "Missing runtime dependency: d3dcompiler_47.dll";
		return false;
	}

	if (!CopyFileChecked(D3DCompiler, PackageRootPath(Settings) / L"d3dcompiler_47.dll", OutError))
	{
		return false;
	}

	std::filesystem::path LuaDll = FindRuntimeDll(L"lua51.dll", ClientSearchDirectories);
	if (LuaDll.empty())
	{
		LuaDll = FindVcpkgRuntimeDll(L"lua51.dll");
	}
	if (LuaDll.empty())
	{
		OutError = "Missing runtime dependency: lua51.dll";
		return false;
	}

	if (!CopyFileChecked(LuaDll, PackageRootPath(Settings) / L"lua51.dll", OutError))
	{
		return false;
	}

	const wchar_t* SfmlDlls[] = {
		L"sfml-audio-3.dll",
		L"sfml-system-3.dll",
		L"sfml-window-3.dll",
		L"OpenAL32.dll",
		L"libvorbis.dll",
		L"libogg-0.dll",
		L"libsndfile-1.dll"
	};
	for (const wchar_t* DllName : SfmlDlls)
	{
		const std::filesystem::path DllPath = FindRuntimeDll(DllName, ClientSearchDirectories);
		if (DllPath.empty())
		{
			OutError = "Missing runtime dependency: " + ToUtf8Path(std::filesystem::path(DllName));
			return false;
		}
		if (!CopyFileChecked(DllPath, PackageRootPath(Settings) / DllName, OutError))
		{
			return false;
		}
	}

	const wchar_t* RmlUiDlls[] = {
		L"rmlui.dll",
		L"freetype.dll",
		L"z.dll",
		L"libpng16.dll",
		L"bz2.dll",
		L"brotlicommon.dll",
		L"brotlidec.dll",
		L"brotlienc.dll",
	};
	for (const wchar_t* DllName : RmlUiDlls)
	{
		const std::filesystem::path DllPath = FindRuntimeDll(DllName, ClientSearchDirectories);
		if (DllPath.empty())
		{
			OutError = "Missing runtime dependency: " + ToUtf8Path(std::filesystem::path(DllName));
			return false;
		}
		if (!CopyFileChecked(DllPath, PackageRootPath(Settings) / DllName, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FGamePackageBuilder::CreateRuntimeWritableDirectories(const FEditorPackageSettings& Settings, FString& OutError)
{
	(void)OutError;
	const std::filesystem::path OutputRoot = PackageRootPath(Settings);
	std::filesystem::create_directories(OutputRoot / L"Saves" / L"Logs");
	std::filesystem::create_directories(OutputRoot / L"Saves" / L"Dump");
	return true;
}

bool FGamePackageBuilder::WriteManifest(const FEditorPackageSettings& Settings, FString& OutError)
{
	return FGamePackageManifestWriter::Write(PackageRootPath(Settings), Settings, OutError);
}

bool FGamePackageBuilder::ValidatePackage(const FEditorPackageSettings& Settings, FString& OutError)
{
	const FGamePackageValidationResult Validation = FGamePackageValidator::Validate(PackageRootPath(Settings), Settings);
	if (Validation.bSuccess)
	{
		return true;
	}

	AppendValidationErrors(Validation, OutError);
	return false;
}

bool FGamePackageBuilder::RunSmokeTest(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path ExecutablePath = PackageRootPath(Settings) / L"GameClient.exe";
	return FGamePackageSmokeTester::Run(ToUtf8Path(ExecutablePath), OutError);
}
