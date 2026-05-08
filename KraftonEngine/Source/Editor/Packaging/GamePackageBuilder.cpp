#include "Editor/Packaging/GamePackageBuilder.h"
#include "Core/ProjectSettings.h"

#include "Editor/EditorEngine.h"
#include "Editor/Packaging/GamePackageManifest.h"
#include "Editor/Packaging/GamePackageSmokeTester.h"
#include "Editor/Packaging/GamePackageValidator.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "GameFramework/WorldContext.h"
#include "SimpleJSON/json.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <utility>
#include <system_error>

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

std::filesystem::path ProjectRootPath()
{
	return std::filesystem::path(FPaths::RootDir()).lexically_normal();
}

void ReportProgress(const FGamePackageProgressCallback& Callback, float Percent, FString Stage)
{
	if (!Callback)
	{
		return;
	}
	if (Percent < 0.0f)
	{
		Percent = 0.0f;
	}
	if (Percent > 100.0f)
	{
		Percent = 100.0f;
	}
	Callback(FGamePackageProgress{ Percent,std::move(Stage) });
}

std::wstring QuoteCommandArgument(const std::filesystem::path& Path)
{
	std::wstring Value = Path.wstring();
	std::wstring Result;
	Result.reserve(Value.size() + 2);
	Result.push_back(L'"');
	for (wchar_t Ch : Value)
	{
		if (Ch == L'"')
		{
			Result += L"\\\"";
		}
		else
		{
			Result.push_back(Ch);
		}
	}
	Result.push_back(L'"');
	return Result;
}

std::wstring QuoteCommandArgument(const std::wstring& Value)
{
	std::wstring Result;
	Result.reserve(Value.size() + 2);
	Result.push_back(L'"');
	for (wchar_t Ch : Value)
	{
		if (Ch == L'"')
		{
			Result += L"\\\"";
		}
		else
		{
			Result.push_back(Ch);
		}
	}
	Result.push_back(L'"');
	return Result;
}

FString ToUtf8Path(const std::filesystem::path& Path)
{
	return FPaths::ToUtf8(Path.generic_wstring());
}

FString Trim(FString Text)
{
	const auto IsSpace = [](unsigned char Ch)
	{
		return Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n';
	};
	while (!Text.empty() && IsSpace(static_cast<unsigned char>(Text.front())))
	{
		Text.erase(Text.begin());
	}
	while (!Text.empty() && IsSpace(static_cast<unsigned char>(Text.back())))
	{
		Text.pop_back();
	}
	return Text;
}

FString NormalizePackagePath(FString Path)
{
	std::replace(Path.begin(), Path.end(), '\\', '/');
	while (Path.starts_with("./"))
	{
		Path.erase(0, 2);
	}
	while (!Path.empty() && Path.front() == '/')
	{
		Path.erase(Path.begin());
	}
	return Trim(Path);
}

bool ContainsWildcard(const FString& Pattern)
{
	return Pattern.find('*') != FString::npos || Pattern.find('?') != FString::npos;
}

bool WildcardMatchRecursive(const char* Text, const char* Pattern)
{
	while (*Pattern)
	{
		if (*Pattern == '*')
		{
			while (*(Pattern + 1) == '*')
			{
				++Pattern;
			}
			++Pattern;
			if (!*Pattern)
			{
				return true;
			}
			while (*Text)
			{
				if (WildcardMatchRecursive(Text, Pattern))
				{
					return true;
				}
				++Text;
			}
			return false;
		}
		if (*Pattern == '?')
		{
			if (!*Text)
			{
				return false;
			}
			++Text;
			++Pattern;
			continue;
		}
		if (*Text != *Pattern)
		{
			return false;
		}
		++Text;
		++Pattern;
	}
	return *Text == '\0';
}

bool WildcardMatch(const FString& Text, const FString& Pattern)
{
	return WildcardMatchRecursive(Text.c_str(), Pattern.c_str());
}

bool PathMatchesRule(const FString& PackagePath, FString Rule)
{
	Rule = NormalizePackagePath(Rule);
	if (Rule.empty())
	{
		return false;
	}
	if (Rule.ends_with("/**"))
	{
		const FString Prefix = Rule.substr(0, Rule.size() - 3);
		return PackagePath == Prefix || PackagePath.starts_with(Prefix + "/");
	}
	if (ContainsWildcard(Rule))
	{
		return WildcardMatch(PackagePath, Rule);
	}
	return PackagePath == Rule || PackagePath.starts_with(Rule + "/");
}

