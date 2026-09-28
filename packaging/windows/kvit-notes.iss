; This Source Code Form is subject to the terms of the Mozilla Public
; License, v. 2.0. If a copy of the MPL was not distributed with this
; file, You can obtain one at https://mozilla.org/MPL/2.0/.
;
; Inno Setup script for the Kvit Notes per-user Windows installer, built by
; packaging/windows/build-windows.sh, which passes the version, the staged
; tree to install and the output folder:
;   ISCC.exe /DKvitVersion=2.0.0 /DStageDir=<stage> /DOutputDir=<dist> kvit-notes.iss
;
; The Go version is the same product as the Qt version it replaces: the same
; AppId, name, per-user folder, Start-menu group and .md association. Run on a
; machine with the Qt Kvit Notes installed, it upgrades that installation in
; place: Inno finds the earlier installation by the AppId, installs into its
; folder, deletes the Qt runtime files the Go program does not use (the
; [InstallDelete] section), and extends the same uninstaller.
;
; packaging/windows/test-windows.sh builds a variant with its own AppId,
; name and ProgID (the Kvit* defines below) to try the installer without
; touching a real installation. Relative paths here are resolved against this
; script's folder.

#ifndef KvitVersion
  #error KvitVersion must be defined (pass /DKvitVersion=...)
#endif
#ifndef StageDir
  #error StageDir must be defined (pass /DStageDir=... the tree to install)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
; The version as four numbers for the installer's own file version, which
; cannot hold a pre-release suffix: 2.0.0 for 2.0.0-rc1.
#ifndef KvitVersionNumeric
  #define KvitVersionNumeric KvitVersion
#endif
; The product's identity. The defaults are the real product's, those of the
; Qt installer; never change them. A leading "{{" in AppId is Inno's way of
; writing one "{".
#ifndef KvitAppId
  #define KvitAppId "{{7B3D2E1A-9C64-4F58-A2D7-0E5F1B8C6A34}"
#endif
#ifndef KvitAppName
  #define KvitAppName "Kvit Notes"
#endif
#ifndef KvitProgId
  #define KvitProgId "KvitNotes.md"
#endif
#ifndef KvitOutputBase
  #define KvitOutputBase "Kvit_Notes-" + KvitVersion + "-setup"
#endif

[Setup]
; A fixed AppId ties every version together as one product, so a new version
; replaces the installed one and the uninstaller can find it.
AppId={#KvitAppId}
AppName={#KvitAppName}
AppVersion={#KvitVersion}
AppPublisher=Kvit Notes
AppPublisherURL=https://kvit.app
AppSupportURL=https://github.com/kvit-s/kvit-notes
DefaultDirName={autopf}\{#KvitAppName}
DefaultGroupName={#KvitAppName}
UninstallDisplayIcon={app}\kvit-notes.exe
; Per-user install: no administrator prompt. {autopf} then resolves to the
; per-user programs folder (%LOCALAPPDATA%\Programs), and the association
; below is written to HKCU.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; unison and the Go runtime need Windows 10.
MinVersion=10.0
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename={#KvitOutputBase}
SetupIconFile=..\icons\kvit.ico
VersionInfoVersion={#KvitVersionNumeric}
VersionInfoTextVersion={#KvitVersion}
VersionInfoProductName={#KvitAppName}
VersionInfoProductTextVersion={#KvitVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associatemd"; Description: "Open .md files with {#KvitAppName}"; GroupDescription: "File associations:"

[InstallDelete]
; The Qt runtime of an earlier Qt installation in the same folder: Qt and
; FFmpeg libraries, the Visual C++ runtime, Qt's plugin and QML folders, and
; Qt's licence texts (packaging/manifests/windows-1.0.0.txt of the Qt
; repository lists them). The Go program uses none of them. math-res is
; removed so the new one replaces it whole. Every name is specific to the Qt
; build, so nothing else in the folder is touched; entries that do not exist
; are skipped.
Type: files; Name: "{app}\Qt6*.dll"
Type: files; Name: "{app}\avcodec-*.dll"
Type: files; Name: "{app}\avformat-*.dll"
Type: files; Name: "{app}\avutil-*.dll"
Type: files; Name: "{app}\swresample-*.dll"
Type: files; Name: "{app}\swscale-*.dll"
Type: files; Name: "{app}\concrt140.dll"
Type: files; Name: "{app}\D3Dcompiler_47.dll"
Type: files; Name: "{app}\icuuc.dll"
Type: files; Name: "{app}\msvcp140*.dll"
Type: files; Name: "{app}\opengl32sw.dll"
Type: files; Name: "{app}\vccorlib140.dll"
Type: files; Name: "{app}\vcruntime140*.dll"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\multimedia"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\qmltooling"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\licenses\qt"
Type: filesandordirs; Name: "{app}\math-res"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#KvitAppName}"; Filename: "{app}\kvit-notes.exe"
Name: "{group}\{cm:UninstallProgram,{#KvitAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#KvitAppName}"; Filename: "{app}\kvit-notes.exe"; Tasks: desktopicon

[Registry]
; A ProgID for .md, registered per user (HKCU) to match the per-user install.
; The association is an optional task rather than forced, and every key is
; flagged so the uninstaller removes it.
Root: HKCU; Subkey: "Software\Classes\{#KvitProgId}"; ValueType: string; ValueName: ""; ValueData: "Markdown note"; Flags: uninsdeletekey; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\{#KvitProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\kvit-notes.exe,0"; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\{#KvitProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kvit-notes.exe"" ""%1"""; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "{#KvitProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associatemd

[Run]
Filename: "{app}\kvit-notes.exe"; Description: "{cm:LaunchProgram,{#KvitAppName}}"; Flags: nowait postinstall skipifsilent
