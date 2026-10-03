@echo off
setlocal

set "PATH=C:\msys64\mingw64\bin;%PATH%"

if not exist "%~dp0build\dzo_convolution.exe" (
    echo Executable not found. Build the project first.
    exit /b 1
)

"%~dp0build\dzo_convolution.exe" %*
exit /b %ERRORLEVEL%
