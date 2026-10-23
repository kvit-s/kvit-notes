# Recording the demo videos on Windows

*Written 2026-08-05. The companion to `marketing-plan.md`, which decides what
the videos show and why; this one covers how to get the capture machinery onto
the Windows machine and run a recording session without fighting it. Capture
itself is the Windows 11 Snipping Tool in its video mode, which records a
screen region to MP4.*

## Why Windows

The application draws through the real GPU there. Inside WSL, Mesa's `d3d12`
driver corrupts text — white renders yellow, some glyphs lose their alpha, dark
text becomes pale outlines — while rectangles and images stay correct, so a
recording can look fine in thumbnail and be wrong in every letter. The
`GALLIUM_DRIVER=llvmpipe` pin that works around it fires only under WSL and is
inert on Windows. Beyond avoiding that, Windows is the largest desktop share,
so the recording shows what most viewers get, with DirectWrite text shaping
rather than the Linux font stack.

## What is built

All of it, as of 2026-08-05, and every scenario was run and checked on Linux
before this was written. What remains untested is the Windows half: the
driver has never been compiled there, and the pointer warping and window
positioning below cannot be exercised from this machine.

The pieces are `tools/uidriver.cpp` (the five scenarios, the caption, the
cursor movement, `--pos`), `win-uidriver.bat` (the Windows build), and
`tools/record-tour.ps1` (the recording session). The rest of this section
describes what each does; the sections after it are the session itself.

1. **One scenario per feature**: `tour-mermaid`, `tour-livepreview`,
   `tour-math`, `tour-repair` and `tour-query`, each driving its feature with
   enough dwell after each action to be followed.
2. **`--title=`**, a caption band across the bottom of the frame holding the
   feature name, faded after about two seconds. It is drawn into the driver's
   own window by parenting an overlay onto the window's content item, so the
   shipped interface never learns about it; the driver is off by default and
   absent from a release build.
3. **`--pos=X,Y`**, so the window appears at the same screen position every
   run. This exists for the Snipping Tool's benefit: it records a region
   rather than a window, so a window that lands somewhere different each time
   means re-selecting the region for every clip.
4. **Real cursor movement.** The driver used to synthesise input events
   without moving the pointer, which would have recorded clicks happening
   while the cursor sat still somewhere else. The tour scenarios now move it
   with `QCursor::setPos` along an eased path to each target, alongside the
   synthetic events that actually drive the interface. Both halves are needed:
   the interface follows delivered events, and the recorder captures the
   operating system's own pointer.
5. **A final frame per take.** Each tour writes `<scenario>-final.png` beside
   the recordings, so a take can be checked without scrubbing the video.

### Three things the scenarios had to work around

Each of these was found by running the scenario, and each would have wasted a
recording session:

- **A ListView delegate's items are invisible to `findChild`.** An item
  declared in the shell's own QML is a QObject child of the window; an item a
  delegate instantiated has its QObject parent set by the delegate's creation
  context. Every per-block item the tour aims at is of the second kind, so the
  driver walks the visual tree instead.
- **Box art has to be pasted fenced.** Pasting it bare produces a run of
  paragraphs, one per line. Fenced, the paste path makes a block, the ingest
  tags it as a diagram, and the columns come out aligned, which is the thing
  worth filming.
- **An outside edit has to come from outside.** The query tour changes a
  project's front matter on disk to show the table re-evaluating. Written
  from inside the driver, nothing happened however long the wait, because the
  application ignores changes it made itself, and a write from the driver is
  a write from the application. It now spawns a separate process to do it,
  which is also what an outside edit means.

## Getting it onto the Windows machine

Code is edited in the WSL tree only. The Windows side is a disposable mirror
that the sync re-creates, and the sync propagates deletions, so nothing is
ever edited there.

One-time, from a WSL shell:

```bash
tools/win-sync.sh --init D:/projects/kvit-notes
```

The UI driver is off in a default configure, so it has its own entry point,
`win-uidriver.bat`, beside the other Windows helpers. It calls
`win-build.bat configure` first, so the mirror-then-build rule that stops a
stale build applies unchanged, then adds `-DKVIT_UI_DRIVER=ON` and builds that
one target:

```
win-uidriver.bat
```

