@echo off
rem Stage the Qt runtime next to the built kvit-notes.exe so it runs from
rem Explorer. Also the basis of the portable zip.
rem
rem --qmldir qml is what makes windeployqt deploy the QML modules the shell
rem imports. The app compiles its QML into resources.qrc, so without a
rem directory to scan the deploy tool has no import graph to follow - the
rem same failure that shipped a Linux AppImage unable to load its own UI.
if not defined QT_ROOT_DIR set QT_ROOT_DIR=C:\Qt\6.10.1\msvc2022_64
cd /d %~dp0
"%QT_ROOT_DIR%\bin\windeployqt.exe" --release --qmldir qml build-windows-msvc-release\Release\kvit-notes.exe > windeploy.log 2>&1
exit /b %errorlevel%
