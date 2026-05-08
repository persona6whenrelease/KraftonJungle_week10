#include "Engine/Platform/Paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

std::wstring FPaths::RootDir()
{
	static std::wstring Cached;
	if (Cached.empty())
	{
		WCHAR Buffer[MAX_PATH];
		GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
		std::filesystem::path ExeDir = std::filesystem::path(Buffer).parent_path();

#if IS_GAME_CLIENT
		Cached = ExeDir.wstring() + L"\\";
#else
		// exe 옆에 Shaders/ 가 있으면 배포 환경, 없으면 개발 환경 (CWD 사용)
		if (std::filesystem::exists(ExeDir / L"Shaders"))
		{
			// 배포: exe와 리소스가 같은 디렉터리
			Cached = ExeDir.wstring() + L"\\";
		}
		else
		{
			// 개발: CWD(= $(ProjectDir))에 리소스가 있음
			Cached = std::filesystem::current_path().wstring() + L"\\";
		}
#endif
	}
	return Cached;
}

std::wstring FPaths::ShaderDir()   { return RootDir() + L"Shaders\\"; }
std::wstring FPaths::ScriptDir()   { return RootDir() + L"LuaScripts\\"; }
std::wstring FPaths::AssetDir()    { return RootDir() + L"Asset\\"; }
std::wstring FPaths::SceneDir()    { return RootDir() + L"Asset\\Scene\\"; }
std::wstring FPaths::PrefabDir()   { return RootDir() + L"Asset\\Prefab\\"; }
std::wstring FPaths::DataDir()     { return RootDir() + L"Data\\"; }
std::wstring FPaths::SaveDir()     { return RootDir() + L"Saves\\"; }
std::wstring FPaths::DumpDir()     { return RootDir() + L"Saves\\Dump\\"; }
std::wstring FPaths::LogDir()      { return RootDir() + L"Saves\\Logs\\"; }
std::wstring FPaths::SettingsDir() { return RootDir() + L"Settings\\"; }

std::wstring FPaths::SettingsFilePath() { return RootDir() + L"Settings\\Editor.ini"; }
std::wstring FPaths::GameSettingsFilePath() { return RootDir() + L"Settings\\Game.ini"; }
std::wstring FPaths::ResourceFilePath() { return RootDir() + L"Settings\\Resource.ini"; }
std::wstring FPaths::ProjectSettingsFilePath() { return RootDir() + L"Settings\\ProjectSettings.ini"; }
std::wstring FPaths::PackageManifestFilePath() { return RootDir() + L"GamePackage.json"; }

std::wstring FPaths::Combine(const std::wstring& Base, const std::wstring& Child)
{
	std::filesystem::path Result(Base);
	Result /= Child;
	return Result.wstring();
}

void FPaths::CreateDir(const std::wstring& Path)
{
	std::filesystem::create_directories(Path);
}

std::wstring FPaths::ToWide(const std::string& Utf8Str)
{
	if (Utf8Str.empty()) return {};
	int32_t Size = MultiByteToWideChar(CP_UTF8, 0, Utf8Str.data(), static_cast<int>(Utf8Str.size()), nullptr, 0);
	std::wstring Result(Size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8Str.data(), static_cast<int>(Utf8Str.size()), Result.data(), Size);
	return Result;
}

std::string FPaths::ToUtf8(const std::wstring& WideStr)
{
	if (WideStr.empty()) return {};
	int32_t Size = WideCharToMultiByte(CP_UTF8, 0, WideStr.data(), static_cast<int>(WideStr.size()), nullptr, 0, nullptr, nullptr);
	std::string Result(Size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, WideStr.data(), static_cast<int>(WideStr.size()), Result.data(), Size, nullptr, nullptr);
	return Result;
}

std::string FPaths::ResolveAssetPath(const std::string& BaseFilePath, const std::string& TargetPath)
{
	// 1. 기준 파일(OBJ 또는 MTL)의 폴더 경로 추출
	std::filesystem::path FileDir(ToWide(BaseFilePath));
	FileDir = FileDir.parent_path();

	// 2. 타겟 파일 경로(텍스처나 MTL 이름)
	std::filesystem::path Target(ToWide(TargetPath));

	// 3. 두 경로 합치기 및 정규화
	std::filesystem::path FullPath = (FileDir / Target).lexically_normal();
	std::filesystem::path ProjectRoot(RootDir());

	std::filesystem::path RelativePath;

	// 4. 절대/상대 경로 분기 처리
	if (FullPath.is_absolute())
	{
		RelativePath = FullPath.lexically_relative(ProjectRoot);
		if (RelativePath.empty())
		{
			// 드라이브가 다르거나 계산이 불가능한 경우 최후의 수단으로 파일명만 추출
			RelativePath = FullPath.filename();
		}
	}
	else
	{
		RelativePath = FullPath;
	}

	// 5. 엔진에서 사용하는 UTF-8 포맷으로 반환 (Windows 백슬래시를 슬래시로 통일)
	return ToUtf8(RelativePath.generic_wstring());
}

