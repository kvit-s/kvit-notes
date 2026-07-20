# Release and rollback procedure

Written before the first release candidate. The
governing rule: **a bad release is superseded, never mutated**. Artifacts
users may have installed are never deleted, and a published tag is never
re-tagged or force-moved.

## Release ladder

1. `v1.0.0-rc1` is a published GitHub **prerelease** for friendly testers.
   RC2, RC3, … follow until the release gates pass.
2. Release gates for promoting an RC to stable:
   - zero open P0 (crash, data loss) and P1 (core feature broken) issues
     against the RC;
   - the docs/qa-checklist.md pass (features + distribution) green on
     Windows, macOS, one X11 Linux, one Wayland Linux;
   - the export gates (tools/export-gates.sh) green against the public tree.
3. The stable tag publishes days before any announcement, never the morning
   of. Testers install the same artifact launch visitors get.

Every artifact is built by CI from the tag (provenance = the workflow run),
carries checksums and THIRD-PARTY-NOTICES.md, and is attached to the GitHub
release before it is published.

## Rolling back a bad release

Trigger: a P0/P1 confirmed against the latest published release.

1. **Mark, don't delete.** Edit the GitHub release: set it back to
   prerelease, retitle it "vX.Y.Z (superseded; do not install)", and add a
   note naming the issue and the replacement version. Leave every artifact
   downloadable.
2. **Publish the fix as a new version.** Branch from the previous good tag,
   apply the fix (cherry-pick if it already landed on main), tag
   `vX.Y.(Z+1)`, and let CI produce and publish the artifacts.
3. **Propagate the supersede** to every channel that mirrors releases:
   - Flathub: run `tools/update-flatpak-commit.sh <version>` so the
     manifest pins the tag's commit, not just the mutable tag, then
     push it. `--check` verifies before submitting;
   - winget: submit the updated manifest PR;
   - AUR `-bin`: run `tools/update-aur-digest.sh <version>`, which bumps
     pkgver and pins `sha256sums` to the digest in the release's
     SHA256SUMS.txt. Never publish a PKGBUILD carrying the placeholder
     digest or `SKIP`; `--check` verifies before publishing;
   - Homebrew tap: bump the cask.
4. **The in-app update check follows automatically**: it reads the GitHub
   `latest` release, which is now the fixed version.
5. Post-mortem line in CHANGELOG.md under the fixed version: what broke,
   what gate should have caught it, and the gate change made.

## Version discipline

- SemVer from `v1.0.0`. `PROJECT_VERSION` in CMakeLists.txt carries the
  three numeric components, which is all CMake's `project(VERSION)`
  accepts; the prerelease identifier lives in the tag.
- The binary reports the full tag version, suffix included. The tag job
  derives it and passes it as `KVIT_VERSION_FULL`, so a `v1.0.0-rc1`
  build calls itself `1.0.0-rc1` in About and in the update check, and
  an installed RC is correctly seen as older than stable `1.0.0`.
- A tag whose numeric core does not match `PROJECT_VERSION` fails the
  packaging job before anything is built. Bump `project(VERSION ...)`
  and retag rather than working around it.
- `CHANGELOG.md` (Keep a Changelog) is updated in the PR that makes the
  change, not at release time; release notes are generated from it.