bool IsExcludedPackagePath(const FString& PackagePath, const FEditorPackageSettings& Settings)
{
	for (const FString& Rule : Settings.ExcludePackagePaths)
	{
		if (PathMatchesRule(PackagePath, Rule))
		{
			return true;
		}
	}
	if (!Settings.StartScenePackagePath.empty() && PathMatchesRule(PackagePath, Settings.StartScenePackagePath))
	{
		return true;
	}
	return false;
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

bool CopyProjectFileToPackage(
	const std::filesystem::path& ProjectRoot,
	const std::filesystem::path& PackageRoot,
	const std::filesystem::path& SourceFile,
	const FEditorPackageSettings& Settings,
	FString& OutError)
{
	const std::filesystem::path Relative = SourceFile.lexically_relative(ProjectRoot);
	const FString PackagePath = NormalizePackagePath(FPaths::ToUtf8(Relative.generic_wstring()));
	if (PackagePath.empty() || IsExcludedPackagePath(PackagePath, Settings))
	{
		return true;
	}
	return CopyFileChecked(SourceFile, PackageRoot / Relative, OutError);
}

bool CopyProjectDirectoryToPackage(
	const std::filesystem::path& ProjectRoot,
	const std::filesystem::path& PackageRoot,
	const std::filesystem::path& SourceDirectory,
	const FEditorPackageSettings& Settings,
	FString& OutError)
{
	if (!std::filesystem::exists(SourceDirectory) || !std::filesystem::is_directory(SourceDirectory))
	{
		OutError = "Missing package directory: " + ToUtf8Path(SourceDirectory);
		return false;
	}

	const std::filesystem::path RelativeRoot = SourceDirectory.lexically_relative(ProjectRoot);
	std::filesystem::create_directories(PackageRoot / RelativeRoot);

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(SourceDirectory))
	{
		if (Entry.is_directory())
		{
			const std::filesystem::path RelativeDirectory = Entry.path().lexically_relative(ProjectRoot);
			const FString PackagePath = NormalizePackagePath(FPaths::ToUtf8(RelativeDirectory.generic_wstring()));
			if (!IsExcludedPackagePath(PackagePath, Settings))
			{
				std::filesystem::create_directories(PackageRoot / RelativeDirectory);
			}
			continue;
		}
		if (!Entry.is_regular_file())
		{
			continue;
		}
		if (!CopyProjectFileToPackage(ProjectRoot, PackageRoot, Entry.path(), Settings, OutError))
		{
			return false;
		}
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

std::filesystem::path FindMsBuildOnPath()
{
	WCHAR Buffer[MAX_PATH];
	WCHAR* FilePart = nullptr;
	const DWORD Length = SearchPathW(nullptr, L"MSBuild.exe", nullptr, MAX_PATH, Buffer, &FilePart);
	if (Length > 0 && Length < MAX_PATH)
	{
		return std::filesystem::path(Buffer);
	}
	return {};
}

std::filesystem::path EnvPath(const wchar_t* Name)
{
	wchar_t* Value = nullptr;
	size_t ValueLength = 0;

	if (_wdupenv_s(&Value, &ValueLength, Name) != 0 || !Value)
	{
		return {};
	}

	std::filesystem::path Result(Value);
	std::free(Value);
	return Result;
}

std::filesystem::path FindMsBuildByKnownInstallLocations()
{
	const TArray<std::filesystem::path> Roots = {
		EnvPath(L"ProgramFiles") / L"Microsoft Visual Studio" / L"2022",
		EnvPath(L"ProgramFiles(x86)") / L"Microsoft Visual Studio" / L"2022",
		EnvPath(L"ProgramFiles(x86)") / L"Microsoft Visual Studio" / L"2019",
	};
	const wchar_t* Editions[] = { L"Community",L"Professional",L"Enterprise",L"BuildTools" };
	for (const std::filesystem::path& Root : Roots)
	{
		if (Root.empty())
		{
			continue;
		}
		for (const wchar_t* Edition : Editions)
		{
			const std::filesystem::path Candidate = Root / Edition / L"MSBuild" / L"Current" / L"Bin" / L"amd64" / L"MSBuild.exe";
			if (std::filesystem::exists(Candidate))
			{
				return Candidate;
			}
		}
	}
	return {};
}

std::filesystem::path ResolveBuildTool(const FEditorPackageSettings& Settings)
{
	if (!Settings.BuildToolPath.empty())
	{
		const std::filesystem::path Explicit = ProjectPath(Settings.BuildToolPath);
		if (std::filesystem::exists(Explicit))
		{
			return Explicit;
		}
	}
	if (!Settings.bAutoFindBuildTool)
	{
		return {};
	}
	std::filesystem::path Tool = FindMsBuildOnPath();
	if (!Tool.empty())
	{
		return Tool;
	}
	return FindMsBuildByKnownInstallLocations();
}

std::filesystem::path ResolveBuildTarget(const FEditorPackageSettings& Settings)
{
	// Packaging must produce the runnable GameClient executable. A selected game project
	// such as CrossyGame.vcxproj is only a dependency/module and must never be used as
	// the direct package build target. Prefer the solution so project dependencies are
	// built in the correct order.
	auto IsClientBuildTarget = [](const std::filesystem::path& Path) -> bool
	{
		const std::wstring FileName = Path.filename().wstring();
		return FileName == L"KraftonEngine.sln" || FileName == L"KraftonEngine.vcxproj";
	};

	if (!Settings.BuildSolutionPath.empty())
	{
		const std::filesystem::path Candidate = ProjectPath(Settings.BuildSolutionPath);
		if (std::filesystem::exists(Candidate) && IsClientBuildTarget(Candidate))
		{
			return Candidate;
		}
	}

	const std::filesystem::path DefaultSolution = ProjectRootPath() / L"KraftonEngine.sln";
	if (std::filesystem::exists(DefaultSolution))
	{
		return DefaultSolution;
	}

	const std::filesystem::path ClientProject = ProjectRootPath() / L"KraftonEngine.vcxproj";
	if (std::filesystem::exists(ClientProject))
	{
		return ClientProject;
	}

	return {};
}

FString GetLastWin32ErrorText(DWORD ErrorCode)
{
	if (ErrorCode == 0)
	{
		return {};
	}

	LPWSTR Buffer = nullptr;
	const DWORD Length = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		ErrorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&Buffer),
		0,
		nullptr);

	FString Result;
	if (Length > 0 && Buffer)
	{
		Result = FPaths::ToUtf8(std::wstring(Buffer, Length));
	}
	if (Buffer)
	{
		LocalFree(Buffer);
	}
	return Result;
}

