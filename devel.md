# Development notes

Environment-specific knowledge for building, running, and visually verifying
the editor. The feature and architecture documentation lives in features.md;
this file covers only what a developer needs to run the app and trust what
they see on screen.

The neighbouring documents: **block-arch.md** for the per-kind block design,
**selection.md** for selection over text that is drawn rather than edited,
**accessibility.md** for what a screen reader, a keyboard and a contrast floor get, and
**multi-vault.md** for the window and vault composition. Accessibility is the
one of those with a check that fails a build: `AccessibleNameGuard` refuses a
control with no accessible name, a glyph label with nothing spoken beside it,
an animation that ignores the reduce-motion setting, and a menu entry that
cannot draw its own disabled state (`qml/DiscoverableMenuItem.qml`).

## Building and running

```
./build.sh            # configure + build
./build.sh --test     # ...then run the test suite
./build.sh --run      # ...then launch the editor
./build/kvit-notes   # launch directly
```

Optional CMake flags: `-DKVIT_AGENT=ON` (premium agent module),
`-DKVIT_UI_DRIVER=ON` (the scriptable UI driver below).

## Where a new file goes

`src/` is seven libraries, one directory each, and they may only include
downward:

```text
src/content/      format transforms with no dependency on anything else:
                  markdown value parsers, inline-markdown mappings,
                  diagrams, math, HTML to markdown
src/domain/       the block document: model, serializer, undo, selection,
                  in-document search, outline, statistics
src/search/       the rebuildable note index (this is the only place
                  Qt6::Sql is linked)
src/platform/     settings, file watching, network policy, tray, hotkeys,
                  appearance tokens (the only place Qt6::Network is linked)
src/repository/   the vault: containment, note files, atomic persistence,
                  trash, backups, recovery, templates, import, assets
src/application/  the document session, startup, the view models, queries,
                  export
src/qml/          the QML type registrations, the editor engine, and the
                  AppContext composition root
```

A file placed in the wrong directory usually announces itself at once,
because each target sees only its own directory and those of the modules it
links. `#include "notecollection.h"` from a `src/domain/` source does not
resolve, and the error is "No such file or directory" rather than anything
about layering. If a header you expect is not found, the question to ask is
whether the include is pointing the wrong way rather than whether the path is
misspelled.

Three things the compiler cannot catch are checked by
`python3 tools/check-layering.py`, which also runs as the `LayeringGuard`
test: the intended dependency graph (so a widened `target_link_libraries`
line cannot quietly legalise an upward include), Qt networking outside
`src/platform/`, and filesystem mutation outside the files listed in that
script. Adding a write means adding the file to `MAY_MUTATE` with the reason,
which is deliberate — the list is the answer to "what can write, and on whose
authority".

Types QML needs are registered as `QML_FOREIGN` wrappers in
`src/qml/qmlsingletons.h`, never with a macro on the class itself.
`qmltyperegistrar` builds the `Kvit` module's type list from one target's own
metatypes, so a macro on a class in any other module is silently ignored and
QML reports "ReferenceError: <Type> is not defined" at runtime.

The reasoning behind all of this, including the three placements a first
reading of the layout would not predict and what is still one large class, is
[docs/adr/0008-module-boundary.md](docs/adr/0008-module-boundary.md).

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
before every configure/build, so a Windows build cannot run stale code
from either entry point. It guesses neither end of that sync: the source
checkout is read from the `source=` line of the `.kvit-notes-mirror`
marker, and the mirror directory is its own location resolved through
`wsl.exe wslpath`. A directory without the marker is refused rather than
synced, so the Windows entry point enforces the same contract as the WSL
one. The mirror excludes `.git`, `build*` directories,
`tests/screenshots`, and the Windows-side result logs; it preserves the
WSL tree's bytes, so files keep the LF endings a CI checkout gets. The
one exception is the bat helpers themselves, which `.gitattributes`
checks out as CRLF on every platform because `cmd.exe` mis-parses labels
and parenthesized blocks in LF-only batch files. `KVIT_NO_SYNC=1` skips
the sync when WSL is unavailable.

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

