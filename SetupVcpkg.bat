@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "TRIPLET=x64-windows"
set "DEFAULT_VCPKG_ROOT=%USERPROFILE%\vcpkg"
set "VCPKG_EXE="

pushd "%ROOT_DIR%" >nul

echo [SetupVcpkg] Project root: %ROOT_DIR%
echo [SetupVcpkg] Triplet: %TRIPLET%

where vcpkg >nul 2>nul
if not errorlevel 1 (
    for /f "delims=" %%I in ('where vcpkg 2^>nul') do (
        if not defined VCPKG_EXE set "VCPKG_EXE=%%I"
    )
)

if not defined VCPKG_EXE (
    echo [SetupVcpkg] vcpkg was not found in PATH.
    echo [SetupVcpkg] Using local install at: %DEFAULT_VCPKG_ROOT%

    if not exist "%DEFAULT_VCPKG_ROOT%\vcpkg.exe" (
        if not exist "%DEFAULT_VCPKG_ROOT%\.git" (
            echo [SetupVcpkg] Cloning vcpkg...
            git clone https://github.com/microsoft/vcpkg "%DEFAULT_VCPKG_ROOT%"
            if errorlevel 1 goto :fail
        )

        echo [SetupVcpkg] Bootstrapping vcpkg...
        call "%DEFAULT_VCPKG_ROOT%\bootstrap-vcpkg.bat"
        if errorlevel 1 goto :fail
    )

    set "VCPKG_EXE=%DEFAULT_VCPKG_ROOT%\vcpkg.exe"
)

echo [SetupVcpkg] Using vcpkg: %VCPKG_EXE%
echo [SetupVcpkg] Installing manifest dependencies from vcpkg.json...
"%VCPKG_EXE%" install --triplet %TRIPLET%
if errorlevel 1 goto :fail

echo.
echo [SetupVcpkg] Done.
echo [SetupVcpkg] Installed packages should be under: %ROOT_DIR%vcpkg_installed
popd >nul
exit /b 0

:fail
echo.
echo [SetupVcpkg] Failed. Check the messages above.
popd >nul
exit /b 1
