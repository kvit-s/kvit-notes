# 0007. Which platforms a release supports

**Status:** Accepted

## Context

Kvit Notes builds on Linux, Windows and macOS. Building on a platform and
shipping a release for it are different commitments, and the project had been
describing the first as though it were the second: the README advertised a
Windows installer and portable zip, a macOS DMG and a Homebrew tap, none of
which existed in the repository. There was no installer script, no macOS bundle
or DMG tooling, and no notarization configuration.

The gap is not only about effort. Publishing a desktop application on Windows or
macOS means code signing and, on macOS, notarization, both of which require
credentials held by an identified organization. Those credentials cannot be
provisioned or tested from this repository, so the packaging jobs that would use
them cannot be written and verified here in the way the Linux job can.

Meanwhile a download table that lists artifacts a tag does not produce is worse
than a short one, because a reader who follows it finds nothing and cannot tell
whether the release failed or the claim was never true.

## Decision

A release publishes what its automation actually produces, and the documentation
lists exactly that.

**Published.** Linux x86_64 only, from the single `package` job in
`.github/workflows/ci.yml`, which runs on `ubuntu-24.04` and only for tags
matching `refs/tags/v`. It produces an AppImage, `SHA256SUMS.txt`, a
`PKGBUILD.aur` with the digest pinned to that run's artifact, and
`THIRD-PARTY-NOTICES.md`, attached to a **draft** release that a human
publishes.

**Published.** Windows x86_64 and macOS. `package-windows` runs on
`windows-2022` and produces the `windeployqt` portable zip and a per-user Inno
Setup installer (`packaging/windows/build-windows.ps1`,
`packaging/windows/kvit-notes.iss`); `package-macos` runs on `macos-14` and
produces a `macdeployqt` bundle inside a compressed DMG
(`packaging/macos/build-macos.sh`). Both attach to the same draft release as the
Linux artifacts on a tag. The Windows artifacts carry no Authenticode
signature, so SmartScreen warns. The macOS job imports its Developer ID
Application certificate from the `release` environment, signs the nested code
with hardened runtime, applies secure timestamps to the app and DMG, notarizes
through an App Store Connect API key, staples the ticket, and blocks publication
unless Gatekeeper accepts it. Manual macOS packaging from main follows the same
signed path and uploads the result as a run artifact.

The `build-test` matrix still builds `windows-msvc-release` and
`macos-release` and runs the blocking `unit` and `shell` gates on all three
platforms on every commit, so what is packaged is what passed.

**Partly automated.** The AUR package `kvit-notes-bin`. CI pins the checksums
and emits the PKGBUILD; pushing to the AUR is a manual step in
`docs/release-rollback.md`. The committed PKGBUILD carries a deliberately
invalid placeholder digest so an unpinned copy fails rather than installing
unverified bytes.

**Manual.** The Flatpak. `packaging/flatpak/org.kvit.Notes.yaml` is submitted by
hand and pins both tag and commit, with a placeholder commit that is not a real
object so an unpinned manifest fails to fetch. No CI job builds it.

**Absent.** Windows Authenticode signing. The macOS Developer ID and
notarization credentials are installed only in the `release`
environment; pull requests and arbitrary manual refs cannot access them.

Supported toolchain: Qt 6.10 or newer, with CI pinned to 6.10.1; CMake 3.21 or
newer; C++20 with GCC 12 or newer, MSVC 2022, or a recent AppleClang.

## Consequences

The download table is true: it lists all three platforms, because a tag now
produces artifacts for all three, and it states plainly that Windows remains
unsigned while macOS is signed and notarized. The macOS packaging job cannot
silently regress to an ad-hoc artifact: absent credentials, a signing failure,
a rejected notarization, an unstapled ticket or a failed Gatekeeper assessment
fails the job.

One loose end is worth recording rather than leaving to be rediscovered:

- `docs/release-rollback.md` still lists winget and a Homebrew tap as
  propagation targets, and neither has a manifest in the tree. Either that
  procedure is aspirational in the same way the README's claims were before they
  were corrected, or the manifests live somewhere outside this repository. This
  has not been determined.

The CMake floor agrees everywhere it is stated: `cmake_minimum_required` in
`CMakeLists.txt`, the presets, and the toolchain line above all say 3.21.

## Evidence in the tree

- `.github/workflows/ci.yml`: the `build-test` matrix, the three tag-gated packaging jobs, and signed manual macOS packaging from main
- `packaging/windows/build-windows.ps1`, `packaging/windows/kvit-notes.iss`: the portable zip and the per-user installer
- `packaging/macos/build-macos.sh`: the bundle, DMG, Developer ID signing, notarization, stapling and Gatekeeper verification
- `packaging/linux/build-appimage.sh`: the AppImage build, with linuxdeploy,
  appimagetool and the static type-2 runtime independently pinned by SHA-256
- `tools/check-appimage.sh`: runs the packed artifact and probes math resources, QML imports, SQLite FTS5 and plugins before publication
- `packaging/aur/kvit-notes-bin/PKGBUILD`, `tools/update-aur-digest.sh`
- `packaging/flatpak/org.kvit.Notes.yaml`, `tools/update-flatpak-commit.sh`
- `docs/release-rollback.md`: the manual publish and propagate steps
- `README.md`, "Download": the user-facing statement of the same scope
- Commit `768266b` "Run the artifacts we ship, and stop advertising what we do not build"