From a Windows prompt in the mirror directory, the bat helpers do the
same. They are tracked at the repo root and reach the mirror like every
other file, through the sync; `win-build.bat` performs that sync itself.
Each reads `QT_ROOT_DIR` and `VS_CMAKE_DIR` from the environment when set,
so a Qt kit or Visual Studio edition installed somewhere other than the
stock paths needs no edit:

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
(src/qml/kvitapplication.cpp) runs before `QApplication` is constructed (the
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

## Reading a Qt Quick suite result

`IntegrationTests` runs the 351 cases of tests/tst_integration.qml through one
process holding one window. That is deliberate, since per-function isolation
maps hundreds of compositor-positioned windows and takes over the desktop while
it runs, but it means the cases are not independent, and the whole-suite
pass/fail count is worth much less than it looks.

Three environments give three different answers, and only one of them answers
the question "did my change break anything".

**Use the VNC platform plugin.** Qt ships `libqvnc.so` beside the other
platform plugins. It is a real windowing surface with working focus and input
that runs entirely off the desktop, and nothing has to connect to the port for
the tests to run:

```
QT_QPA_PLATFORM=vnc:size=1600x1200:port=5921 QT_QUICK_BACKEND=software \
  build/tests/test_integration -input tests/tst_integration.qml
```

All 351 cases run, none of them skip, and it takes about 150 seconds. The
result is stable enough to diff: two builds at different commits produced
identical failure name lists. Concurrent runs need different ports.

**`QT_QPA_PLATFORM=offscreen` skips two thirds of the file.** `isHeadless` in
tst_integration.qml is `Qt.platform.pluginName === "offscreen"`, and 252 of the
351 cases test themselves against it and skip. The run is stable, so it is a
usable smoke test, and it proves nothing about keyboard or mouse behaviour. A
new interaction test silently skips there.

**A run on the developer's display reports 150 to 170 failures whatever the
tree contains.** All the cases share one window, so once that window loses
activation part-way through (typing in another application will do it, and
WSLg drops it unprompted), everything after that point which types or clicks
fails. The loss point moves between runs, so the failing set is not even stable
against itself, and a difference of a few in the count carries no information.

**What the cases start from.** The suite's `init()` puts the application graph
back to the shell's opening state before every function: no collection open,
no file associated, the fallback document loaded through
`StartupController::initializeFallbackDocument()`, and the window's panel and
view properties at their defaults. Reach for it when a case needs a clean
document rather than building one by hand, and extend it rather than working
around it when a case needs something the reset does not yet cover. It is not
complete: roughly twenty cases in the `z` range still pass alone and fail late
in a shared run, on residue accumulated across a few hundred preceding cases
rather than on any one of them, so treat a whole-suite result as a strong
signal rather than a proof.

**Which entry carries the verdict.** `IntegrationTestsIsolated` does. It runs
`tools/run-integration-gate.sh`, which gives every case its own process (three
attempts each, a per-case timeout so one hang cannot stall the run) under the
same VNC platform, and it passes 351 of 351 in about eight minutes. No case
there can be failed by what an earlier one left behind, which is the whole
point. It is labelled `shell` and blocks a merge.

That entry spent a long time registered and disabled, and it had to be: its
runner forced `offscreen`, where 252 of the cases skip themselves, so a gate
named for the whole suite exercised under a third of it and could not be
believed either way.

`IntegrationTests`, the single-process entry, is the fast local signal at two
and a half minutes and is labelled `visual`, which is informational. Read a
new failure there as something to look into and confirm the case on its own
before treating it as a defect.

Run the Qt Quick suites serially. `ctest -j4` turned 30 failures into 106,
because `VisualTests` and `IntegrationTests` then compete for the display.

**A case that passes in the full run and fails alone is not automatically
noise.** `test_t3_ctrlNCreatesNoteInCurrentScope` did that for about a hundred
commits, and the reason was a real defect: Ctrl+N and Ctrl+O were declared as
`Shortcut` items inside DocumentSessionDialogs.qml, which main.qml builds on
first use, so on a freshly launched window neither shortcut object existed and
neither key did anything until an error, a recovery prompt or an unsaved-close
question had opened one of that component's dialogs. Inside the full run an
earlier case had always opened one. The general rule it leaves behind: nothing
that must exist before the user asks for it may be declared inside a lazily
loaded component, and a window-level shortcut is exactly that.

Fixing that one uncovered a second defect of the same shape in the case
immediately after it, which the first had been masking. Committing an inline
note rename with Return left the key unaccepted, so it travelled on to the
note list's own `Keys.onPressed`, which reads Return as "open the current
row": renaming a note the keyboard cursor was not sitting on renamed the right
file and then opened a different note in the editor. Both defects had been in
the tree for about a hundred commits with a test failing on each of them the
whole time.

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

For screenshots that will be committed, stage the demo vault under a neutral
path such as `/tmp/kvit-demo/vault`. The status bar renders the note's absolute
path, so capturing from a home directory or an agent scratch directory exposes
that machine-specific path in the image pixels. Inspect the full frame,
including the status bar, before adding it to `screenshots/press/`.

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

## Network egress goes through one policy

A note is untrusted input. Anyone who can hand you a `.md` file can put a
URL in it, so any request the editor makes on the note's behalf discloses
the reader's address, user agent and reading time to whoever chose that
URL, and points the editor at whatever the URL names, including hosts only
the reader's machine can reach. Two objects exist to contain that:

- `EgressPolicy` (src/egresspolicy.{h,cpp}) decides. It holds the master
  switch `network.autoLoadRemoteContent` (off by default), the set of
  origins the reader has approved (`network.allowedOrigins`), the
  scheme and credential rules, and the address classification that rejects
  loopback, RFC1918, IPv6 unique-local, link-local (which is where
  `169.254.169.254` lives), multicast and reserved ranges. It is published
  to QML as `egressPolicy`.
- `EgressFetcher` (src/egressfetcher.{h,cpp}) executes, and is the only
  `QNetworkAccessManager` in the tree. It resolves the hostname first,
  checks every returned address, pins the connection to the address it
  checked while keeping the original hostname for the `Host` header and TLS
  verification, refuses to let Qt follow redirects so each hop is
  re-checked from scratch, caps the read buffer so an oversized body is
  abandoned rather than buffered, and enforces a timeout and a content-type
  check.

Rules that are easy to break without noticing:

- **Never bind a remote URL to a QML `Image.source`.** Qt's own network
  stack would fetch it, outside every check above. Delegates call
  `egressPolicy.imageSourceFor(url)`, which passes local paths through and
  turns an approved http(s) URL into an `image://remote/...` id served by
  `RemoteImageProvider` over the fetcher.
- **`isAllowed()` and `allowedOrigins()` are function calls, not
  properties.** A QML binding over them never re-evaluates on its own, so
  read `egressPolicy.revision` inside the binding to make it live. Every
  decision-affecting change bumps that revision.
- **Loopback is blocked, which a hermetic test needs to undo.**
  `EgressPolicy::setLoopbackAllowedForTests()` is the only way, and it is
  deliberately neither `Q_INVOKABLE` nor backed by a setting.
- **Never give `MediaPlayer` a remote URL.** `MediaBlock.qml` asks
  `RemoteMediaCache` for approved audio/video. The cache downloads through
  `EgressFetcher` with DNS/redirect validation, a 64 MiB cap, a 30-second
  timeout, and media content-type checks, then gives QtMultimedia only a
  temporary local file URL. The cache lifetime owns and removes that file.

The suite is `tests/test_egresspolicy.cpp` (`EgressPolicyTests`), which
drives a loopback `QTcpServer` rather than the real internet and covers the
refusals, redirect re-checks, streaming caps, and local-only media handoff.

## Extensions are first-party code, and that is the decision

Kvit Notes is the open core of a two-repository product. The private
`kvit-notes-pro` repository links this one in as a submodule and adds a
premium module on top, behind the `KVIT_AGENT` option. `KvitExtension` and
`ExtensionRegistry` exist so that module can attach without this tree
carrying any conditional that refers to it.

**Extensions are statically linked and fully trusted.** There is no plugin
loader, nothing is discovered at runtime, and nothing is loaded from disk. A
module is C++ compiled into the same binary, so it already has the address
space: it can call any function the process can call, read any memory the
process can read, and do so without going near this interface. Building a
capability system, a permission prompt or a sandbox around the seam would
constrain nothing an actual attacker faces, while making every first-party
change more expensive. If a module is ever loaded from disk or written by a
third party, that decision reopens this one; until then the binary is where
the trust boundary sits, and the interface carries none of it.

What the interface is for is therefore clarity between first-party
components. It narrows what a module can do by accident, and it makes what a
module contributes legible from the core:

- **Contributions are namespaced.** A module declares a `qmlNamespace()` and
  returns its objects from `contextObjects()`; the registry publishes one
  context property per module, so QML reaches them as `agent.session` rather
  than as bare globals. This replaced an `installContextProperties(QQmlContext
  *)` callback that handed each module the shell's root context to set any
  name it liked. A namespace that is not a valid identifier, that another
  module already took, or that collides with a name the core published is
  refused with a warning on the `kvit.extensions` category, and the module
  publishes nothing. `AppContext::installContextProperties` builds the
  reserved list as it publishes, so it cannot fall behind what the core
  actually occupies.
- **Registries are instance owned.** `BlockKindRegistry` and
  `ExtensionRegistry` have no `instance()`. `AppContext` owns both and
  publishes them; `BlockModel` resolves fence kinds against the one wired into
  it, falling back to a private registry holding the built-ins so a bare
  `BlockModel` in a unit test still renders a `kanban` fence as a board. The
  payoff is test isolation: cases used to depend on `reset()` being called in
  the right order in `init()`, and a suite that forgot inherited whatever the
  last one registered. Now each case constructs what it needs. The sharpest
  evidence for this is in "One composition root, in production and in tests"
  below: the Qt Quick harness had drifted from the real object graph without a
  single failing test, and shared mutable setup is how that stays invisible.
- **A block kind is one class.** Every question decided per kind is a pure
  virtual on `BlockKindDef` (`src/domain/blockkinddef.h`): how the kind
  serializes, what its three text projections are, whether its content is
  verbatim, what markup it exports as, which delegate draws it, and what menu
  rows it contributes. One class per kind lives under
  `src/domain/blockkinds/`, and a kind that has not answered something does
  not compile. `BlockKind` (`src/domain/blockkind.h`) is the one enumeration
  of them, numbered at the values already in use; it is a key rather than
  something features switch on. The reasoning, and the export defect that
  prompted it, is [block-arch.md](block-arch.md).

  Two guards cover what the compiler cannot: `BlockKindDefTests` walks the
  enumeration from its metaobject and checks that every kind has a definition,
  round-trips its markdown, carries its attribute tag where the parser expects
  it, and exports as something in both formats; `tools/check-block-kinds.py`
  (the `BlockKindsGuard` ctest entry) checks the same completeness without a
  built tree and that every delegate URL a kind names is in `resources.qrc`.

- **Where a module may draw.** Four named UI slots (`KvitSlots` in
  `src/application/extensionregistry.h`) are empty `Loader`s in `qml/main.qml`
  that a module fills by returning a QML file from `qmlSlot()`: a banner strip
  at the top, a bar above the status bar, a panel beside the outline and
  backlinks panes, and a header pinned across the top of the editor column
  that the document scrolls under. Each resolves to an empty source in the
  open build, which leaves its Loader inactive and zero-sized.

  Inside the document there is a second seam, `DocumentDecorations`
  (`src/application/documentdecorations.h`), because the slots can only put
  content around the editor and annotations, review markers and comment
  threads want to sit between and beside the blocks. A module registers a
  CONTAINER after a block, a glyph in the reserved column at the right edge
  addressed by block and by visual text line, or a SPAN marking a run of
  characters inside one block; all three are rendered rather than inserted, so
  `BlockModel` is untouched, every row index is what it would have been, and
  nothing drawn there reaches the note's undo stack. The rows themselves do
  the drawing, in `qml/BlockDelegateBase.qml`: a delegate binds
  `blockContentHeight` for its own content and the base adds whatever is
  drawn after it, which is what makes a container occupy space between two
  blocks. Positions come back through four geometry queries the block list
  answers, since a module has no way to see a laid-out Qt Quick item. With
  nothing registered the margin column has no width, each row asks one cheap
  question and gets an empty answer, and the layout is what it was.

  A span is addressed the way a search hit is: `{block, start, length}` in the
  block's DISPLAY text, which `InlineMarkdown::mapDisplayRange` turns into the
  document coordinates of the current reveal state, so the mark follows the
  characters as markers appear and disappear under the caret. It has two
  visual channels, which compose. The wash is a background, painted by
  `BlockEditorEngine`'s highlight pass beside the search tint (and under it,
  since a background can only be one color per character). The outline is a
  border, which no character format can express, so it is drawn as items over
  the text by `qml/SpanDecorationOverlay.qml`, one box per visual line the
  span crosses. Because the wash lives in the highlighter, a marked block
  renders through the editing engine rather than through the lightweight
  read-only text path, exactly as a block with a search match does; that is
  the reason `hasDecorationSpans` appears in `useReadOnlyText` and
  `useReadOnlyShell`.

