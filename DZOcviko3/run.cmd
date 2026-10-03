@echo off
setlocal

set "PATH=C:\msys64\mingw64\bin;%PATH%"

if not exist "%~dp0build\dzo_anisotropic.exe" (
    echo Executable not found. Build the project first.
    exit /b 1
)

pushd "%~dp0"
"%~dp0build\dzo_anisotropic.exe" %*
set "PROGRAM_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %PROGRAM_EXIT_CODE%
