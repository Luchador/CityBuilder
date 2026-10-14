@echo off
cd /d "%~dp0"
cmake --build build && build\CityBuilder.exe