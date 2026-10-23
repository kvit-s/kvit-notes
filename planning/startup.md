# Where the first second of startup goes

*Working notes, kept in `planning/`, which is gitignored. Measured 2026-08-09
on the developer's machine, against the installed Windows build and the WSL
Linux build of the same tree. Nothing in the repository was changed as a
result; this file records what the numbers are, how to reproduce them, what
was ruled out, and which changes would give time back.*

## The question this answers

Kvit Notes felt slower to start than Obsidian on Windows. The question was
whether that impression corresponds to something real and, if so, what the
time is being spent on. The short answer: a warm launch reaches a painted
window in about one second, and roughly 850 ms of that is the QML engine
building the editor shell. The application's own C++ setup (settings, the
window registry, the whole `AppContext` object graph) costs about a
millisecond. Opening the vault and the note costs 70 ms and happens after the
window is already on screen.

## What was measured

The installed build, `C:\Users\sk\AppData\Local\Programs\Kvit Notes\kvit-notes.exe`
(built 2026-08-05), launched against `C:\kvit-demo\vault` (8 notes). Three
runs per configuration. Two clocks were used together:

- **The application's own perf log.** `KVIT_PERF=verbose` plus
  `KVIT_PERF_LOG=<path>` writes a JSON line per sample. The relevant
  operations are `startup.pre_qml`, `startup.first_frame`, `startup.scan` and
  `startup.initial_open`. Its zero point is the `PerfLog` singleton's
  construction, which happens in the `KvitApplication` constructor, just after
  `QApplication`.
- **An external stopwatch in PowerShell**, polling the process's
  `MainWindowHandle` until it becomes non-zero. That is the moment a window
  exists on screen, before it has painted anything.

The two clocks were tied together by comparing the perf log file's creation
time against the process's `StartTime`, which gives the offset from process
start to "Qt is up". It was 77–99 ms.

All numbers below are warm: the application had been run before, so its DLLs
and the QML disk cache were in the file cache.

## The timeline

Every figure is milliseconds from process start, as the range across three
runs.

| Milestone | ms |
|---|---|
| Qt initialised: DLLs loaded, `QApplication` constructed | 77–99 |
| All C++ wiring done: settings store, window registry, `AppContext` members | ~1 ms after the above |
| Window on screen, not yet painted | 749–884 |
| First painted frame | 937–1070 |
| Vault scanned (8 notes, 25–27 ms) and the note loaded (37–42 ms) | ~1010–1140 |

Two things follow from the shape of this.

The C++ side is already free. `startup.pre_qml` lands at t=0 on the perf log's
own clock, and the first QML-driven sample (`search.doc_recompute`, emitted
from `AppContext::wire()`) lands at t=1. Optimising anything in
`src/` outside the QML seam would win nothing.

The vault is not involved. `VaultWindow::instrumentFirstFrame()` defers
`StartupController::start()` until after the first rendered frame, which is
working as intended: the scan and the note open together cost 70 ms, and they
happen after the window is up.

## How much of it is Qt's floor

A minimal Qt Quick Controls window holding one `Label` and one `Button` inside
an `ApplicationWindow`, run through Qt's own `qml.exe` on the same machine with
the same Fusion style, reaches a window in **440–496 ms**. Kvit reaches a
window in 749–884 ms.

So a little over half of the startup is the price of any Qt Quick + Controls
application on this machine, and Kvit's own 91-file shell roughly doubles it.
That bounds what is worth chasing: even a shell that instantiated instantly
would still start in about half a second.

Reproduce with:

```qml
// min.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    width: 800; height: 600; visible: true
    ColumnLayout { anchors.centerIn: parent; Label { text: "hello" } Button { text: "ok" } }
}
```

```
set QT_QUICK_CONTROLS_STYLE=Fusion
C:\Qt\6.10.1\msvc2022_64\bin\qml.exe min.qml
```

## What was ruled out

**The shell's module imports cost nothing measurable.** Adding every module the
shell imports (`QtQuick.Window`, `QtQuick.Layouts`, `QtQuick.Dialogs`,
`QtQuick.Effects`, `QtMultimedia`, `Qt.labs.qmlmodels`) to `min.qml` above
left it at 437–486 ms, indistinguishable from the plain version.
`Qt6Multimedia.dll`, `Qt6MultimediaQuick.dll` and `quickmultimediaplugin.dll`
do load on every launch even with no media block on screen, because
`MediaBlock.qml` imports `QtMultimedia` and is resolved as part of the delegate
graph. The ffmpeg libraries behind them (`avcodec-61.dll` and friends) stay
unloaded, so the media backend is never initialised. Checking the process's
loaded modules confirmed it: 158 modules, no ffmpeg.

**The QML disk cache is working.** Qt writes compiled units to
`%LOCALAPPDATA%\Kvit\Kvit Notes\cache\qmlcache` (91 `.qmlc` files, 2.7 MB)
and reuses them across launches; their modification times do not move on a
normal run. Forcing them off with `QML_DISABLE_DISK_CACHE=1` moved the first
frame from ~890 ms to ~1092 ms on the perf log's clock. That 200 ms is what
every *first* launch after an application update costs, since a new binary
invalidates the cache.

