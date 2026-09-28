# Checks the Windows artifacts on Windows; packaging/windows/test-windows.sh
# copies this script, the portable zip and a test installer into a scratch
# folder and runs it there.
#
# The test installer is built from the same kvit-notes.iss with its own AppId,
# product name and ProgID, so installing and uninstalling it cannot change a
# real Kvit Notes installation (Qt or Go) or its .md association. The script
# refuses to run with the real product's AppId or ProgID, and compares the real
# product's registry keys before and after to show they were left alone.
#
# What it does:
#   1. unzips the portable zip and starts kvit-notes.exe briefly;
#   2. puts files named like the Qt runtime into the install folder, installs
#      the test installer silently into that folder, and checks the installed
#      files, that the Qt files were removed, the Start-menu shortcut and the
#      HKCU keys of the .md association;
#   3. starts the installed kvit-notes.exe briefly;
#   (after 1 and 3, the math self-test in that folder;)
#   4. uninstalls silently and checks that the files, the shortcut and the
#      keys are gone.
# "Starting briefly" opens a scratch vault with --close-after, which closes
# the window after a few seconds, with APPDATA and LOCALAPPDATA pointed into
# the scratch folder so the program's settings and caches go there too.

param(
    [Parameter(Mandatory)] [string]$Work,
    [Parameter(Mandatory)] [string]$Zip,
    [Parameter(Mandatory)] [string]$Setup,
    [Parameter(Mandatory)] [string]$AppId,
    [Parameter(Mandatory)] [string]$AppName,
    [Parameter(Mandatory)] [string]$ProgId,
    # How to run the math self-test: "program" (kvit-notes.exe --math-selftest)
    # or "probe" (mathprobe.exe in the work folder, put beside kvit-notes.exe
    # for the run and removed again).
    [Parameter(Mandatory)] [ValidateSet('program', 'probe')] [string]$SelfTest
)
$ErrorActionPreference = 'Stop'

$RealAppId = '7B3D2E1A-9C64-4F58-A2D7-0E5F1B8C6A34'
$RealProgId = 'KvitNotes.md'
if ($AppId -eq $RealAppId -or $ProgId -eq $RealProgId -or $AppName -eq 'Kvit Notes') {
    throw "Refusing to test with the real product's identity ($AppId, $ProgId, $AppName)."
}

$script:failures = @()
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Output "  ok    $what" } else { Write-Output "  FAIL  $what"; $script:failures += $what }
}

$Classes = 'HKCU:\Software\Classes'
$UninstallRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'

# Every value under a registry key and its subkeys, as "key | name = data"
# lines, for comparing before and after.
function Get-RegistryLines([string]$key) {
    if (-not (Test-Path $key)) { return @("$key (absent)") }
    $keys = @(Get-Item $key) + @(Get-ChildItem $key -Recurse -ErrorAction SilentlyContinue)
    foreach ($k in $keys) {
        foreach ($name in $k.GetValueNames()) {
            $shown = if ($name -eq '') { '(default)' } else { $name }
            "$($k.Name) | $shown = $($k.GetValue($name))"
        }
    }
}

# The scratch vault the program opens.
$Vault = Join-Path $Work 'vault'
New-Item -ItemType Directory -Force $Vault | Out-Null
Set-Content -Encoding utf8 (Join-Path $Vault 'Packaging probe.md') "# Packaging probe`n`nA note for the packaging test.`n`n`$E = mc^2`$`n"

