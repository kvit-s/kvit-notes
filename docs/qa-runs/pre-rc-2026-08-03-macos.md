# QA run: macOS, pre-release-candidate

| Field | Value |
|---|---|
| Platform | macOS, Apple Silicon |
| Artifact | arm64 DMG from a `workflow_dispatch` packaging run |
| Version / commit | untagged; two artifacts, before and after `b5b2381` |
| Checksum | not published (no tag) |
| Machine | Mac with a 4K display |
| OS version | recorded as current at the time of the run |
| Date | 2026-08-02 to 2026-08-03 |
| Runner | owner |
| Result | pass with deviations; two fixes are not yet re-checked on the Mac |

Two sessions. The first got as far as Gatekeeper and stopped there; the
second ran the app from a notarized DMG and found two display defects that
only appear on macOS hardware.

## Deviations

1. **Gatekeeper refused the first DMG.** It was ad-hoc signed rather than
   notarized: manual packaging runs never entered the `release`
   environment, and the workflow read different secret names than the ones
   configured. Fixed in `b5b2381`, which wires signing for both release
   tags and manual runs from `main` and fails the job rather than uploading
   an unsigned artifact. A Developer ID-signed, notarized and stapled arm64
   DMG was accepted by Gatekeeper the same night.
2. **The menus drew inside the window, Windows-style,** instead of in the
   system menu bar where a Mac application's menus belong. Fixed in
   `81cf4f4`, which puts File and View in the system menu bar and places
   Settings and the quit command in the application menu.
3. **Equations rendered at the wrong size on a 4K display.** Fixed in
   `3eb7561`, with `0dddb8d` following on 2026-08-04 for the inline
   baseline.

Findings 2 and 3 were implemented and verified on Linux, across scale
factors for the math case. **Neither has been re-checked on the Mac.**

## Not exercised

- The macOS watch-list items: inline math spacing in its five positions,
  and created-time ordering under APFS (the checklist carries `cp -p` as
  the reproduction).
- The standing features pass, Cmd shortcuts, and Retina rendering.
- VoiceOver.
- The distribution section beyond the Gatekeeper first-open: `spctl -a`,
  install and uninstall, the offline pass, hostile paths and usernames.

## Note on architecture

The DMG is arm64 only. `packaging/macos/build-macos.sh` defaults to the
host slice and the packaging job runs on an Apple Silicon runner, so an
Intel Mac is not served by this artifact. A universal binary is available
through `KVIT_MACOS_ARCHS="arm64;x86_64"` and the Qt kit supports it; which
one ships is an open decision.