bool RunProcessWithProgress(
	const std::wstring& CommandLine,
	const std::filesystem::path& WorkingDirectory,
	const FGamePackageProgressCallback& ProgressCallback,
	float StartPercent,
	float EndPercent,
	const FString& Stage,
	int& OutExitCode,
	FString& OutError)
{
	OutExitCode = -1;
	std::wstring MutableCommandLine = CommandLine;

	STARTUPINFOW StartupInfo{};
	StartupInfo.cb = sizeof(StartupInfo);
	PROCESS_INFORMATION ProcessInfo{};

	ReportProgress(ProgressCallback, StartPercent, Stage);
	const BOOL bCreated = CreateProcessW(
		nullptr,
		MutableCommandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_UNICODE_ENVIRONMENT,
		nullptr,
		WorkingDirectory.empty() ? nullptr : WorkingDirectory.wstring().c_str(),
		&StartupInfo,
		&ProcessInfo);

	if (!bCreated)
	{
		const DWORD ErrorCode = GetLastError();
		OutError = "Failed to start build process. " + GetLastWin32ErrorText(ErrorCode)
		+ "Command: " + FPaths::ToUtf8(CommandLine);
		return false;
	}

	const auto BeginTime = std::chrono::steady_clock::now();
	for (;;)
	{
		const DWORD WaitResult = WaitForSingleObject(ProcessInfo.hProcess, 250);
		if (WaitResult == WAIT_OBJECT_0)
		{
			break;
		}
		if (WaitResult != WAIT_TIMEOUT)
		{
			OutError = "Failed while waiting for build process. Command: " + FPaths::ToUtf8(CommandLine);
			TerminateProcess(ProcessInfo.hProcess, 1);
			CloseHandle(ProcessInfo.hThread);
			CloseHandle(ProcessInfo.hProcess);
			return false;
		}

		const auto Now = std::chrono::steady_clock::now();
		const float Seconds = std::chrono::duration<float>(Now - BeginTime).count();
		const float Span = EndPercent - StartPercent;
		// MSBuild does not expose a cheap percent signal here, so keep a bounded
		// build-phase estimate moving while the child process is alive.
		const float EstimatedPercent = StartPercent + (std::min)(Span - 1.0f, Seconds * 0.5f);
		ReportProgress(ProgressCallback, EstimatedPercent, Stage);
	}

	DWORD ProcessExitCode = 1;
	GetExitCodeProcess(ProcessInfo.hProcess, &ProcessExitCode);
	CloseHandle(ProcessInfo.hThread);
	CloseHandle(ProcessInfo.hProcess);

	OutExitCode = static_cast<int>(ProcessExitCode);
	ReportProgress(ProgressCallback, EndPercent, Stage);
	return true;
}