**`KVIT_AGENT=ON` against this checkout stops at configure time with an
explanation.** The module's sources are deliberately absent here, so the
option now checks for them and explains that the premium module lives in
`kvit-notes-pro` and that this repository builds the open editor with the
option off. It used to fail deep inside `qt_add_executable` with "Cannot find
source file" for each missing path, which reads like a broken checkout.

## A subtree the application manages, and the few files in it the index sees

The scanner skips every dot-prefixed directory, so a tree an application keeps
beside a person's notes (`.kvit`, and anything a linked module writes) is
invisible to the index, to search, to links, to the counts and to export.
That default is right for working copies, caches and control data. It is
wrong for the one file in each of those folders that is a document in its own
right: a report, a summary, a log somebody may want to find again.

`ReservedSubtrees` (`src/domain/reservedsubtrees.h`) is the narrow opt-in.
A registration names the subtree, a wildcard over the paths inside it that are
admitted, the label the admitted files are grouped under, and the
`kvit-type` front-matter value they have to carry. Recognition is by path so
the walk need not read every file it passes; the front matter is the
cross-check that stops a file which merely landed in the right place from
being taken for one of the application's.

Admitted files form a **realm**. A realm file is indexed, full-text
searchable, and reachable by a folder-qualified `[[link]]`. It is not one of
the user's notes: `NoteCollection::noteRelPaths()` and `noteCount()` exclude
it, which is what keeps it out of the note list, the quick switcher's main
group, the query blocks, the statistics and a vault-wide export without any of
those having to know realms exist. Three things do know: the walk, which
admits it; `WikiLinkIndex`, where a bare `[[report]]` never resolves into a
realm because every folder in the subtree holds a file of that name; and the
switcher and note list, which draw each realm as a section of its own under
its label.

