@echo off
setlocal
cd /d "%~dp0"
if not exist build-render-kit\CMakeCache.txt (
    cmake -S render_kit -B build-render-kit -G Ninja -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)
cmake --build build-render-kit --target RenderKit
if errorlevel 1 exit /b 1
build-render-kit\RenderKit.exe
