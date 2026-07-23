; This Source Code Form is subject to the terms of the Mozilla Public
; License, v. 2.0. If a copy of the MPL was not distributed with this
; file, You can obtain one at https://mozilla.org/MPL/2.0/.
;
; Inno Setup script for the Kvit Notes per-user Windows installer. It is
; driven by packaging/windows/build-windows.ps1, which passes the version, the
; staged tree to install, and the output directory. To build by hand:
;   iscc /DKvitVersion=1.0.0 /DStageDir=<stage-dir> /DOutputDir=<dist> kvit-notes.iss
;
; Relative paths (the icon, the [Files] source when StageDir is relative) are
; resolved against this script's directory, which Inno uses as SourceDir.

#ifndef KvitVersion
  #error KvitVersion must be defined (pass /DKvitVersion=...)
#endif
#ifndef StageDir
  #error StageDir must be defined (pass /DStageDir=... the tree to install)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; A fixed AppId ties every version together as one product, so upgrades
; replace the prior install and the uninstaller can find it. Never change it.
AppId={{7B3D2E1A-9C64-4F58-A2D7-0E5F1B8C6A34}
AppName=Kvit Notes
AppVersion={#KvitVersion}
AppPublisher=Kvit Notes
AppPublisherURL=https://kvit.app
AppSupportURL=https://github.com/kvit-s/kvit-notes
DefaultDirName={autopf}\Kvit Notes
DefaultGroupName=Kvit Notes
UninstallDisplayIcon={app}\kvit-notes.exe
; Per-user install: no administrator prompt. {autopf} then resolves to the
; per-user programs location, and the association below is written to HKCU.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=Kvit_Notes-{#KvitVersion}-setup
SetupIconFile=..\icons\kvit.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associatemd"; Description: "Open .md files with Kvit Notes"; GroupDescription: "File associations:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Kvit Notes"; Filename: "{app}\kvit-notes.exe"
Name: "{group}\{cm:UninstallProgram,Kvit Notes}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Kvit Notes"; Filename: "{app}\kvit-notes.exe"; Tasks: desktopicon

[Registry]
; A ProgID for .md, registered per user (HKCU) to match the per-user install.
; The association is an optional task rather than forced, and every key is
; flagged so the uninstaller removes it.
Root: HKCU; Subkey: "Software\Classes\KvitNotes.md"; ValueType: string; ValueName: ""; ValueData: "Markdown note"; Flags: uninsdeletekey; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\KvitNotes.md\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\kvit-notes.exe,0"; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\KvitNotes.md\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kvit-notes.exe"" ""%1"""; Tasks: associatemd
Root: HKCU; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "KvitNotes.md"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associatemd

[Run]
Filename: "{app}\kvit-notes.exe"; Description: "{cm:LaunchProgram,Kvit Notes}"; Flags: nowait postinstall skipifsilent