**Graphics device setup is a small slice.** `QT_QUICK_BACKEND=software` moved
the first frame from ~890 ms to ~781 ms, so creating the D3D device costs about
110 ms. There is nothing to do about it, since the software renderer is not an
option for the shipped application.

## Perception, on top of the measured time

The window is put on screen 180–190 ms before it has painted anything, and the
note text arrives about 70 ms after the first frame. The user sees an empty
frame, then chrome with an empty document, then the note. An application that
shows nothing and then appears complete reads as faster at the same total
time, which is what Electron applications including Obsidian do by holding the
window hidden until the renderer is ready.

Everything here is also a warm start. The install directory is 143 MB across
123 DLLs and Defender real-time protection is on, so a first launch after a
boot will be materially worse than one second. Obsidian is used daily on this
machine and is likely always warm.

Obsidian itself was not timed. It was running with five processes at the time,
and launching it again only focuses the existing window, so a comparison needs
it closed first.

## Where time could come back

Ordered by expected size. The first two are the only ones that address the
850 ms.

1. **Compile the QML ahead of time.** The QML files ship through
   `resources.qrc` rather than through `QML_FILES` on the `qt_add_qml_module`
   call in `CMakeLists.txt`, which the file itself notes was left undone
   because moving them is a larger change than the type checking needed.
   The consequence is that nothing is compiled at build time: the engine
   compiles all 91 files on first run and caches the result per user. Moving
   them removes the 200 ms first-run-after-update penalty and lets
   `qmlcachegen` do its work at build time.

2. **Stop building every dialog at startup.** `qml/main.qml` instantiates
   `SettingsDialog`, `ExportDialog`, `ImportDialog`, `TemplateDialog`,
   `BackupRestoreDialog`, `ShortcutReference` and `StatisticsPanel` at the root,
   and `qml/SystemIntegration.qml` instantiates `QuickCaptureWindow`. A `Popup`
   builds its entire contents at creation time rather than waiting for the open.
   **This has not been measured in isolation**, so treat it as the leading
   candidate for the shell's own share rather than as a confirmed cost. Note the constraint recorded at
   `qml/main.qml:593`: a shortcut cannot live behind a lazy `Loader`, so the
   split has to keep the shortcut outside and move only the dialog body inside.

3. **Hold the window back until the first frame is ready.** This removes the
   180 ms blank window without making anything faster. `VaultWindow::load()`
   already has the hook: `instrumentFirstFrame()` connects to
   `QQuickWindow::afterFrameEnd`.

4. **Session restore multiplies the shell cost.** Each `VaultWindow` owns its own
   `QQmlApplicationEngine` (`src/qml/vaultwindow.h`) and instantiates the shell
   from scratch, so `WindowRegistry::openSession()` reopening two vaults pays
   the shell cost twice, in series. The compiled units come off the shared disk
   cache, but instantiation does not.

## A Linux-only finding

`UrlLauncher`'s constructor calls `UrlLauncher::desktopOpeners()`, which runs
`QStandardPaths::findExecutable()` for about ten browser programs. Under WSL,
where `PATH` includes the whole Windows path across a slow filesystem, this
takes **343–497 ms**, the single largest item in the WSL build's startup and
larger than the QML engine load, which is 150–200 ms there. The whole
block is inside `#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)`, so Windows
never pays it, and a native Linux machine with a short `PATH` will pay far
less. Deferring the probe to the first `open()` call would remove it.

Found by bisecting `AppContext`'s member construction with a probe type whose
constructor writes a perf mark, declared between the members. Declaration
order is construction order, so interleaved probes give a per-member timeline.
Worth remembering as a technique; the same trick will localise any cost hidden
in a large member list.

## Reproducing the Windows measurement

The probes used for the bisection were temporary and have been reverted. The
Windows measurement needs no code changes:

```powershell
$exe = "C:\Users\sk\AppData\Local\Programs\Kvit Notes\kvit-notes.exe"
$env:KVIT_NO_SINGLE_INSTANCE = "1"     # a second instance, not a forward to the first
$env:KVIT_DISABLE_UPDATE_CHECK = "1"
$env:KVIT_PERF = "verbose"
$env:KVIT_PERF_LOG = "$env:TEMP\kvit-perf.jsonl"

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList "C:\kvit-demo\vault" -PassThru
while ($p.MainWindowHandle -eq 0) { $p.Refresh(); Start-Sleep -Milliseconds 5 }
$windowMs = $sw.ElapsedMilliseconds
Start-Sleep -Seconds 5
# process start -> "Qt is up", which is the perf log's zero point
$offsetMs = ((Get-Item $env:KVIT_PERF_LOG).CreationTime - $p.StartTime).TotalMilliseconds
Stop-Process -Id $p.Id -Force
```

`startup.first_frame` from the log plus `$offsetMs` gives the first painted
frame from process start. Point the run at a vault no other process holds, or
the second instance is refused the vault lock. That refusal leaves the startup
timings intact, but it does change what ends up on screen.

One side effect to know about: a run killed this way leaves its vault in
`session.openVaults` in the real `settings.json`, so the next ordinary launch
reopens it. A clean quit of the application rewrites that setting correctly.
