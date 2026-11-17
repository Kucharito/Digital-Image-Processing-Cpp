@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"

if not exist "%~dp0build\dzo_dft.exe" (
    echo Executable not found. Build the project first; see README.md.
    exit /b 1
)

rem Run from the project folder so the default image and outputs are found.
pushd "%~dp0"
"%~dp0build\dzo_dft.exe" %*
set "PROGRAM_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %PROGRAM_EXIT_CODE%