Two details worth knowing before changing this. A dot-directory is hidden, and
the directory listings deliberately do not ask for hidden entries, since that
would also change what an ordinary folder shows on a platform where "hidden"
is a file attribute; the reserved subtrees are therefore reached by name after
the listing, in all three walks. And admission is provisional in the asynchronous
scan: the walk lists a nominated file before anything has read it, and the
parse that follows takes back the ones whose front matter refuses. The rules,
including what happens with nothing registered, are pinned in
`tests/test_reservedsubtrees.cpp`.

## One writer per vault

Notes, the JSON sidecar, `collection.json` and the search index are all read
into memory when a vault opens and written back whole. `QSaveFile` makes each
of those writes atomic, which prevents a half-written file and nothing else.
Two Kvit processes on one vault both load the same state, each change
something different, and whichever saves second silently discards the other's
work. `tests/test_vaultlock.cpp` demonstrates it with two processes and a tag
colour; before the lock, one of the two colours was simply gone.

`VaultLock` (src/vaultlock.{h,cpp}) takes an advisory lock on
`<vault>/.kvit/vault.lock` for the life of the session. `NoteCollection`
acquires it in `prepareRootPath()`, before any state is read, and releases it
in `closeRoot()`.

**Why a lock rather than compare-and-swap.** A revision protocol would need a
merge policy designed and maintained separately for note bodies, the sidecar,
the collection state blob and the SQLite index, and it would have to stay
correct on every future write path. A notes app has no multi-writer story
worth that. Refusing the second session is a smaller guarantee, but it is one
that cannot rot.

