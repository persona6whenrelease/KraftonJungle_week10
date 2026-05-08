@echo off
setlocal
set SCRIPT=%~dp0Scripts\GenerateProjectFiles.py
set PYTHON_EMBED=%~dp0Scripts\python\python.exe

if exist "%PYTHON_EMBED%" (
    "%PYTHON_EMBED%" "%SCRIPT%" %*
) else (
    python "%SCRIPT%" %*
)

pause