function Start-Briefly([string]$exe, [string]$label) {
    $appdata = Join-Path $Work "appdata-$label"
    New-Item -ItemType Directory -Force "$appdata\Roaming", "$appdata\Local" | Out-Null
    $saved = $env:APPDATA, $env:LOCALAPPDATA
    $env:APPDATA = "$appdata\Roaming"
    $env:LOCALAPPDATA = "$appdata\Local"
    $out = Join-Path $Work "$label.out.txt"
    $err = Join-Path $Work "$label.err.txt"
    try {
        $p = Start-Process -FilePath $exe -ArgumentList @('--close-after', '8s', "`"$Vault`"") `
            -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
        # Reading the handle now keeps the exit code readable after the exit.
        $null = $p.Handle
    } finally {
        $env:APPDATA, $env:LOCALAPPDATA = $saved
    }
    # While the window is open: which of the program's own libraries it loaded.
    Start-Sleep -Seconds 4
    $mods = @()
    try { $mods = (Get-Process -Id $p.Id).Modules | ForEach-Object { $_.ModuleName } } catch {}
    $math = if ($mods -contains 'kvitmath.dll') { 'loaded kvitmath.dll' } else { 'did not load kvitmath.dll (yet)' }
    Write-Output "  info  $label`: while running it $math"
    if (-not $p.WaitForExit(60000)) {
        Stop-Process -Id $p.Id -Force
        Check $false "$label`: kvit-notes.exe closed by itself within 60 s"
        return
    }
    $text = ((Get-Content $out -Raw) + (Get-Content $err -Raw)).Trim()
    Check ($p.ExitCode -eq 0) "$label`: kvit-notes.exe exited with code $($p.ExitCode)"
    Check ($text -match 'first frame after') "$label`: the window drew ($text)"
}

# The math self-test from the folder of an unpacked or installed program:
# it must pass with the math library and math-res of that folder.
function Test-Math([string]$dir, [string]$label) {
    $out = Join-Path $Work "$label.selftest.txt"
    $run = @{ WorkingDirectory = $Work; Wait = $true; PassThru = $true
              RedirectStandardOutput = $out; RedirectStandardError = "$out.err" }
    if ($SelfTest -eq 'program') {
        $exe = Join-Path $dir 'kvit-notes.exe'
        $run.ArgumentList = @('--math-selftest')
    } else {
        $exe = Join-Path $dir 'mathprobe.exe'
        Copy-Item (Join-Path $Work 'mathprobe.exe') $exe
        $label = "$label (mathtex.SelfTest through the probe)"
    }
    $p = Start-Process -FilePath $exe @run
    if ($SelfTest -eq 'probe') { Remove-Item $exe }
    $text = (Get-Content $out -Raw).Trim()
    $lib = (Join-Path $dir 'kvitmath.dll')
    Check ($p.ExitCode -eq 0 -and $text -match 'selftest: OK' -and $text.Contains("math-lib: $lib")) `
        "$label`: math self-test passed with the folder's own files"
    $text -split "`n" | ForEach-Object { Write-Output "          $($_.Trim())" }
}

# ── 1. The portable zip
Write-Output "== portable zip: $(Split-Path $Zip -Leaf)"
$ZipDir = Join-Path $Work 'zip'
Expand-Archive -Path $Zip -DestinationPath $ZipDir
$tops = @(Get-ChildItem $ZipDir)
Check ($tops.Count -eq 1) "the zip has one top folder ($($tops.Name -join ', '))"
$ZipExe = Join-Path $tops[0].FullName 'kvit-notes.exe'
Check (Test-Path $ZipExe) "kvit-notes.exe is in it"
$ver = (Get-Item $ZipExe).VersionInfo
Write-Output "  info  version information: $($ver.ProductName) $($ver.ProductVersion), file version $($ver.FileVersion)"
Start-Briefly $ZipExe 'zip'
Test-Math $tops[0].FullName 'zip'

# ── 2. Install
Write-Output "== test installer: $(Split-Path $Setup -Leaf) ($AppName, AppId {$AppId}, ProgID $ProgId)"
$Install = Join-Path $Work 'install'
$Start = Join-Path ([Environment]::GetFolderPath('Programs')) $AppName
$ProgKey = "$Classes\$ProgId"
$OpenWith = "$Classes\.md\OpenWithProgids"
$UninstallKey = "$UninstallRoot\{$AppId}_is1"
Check (-not (Test-Path $UninstallKey)) "no earlier test installation is registered"

$realBefore = @(Get-RegistryLines "$Classes\$RealProgId") + @(Get-RegistryLines "$UninstallRoot\{$RealAppId}_is1") +
    @(Get-ItemProperty $OpenWith -ErrorAction SilentlyContinue | ForEach-Object { $_.PSObject.Properties.Name } | Where-Object { $_ -notlike 'PS*' })

# Files an earlier Qt installation leaves in the folder, which the installer
# must remove, and one file of the user's, which it must leave.
$qtFiles = 'Qt6Core.dll', 'avcodec-61.dll', 'msvcp140.dll', 'platforms\qwindows.dll', 'qml\QtQuick\qmldir', 'licenses\qt\LICENSE.LGPL3'
foreach ($f in $qtFiles) {
    New-Item -ItemType Directory -Force (Split-Path (Join-Path $Install $f)) | Out-Null
    Set-Content (Join-Path $Install $f) 'stand-in for a Qt runtime file'
}
Set-Content (Join-Path $Install 'keep-me.txt') 'not the installer''s'

$log = Join-Path $Work 'install.log'
$p = Start-Process -FilePath $Setup -Wait -PassThru -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
    "/DIR=`"$Install`"", '/TASKS=associatemd', "/LOG=`"$log`"")
Check ($p.ExitCode -eq 0) "the installer exited with code $($p.ExitCode)"

$installed = @(Get-ChildItem $Install -Recurse -File | ForEach-Object { $_.FullName.Substring($Install.Length + 1) })
Write-Output "  info  $($installed.Count) files in $Install"
foreach ($f in 'kvit-notes.exe', 'kvitmath.dll', 'unins000.exe', 'unins000.dat', 'licenses\LICENSE',
               'licenses\THIRD-PARTY-NOTICES.md', 'math-res\fonts\licences\OFL.txt') {
    Check ($installed -contains $f) "installed $f"
}
foreach ($f in $qtFiles) { Check (-not (Test-Path (Join-Path $Install $f))) "removed the Qt file $f" }
foreach ($d in 'platforms', 'qml', 'licenses\qt') { Check (-not (Test-Path (Join-Path $Install $d))) "removed the Qt folder $d" }
Check (Test-Path (Join-Path $Install 'keep-me.txt')) "left keep-me.txt alone"
Remove-Item (Join-Path $Install 'keep-me.txt')

$shortcuts = @(Get-ChildItem $Start -Filter *.lnk -ErrorAction SilentlyContinue)
$shell = New-Object -ComObject WScript.Shell
foreach ($s in $shortcuts) { Write-Output "  info  Start menu: $($s.FullName) -> $($shell.CreateShortcut($s.FullName).TargetPath)" }
$main = Join-Path $Start "$AppName.lnk"
Check ((Test-Path $main) -and $shell.CreateShortcut($main).TargetPath -eq (Join-Path $Install 'kvit-notes.exe')) "Start-menu shortcut $AppName.lnk points at the installed kvit-notes.exe"

Write-Output "  info  registry values written:"
$written = @(Get-RegistryLines $ProgKey) + @(Get-RegistryLines $UninstallKey)
$written | ForEach-Object { Write-Output "          $_" }
Check ((Get-ItemProperty $ProgKey).'(default)' -eq 'Markdown note') "$ProgKey (default) = Markdown note"
Check ((Get-ItemProperty "$ProgKey\DefaultIcon").'(default)' -eq "$Install\kvit-notes.exe,0") "$ProgKey\DefaultIcon = the installed exe's icon"
Check ((Get-ItemProperty "$ProgKey\shell\open\command").'(default)' -eq "`"$Install\kvit-notes.exe`" `"%1`"") "$ProgKey\shell\open\command opens the file with the installed exe"
$openWithValues = Get-ItemProperty $OpenWith -ErrorAction SilentlyContinue
Check ($null -ne $openWithValues -and $openWithValues.PSObject.Properties.Name -contains $ProgId) "$OpenWith has the value $ProgId"
Check (Test-Path $UninstallKey) "the uninstall entry $UninstallKey exists"

# ── 3. Start the installed program
Write-Output "== installed program"
Start-Briefly (Join-Path $Install 'kvit-notes.exe') 'installed'
Test-Math $Install 'installed'

# ── 4. Uninstall
Write-Output "== uninstall"
$p = Start-Process -FilePath (Join-Path $Install 'unins000.exe') -Wait -PassThru -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
Check ($p.ExitCode -eq 0) "the uninstaller exited with code $($p.ExitCode)"
# The uninstaller runs a copy of itself from the temporary folder and returns
# at once, so wait until that copy has removed the uninstall entry and folder.
# The uninstaller removes the folders it created and leaves those that
# existed before, here the install folder and licenses, which the Qt
# stand-ins made (a real Qt installation's folders are in the uninstall log
# the Go installer extends, so they are removed there).
$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline -and ((Test-Path $UninstallKey) -or (Test-Path (Join-Path $Install 'unins000.exe')))) {
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Seconds 2
$leftFiles = @(Get-ChildItem $Install -Recurse -File -ErrorAction SilentlyContinue)
Check ($leftFiles.Count -eq 0) "no file is left in the install folder ($($leftFiles.Count) left)"
$leftDirs = @(Get-ChildItem $Install -Recurse -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($Install.Length + 1) })
Write-Output "  info  folders left, which existed before the install: $(if ($leftDirs) { $leftDirs -join ', ' } else { 'none' })"
Check (-not (Test-Path $Start)) "the Start-menu folder $Start is gone"
Check (-not (Test-Path $ProgKey)) "$ProgKey is gone"
$openWithValues = Get-ItemProperty $OpenWith -ErrorAction SilentlyContinue
Check ($null -eq $openWithValues -or $openWithValues.PSObject.Properties.Name -notcontains $ProgId) "the value $ProgId is gone from $OpenWith"
Check (-not (Test-Path $UninstallKey)) "the uninstall entry is gone"

$realAfter = @(Get-RegistryLines "$Classes\$RealProgId") + @(Get-RegistryLines "$UninstallRoot\{$RealAppId}_is1") +
    @(Get-ItemProperty $OpenWith -ErrorAction SilentlyContinue | ForEach-Object { $_.PSObject.Properties.Name } | Where-Object { $_ -notlike 'PS*' })
Check ((Compare-Object $realBefore $realAfter | Measure-Object).Count -eq 0) "the real product's keys ($RealProgId, {$RealAppId}_is1, .md\OpenWithProgids) are as they were"

# ── Result
$left = @(Get-Process kvit-notes -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Work*" })
Check ($left.Count -eq 0) "no kvit-notes.exe from the scratch folder is still running"
$left | Stop-Process -Force -ErrorAction SilentlyContinue
if ($script:failures.Count -gt 0) {
    Write-Output "$($script:failures.Count) check(s) failed."
    exit 1
}
Write-Output "All Windows checks passed."