namespace
{
	std::string NormalizeSlashes(std::string Text)
	{
		std::replace(Text.begin(), Text.end(), '\\', '/');
		return Text;
	}

	bool HasParentTraversal(const std::filesystem::path& Path)
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

	std::wstring LowerPath(std::wstring Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](wchar_t Ch)
			{
				return static_cast<wchar_t>(std::towlower(Ch));
			});
		return Text;
	}
}

bool FPaths::TryResolvePackagePath(const std::string& PackageRelativePath, std::wstring& OutDiskPath, std::string* OutError)
{
	const std::string NormalizedText = NormalizeSlashes(PackageRelativePath);
	if (NormalizedText.empty())
	{
		if (OutError)
		{
			*OutError = "Path is empty.";
		}
		return false;
	}

	const std::filesystem::path Root = std::filesystem::path(RootDir()).lexically_normal();
	const std::filesystem::path Input = std::filesystem::path(ToWide(NormalizedText));

#if IS_GAME_CLIENT
	if (Input.is_absolute() || Input.has_root_name())
	{
		if (OutError)
		{
			*OutError = "Package path must be relative: " + NormalizedText;
		}
		return false;
	}

	if (HasParentTraversal(Input))
	{
		if (OutError)
		{
			*OutError = "Package path must not contain ../: " + NormalizedText;
		}
		return false;
	}
#endif

	std::filesystem::path Resolved =
		Input.is_absolute()
		? Input.lexically_normal()
		: (Root / Input).lexically_normal();

#if IS_GAME_CLIENT
	if (!IsPathInsideRoot(Root, Resolved))
	{
		if (OutError)
		{
			*OutError = "Path escapes package root: " + NormalizedText;
		}
		return false;
	}
#endif

	OutDiskPath = Resolved.wstring();
	return true;
}

bool FPaths::TryResolveShaderPath(const std::string& ShaderPath, std::wstring& OutDiskPath, std::string* OutError)
{
	const std::string Normalized = NormalizeSlashes(ShaderPath);

#if IS_GAME_CLIENT
	if (Normalized.rfind("Shaders/", 0) != 0)
	{
		if (OutError)
		{
			*OutError = "Shader path must be under Shaders/: " + Normalized;
		}
		return false;
	}
#endif

	return TryResolvePackagePath(Normalized, OutDiskPath, OutError);
}

bool FPaths::TryResolveAssetPath(const std::string& AssetPath, std::wstring& OutDiskPath, std::string* OutError)
{
	const std::string Normalized = NormalizeSlashes(AssetPath);

#if IS_GAME_CLIENT
	if (Normalized.rfind("Asset/", 0) != 0)
	{
		if (OutError)
		{
			*OutError = "Asset path must be under Asset/: " + Normalized;
		}
		return false;
	}
#endif

	return TryResolvePackagePath(Normalized, OutDiskPath, OutError);
}

bool FPaths::TryResolveScriptPath(const std::string& ScriptPath, std::wstring& OutDiskPath, std::string* OutError)
{
	std::string Normalized = NormalizeSlashes(ScriptPath);
#if IS_GAME_CLIENT
	const std::filesystem::path OriginalInput = std::filesystem::path(ToWide(Normalized));
	if (OriginalInput.is_absolute() || OriginalInput.has_root_name())
	{
		if (OutError)
		{
			*OutError = "Script path must be relative: " + ScriptPath;
		}
		return false;
	}
#endif

	if (Normalized.rfind("LuaScripts/", 0) != 0)
	{
		Normalized = "LuaScripts/" + Normalized;
	}

#if IS_GAME_CLIENT
	if (Normalized.rfind("LuaScripts/", 0) != 0)
	{
		if (OutError)
		{
			*OutError = "Script path must be under LuaScripts/: " + ScriptPath;
		}
		return false;
	}
#endif

	return TryResolvePackagePath(Normalized, OutDiskPath, OutError);
}

bool FPaths::IsPathInsideRoot(const std::filesystem::path& Root, const std::filesystem::path& Target)
{
	std::wstring RootText = Root.lexically_normal().wstring();
	std::wstring TargetText = Target.lexically_normal().wstring();

	if (!RootText.empty() && RootText.back() != L'\\' && RootText.back() != L'/')
	{
		RootText += L'\\';
	}

	RootText = LowerPath(RootText);
	TargetText = LowerPath(TargetText);

	return TargetText.rfind(RootText, 0) == 0;
}
