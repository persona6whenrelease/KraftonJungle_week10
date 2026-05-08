#include "Editor/Packaging/GamePackageSmokeTester.h"

#include "Engine/Platform/Paths.h"

#include <Windows.h>
#include <filesystem>

bool FGamePackageSmokeTester::Run(const FString& ExecutablePath, FString& OutError)
{
	const std::wstring Exe = FPaths::ToWide(ExecutablePath);
	if (Exe.empty() || !std::filesystem::exists(std::filesystem::path(Exe)))
	{
		OutError = "GameClient smoke test executable does not exist.";
		return false;
	}

	STARTUPINFOW StartupInfo = {};
	StartupInfo.cb = sizeof(StartupInfo);

	PROCESS_INFORMATION ProcessInfo = {};
	std::wstring WorkingDir = std::filesystem::temp_directory_path().wstring();

	BOOL bStarted = CreateProcessW(
		Exe.c_str(),
		nullptr,
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		WorkingDir.c_str(),
		&StartupInfo,
		&ProcessInfo);

	if (!bStarted)
	{
		OutError = "Failed to start GameClient smoke test.";
		return false;
	}

	const DWORD WaitResult = WaitForSingleObject(ProcessInfo.hProcess, 5000);
	if (WaitResult == WAIT_OBJECT_0)
	{
		DWORD ExitCode = 0;
		GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
		CloseHandle(ProcessInfo.hProcess);
		CloseHandle(ProcessInfo.hThread);

		OutError = "GameClient exited during smoke test. ExitCode=" + std::to_string(ExitCode);
		return false;
	}

	TerminateProcess(ProcessInfo.hProcess, 0);
	CloseHandle(ProcessInfo.hProcess);
	CloseHandle(ProcessInfo.hThread);
	return true;
}
