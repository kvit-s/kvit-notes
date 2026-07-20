@echo off
rem Run one ctest test very verbosely: win-ctest-one.bat <TestNameRegex>
if not defined QT_ROOT_DIR set QT_ROOT_DIR=C:\Qt\6.10.1\msvc2022_64
if not defined VS_CMAKE_DIR set VS_CMAKE_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set PATH=%QT_ROOT_DIR%\bin;%PATH%
set CTEST=%VS_CMAKE_DIR%\ctest.exe
cd /d %~dp0
"%CTEST%" --test-dir build-windows-msvc-release -C Release -R %1 -VV --no-compress-output > ctest-one.txt 2>&1
exit /b %errorlevel%
