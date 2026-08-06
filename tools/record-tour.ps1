# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Run one demo-tour segment on Windows, for screen recording.
#
# The driver plays a scripted feature at a pace a viewer can follow, with a
# caption naming it and the real mouse pointer moving to each target, so a
# screen recorder that captures the cursor shows a visible cause for what
# happens. Capture is the Windows 11 Snipping Tool in video mode, or anything
# else that records a screen region.
#
# Why the window position matters: a region recorder captures a rectangle of
# the screen rather than a window, so -X/-Y put the window in the same place
# every run and the region only has to be selected once for the whole set.
#
#   powershell -ExecutionPolicy Bypass -File tools\record-tour.ps1 `
#       -Segment tour-mermaid -Title "Drag a node, the markdown rewrites itself"
#
# Build the driver first with win-uidriver.bat.

param(
    [Parameter(Mandatory = $true)]
    # tour-all plays the five in order in one window, for a single continuous
    # take; the others are recorded separately, which is what makes each one
    # re-shootable and gives the README its short loops.
    [ValidateSet("tour-all", "tour-mermaid", "tour-livepreview", "tour-math",
                 "tour-repair", "tour-query")]
    [string]$Segment,

    [string]$Title = "",
    [int]$Width = 1600,
    [int]$Height = 1000,
    [int]$X = 100,
    [int]$Y = 80,

    # Seconds between "the recording is running" and the first thing that
    # happens on screen. The Snipping Tool counts down about three of its own,
    # and a couple of quiet seconds at the head of a clip is useful anyway.
    [int]$LeadIn = 6,

    # Skip the prompt, for re-running a segment while tuning it.
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "build-windows-msvc-release\Release\kvit-uidriver.exe"
$qt   = if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { "C:\Qt\6.10.1\msvc2022_64" }

if (-not (Test-Path $exe)) {
    throw "No driver at $exe. Build it first with win-uidriver.bat."
}

# A neutral vault path, because the status bar renders the open note's
# absolute path and a published recording should show no username and no
# machine-specific directory. Staged fresh every run, so a segment that edits
# a note (the query one changes a project's front matter) starts from the
# same state as the take before it.
$demo = "C:\kvit-demo\vault"
if (Test-Path $demo) { Remove-Item -Recurse -Force $demo }
New-Item -ItemType Directory -Force -Path $demo | Out-Null
Copy-Item -Recurse -Force (Join-Path $root "screenshots\demo-vault\*") $demo

$env:PATH = "$qt\bin;$env:PATH"

# Frames the driver writes for checking a take go beside the recordings
# rather than into the source tree.
$out = Join-Path $env:USERPROFILE "Videos\kvit-tour"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$runtime = if ($Segment -eq "tour-all") { "about 75 seconds" } else { "15 to 25 seconds" }

Write-Host ""
Write-Host "  runs for: $runtime"
Write-Host "  segment : $Segment"
Write-Host "  caption : $(if ($Title) { $Title } else { '(none)' })"
Write-Host "  window  : ${Width}x${Height} at ${X},${Y}"
Write-Host "  vault   : $demo"
Write-Host "  frames  : $out"
Write-Host ""

if (-not $NoPrompt) {
    Write-Host "Start the recording now: Win+Shift+S, video mode, select the region"
    Write-Host "covering the window, press Start. Then come back here."
    Read-Host "Press Enter once the recording is running"
}

for ($i = $LeadIn; $i -gt 0; $i--) {
    Write-Host -NoNewline "`r  starting in $i... "
    Start-Sleep -Seconds 1
}
Write-Host "`r  running.            "

& $exe "--scenario=$Segment" "--vault=$demo" "--title=$Title" `
       "--size=${Width}x${Height}" "--pos=$X,$Y" "--out=$out"
$code = $LASTEXITCODE

Write-Host ""
if ($code -eq 0) {
    Write-Host "Segment finished. Stop the recording and save it as $Segment.mp4"
    Write-Host "Last frame for checking the take: $out\$Segment-final.png"
} else {
    Write-Host "Driver exited $code. The console output above says which step"
    Write-Host "could not find what it needed; nothing was recorded worth keeping."
}
exit $code
