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

**Published, unsigned.** Windows x86_64 and macOS. `package-windows` runs on
`windows-2022` and produces the `windeployqt` portable zip and a per-user Inno
Setup installer (`packaging/windows/build-windows.ps1`,
`packaging/windows/kvit-notes.iss`); `package-macos` runs on `macos-14` and
produces a `macdeployqt` bundle inside a compressed DMG
(`packaging/macos/build-macos.sh`). Both are tag-gated, both take their own
approval in the protected `release` environment, and both attach to the same
draft release as the Linux artifacts. Neither is signed: the Windows artifacts
carry no Authenticode signature, and the DMG is ad-hoc-signed and not
notarized, so SmartScreen and Gatekeeper both warn. The signing and
notarization steps read their credentials from the release environment and
take the unsigned path while those secrets are empty, which is the state
today.

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

**Absent.** Every form of signing and notarization. All three packaging jobs
declare the protected `release` environment, but no step in any of them
consumes a signing credential, because there is none to consume: those
credentials require an identified organization and cannot be provisioned or
tested from this repository.

Supported toolchain: Qt 6.10 or newer, with CI pinned to 6.10.1; CMake 3.21 or
newer; C++20 with GCC 12 or newer, MSVC 2022, or a recent AppleClang.

## Consequences

The download table is true: it lists all three platforms, because a tag now
produces artifacts for all three, and it states plainly that two of them are
unsigned. A reader who downloads the Windows installer or the macOS DMG meets
an operating-system warning, and the README tells them so before they do.

The cost is that an unsigned download is a worse first impression than no
download at all for some readers, and that the packaging jobs are exercised
end to end without ever exercising the signing path they will eventually take.
That path is written and inert rather than absent, so enabling it is a matter
of adding credentials to the release environment.

One loose end is worth recording rather than leaving to be rediscovered:

- `docs/release-rollback.md` still lists winget and a Homebrew tap as
  propagation targets, and neither has a manifest in the tree. Either that
  procedure is aspirational in the same way the README's claims were before they
  were corrected, or the manifests live somewhere outside this repository. This
  has not been determined.

The CMake floor agrees everywhere it is stated: `cmake_minimum_required` in
`CMakeLists.txt`, the presets, and the toolchain line above all say 3.21.

## Evidence in the tree

- `.github/workflows/ci.yml`: the `build-test` matrix and the three tag-gated packaging jobs (`package`, `package-windows`, `package-macos`)
- `packaging/windows/build-windows.ps1`, `packaging/windows/kvit-notes.iss`: the portable zip and the per-user installer
- `packaging/macos/build-macos.sh`: the bundle, the DMG, and the signing and notarization steps that stay inert while their credentials are empty
- `packaging/linux/build-appimage.sh`: the AppImage build, with linuxdeploy,
  appimagetool and the static type-2 runtime independently pinned by SHA-256
- `tools/check-appimage.sh`: runs the packed artifact and probes math resources, QML imports, SQLite FTS5 and plugins before publication
- `packaging/aur/kvit-notes-bin/PKGBUILD`, `tools/update-aur-digest.sh`
- `packaging/flatpak/org.kvit.Notes.yaml`, `tools/update-flatpak-commit.sh`
- `docs/release-rollback.md`: the manual publish and propagate steps
- `README.md`, "Download": the user-facing statement of the same scope
- Commit `768266b` "Run the artifacts we ship, and stop advertising what we do not build"
