@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
pushd "%~dp0"
if not exist build mkdir build
g++ -std=c++17 -O2 -Wall -Wextra main.cpp -IC:/msys64/mingw64/include/opencv5 -LC:/msys64/mingw64/lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui -o build/dzo_frequency_filter.exe
if errorlevel 1 (
    popd
    exit /b 1
)
"%~dp0build\dzo_frequency_filter.exe" %*
set "PROGRAM_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %PROGRAM_EXIT_CODE%