The result is `build-windows-msvc-release\Release\kvit-uidriver.exe`. It has
never been compiled on Windows, so expect an hour of first-build friction; the
target links `kvit-core`, `Qt6::Test` and `Qt6::Widgets`, all of which the
existing Windows build already produces or finds.

## The recording script

`tools/record-tour.ps1`, run from the mirror directory. It stages the demo
vault at `C:\kvit-demo\vault` (a neutral path, because the status bar renders
the open note's absolute path), puts Qt on `PATH`, prompts you to start the
recording, counts past the Snipping Tool's own three-second lead-in, and then
plays one segment. Frames for checking a take go to
`%USERPROFILE%\Videos\kvit-tour`.

The vault is re-staged on every run, so a segment that edits a note starts
from the same state as the take before it. That matters for `tour-query`,
which changes a project's front matter to make a row appear.

Useful switches: `-Title` for the caption, `-X`/`-Y` and `-Width`/`-Height`
for where the window lands, `-LeadIn` for the delay before the action starts,
and `-NoPrompt` for re-running a segment while tuning it without recording.

Recording the whole set is then five runs:

```powershell
.\tools\record-tour.ps1 -Segment tour-mermaid     -Title "Drag a diagram node, and the markdown rewrites itself"
.\tools\record-tour.ps1 -Segment tour-livepreview -Title "Markdown syntax reveals itself where the caret is"
.\tools\record-tour.ps1 -Segment tour-math        -Title "TeX math, rendered as you type"
.\tools\record-tour.ps1 -Segment tour-repair      -Title "Crooked box art in, a straight diagram out"
.\tools\record-tour.ps1 -Segment tour-query       -Title "A live table built from front matter across the vault"
```

## Before the first take: three things that will otherwise show up in frame

**The window's saved settings.** On Windows the application keeps its settings
in the registry under `HKCU\Software\Kvit\Kvit Notes`, so a recording inherits
whatever theme, font size and block spacing that machine has. Decide which
appearance the launch shows, set it deliberately, and record every segment
without changing it. To record a first-run state instead, delete that registry
key beforehand, which discards that machine's application settings and cannot
be undone.

**Display scaling.** At 125% or 150% a 1600×1000 window occupies more physical
pixels, and the Snipping Tool records physical ones. Record on a display set to
100%, or accept the scaled resolution and pick region dimensions to match. Fix
this before the first take rather than between segments, so the clips
concatenate without a resolution change.

**What is behind the window.** The Snipping Tool records a region rather than a
window, so anything overlapping the frame is in the video. A clean desktop and
no notifications; turn on Focus Assist or Do Not Disturb for the session.

## Session order

1. `win-uidriver.bat` from the mirror. Confirm the exe exists.
2. Run one segment without recording, to check the window lands where `--pos`
   says and that the pacing reads well.
3. Set the appearance, the display scale and Do Not Disturb.
4. Select the capture region once, sized to the window plus a small margin.
5. Record the five segments, one file each, re-shooting any that go wrong.
6. Record the test-suite video, which is a separate session using
   `ctest -R IntegrationTests` rather than the driver, covered in
   `marketing-plan.md`.

## After the capture

The Snipping Tool writes MP4. From there:

- The full tour is the five clips concatenated, in the order the shot list
  gives.
- Each clip also becomes a short loop for the README, converted to GIF with
  `ffmpeg` and kept under about 5 MB so it does not stall on a phone.
- One poster frame for the video and a 1200×630 social card.

Finished media stays in `planning/`, which is gitignored, since a repository is
a poor place to version binary files. The GIFs the README references are the
exception and go in the tree beside the existing press stills.

## Check before anything is published

- No absolute home path, username, or machine name anywhere in frame,
  particularly the status bar. The OCR leak scan in CI reads shipped still
  images and does not look inside a video.
- Text renders correctly throughout: no yellow-tinted white text, no
  see-through glyphs. Those indicate the recording captured the corrupted GPU
  path, which should be impossible on Windows and is worth confirming rather
  than assuming.
- The product name reads "Kvit Notes" everywhere, with no leftover of the
  pre-rename "Kvit Editor".
- No notification, taskbar preview or overlapping window intrudes on any
  frame.
