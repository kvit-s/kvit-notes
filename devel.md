# Development notes

Environment-specific knowledge for building, running, and visually verifying
the editor. The feature and architecture documentation lives in features.md;
this file covers only what a developer needs to run the app and trust what
they see on screen.

## Building and running

```
./build.sh            # configure + build + run the test suite
./build.sh --run      # ...then launch the editor
./build/kvit-notes   # launch directly
```

Optional CMake flags: `-DKVIT_AGENT=ON` (premium agent module),
`-DKVIT_UI_DRIVER=ON` (the scriptable UI driver below).

## Building on Windows: the two-tree workflow

Since 2026-07-19 the Windows port builds and passes the full unit suite
locally. Two trees are involved, with a strict role split:

- **The WSL checkout of this repo is the only place code is edited.**
  Agent sessions, git history, and the Linux build all live here.
- **A directory on an NTFS drive — say `D:\projects\kvit-notes` — is a
  disposable one-way mirror, not a git clone.** It exists only because
  MSVC builds want local NTFS I/O and
  reliable incremental-build timestamps; building in place over the
  `\\wsl$` share was considered and rejected (measured ~1.5x slower on
  bulk reads, with per-open latency and cross-filesystem timestamp
  semantics as the compounding risks). Never edit the mirror; anything
  written there outside the build outputs is overwritten or deleted by
  the next sync.

**Create the mirror once.** The sync is an rsync mirror with delete
propagation, so it will erase anything in the destination that is not in
this tree. `tools/win-sync.sh` therefore has no default destination: name
it on the command line or in `KVIT_WIN_ROOT`, and initialize it once
before any sync will run against it.

```
tools/win-sync.sh --init /mnt/d/projects/kvit-notes
```

`--init` accepts only a new or empty directory, refuses one holding a
`.git` directory, and writes a `.kvit-notes-mirror` marker naming the
checkout it came from. Later syncs require that marker, so a mistyped or
recycled path is refused rather than emptied. The script also refuses a
destination equal to, inside, or containing the source tree, and any path
broad enough to be more than a project directory (`/`, `/mnt/d`,
`/mnt/d/projects`). Every run prints the resolved source and destination
and states that deletions propagate. The `WinSyncGuards` CTest entry
(`tests/test_win_sync.sh`) covers these refusals.

**Sync is automatic after that.** `win-build.bat` calls back into WSL
(`wsl.exe -e bash tools/win-sync.sh "$KVIT_WIN_ROOT"`) before every
configure/build, so a Windows build cannot run stale code from either
entry point. The mirror excludes `.git`, `build*` directories,
`tests/screenshots`, and the Windows-side result logs; it preserves the
WSL tree's bytes, so files keep the LF endings a CI checkout gets.
`KVIT_NO_SYNC=1` skips it when WSL is unavailable.

**Toolchain.** Visual Studio 2022 Community plus Qt 6.10.1
`msvc2022_64` under `C:\Qt` (the `qtmultimedia` module was added with
aqtinstall, so the Qt MaintenanceTool does not know it is there). The
`windows-msvc-release` preset uses the Visual Studio generator and reads
`CMAKE_PREFIX_PATH` from the `QT_ROOT_DIR` environment variable; the
bat helpers below set it, along with the VS-bundled cmake/ctest paths.

**Entry points.** From a WSL shell, `tools/win.sh` is the whole
workflow. Each verb takes the mirror directory after it, or reads
`KVIT_WIN_ROOT`, and refuses a directory without the marker:

```
tools/win.sh build MIRROR      sync + configure if needed + build
tools/win.sh test MIRROR       sync + build + full unit suite (prints verdict)
tools/win.sh deploy MIRROR     windeployqt staging incl. the qoffscreen.dll copy
tools/win.sh selftest MIRROR   deployed --math-selftest probe
tools/win.sh sync MIRROR       mirror only (add --init the first time)
```

Exporting `KVIT_WIN_ROOT=/mnt/d/projects/kvit-notes` in the shell profile
keeps this to one word per command.

From a Windows prompt in the mirror directory, the bat helpers
(checked in at the repo root, mirrored like everything else) do the
same; `win-build.bat` performs the sync itself:

```
win-build.bat            sync + configure + build (also: configure | build | test)
win-test.bat             full unit suite -> win-test-result.txt
win-ctest-one.bat NAME   one suite, verbose -> ctest-one.txt
win-deploy.bat           windeployqt next to the built exe -> windeploy.log
win-selftest.bat         deployed --math-selftest probe -> selftest.log
```

**Traps, each of which has already cost time:**

- Never pipe Windows ctest or test-binary output back into WSL. The
  interop pipe leaves test stdout fully buffered (so it vanishes on any
  crash) and produced phantom test failures; every helper therefore
  redirects to a Windows-side file, and diagnosis reads that file.
- `cmd.exe /c` with backslash-escaped quotes fails in confusing ways
  (sometimes silently). Put anything beyond a trivial command into a bat
  file and invoke that.
- Close the running app before rebuilding. A locked `kvit-notes.exe`
  fails its link step (LNK1104) while the rest of the build continues,
  which leaves a stale exe that looks fresh if only errors are grepped.
- `windeployqt` does not ship `qoffscreen.dll`; copy it into
  `platforms\` beside the exe or `--math-selftest` aborts into a hung
  error-report dialog.
- The MSVC-vs-GCC portability findings from the first port (narrowing
  in braced init, compound literals, `QVariantList({N})`, NTFS ignoring
  read-only directories, the fontless Windows offscreen platform, the
  query-budget allowance) are recorded in the commit history.

## GPU rendering on WSL corrupts text, so the app pins software GL

**Symptom.** Under WSLg, rendering through the GPU (Mesa's `d3d12` Gallium
driver, which routes GL to the Windows GPU via /dev/dxg) corrupts Qt Quick's
glyph rendering: white text renders yellow (`#ffff00`), some text loses its
alpha channel entirely (the desktop shows through the window), dark text
renders as pale outlines. Only text is affected — rectangles, borders and
images render correctly, so the window looks structurally normal but
discolored. Measured on Mesa 25.2.8 with an Intel UHD 770; the discrete
NVIDIA adapter rendered correctly in frame grabs, but GPU GL under WSLg is
not trusted for this app. Mesa's software rasterizer (`llvmpipe`) is
pixel-correct, and the performance-plan numbers
were achieved on it.

**What the app does.** `KvitApplication::applyPlatformWorkarounds()`
(src/kvitapplication.cpp) runs before `QApplication` is constructed (the
platform plugin initializes EGL immediately, so this is the last moment the
driver choice can be influenced) and, when /proc/version says the kernel is
WSL, sets `GALLIUM_DRIVER=llvmpipe`, overriding anything inherited from the
shell. Both entry points call it (src/main.cpp, tools/uidriver.cpp). It is a
no-op outside WSL.

**Escape hatches.**

- `KVIT_ALLOW_GPU_GL=1` — skip the pin and use whatever the environment says.
- An explicit `QT_QUICK_BACKEND` or `LIBGL_ALWAYS_SOFTWARE` is respected.

**Why the pin lives in the binary.** Environment-file fixes recurred: a
`~/.bashrc` edit does not reach terminals that are already open, so a stale
`GALLIUM_DRIVER=d3d12` export kept reaching the app through old shells. The
in-binary pin makes the launch path irrelevant.

**Verifying the active renderer.** Launch with `QSG_INFO=1` and read the
`qt.rhi.general ... RENDERER:` line. Correct: `llvmpipe (...)`. Broken:
`D3D12 (Intel(R) UHD Graphics 770)`.

## Why the test suite cannot catch rendering corruption

Every automated suite renders on the CPU: the C++ and QML tests run with
`QT_QPA_PLATFORM=offscreen` or `QT_QUICK_BACKEND=software`
(tests/CMakeLists.txt, tools/run-integration-gate.sh). The CPU rasterizer is
pixel-correct, so a bug that lives in a GPU driver's shader path is invisible
to all of them. The theme test asserts the token values themselves and never
samples a rendered pixel. Screen-truth can only come from a real frame grab
of the running shell:

```
cmake -DKVIT_UI_DRIVER=ON . && make kvit-uidriver
./build/kvit-uidriver --scenario=dropcap --out=/tmp/shots
```