**Why a kernel lock rather than a PID file.** `flock` on POSIX and
`LockFileEx` on Windows, held on an open descriptor. The kernel drops the lock
when the process dies by any means, so a stale lock is impossible rather than
merely detectable, and a hard kill can never leave a vault that will not open.
It also behaves correctly where a recorded process id would not: a Flatpak
session and a host session see different pid numbers but the same inode, so
they contend properly, and a lock file copied to another machine by a file
sync carries no lock with it. `QLockFile` was the obvious alternative and is
wrong here for exactly that reason, since its staleness check would decide a
live owner in another pid namespace was dead and steal the lock.

That namespace claim was checked rather than assumed, using `bwrap` (the
sandbox Flatpak itself runs on) with `--unshare-pid`: a host session holding
the vault correctly refuses a sandboxed second session, a sandboxed holder
correctly refuses a host session, and `SIGKILL` on either lets the next
process straight in. The manifest's `--filesystem=home` is what makes the
inode the same on both sides. A full Flatpak build was not exercised here,
only the namespace behaviour the design depends on.

Things worth knowing before changing this:

- **Failure is open, not closed.** Only "another process holds it" refuses the
  vault. A filesystem without locking, a read-only directory, or any other
  kernel error opens the vault unlocked with a warning on the
  `kvit.vaultlock` logging category. A vault nobody can open is a worse bug
  than an unguarded one.
