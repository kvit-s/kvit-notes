# Qt LGPL-3 obligations checklist (per platform)

Kvit Notes links Qt 6 dynamically under LGPL-3. Packaged builds are a
distribution of the Qt libraries, which triggers the obligations below
(reference: https://www.qt.io/development/open-source-lgpl-obligations).
Every release candidate runs this list per artifact; the items marked
"deploy-time enumeration" are completed the first time each Phase D
packaging pipeline produces a real artifact, then re-verified per release.

## All platforms

- [ ] Qt is linked dynamically. No static Qt anywhere; the user must be able
      to replace the Qt libraries with their own build (relink/replace
      ability is what dynamic linking preserves).
- [ ] The artifact carries the LGPL-3 text and the Qt license notices in a
      `licenses/qt/` folder. The canonical LGPL-3 and GPL-3 texts are
      vendored at `packaging/licenses/qt/` and copied in by the packaging
      script; the Qt kit's own licensing document is added alongside when the
      kit provides one. Do not rely on the kit alone: a Qt 6.10.1 kit ships
      `Licenses/{LICENSE,LICENSE.FDL,COPYING.txt,Copyright.txt}` and no file
      matching `LICENSE.LGPL*`, so the previous glob copied nothing and left
      an empty directory behind.
- [ ] The artifact carries `THIRD-PARTY-NOTICES.md`, the project's own
      MPL-2.0 `LICENSE`, and the MicroTeX and tinyxml2 texts. This is
      enforced on every platform: `tools/check-license-payload.sh` runs inside
      `packaging/linux/build-appimage.sh`, `packaging/windows/build-windows.ps1`
      and `packaging/macos/build-macos.sh`, and fails the build when any
      required notice is missing or empty.
- [ ] The download page / README states where the exact Qt sources for the
      shipped version can be obtained (https://download.qt.io/official_releases/qt/
      plus the version), satisfying the source-availability obligation.
- [ ] Deploy-time enumeration: list every Qt library and plugin the deploy
      tool actually bundled (windeployqt/macdeployqt/linuxdeploy output),
      commit the list under `packaging/manifests/<platform>-<version>.txt`,
      and reconcile it against `packaging/sbom.yaml`.
- [ ] FFmpeg backend: confirm the bundled Qt Multimedia ffmpeg plugin is the
      LGPL build with no GPL-only components enabled (check the plugin's
      configuration report in the Qt binary distribution notes).

## Windows (portable zip + Inno installer)

- [ ] `licenses/qt/` present inside both the zip and the installed tree.
- [ ] windeployqt manifest committed (deploy-time enumeration above).
- [ ] The qsvg imageformats plugin ships (user-inserted .svg image blocks
      load through it even though the app links no QtSvg directly - see the
      note in CMakeLists.txt).

## macOS (DMG)

- [ ] License folder inside the .app bundle resources.
- [ ] macdeployqt manifest committed.
- [ ] Frameworks keep their Qt version numbers and are not stripped in a way
      that prevents replacement.

## Linux (AppImage / Flatpak)

- [ ] AppImage: license folder inside the AppDir; linuxdeploy manifest
      committed.
- [ ] Flatpak: Qt comes from the KDE runtime, so the runtime carries the Qt
      licenses; the app manifest must not bundle a second Qt.

## Resolved flags

- [x] Greek and Cyrillic MicroTeX font packs (GPL-3, no font exception):
      **owner ruled 2026-07-19 to ship them** with the GPL text beside
      them (aggregation position; the app rasterizes math in-process and
      never embeds these fonts into exported documents). The installed
      math-res tree carries res/greek/LICENSE and res/cyrillic/LICENSE;
      per-release verification is the existing "SBOM/notices present"
      distribution check plus this line: confirm both LICENSE files exist
      inside each artifact's math-res.