FString InferRuntimeModuleName(const FEditorPackageSettings& Settings)
{
	if (!Settings.RuntimeModules.empty())
	{
		return Settings.RuntimeModules.front();
	}
	if (!Settings.SelectedGame.empty())
	{
		if (Settings.SelectedGame.ends_with("Game"))
		{
			return Settings.SelectedGame;
		}
		return Settings.SelectedGame + "Game";
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
	const FEditorPackageSettings& Settings,
	FGamePackageProgressCallback ProgressCallback)
{
	FGamePackageBuildResult Result;
	Result.OutputDirectory = Settings.OutputDirectory;

	FString Error;
	ReportProgress(ProgressCallback, 0.0f, "Starting package build");
	if (!BuildGameClient(Settings, ProgressCallback, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 36.0f, "Preparing output directory");
	if (!PrepareOutputDirectory(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 45.0f, "Exporting current editor world");
	if (!ExportCurrentWorld(Editor, Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 55.0f, "Copying client executable");
	if (!CopyClientExecutable(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 62.0f, "Writing Settings/Game.ini");
	if (!WriteGameIni(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 70.0f, "Copying selected package files");
	if (!CopyExplicitPackageInputs(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 82.0f, "Copying runtime dependencies");
	if (!CopyRuntimeDependencies(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 90.0f, "Creating writable runtime folders");
	if (!CreateRuntimeWritableDirectories(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 94.0f, "Writing package manifest");
	if (!WriteManifest(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	ReportProgress(ProgressCallback, 97.0f, "Validating package");
	if (!ValidatePackage(Settings, Error))
	{
		Result.ErrorMessage = Error;
		return Result;
	}

	if (Settings.bRunSmokeTest)
	{
		ReportProgress(ProgressCallback, 98.0f, "Running smoke test");
		if (!RunSmokeTest(Settings, Error))
		{
			Result.ErrorMessage = Error;
			return Result;
		}
	}

	ReportProgress(ProgressCallback, 100.0f, "Package build complete");
	Result.bSuccess = true;
	return Result;
}

bool FGamePackageBuilder::BuildGameClient(const FEditorPackageSettings& Settings, const FGamePackageProgressCallback& ProgressCallback, FString& OutError)
{
	if (!Settings.bBuildBeforePackage)
	{
		return true;
	}

	if (!Settings.GameProjectPath.empty() && !std::filesystem::exists(ProjectPath(Settings.GameProjectPath)))
	{
		OutError = "Selected game project is missing: " + Settings.GameProjectPath;
		return false;
	}

	const std::filesystem::path BuildTool = ResolveBuildTool(Settings);
	if (BuildTool.empty())
	{
		OutError = "MSBuild.exe was not found. Set Build Tool Path or install Visual Studio Build Tools.";
		return false;
	}

	const std::filesystem::path Target = ResolveBuildTarget(Settings);
	if (Target.empty())
	{
		OutError = "Build target was not found. Set Build Solution Path or keep KraftonEngine.sln/KraftonEngine.vcxproj in the project root.";
		return false;
	}

	const FString Configuration = Settings.BuildConfiguration.empty() ? "GameClient" : Settings.BuildConfiguration;
	const FString Platform = Settings.BuildPlatform.empty() ? "x64" : Settings.BuildPlatform;

	std::wstring Command = QuoteCommandArgument(BuildTool) + L" " + QuoteCommandArgument(Target) + L" /m";
	Command += L" /p:Configuration=" + QuoteCommandArgument(FPaths::ToWide(Configuration));
	Command += L" /p:Platform=" + QuoteCommandArgument(FPaths::ToWide(Platform));

	int ExitCode = -1;
	FString ProcessError;
	const FString Stage = "Building GameClient (" + Configuration + "|" + Platform + ")";
	if (!RunProcessWithProgress(Command, ProjectRootPath(), ProgressCallback, 5.0f, 35.0f, Stage, ExitCode, ProcessError))
	{
		OutError = ProcessError;
		return false;
	}
	if (ExitCode != 0)
	{
		OutError = "Game client build failed with exit code " + std::to_string(ExitCode)
		+ ". Command: " + FPaths::ToUtf8(Command);
		return false;
	}

	const std::filesystem::path ClientExecutable = ProjectPath(Settings.ClientExecutablePath);
	if (!std::filesystem::exists(ClientExecutable) || !std::filesystem::is_regular_file(ClientExecutable))
	{
		OutError = "Game client build finished, but executable was not produced: " + ToUtf8Path(ClientExecutable)
		+ ". Build target must be KraftonEngine.sln or KraftonEngine.vcxproj, not the selected game module project.";
		return false;
	}

	return true;
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

	Root["Runtime"]["SelectedGame"] = Settings.SelectedGame;
	Root["Runtime"]["RequireStartupScene"] = Settings.bRequireStartupScene;
	Root["Runtime"]["EnableOverlay"] = Settings.bEnableOverlay;
	Root["Runtime"]["EnableDebugDraw"] = Settings.bEnableDebugDraw;
	Root["Runtime"]["EnableLuaHotReload"] = Settings.bEnableLuaHotReload;

	TArray<FString> RuntimeModules = Settings.RuntimeModules;
	if (RuntimeModules.empty())
	{
		const FString ModuleName = InferRuntimeModuleName(Settings);
		if (!ModuleName.empty())
		{
			RuntimeModules.push_back(ModuleName);
		}
	}
	if (RuntimeModules.empty())
	{
		RuntimeModules = FProjectSettings::Get().RuntimeModules;
	}
	json::JSON Modules = json::Array();
	for (const FString& ModuleName : RuntimeModules)
	{
		if (!ModuleName.empty())
		{
			Modules.append(ModuleName);
		}
	}
	Root["Runtime"]["Modules"] = Modules;

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

bool FGamePackageBuilder::CopyExplicitPackageInputs(const FEditorPackageSettings& Settings, FString& OutError)
{
	const std::filesystem::path ProjectRoot = std::filesystem::path(FPaths::RootDir()).lexically_normal();
	const std::filesystem::path OutputRoot = PackageRootPath(Settings);
	if (Settings.IncludePackagePaths.empty())
	{
		OutError = "No package include paths configured.";
		return false;
	}

	for (const FString& RawRule : Settings.IncludePackagePaths)
	{
		const FString Rule = NormalizePackagePath(RawRule);
		if (Rule.empty())
		{
			continue;
		}

		if (!ContainsWildcard(Rule))
		{
			const std::filesystem::path Source = ProjectPath(Rule);
			if (std::filesystem::is_regular_file(Source))
			{
				if (!CopyProjectFileToPackage(ProjectRoot, OutputRoot, Source, Settings, OutError))
				{
					return false;
				}
				continue;
			}
			if (std::filesystem::is_directory(Source))
			{
				if (!CopyProjectDirectoryToPackage(ProjectRoot, OutputRoot, Source, Settings, OutError))
				{
					return false;
				}
				continue;
			}
			OutError = "Package include path does not exist: " + Rule;
			return false;
		}

		bool bMatchedAnyFile = false;
		for (const auto& Entry : std::filesystem::recursive_directory_iterator(ProjectRoot))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}
			const std::filesystem::path Relative = Entry.path().lexically_relative(ProjectRoot);
			const FString PackagePath = NormalizePackagePath(FPaths::ToUtf8(Relative.generic_wstring()));
			if (!PathMatchesRule(PackagePath, Rule))
			{
				continue;
			}
			bMatchedAnyFile = true;
			if (!CopyProjectFileToPackage(ProjectRoot, OutputRoot, Entry.path(), Settings, OutError))
			{
				return false;
			}
		}
		if (!bMatchedAnyFile)
		{
			// A wildcard include is allowed to match zero files so platform-specific optional
			// content can share one packaging profile.
			continue;
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

	if (Settings.bIncludeD3DCompiler47)
	{
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
	std::filesystem::create_directories(OutputRoot / L"LuaScripts" / L"Config");
	std::filesystem::create_directories(OutputRoot / L"LuaScripts" / L"Game");
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
