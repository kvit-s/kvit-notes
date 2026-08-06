@echo off
rem Record one demo-tour segment. A thin wrapper over tools\record-tour.ps1,
rem so this works the same way as the other win-*.bat helpers.
rem
rem   win-record.bat tour-mermaid
rem   win-record.bat tour-mermaid "Drag a node, the markdown rewrites itself"
rem
rem Segments: tour-mermaid, tour-livepreview, tour-math, tour-repair,
rem tour-query. Build the driver first with win-uidriver.bat.
rem
rem The driver cannot be started by double-clicking it: it needs Qt's DLLs on
rem PATH, which Explorer does not provide, and it needs a scenario and a vault
rem on its command line. This script supplies all three.
setlocal
cd /d %~dp0

if "%~1"=="" (
    echo usage: win-record.bat SEGMENT ["caption"]
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