The UI driver (tools/uidriver.cpp) composes the app exactly as main() does,
drives the live window with real input, and saves frames with
`QQuickWindow::grabWindow()`. Note its limits: it runs against the real
settings and notes collection unless you point `HOME`/`XDG_CONFIG_HOME` at a
scratch directory, and a grab captures what Qt renders — corruption
introduced later in the display path would not appear in it (compare against
the screen by eye when it matters).

## Tainted reference screenshots

The reference PNGs in tests/screenshots captured before 2026-07-18 were
grabbed through the corrupted d3d12 path and carry a mild form of the glyph
bug (pure-`#000000` glyphs with yellow/cyan fringes where the light theme's
text color is `#1a1a1a`; yellow-tinted headings in the dark set). Recapture
under llvmpipe before using any of them as a pixel baseline.

## Settings wiring order in AppContext

`Theme::setSettings()` and `Typography::setSettings()` snapshot the store's
values at attach time. They are therefore attached inside
`AppContext::openSettings()`, after the file is loaded, rather than in
`wire()`, which runs during construction while the store is still empty. Attaching
them early silently discards the persisted theme and typography (the app
starts light regardless of the saved theme). The regression test is
`persistedAppearanceSettingsApplyAtStartup` in tests/test_shell.cpp.

## One composition root, in production and in tests

`AppContext` constructs and wires every long-lived object the editor runs on,
and publishes them to QML as context properties. Both Qt Quick test binaries
(`test_integration`, `test_visual`) compose that same class through
`tests/testsetup.h`, and `test_shell` composes it and loads the shipped
`qml/main.qml` on top.

They did not always. `testsetup.h` used to rebuild the graph by hand, and had
drifted from the real one: `startupController` was never published,
`CollectionSearchIndex` was never constructed — so every global-search test
ran against an unindexed collection — and three of the four `FileWatcher`
connections were missing, which left the own-write guard inactive throughout
the Qt Quick suites. None of that was visible as a failure; the tests passed
against a graph the application never runs.

So: **do not hand-build the object graph in a test.** Construct `AppContext`,
and layer test-specific state on top of it. If a test needs the composition to
behave differently, that difference belongs in `AppContext::Options`, which is
deliberately tiny — every field in it is a place where the two compositions
diverge, and so a place a defect can hide. Today it holds two flags, both for
things that reach outside the process (the tray asks the desktop session for a
status-notifier item; PerfLog writes to a path from settings). To substitute a
service rather than a flag, add a narrow interface and a setter in the shape of
`EmbedFetcher`/`setEmbedFetcher`.

Three checks keep the wiring honest, and all three block a merge:

- **ShellTests** loads the shipped `resources.qrc` against the real context and
  fails on any QML warning emitted during load. QML reports an unknown context
  property or an unresolvable type as a warning and then carries on with an
  undefined value, so without this the load "succeeds" no matter how much of
  the shell failed to wire up. It also pins the published context-property
  names, so a rename has to be made deliberately in both C++ and QML.
- **QrcSyncGuard** (`tools/check-qrc-sync.py`) compares `resources.qrc`,
  `tests/integration_tests.qrc` and the files actually in `qml/`. A file added
  to only one list either breaks the shipped shell or hangs the Qt Quick
  harness until its CTest timeout.
- **qmllint** reads every file in `qml/`, including the ones no test
  instantiates. This matters because the runtime gate only sees what the
  initial scene actually builds: a bad binding inside an inactive `Loader`
  never evaluates, and a missing *sub-property* of an object that does exist
  (`noteCollection.somethingGone`) evaluates to `undefined` silently, with no
  warning at all. Static analysis is what covers those.

## Making the filesystem fail in a test

`tests/faultinjection.h` holds the RAII guards for forcing I/O failure:
`DeniedWrites` (a directory that rejects new files), `DeniedFileWrites` (the
Windows-compatible form, denying one existing file) and `FileSizeLimit`
(`RLIMIT_FSIZE`, so a write fails partway — the shape a full disk produces).

Use them rather than manipulating permissions inline. Each restores what it
changed in its destructor, so a failed assertion cannot leak a read-only
directory into whatever runs next, and each reports `supported()` — a denial
that root or NTFS ignores must become a `QSKIP`, because the alternative is a
test that passes without having tested anything.

Several shipped defects were invisible to the suite purely for want of a way to
make I/O fail: a save that ignored a short write, a capture that dropped the
user's text when the vault was read-only, an import that counted a truncated
copy as success.
