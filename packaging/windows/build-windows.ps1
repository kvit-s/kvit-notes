# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Build the Windows artifacts: configure and install the windows-msvc-release
# preset into a staging tree, bundle the Qt runtime with windeployqt and the
# MSVC C runtime, then emit into dist/:
#   Kvit_Notes-<version>-windows-x64.zip   portable zip
#   Kvit_Notes-<version>-setup.exe         Inno Setup per-user installer
#   SHA256SUMS-windows.txt                 checksums of both
#
# Run from anywhere; it works from the repo root. A clean checkout needs only
# the Qt msvc2022_64 kit (QT_ROOT_DIR on the environment or windeployqt on
# PATH), Visual Studio 2022, and Inno Setup 6 (iscc). It configures the preset
# itself, because the tag-triggered packaging job in CI runs on a fresh
# checkout. This mirrors packaging/linux/build-appimage.sh; keep the two in
# step.
#
# Version: pass KVIT_VERSION_FULL, or let the script derive it from the tag
# being built (GITHUB_REF_NAME, or `git describe`).

$ErrorActionPreference = 'Stop'
# Native tools (cmake, windeployqt, iscc) do not raise PowerShell errors, so
# every invocation below checks $LASTEXITCODE explicitly.

function Assert-Exit([string]$what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $RepoRoot
$BuildDir = Join-Path $RepoRoot 'build-windows-msvc-release'
$Dist = Join-Path $RepoRoot 'dist'

# ── Release version (same rules and precedence as build-appimage.sh) ──
#
# PROJECT_VERSION carries the three numeric components; the version the binary
# reports and the artifacts are named for is the full SemVer string including
# any prerelease suffix. An explicit KVIT_VERSION_FULL wins; otherwise the tag
# supplies it; otherwise this is a local build of the base version. A tag
# whose base version disagrees with PROJECT_VERSION is an error.
$cmakeText = Get-Content -Raw (Join-Path $RepoRoot 'CMakeLists.txt')
if ($cmakeText -match 'project\(kvit-notes VERSION (\d+\.\d+\.\d+)') {
    $BaseVersion = $Matches[1]
} else {
    throw 'Could not read PROJECT_VERSION from CMakeLists.txt'
}

if ($env:KVIT_VERSION_FULL) {
    $Version = $env:KVIT_VERSION_FULL
    $VersionSource = 'KVIT_VERSION_FULL'
} elseif ($env:GITHUB_REF_NAME) {
    $Version = $env:GITHUB_REF_NAME -replace '^v', ''
    $VersionSource = "tag $($env:GITHUB_REF_NAME)"
} else {
    $tag = ''
    try { $tag = (& git describe --tags --exact-match 2>$null) } catch { $tag = '' }
    if ($tag) {
        $Version = ("$tag").Trim() -replace '^v', ''
        $VersionSource = "tag $tag"
    } else {
        $Version = $BaseVersion
        $VersionSource = 'CMakeLists.txt (untagged build)'
    }
}

$semver = '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'
if ($Version -notmatch $semver) {
    throw "Version '$Version' (from $VersionSource) is not a valid SemVer version. Tags must look like v1.0.0 or v1.0.0-rc1."
}
$baseEsc = [regex]::Escape($BaseVersion)
if ($Version -ne $BaseVersion -and $Version -notmatch "^$baseEsc[-+]") {
    throw "Version mismatch: '$Version' (from $VersionSource) does not have base version '$BaseVersion' from CMakeLists.txt. Bump project(kvit-notes VERSION ...) or retag."
}
Write-Host "Building version $Version (from $VersionSource)"

# ── Configure, build, install into the staging tree ──
& cmake --preset windows-msvc-release "-DKVIT_VERSION_FULL=$Version"
Assert-Exit 'cmake configure'
& cmake --build --preset windows-msvc-release --target kvit-notes
Assert-Exit 'cmake build'

$StageName = "Kvit_Notes-$Version-windows-x64"
$Stage = Join-Path $BuildDir $StageName
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
& cmake --install $BuildDir --config Release --prefix $Stage
Assert-Exit 'cmake install'

# ── Deploy the Qt runtime ──
#
# --qmldir gives windeployqt the import graph: the app compiles its QML into
# resources.qrc, so there is no QML tree to scan otherwise, the same failure
# that once shipped a Linux AppImage unable to load its own UI.
$windeployqt = if ($env:QT_ROOT_DIR) { Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe' } else { 'windeployqt.exe' }
& $windeployqt --release --qmldir (Join-Path $RepoRoot 'qml') (Join-Path $Stage 'kvit-notes.exe')
Assert-Exit 'windeployqt'

if (-not $env:QT_ROOT_DIR) { throw 'QT_ROOT_DIR is not set; cannot locate the offscreen plugin.' }
# The offscreen platform plugin rides along so --math-selftest runs headless
# from the packaged layout; windeployqt stages qwindows, not qoffscreen.
$platDir = Join-Path $Stage 'platforms'
New-Item -ItemType Directory -Force -Path $platDir | Out-Null
Copy-Item (Join-Path $env:QT_ROOT_DIR 'plugins\platforms\qoffscreen.dll') $platDir -Force

# ── MSVC C runtime, so the zip runs without a separately installed redist ──
#
# Bundle the newest redist across every Visual Studio install: the C runtime's
# compatibility rule is that a runtime at least as new as the toolset that
# built the binary is safe, so the highest version present is always correct.
# This walks the install tree directly rather than trusting vswhere, which on
# a machine carrying both VS 2022 (VC143) and VS 2019 Build Tools (VC142)
# resolved to the older redist. Only numeric `x.y.z` version directories are
# considered (the `vNNN` alias dirs are skipped), and DebugCRT is excluded.
$vsRoots = @(
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
) | Where-Object { Test-Path $_ }
$crtCandidates = foreach ($root in $vsRoots) {
    foreach ($year in Get-ChildItem $root -Directory -ErrorAction SilentlyContinue) {
        foreach ($edition in Get-ChildItem $year.FullName -Directory -ErrorAction SilentlyContinue) {
            $redist = Join-Path $edition.FullName 'VC\Redist\MSVC'
            if (-not (Test-Path $redist)) { continue }
            Get-ChildItem $redist -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                ForEach-Object {
                    $crt = Get-ChildItem (Join-Path $_.FullName 'x64') -Directory `
                        -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -notmatch 'Debug' } | Select-Object -First 1
                    if ($crt) { [pscustomobject]@{ Ver = [version]$_.Name; Dir = $crt } }
                }
        }
    }
}
if (-not $crtCandidates) { throw 'No Microsoft.VC*.CRT redist found in any Visual Studio install.' }
Write-Host ("Redist candidates: " + (($crtCandidates | Sort-Object Ver -Descending |
    ForEach-Object { "$($_.Dir.Name)@$($_.Ver)" }) -join ', '))
$crtDir = ($crtCandidates | Sort-Object Ver -Descending | Select-Object -First 1).Dir
Copy-Item (Join-Path $crtDir.FullName '*.dll') $Stage -Force
Write-Host "Bundled the MSVC C runtime $($crtDir.Name) (redist $($crtDir.Parent.Parent.Name))"

# ── Qt LGPL obligations (packaging/qt-lgpl-checklist.md) ──
#
# The project's own licenses arrive through `cmake --install`; only Qt's are
# added here, since they belong to the Qt kit rather than this source tree.
$qtLicDir = Join-Path $Stage 'licenses\qt'
New-Item -ItemType Directory -Force -Path $qtLicDir | Out-Null
Copy-Item (Join-Path $RepoRoot 'packaging\licenses\qt\LICENSE.LGPL3') $qtLicDir -Force
Copy-Item (Join-Path $RepoRoot 'packaging\licenses\qt\LICENSE.GPL3') $qtLicDir -Force
$qtKitLicense = Join-Path $env:QT_ROOT_DIR '..\..\Licenses\LICENSE'
if (Test-Path $qtKitLicense) {
    Copy-Item $qtKitLicense (Join-Path $qtLicDir 'LICENSE.qt-kit') -Force
    Write-Host 'Included the Qt kit''s own licensing document.'
}

# ── Fail closed: a complete license payload and a runnable interface ──
#
# check-license-payload.sh runs under git-bash; give it a repo-relative,
# forward-slash path rather than an absolute Windows path whose backslashes
# bash would treat as escapes.
$StageRel = "build-windows-msvc-release/$StageName"
& bash tools/check-license-payload.sh $StageRel --qt
Assert-Exit 'license payload check'

# Prove the staged tree is self-contained: run the math self-test with the Qt
# kit removed from PATH, so only the staged DLLs can satisfy it.
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & (Join-Path $Stage 'kvit-notes.exe') --math-selftest
    Assert-Exit 'packaged --math-selftest'
} finally {
    $env:PATH = $savedPath
}

# Bundled-runtime manifest, mirroring packaging/manifests/linux-<version>.txt.
New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot 'packaging\manifests') | Out-Null
Get-ChildItem -Recurse -Path $Stage -Include *.dll, *.exe |
    ForEach-Object { $_.FullName.Substring($Stage.Length + 1) } |
    Sort-Object |
    Set-Content -Encoding ascii (Join-Path $RepoRoot "packaging\manifests\windows-$Version.txt")

# ── Portable zip (a single top-level folder) ──
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$Zip = Join-Path $Dist "$StageName.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip
Write-Host "Portable zip: $Zip"

# ── Inno Setup per-user installer ──
$isccCmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
if ($isccCmd) {
    $iscc = $isccCmd.Source
} else {
    # PATH first, then the machine and per-user install locations Inno's own
    # installer uses (the winget package installs per user under LOCALAPPDATA).
    $iscc = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup (iscc) not found on PATH or at a known install location.' }
}
& $iscc "/DKvitVersion=$Version" "/DStageDir=$Stage" "/DOutputDir=$Dist" (Join-Path $RepoRoot 'packaging\windows\kvit-notes.iss')
Assert-Exit 'Inno Setup'

# ── Checksums ──
$sums = Get-ChildItem -Path $Dist -File |
    Where-Object { $_.Extension -in '.zip', '.exe' } |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name }
Set-Content -Encoding ascii (Join-Path $Dist 'SHA256SUMS-windows.txt') $sums

Write-Host ''
Write-Host "Windows artifacts in $Dist :"
Get-ChildItem -File $Dist | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host "Bundled-runtime manifest: packaging\manifests\windows-$Version.txt"