- **Ownership is reference counted per canonical path.** One process
  legitimately holds several `NoteCollection` objects on a root (the
  warm-cache tests in test_notecollection.cpp do), and a POSIX `flock` taken
  on a second descriptor of the same file fails against its own process. The
  count is what stops Kvit refusing itself.
- **The lock file's contents are advisory.** They name the holder for the
  refusal message; correctness never reads them. A truncated or hostile file
  leaves the lock itself working and only makes the message vaguer.
- **Single-file mode takes no lock.** `kvit-notes note.md` opens no
  collection, so nothing is acquired and nothing is refused. Two editors on
  one *file* remain the file watcher's problem, and it already has a
  keep-mine/load-theirs banner for that.
- **The second session refuses and explains** rather than opening read-only or
  handing off to the running instance. Read-only would mean auditing every
  mutation path to be sure nothing slips through, and a half-enforced
  read-only mode is worse than a refusal. Handing off is a better experience
  and a genuinely separate feature: it needs single-instance IPC
  (QLocalServer) and a window-raise protocol. `NoteCollection::vaultInUse`
  carries the holder's description, and main.qml explains it.

## Menu labels carry their access key

Every hand-written menu label in `qml/` is written with its access key marked
by `&`, and reaches the menu through the `MenuText` singleton:

