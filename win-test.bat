@echo off
rem Full unit-label suite; all output to win-test-result.txt (Windows-side).
rem
rem The redirection is not a convenience: piping Windows ctest output back
rem through the WSL interop pipe swallows test stdout and manufactures
rem phantom failures (devel.md, "Traps"). Read the file afterwards instead.
if not defined QT_ROOT_DIR set QT_ROOT_DIR=C:\Qt\6.10.1\msvc2022_64
if not defined VS_CMAKE_DIR set VS_CMAKE_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set PATH=%QT_ROOT_DIR%\bin;%PATH%
set CTEST=%VS_CMAKE_DIR%\ctest.exe
cd /d %~dp0
"%CTEST%" --test-dir build-windows-msvc-release -C Release -L unit --output-on-failure > win-test-result.txt 2>&1
echo suite-exit=%errorlevel% >> win-test-result.txt
exit /b 0
