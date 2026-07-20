@echo off
rem Configure + build Kvit Notes with MSVC 2022 / Qt 6.10.1.
rem Usage: win-build.bat [configure|build|test]  (no arg = configure+build)
rem
rem Run from the Windows mirror directory, not from a WSL checkout. The
rem mirror is created once with `tools/win-sync.sh --init DESTINATION`, which
rem writes the .kvit-notes-mirror marker this script reads.
rem
rem Every configure/build re-syncs the mirror from the WSL checkout first, so
rem a Windows build can never run stale code. Set KVIT_NO_SYNC=1 to skip it
rem when WSL is unavailable.
rem
rem Toolchain paths can be overridden in the environment; the defaults match
rem a stock Visual Studio 2022 Community plus Qt under C:\Qt.
setlocal enabledelayedexpansion
if not defined QT_ROOT_DIR set QT_ROOT_DIR=C:\Qt\6.10.1\msvc2022_64
if not defined VS_CMAKE_DIR set VS_CMAKE_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set CMAKE=%VS_CMAKE_DIR%\cmake.exe
set CTEST=%VS_CMAKE_DIR%\ctest.exe
cd /d %~dp0

if "%KVIT_NO_SYNC%"=="1" goto synced

rem The sync propagates deletions, so neither end of it is guessed. The
rem source checkout is read from the marker that --init wrote here, and this
rem directory is named explicitly as the destination. A mirror without a
rem marker is not synced at all: win-sync.sh refuses it, and so does this.
if not exist "%~dp0.kvit-notes-mirror" (
    echo win-build: not an initialized Kvit Notes mirror ^(no .kvit-notes-mirror^).
    echo win-build: create one from the WSL checkout with:
    echo     tools/win-sync.sh --init DESTINATION
    exit /b 1
)

set "WSL_SRC="
for /f "usebackq tokens=1,* delims==" %%A in ("%~dp0.kvit-notes-mirror") do (
    if "%%A"=="source" set "WSL_SRC=%%B"
)
if not defined WSL_SRC (
    echo win-build: .kvit-notes-mirror names no source checkout.
    echo win-build: re-create the mirror with tools/win-sync.sh --init.
    exit /b 1
)

set "WSL_DST="
for /f "usebackq delims=" %%P in (`wsl.exe -e wslpath -a "%CD%"`) do set "WSL_DST=%%P"
if not defined WSL_DST (
    echo win-build: could not resolve this directory as a WSL path.
    exit /b 1
)

wsl.exe -e bash "!WSL_SRC!/tools/win-sync.sh" "!WSL_DST!"
if errorlevel 1 (
    echo win-build: sync from WSL failed, refusing to build stale code
    exit /b 1
)
:synced

if "%1"=="build" goto build
if "%1"=="test" goto test

"%CMAKE%" --preset windows-msvc-release
if errorlevel 1 exit /b 1
if "%1"=="configure" exit /b 0

:build
"%CMAKE%" --build --preset windows-msvc-release -j 8
exit /b %errorlevel%

:test
set PATH=%QT_ROOT_DIR%\bin;%PATH%
"%CTEST%" --test-dir build-windows-msvc-release -C Release -L unit --output-on-failure
exit /b %errorlevel%