```qml
MenuItem { text: MenuText.label(qsTr("&Copy")) }        // a label
MenuItem { text: MenuText.plain(folderName) }           // a name off the disk
Menu     { title: MenuText.label(qsTr("Copy &as…")) }   // a submenu's entry
```

Qt does the rest: the label goes through `QQuickMnemonicLabel`, which draws the
underline, and `QQuickAbstractButton` builds Alt+<letter> from the same text.
The binding is live only while the item is shown, so a context menu's letters
cost nothing while it is closed. The three toolbar buttons that open menus are
the exception worth knowing: they are always shown, which is exactly why
marking them gives Alt+F, Alt+V and Alt+I from anywhere in the window.

Three things this rule exists for.

`MenuText.label()` removes the markers on macOS, which has no access keys. That
matters beyond appearance for one entry: Qt moves "Settings…" into the macOS
application menu by matching the item's text, so a surviving marker would leave
it in File. Nothing here can test the macOS side, so it is item 6 of the macOS
watch-list in docs/qa-checklist.md.

`MenuText.plain()` is for text nobody wrote as a label — a vault path, a
template's file name, a folder or a board column. Without it a folder called
"R&D" shows as "RD" with an underlined D, because the menu reads the ampersand
as a marker. It escapes rather than strips, so the name survives on every
platform.

The letters have to be distinct within one menu, which the
`MenuAccessKeyGuard` test (`tools/check-menu-access-keys.py`) checks over the
QML. A clash is invisible in review and invisible in a screenshot — both
letters are underlined either way — and shows only as a key that runs the wrong
command.

The rule itself, in both directions, is `src/platform/menuaccesskeys.h`, and
`tests/test_menuaccesskeys.cpp` exercises both platforms by naming the platform
rather than running on it.

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
  fails on any QML warning, both during load and for the rest of the suite as
  the loaded shell is exercised. QML reports an unknown context property or an
  unresolvable type as a warning and then carries on with an undefined value,
  so without this the load "succeeds" no matter how much of the shell failed to
  wire up. A case that provokes a warning on purpose declares it in
  `g_expectedWarnings`. The test also asserts that the published
  context-property set is empty and checks every singleton in
  `KVIT_QML_SINGLETONS`, reading that registry directly so the two cannot
  drift.
- **QrcSyncGuard** (`tools/check-qrc-sync.py`) compares `resources.qrc` with
  the files actually in `qml/`. A component missing from that list breaks the
  shipped shell and hangs the Qt Quick harness until its CTest timeout, since
  a QML load error leaves the harness waiting on its `when:` condition rather
  than failing. `resources.qrc` is the only list: the application, `test_shell`
  and the Qt Quick Test binaries all compile it, and
  `tests/integration_tests.qrc` holds the `tst_*.qml` suite files alone. The
  guard also fails if a component list reappears there.
- **qmllint** reads every file in `qml/`, including the ones no test
  instantiates, and the Qt Quick Test files in `tests/` as well. This matters
  because the runtime gate only sees what the initial scene actually builds: a
  bad binding inside an inactive `Loader` never evaluates, and a missing
  *sub-property* of an object that does exist (`noteCollection.somethingGone`)
  evaluates to `undefined` silently, with no warning at all. Static analysis is
  what covers those. Reach it as the `qmllint` build target or the `QmlLint`
  CTest entry rather than by calling `tools/run-qmllint.sh` directly; both
  regenerate `kvit-qmltypes` first, and the script needs that type description
  to resolve C++-declared properties. Test QML runs a looser profile, because
  the harness handles are context properties and the suites drive the app
  through a `QObject`-typed loader; an unqualified name there passes only if
  `tests/testsetup.h` publishes it.

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
