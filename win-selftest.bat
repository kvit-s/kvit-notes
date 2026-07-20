@echo off
rem Run the deployed exe's math self-test WITHOUT Qt on PATH: proves the
rem windeployqt staging is self-contained and that math-res resolves
rem relative to the executable in the deployed layout.
cd /d %~dp0\build-windows-msvc-release\Release
kvit-notes.exe --math-selftest > ..\..\selftest.log 2>&1
echo selftest-exit=%errorlevel% >> ..\..\selftest.log
