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

**Built and tested but not published.** Windows and macOS. The `build-test`
matrix builds `windows-msvc-release` on `windows-2022` and `macos-release` on
`macos-14`, and both run the blocking `unit` and `shell` gates on every commit.
Neither has any packaging step.

**Partly automated.** The AUR package `kvit-notes-bin`. CI pins the checksums
and emits the PKGBUILD; pushing to the AUR is a manual step in
`docs/release-rollback.md`. The committed PKGBUILD carries a deliberately
invalid placeholder digest so an unpinned copy fails rather than installing
unverified bytes.

**Manual.** The Flatpak. `packaging/flatpak/org.kvit.Notes.yaml` is submitted by
hand and pins both tag and commit, with a placeholder commit that is not a real
object so an unpinned manifest fails to fetch. No CI job builds it.

**Absent.** Installers of any kind for Windows, DMG or bundle tooling for macOS,
a published portable zip, and every form of signing and notarization. The
`package` job declares the protected `release` environment, but no step in it
consumes a signing credential, because there is none to consume.

Supported toolchain: Qt 6.10 or newer, with CI pinned to 6.10.1; CMake 3.21 or
newer; C++20 with GCC 12 or newer, MSVC 2022, or a recent AppleClang.

## Consequences

The download table is short and true. A reader learns from the README that
Windows and macOS builds compile and pass the suite but are not published, and
that building from source works on all three platforms today.

The cost is that the compiling-and-tested state of the Windows and macOS ports
is easy to mistake for neglect. It is deliberate: those ports are kept green so
that adding a packaging job later is a packaging problem rather than a porting
problem. The standing TODO in `ci.yml` names the shape of that future work,
being a windeployqt zip with an Inno installer on Windows and a macdeployqt
notarized DMG on macOS, both in the same protected environment.

Two loose ends are worth recording rather than leaving to be rediscovered:

- `docs/release-rollback.md` still lists winget and a Homebrew tap as
  propagation targets, and neither has a manifest in the tree. Either that
  procedure is aspirational in the same way the README's claims were before they
  were corrected, or the manifests live somewhere outside this repository. This
  has not been determined.
- `CMakeLists.txt` requires CMake 3.16 while the presets and the documentation
  require 3.21. In practice the presets gate it, so a CMake between 3.16 and
  3.20 fails at `--preset` rather than at configure. The numbers should agree.

## Evidence in the tree

- `.github/workflows/ci.yml`: the `build-test` matrix, the tag-gated `package` job, the standing packaging TODO
- `packaging/linux/build-appimage.sh`: the AppImage build, with linuxdeploy pinned by SHA-256
- `tools/check-appimage.sh`: runs the packed artifact and probes math resources, QML imports, SQLite FTS5 and plugins before publication
- `packaging/aur/kvit-notes-bin/PKGBUILD`, `tools/update-aur-digest.sh`
- `packaging/flatpak/org.kvit.Notes.yaml`, `tools/update-flatpak-commit.sh`
- `docs/release-rollback.md`: the manual publish and propagate steps
- `README.md`, "Download": the user-facing statement of the same scope
- Commit `768266b` "Run the artifacts we ship, and stop advertising what we do not build"
