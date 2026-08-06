@echo off
rem Record one demo-tour segment. A thin wrapper over tools\record-tour.ps1,
rem so this works the same way as the other win-*.bat helpers.
rem
rem   win-record.bat tour-all
rem   win-record.bat tour-mermaid
rem   win-record.bat tour-mermaid "Drag a node, the markdown rewrites itself"
rem
rem tour-all plays all five features in one window, about 75 seconds, for a
rem single continuous take. The individual segments run 15 to 25 seconds each
rem and are the ones to record separately, since a bad take is then re-shot
rem alone and each clip is also a short loop for the README.
rem
rem Each segment captions itself, so the caption argument is only needed to
rem override the built-in one. Build the driver first with win-uidriver.bat.
rem
rem The driver cannot be started by double-clicking it: it needs Qt's DLLs on
rem PATH, which Explorer does not provide, and it needs a scenario and a vault
rem on its command line. This script supplies all three.
setlocal
cd /d %~dp0

if "%~1"=="" (
    echo usage: win-record.bat SEGMENT ["caption"]
    echo.
    echo   tour-all           all five below, one window, about 75 seconds
    echo.
    echo   tour-mermaid       drag a node, the fence rewrites itself
    echo   tour-livepreview   syntax reveals itself around the caret
    echo   tour-math          an equation typed a character at a time
    echo   tour-repair        crooked box art pasted, rendered straight
    echo   tour-query         a table gaining a row when a note changes
    exit /b 2
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\record-tour.ps1" -Segment %1 -Title "%~2"
exit /b %errorlevel%
