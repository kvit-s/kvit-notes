# Kvit Notes — project instructions

An Obsidian-style block editor for Markdown notes, Qt 6 / QML, MPL-2.0.
This repository is the whole open product; the premium agent module lives
elsewhere (see the last section).

Read devel.md before debugging anything visual or running a Qt Quick suite.
The points below are the ones that repeatedly cost time.

Where the documentation is: **features.md** specifies behaviour,
**devel.md** covers building and visual verification, **block-arch.md** the
per-kind block design, **selection.md** selection across rendered blocks,
**accessibility.md** what a screen reader, a keyboard and a contrast floor
get, **multi-vault.md** the window/vault composition, **docs/adr/** the eight
decisions that constrain new work, **docs/backlog.md** the known gaps.

A new control drawn as a `Rectangle` with a `MouseArea` on it is invisible to
the accessibility tree and unreachable without a pointer. Use
`qml/IconButton.qml`, or set `Accessible.role` and `Accessible.name` by hand;
`python3 tools/check-accessible-names.py` (the `AccessibleNameGuard` test) is
what catches the omission, along with a glyph label with no name beside it and
an animation `duration:` that does not multiply by `Theme.motionScale`.

A menu entry is a `qml/DiscoverableMenuItem.qml`, never a bare `MenuItem`, and
a menu holding a submenu adds `delegate: DiscoverableMenuItem {}` for the row
Qt builds for that submenu. Fusion draws every menu label with `palette.text`
whatever the entry's state, so either omission leaves a disabled command
looking exactly like a live one. Setting a disabled color on the window
palette instead does not work, for the reason that file gives.

## Where a new file goes

`src/` is seven libraries (`content`, `domain`, `search`, `platform`,
`repository`, `application`, `qml`), and includes may only point downward.
A file in the wrong directory usually announces itself as
`No such file or directory` on an include that looks fine, so read that
error as "wrong direction" before assuming a typo.

Three things the compiler cannot catch are enforced by
`python3 tools/check-layering.py`, which also runs as the `LayeringGuard`
test: the dependency graph, Qt networking outside `src/platform/`, and
filesystem mutation outside the files listed in that script's `MAY_MUTATE`.
Adding a write means adding the file there with a reason.

Types QML needs are registered as `QML_FOREIGN` wrappers in
`src/qml/qmlsingletons.h`, never with a macro on the class itself.
`qmltyperegistrar` reads one target's metatypes, so a macro anywhere else
is silently ignored and QML fails at runtime with
`ReferenceError: <Type> is not defined`.

Rationale: devel.md "Where a new file goes",
docs/adr/0008-module-boundary.md.

## Rendering on this machine (WSLg)

- GPU GL (Mesa's `d3d12` Gallium driver) corrupts Qt Quick TEXT rendering:
  white text renders `#ffff00`, some glyphs lose alpha (the window goes
  see-through), dark text renders as pale outlines. Rectangles and images
  stay correct, so a screenshot can look structurally fine while every
  glyph is wrong.
- The binary pins `GALLIUM_DRIVER=llvmpipe` under WSL in
  `KvitApplication::applyPlatformWorkarounds()`
  (src/qml/kvitapplication.cpp), called before `QApplication` in
  src/main.cpp and tools/uidriver.cpp. Never remove this pin, and never
  advise fixing rendering through `~/.bashrc` alone: env edits do not
  reach already-open terminals, which is exactly how this bug kept coming
  back.
- Escape hatch for experiments: `KVIT_ALLOW_GPU_GL=1`. Verify the active
  renderer with `QSG_INFO=1`: the `qt.rhi.general ... RENDERER:` line must
  say `llvmpipe`.

## Visual verification

- No automated suite can see rendering-driver bugs: every test renders on
  CPU (`QT_QPA_PLATFORM=offscreen` / `QT_QUICK_BACKEND=software`), which is
  always pixel-correct. "All tests green" says nothing about what the GPU
  path draws.
- To observe the real UI, build with `-DKVIT_UI_DRIVER=ON` and use
  `./build/kvit-uidriver --scenario=... --out=...` (tools/uidriver.cpp).
  Point `HOME`/`XDG_CONFIG_HOME` at a scratch directory first; otherwise it
  edits the user's real notes collection.
- For screenshots that will be committed, stage the vault under a neutral
  path such as `/tmp/kvit-demo/vault`. The status bar renders the note's
  absolute path, so a capture from a home or agent scratch directory bakes
  that machine-specific path into the pixels.
- Reference PNGs in tests/screenshots captured before 2026-07-18 carry mild
  glyph corruption from the old GPU path; recapture under llvmpipe before
  using them as pixel baselines.

## Reading a Qt Quick suite result

All the cases in tests/tst_integration.qml share one process and one
window, so the whole-suite pass count is worth much less than it looks.
Three environments give three different answers:

- **`IntegrationTestsIsolated` carries the verdict.** It runs
  `tools/run-integration-gate.sh`, one process per case under the VNC
  platform, in about eight minutes. It blocks a merge.
- **`IntegrationTests`** is the fast local signal (a couple of minutes,
  one process, VNC). Confirm any new failure on its own before calling it
  a defect.
- **`QT_QPA_PLATFORM=offscreen` skips about two thirds of the cases**, so
  a new interaction test silently skips there. Usable as a smoke test
  only.
- **On the developer's display, well over a hundred failures appear
  whatever the tree contains**, because WSLg drops window activation
  part-way through. The failing set is not stable against itself, so the
  count carries no information.

Run the Qt Quick suites serially: `ctest -j4` turned 30 failures into 106
by making `VisualTests` and `IntegrationTests` compete for the display.

A case that passes in the full run and fails alone is not automatically
noise — that pattern has twice been a real defect masked by state an
earlier case left behind. Details in devel.md "Reading a Qt Quick suite
result".

## Windows builds

- Code is edited ONLY in this WSL tree. A directory such as
  `D:\projects\kvit-notes` is a disposable one-way mirror, created once
  with `tools/win-sync.sh --init` and re-synced from WSL before every
  build. Never edit the mirror; the sync propagates deletes.
- `tools/win.sh build|test|deploy|selftest MIRROR` is the one-command flow
  from a WSL shell (or set `KVIT_WIN_ROOT`).
- Never pipe Windows ctest or test-binary output back through the WSL
  interop pipe. It leaves stdout fully buffered, so output vanishes on a
  crash and phantom failures appear. The win-*.bat helpers redirect to
  Windows-side files; diagnose from those.
- Close the running app before rebuilding: a locked `kvit-notes.exe` fails
  its link with LNK1104 while the rest of the build succeeds, leaving a
  stale exe that looks fresh.

Full workflow and the remaining traps: devel.md "Building on Windows".

## Wiring constraints

- `Theme::setSettings()` / `Typography::setSettings()` snapshot values at
  attach time: they must be attached in `AppContext::openSettings()` after
  the store loads, never in `wire()`. Attaching early silently discards the
  persisted theme. Regression test:
  `persistedAppearanceSettingsApplyAtStartup` in tests/test_shell.cpp.
- Do not hand-build the object graph in a test. Construct `AppContext` and
  layer test state on top; a composition that must differ belongs in
  `AppContext::Options`. The hand-built graph in `tests/testsetup.h` once
  drifted far enough that every global-search test ran against an unindexed
  collection and no test noticed.
- `resources.qrc` is the only component list. A QML file missing from it
  breaks the shipped shell and hangs the Qt Quick harness until its CTest
  timeout rather than failing. `QrcSyncGuard` checks this;
  `tests/integration_tests.qrc` holds the `tst_*.qml` suite files alone.
- Reach qmllint through the `qmllint` build target or the `QmlLint` CTest
  entry, not `tools/run-qmllint.sh` directly — both regenerate the type
  description the script needs.

## Two invariants worth knowing before changing behaviour

- **One writer per vault.** `VaultLock`
  (src/repository/vaultlock.{h,cpp}) holds a kernel advisory lock for the
  session; a second process is refused with an explanation. Failure is
  open, not closed: a filesystem without locking opens the vault unlocked
  with a warning rather than refusing it. See
  docs/adr/0005-multi-process-behaviour.md.
- **A note is untrusted input**, so every request made on a note's behalf
  goes through the single egress policy in `src/platform/`. See
  docs/adr/0003-network-egress-policy.md.

## The premium agent module

`src/agent`, `qml/agent`, `tests/agent` and `agent_resources.qrc` are not
in this repository — they live in the private `kvit-notes-pro` tree, along
with the product specification. `CMakeLists.txt` keeps the `KVIT_AGENT`
option and stops at configure time with a message saying so. The open tree
refers to that module in exactly two `#ifdef KVIT_AGENT` fragments in
src/main.cpp; keep it that way. The seam itself, covering block kinds, QML
context objects and the three UI slots, is documented in devel.md
"Extensions are first-party code" and docs/adr/0006-extension-trust.md.
