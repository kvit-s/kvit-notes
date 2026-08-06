@echo off
rem Build the scriptable UI driver (tools/uidriver.cpp) on Windows, which is
rem what records the demo videos: real GPU text rather than the corrupted
rem glyphs the WSL software path produces.
rem
rem Usage: win-uidriver.bat        (re-syncs from WSL, configures, builds)
rem
rem Run from the Windows mirror directory, not from a WSL checkout, the same
rem rule win-build.bat follows. The driver is off in a default configure
rem (KVIT_UI_DRIVER=OFF), so this adds the flag and builds that one target;
rem the flag persists in the cache, so a later win-build.bat keeps it.
rem
rem Toolchain paths can be overridden in the environment; the defaults match
rem a stock Visual Studio 2022 Community plus Qt under C:\Qt.
setlocal
if not defined QT_ROOT_DIR set QT_ROOT_DIR=C:\Qt\6.10.1\msvc2022_64
if not defined VS_CMAKE_DIR set VS_CMAKE_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set CMAKE=%VS_CMAKE_DIR%\cmake.exe
cd /d %~dp0

rem Mirror first, so the driver never records stale code. win-build.bat owns
rem that step and refuses an uninitialized mirror, so call it rather than
rem repeating the check here.
call win-build.bat configure
if errorlevel 1 exit /b 1

"%CMAKE%" --preset windows-msvc-release -DKVIT_UI_DRIVER=ON
if errorlevel 1 exit /b 1

"%CMAKE%" --build --preset windows-msvc-release --target kvit-uidriver -j 8
if errorlevel 1 exit /b 1

echo.
echo Driver built: build-windows-msvc-release\Release\kvit-uidriver.exe
echo Record a segment with: powershell -ExecutionPolicy Bypass -File tools\record-tour.ps1 -Segment tour-mermaid
exit /b 0
