# Packaging Kvit Notes

This folder turns Kvit Notes into the files users download: a Windows
installer and portable zip, a macOS app and disk image, and a Linux tar.gz,
AppImage, AUR package and Flatpak manifest. The artifact
names, the Windows installer's identity, the macOS bundle identifier and the
Linux launcher stay stable across releases, so each package upgrades the one
before it as the same product.

Everything except signing the macOS app is built on the Linux (WSL)
development machine. The Windows installer is compiled by Inno Setup, a
Windows program, which WSL starts directly.

## Building

```sh
packaging/build-all.sh           # every artifact into dist/
packaging/build-all.sh --test    # the same, then try them on this machine
packaging/build-all.sh linux     # one platform: windows, macos, linux or flatpak
```

`build-all.sh` empties `dist/`, runs the platform scripts below, and writes
`dist/SHA256SUMS.txt` over everything it made. Each platform script can also
be run on its own; it writes into `dist/` and stages its files under
`build/packaging/`.

| Script | Makes in `dist/` |
|---|---|
| `windows/build-windows.sh` | `Kvit_Notes-<version>-windows-x64.zip`, `Kvit_Notes-<version>-setup.exe`, `SHA256SUMS-windows.txt` |
| `macos/build-macos.sh` | on Linux: `Kvit_Notes-<version>-macos-universal.zip` (the app, unsigned); on a Mac: `Kvit_Notes-<version>-macos-universal.dmg` (signed); `SHA256SUMS-macos.txt` |
| `linux/build-linux.sh` | `Kvit_Notes-<version>-linux-x86_64.tar.gz`, `Kvit_Notes-<version>-x86_64.AppImage`, `aur/PKGBUILD`, `SHA256SUMS-linux.txt` |
| `flatpak/make-sources.sh` | `Kvit_Notes-<version>-flatpak-sources.tar.gz`, `flatpak/org.kvit.Notes.yaml` (run after `build-linux.sh`) |
| `windows/test-windows.sh` | nothing; tries the zip and a test installer on Windows |
| `linux/test-linux.sh` | nothing; runs the AppImage, the tar.gz and the AUR package's install step |

## The version

Every artifact, and the version the program reports, comes from one release
version, chosen by these rules:

1. `KVIT_VERSION_FULL`, when it is set (`KVIT_VERSION_FULL=2.0.0-rc1 packaging/build-all.sh`);
2. otherwise the tag being built: `GITHUB_REF_NAME` in a GitHub tag job, or a
   `v<version>` tag on the checked-out commit;
3. otherwise the base version, for an untagged local build.

The base version is `baseVersion` in `cmd/kvit-notes/version.go` (now
`2.0.0`). The release version must be SemVer and start with the base
version, so a tag `v2.0.0-rc1` builds only from source whose base version is
2.0.0. The scripts pass it to the program with
`go build -ldflags "-X main.version=<version>"`; a build without that flag
(`./build.sh`, `go build`) reports `2.0.0-dev+<commit>`. Where a platform
accepts only numbers (the Windows file version, the macOS bundle version) the
three numbers of the base version are used.

## What each package contains

Every package holds the program, the math library, the math library's
resource folder `math-res` (a copy of `third_party/microtex/res`: MicroTeX's
XML files and the TeX fonts with their licences), and a licences folder. The
Go package `mathtex` looks for the library and `math-res` in exactly these
places relative to the program:

| Package | Program | Math library | math-res | Licences |
|---|---|---|---|---|
| Windows zip and installation | `kvit-notes.exe` | `kvitmath.dll` beside it | `math-res\` beside it | `licenses\` |
| macOS `kvit-notes.app` | `Contents/MacOS/kvit-notes` | `Contents/Frameworks/libkvitmath.dylib` | `Contents/Resources/math-res` | `Contents/Resources/licenses` |
| Linux tar.gz, AppImage (`usr/`), AUR (`/usr`), Flatpak (`/app`) | `bin/kvit-notes` | `lib/kvit-notes/libkvitmath.so` | `share/kvit-notes/math-res` | `share/licenses/kvit-notes` |

The Linux packages also have the launcher `share/applications/kvit-notes.desktop`
(it offers to open `text/markdown` files), the icon in
`share/icons/hicolor`, and the AppStream description
`share/metainfo/org.kvit.Notes.metainfo.xml`.

`packaging/manifests/<platform>.txt` lists every file of each package, and
the build fails when a staged package has a file the list lacks or lacks one
it has. After an intended change, rewrite the list and review the difference:

```sh
KVIT_UPDATE_MANIFESTS=1 packaging/linux/build-linux.sh && git diff packaging/manifests
```

### Windows

- **The executable's resources.** `windows/make-syso.sh` writes
  `cmd/kvit-notes/rsrc_windows_amd64.syso`, which the Go linker puts into
  every windows/amd64 build of the program: the icon (`icons/kvit.ico`), the
  version information shown in Explorer's Properties, and the application
  manifest `windows/kvit-notes.manifest` (per-monitor DPI awareness, the
  UTF-8 code page, Common Controls 6, long paths, no administrator rights;
  the file says what each entry does). The tool is go-winres, run with
  `go run github.com/tc-hib/go-winres@v0.3.3`, so nothing is installed and
  `go.mod` is not changed. unison's own packaging tool, upack, was not used:
  it builds only on Windows and its manifest is fixed, without the UTF-8
  code page and Common Controls 6. The file in the repository holds the base
  version; a pre-release build writes its own version into it and puts the
  base version back afterwards. The release build is a Windows GUI program
  (`-H windowsgui`), so no console window opens with it.
- **The installer** (`windows/kvit-notes.iss`) installs per user into
  `%LOCALAPPDATA%\Programs\Kvit Notes` without asking for administrator
  rights, adds a Start-menu group, optionally a desktop shortcut, and
  optionally registers `.md` files (the ProgID `KvitNotes.md` under
  `HKCU\Software\Classes`, and an `OpenWithProgids` entry for `.md`). Its
  AppId `{7B3D2E1A-9C64-4F58-A2D7-0E5F1B8C6A34}`, product name and folder are
  those of every Kvit Notes installer, so on a machine with Kvit Notes it
  upgrades that installation in place: it installs into the same folder,
  deletes the files of an earlier version built with Qt that this version
  does not use (Qt and FFmpeg libraries, the Visual C++ runtime, Qt's plugin
  and QML folders, Qt's licence texts), and extends the same uninstaller.
- **The portable zip** has the same files under one top folder,
  `Kvit_Notes-<version>-windows-x64`.

### macOS

`macos/build-macos.sh` builds the program for arm64 and x86_64 and joins the
two with `lipo` into one universal executable; the math library is joined the
same way. `Info.plist` has the bundle identifier `org.kvit.Notes`,
name and icon, declares Markdown documents (`net.daringfireball.markdown`,
`.md` and `.markdown`, as an alternative editor rather than the default one),
is high-resolution capable, and requires macOS 13 Ventura, the oldest version
Go 1.27 supports (Go writes 13.0 into every executable it links; the math
library asks for 12.0). The script checks that both architectures are in
each file, that no architecture needs a later macOS than the plist says, and
that the arm64 code carries the ad-hoc signature Go's linker and zig give it,
without which Apple silicon refuses to run it.

On Linux the script stops there and zips the app. On a Mac, the same script
signs it (ad hoc, or with `KVIT_CODESIGN_IDENTITY` and the hardened runtime),
checks the signature, runs `kvit-notes --help` in both architectures, makes
the disk image with `hdiutil`, notarises it when credentials are given, and
staples the ticket. `KVIT_REQUIRE_SIGNING=1` makes a missing identity or
notarisation an error. The script's header lists the environment variables
it reads. To sign the app built on Linux:

```sh
packaging/macos/build-macos.sh --from-zip Kvit_Notes-<version>-macos-universal.zip
```

### Linux

- **tar.gz**: the tree above under `Kvit_Notes-<version>-linux-x86_64/`. It
  runs from wherever it is unpacked. The program needs `libX11` and `libGL`
  from the system and nothing else; the math library needs glibc 2.28 or
  later.
- **AppImage**: the same tree as `usr/` of an AppDir whose `AppRun` is a
  link to the program, packed by appimagetool with a separately pinned
  type-2 runtime (both downloaded into `packaging/.tools` and checked by
  SHA-256). It also holds the
  runtime's licence texts. The build unpacks the finished AppImage and checks
  its contents against the manifest.
- **AUR** (`aur/kvit-notes-bin/PKGBUILD`): installs the release tar.gz into
  `/usr`. The file in the repository has a placeholder version and digest;
  `build-linux.sh` writes `dist/aur/PKGBUILD` with the version and the
  tar.gz's SHA-256, which is the copy to publish.
- **Flatpak** (`flatpak/org.kvit.Notes.yaml`, `flatpak/org.kvit.Notes.metainfo.xml`):
  built from source on the freedesktop 25.08 runtime with the golang SDK
  extension, and zig 0.16.0 downloaded by digest for the math library.
  Flathub builds without network access, so `flatpak/make-sources.sh` makes
  the release asset `Kvit_Notes-<version>-flatpak-sources.tar.gz` with the
  output of `go mod vendor` and the licence folder, and writes
  `dist/flatpak/org.kvit.Notes.yaml` with that archive's digest; the git
  commit of the tag is filled in when the tag exists. The golang extension
  must provide Go 1.27 or later; if it does not yet, the manifest has a
  commented source for the official Go archive to use instead.

## Licences

`sbom.yaml` lists every third-party component the packages ship: the Go
standard library, every Go module compiled into the program, the fonts
embedded in it (Roboto and DejaVu Sans Mono from unison, the Phosphor icon
font from kvit-ui), the math library's parts (MicroTeX, tinyxml2, and the
LLVM C++ runtime, zig's compiler runtime and, on Windows, the mingw-w64
start-up code that zig links into it), the math fonts, the AppImage runtime,
and the application icon. `THIRD-PARTY-NOTICES.md` is generated from it:

```sh
packaging/sbom.py notices          # rewrite THIRD-PARTY-NOTICES.md
packaging/sbom.py notices --check  # fail when it is out of date
```

Every package build checks the executable against `sbom.yaml` with
`sbom.py check`: the Go toolchain and every module in `go version -m` must
have an entry with the same version, and every file embedded with
`//go:embed` in a package the program is built from must match an entry's
`embedded` pattern. A new dependency, a version change or a new embedded file
therefore stops the packaging until `sbom.yaml` describes it. Then
`sbom.py licenses` copies into the package's licences folder the project's
`LICENSE`, the notices, and the licence text of every component in that
package: from the Go module cache for Go modules (`go/<module path>/`), from
the module for embedded fonts (`fonts/`), and from `packaging/licenses/` for
the zig runtimes (`math-runtime/`, copied from zig 0.16.0's `lib/`) and the
AppImage runtime (`appimage-runtime/`).

## Tools

| Tool | Used for | Where it comes from |
|---|---|---|
| Go 1.27.1 | the program | the `toolchain` line of `go.mod`; Go fetches it into the module cache |
| zig 0.16.0 | the math library (`tools/build-mathlib.sh`) | https://ziglang.org/download; here `~/.local/share/zig/0.16.0`, linked from `~/.local/bin/zig` |
| Python 3 with PyYAML | `sbom.py` | the system (`python3-yaml`) |
| go-winres v0.3.3 | Windows resources | `go run`, from the Go module proxy |
| Inno Setup 6 | the Windows installer | installed per user, without administrator rights, in `C:\Users\sk\AppData\Local\Programs\Inno Setup 6` (version 6.7.3); `windows/wsl.sh` also looks on the PATH and in Program Files, and `KVIT_ISCC` names another `ISCC.exe`. To install it elsewhere: https://jrsoftware.org/isdl.php or `winget install JRSoftware.InnoSetup` |
| llvm-readobj-18, llvm-lipo-18, llvm-otool-18 | checking the Windows resources; joining and checking the macOS files | the `llvm-18` package; on a Mac, Xcode's `lipo` and `otool` |
| appimagetool and the type-2 runtime | the AppImage | downloaded by `build-linux.sh` into `packaging/.tools`, checked by SHA-256 |
| zip, tar, gzip, appstreamcli (optional) | archives; checking the AppStream file | the system |
| Xcode command line tools | signing, disk image, notarisation | a Mac |
| flatpak-builder | building the Flatpak | not installed here; Flathub builds it from the manifest |

## What runs where

On this WSL machine: every build step except the ones below, the Windows
installer (Inno Setup runs on Windows, started from WSL), and both test
scripts. `test-windows.sh` never runs the real installer, because this
machine has Kvit Notes installed under the same AppId and the
installer would replace it; it builds a test installer from the same script
with its own AppId (`{5E0C6B7A-3D1F-4C2B-9A8E-7F60D1B2C3A4}`), name ("Kvit
Notes Packaging Test") and ProgID, installs it silently into
`%TEMP%\kvit-notes-packaging-test`, checks the files, the removal of
stand-ins for a version built with Qt placed there first, the Start-menu shortcut and the association keys,
starts the program, uninstalls, checks that everything it wrote is gone and
that the real product's keys did not change. `test-linux.sh` runs the
AppImage and the unpacked tar.gz with a scratch vault and scratch settings
folders, runs the math self-test in each package's own layout, and runs the
AUR package's install step into a scratch folder.

On a Mac only: signing, `hdiutil`, notarisation and stapling, and running the
macOS program. No Mac was available when these scripts were written, so that
part of `macos/build-macos.sh` has not been run.

## Releasing

1. Set `baseVersion` in `cmd/kvit-notes/version.go`, and add the release to
   `flatpak/org.kvit.Notes.metainfo.xml` (the Linux packages add an entry
   for their version when the file has none, but the Flatpak uses the file as
   it is). Commit and tag `v<version>`.
2. `KVIT_REQUIRE_MATH=1 packaging/build-all.sh --test` on this machine.
3. On a Mac with the signing identity and notarisation credentials:
   `KVIT_REQUIRE_SIGNING=1 packaging/macos/build-macos.sh` (or `--from-zip`
   with the zip from step 2).
4. Publish `dist/` with the disk image; publish `dist/aur/PKGBUILD` to the
   AUR and submit `dist/flatpak/org.kvit.Notes.yaml` to Flathub.

## Differences from the packages of the versions built with Qt

Kvit Notes 1.0.0 was built with Qt. The Go program is one executable plus
the math library, so the deployment tools those packages needed
(windeployqt, macdeployqt, linuxdeploy and its Qt plugin), the Visual C++
runtime, the LGPL checklist for Qt and the KDE Flatpak runtime have no
counterpart here. The per-file manifests list every file of each package
rather than only its libraries, and the Linux release has a tar.gz, which
the AUR package installs instead of unpacking the AppImage. The macOS app
is universal rather than one architecture per build.
