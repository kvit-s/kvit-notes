# The feature gallery: what exists and how to change it

*Working notes, kept in `planning/`, which is gitignored. The gallery itself
is finished and committed; this file records the decisions behind it and the
things that cost a run or several to find, so that changing a clip does not
mean rediscovering them.*

## What shipped

`gallery.md` at the repository root: fourteen sections, one per feature, each
a GIF of the real application doing the thing with two to four sentences under
it. The clips live in `screenshots/gallery/` and total about 3.2 MB. The
README links to it under the hero still and from its documentation table.
The page is written for someone deciding whether to download the application;
nothing about how the clips are made belongs on it, and devel.md holds that.

| Clip | Runs | Size | Shows |
|---|---|---|---|
| `mermaid.gif` | 9.0 s | 228 KB | a node dragged, the fence rewriting itself |
| `livepreview.gif` | 8.8 s | 232 KB | syntax opening around the caret |
| `math.gif` | 6.6 s | 148 KB | TeX typed and set |
| `palette.gif` | 9.3 s | 272 KB | the `/` menu, filtered, applied |
| `tables.gif` | 8.4 s | 188 KB | cell editing, Tab, a row added |
| `kanban.gif` | 8.6 s | 196 KB | a card dragged between columns |
| `query.gif` | 10.4 s | 136 KB | a live table answering an outside edit |
| `wikilinks.gif` | 10.8 s | 176 KB | `[[` completion, following, backlinks |
| `search.gif` | 10.6 s | 156 KB | the find bar, then collection search |
| `theme.gif` | 9.1 s | 408 KB | light, dark, sepia across the whole window |
| `asciipaste.gif` | 9.6 s | 228 KB | crooked box art squared up as it is pasted |
| `astext.gif` | 7.9 s | 268 KB | a diagram copied out as characters |
| `export.gif` | 8.5 s | 456 KB | format, scope, destination, the written file |
| `singlefile.gif` | 7.3 s | 120 KB | one `.md` opened with no vault |

## How to remake one

```bash
cmake -S . -B build -DKVIT_UI_DRIVER=ON
cmake --build build --target kvit-uidriver -j 8
tools/record-gallery.sh kanban       # one clip
tools/record-gallery.sh              # all fourteen, about fifteen minutes
```

The script stages a fresh vault at `/tmp/kvit-notes-demo/vault` for every
clip, from `screenshots/demo-vault/` with `screenshots/demo-vault-gallery/`
laid on top, and runs the driver under the VNC platform plugin with `HOME`
pointed at a scratch directory. It runs from the staged vault's parent
directory, because a file chooser opens on the working directory and a run
started in a checkout would put that checkout's path into the export clip.

Each clip is one `tour-*` scenario in the segment table in
`tools/uidriver.cpp`. A row holds the name, the caption, the function and
whether `tour-all` plays it; only the original five are in `tour-all`, since a
take of all fourteen would run for minutes and `tour-singlefile` cannot be in
one at all (it needs the process started on a file rather than on a vault).

Pacing is set at assembly time: `tools/make-gif.sh` divides every frame's
captured duration by the speed argument, and the house speed is 2. Tune there
rather than retuning dwells in a scenario.

## Decisions

- **The clips ship in the repository** (2026-08-08), about 3.2 MB in total.
- **The gallery is a repository document** rather than only a page on
  kvit.app (2026-08-08). The same content can feed the website later.
- **Clips play at 2x** (2026-08-08).
- **The extra notes the gallery needs live apart from the demo vault**, in
  `screenshots/demo-vault-gallery/`, so that adding a clip cannot change the
  four committed press stills, which are captured from the demo vault alone.
  Two notes are there: the kanban board and the crooked box art.
- **The five originally approved clips were re-recorded** through the new
  script, so the whole set comes from one command. Their content is
  unchanged; the note list in them now has one more row, the kanban board's
  note.
- **The owner reviewed the finished set and approved it** (2026-08-09), all
  fourteen clips, after the caption band came off and the ASCII-repair clip
  was added. The review the rules asked for has happened; a further change to
  a clip is a new decision rather than a continuation of this work.
- **Ordering leads with the Mermaid drag** and puts the two weakest, copy-as-
  text and export, late. A reader who stops after one section has seen the
  best thing.
- **No caption band on a gallery clip** (`--no-caption`). The band names the
  feature across the bottom of the frame, which is what a continuous video
  tour needs and what a clip sitting under its own heading and paragraph does
  not: it repeated the heading and cost every clip its first three seconds.

## Things that cost a run to find

These are all written into the comments at the point they matter in
`tools/uidriver.cpp`; this is the index.

- **`findChild` does not reach a ListView delegate's items.** An item declared
  in the shell's QML is a QObject child of the window; an item a delegate
  instantiated belongs to the delegate's creation context. Use `namedItem`,
  which walks the visual tree, and `namedItemIn` to scope the search to one
  delegate.
- **A menu entry has no object name.** The theme entries are built by a
  Repeater and a submenu's own row is built by Qt, so `itemWithLabel` finds a
  row by the words on it, with the `&` access-key marker stripped.
- **`tableGrid` is not the rows.** It is the column holding the rows, the
  row-window notice and the add-row chips, and it grows by the height of those
  chips as soon as a cell is being edited. Anything aiming at a cell measures
  against `tableFrame`, which is the rows alone. Aiming at the wrong one typed
  a new row's text into the row above it.
- **A wiki-link's position is not in the source.** The brackets are hidden
  while the caret is elsewhere, so the displayed text is what the layout knows
  about: find the word in `blockTextArea`'s `text` and ask
  `positionToRectangle` where that character is. `Text.linkAt` is not the
  mechanism here and returns nothing.
- **A kanban card is found by column, not by order.** `namedItemsIn` collects
  every `kanbanCard`; which column one is in is a horizontal position, read
  off the `kanbanColName` headers. Grab a card near its top edge, which is its
  drag handle rather than the editor a click in the middle opens.
- **Three columns are wider than a window with both side panes.** The kanban
  clip folds the note list away first, or the right-hand column runs off the
  frame.
- **`ffmpeg` eats standard input.** In `tools/check-image-leaks.sh` the clip
  list is piped into the loop that calls it, and without `-nostdin` ffmpeg
  swallowed the next few filenames: seven of thirteen clips were silently
  never scanned while the run reported clean.
- **A block appended at the end of a note is below the viewport**, and a
  virtualised list has no delegate at all for an off-screen index. `showBlock`
  calls `positionViewAtIndex` first for this reason. The same trap hides a
  closing "click the heading to take the caret away": if block 0 has scrolled
  out, `delegateAt(0)` is null and the click never happens.
- **A file written from inside the driver does not reach the file watcher.**
  The application ignores changes it made itself, and the driver is the
  application. `tour-query` spawns a separate process to edit front matter.
- **Chips are hover controls**, at zero opacity until the pointer is over the
  block.
- **The caret does not rest on a hidden syntax marker.** Set `cursorPosition`
  to the span's opening marker and step in with four Right presses.
- **Typing math meets the dollar auto-pair.** Append the closing `$` only if
  the content does not already end with one.
- **`QCursor::pos()` is not in any window under the VNC plugin.** `glide`
  replaces an implausible origin with the window centre.
- **A query result's rows are links.** Aim at the block's header strip, about
  `(90, 14)` in delegate coordinates.
- **A drag needs about two seconds and three pauses**, or the object appears
  to teleport.
- **A diagram block shows either its source or its render, never both.**

## Diagram repair does film, and how

An earlier round of this work concluded that the ingest repair could not be
shown and should not be attempted again. That was wrong, and the reason it was
wrong is worth keeping, because the same mistake is available to anyone
picking a demo input.

Two things had to be true at once. The before has to be on screen, and a paste
is repaired at the moment it lands, so a clip that pastes crooked art shows
only the straight result. The source therefore sits in the note in a fence the
ingest pass leaves alone: the eligible info strings are the empty one, `text`,
`plaintext` and `ascii`, and `screenshots/demo-vault-gallery/Pasted diagram.md`
uses `txt`. The clip copies from there into a block of its own, which leaves
the crooked original above the straightened copy.

The flaws also have to be ones the repair will actually take. It is
conservative by design and the tolerances are small: `growBox` only closes a
box when its bottom corners land within `kTol` (3) of its top corners, so a
box whose top edge is 2 short and whose bottom edge is 2 long differs by 4 and
is not recognised as a box at all — it was silently left alone in the first
two takes. The demo art now uses a top edge 2 short, a bottom edge 1 long, and
a connector tee 2 columns off its bar, and all three are repaired. Three
columns is about the largest flaw that both reads on screen and is inside the
tolerances.

## Deliberate weaknesses

- **Copy-as-text ends with the caret still in the pasted block.** Box-drawing
  characters are how a diagram fence is recognised, so that block renders as a
  second drawing the moment it stops being edited. Holding it open is the only
  way the characters themselves are on screen, and the clip and the paragraph
  both say so rather than papering over it.
- **Export stops at the status bar** rather than in a PDF viewer, and single-
  file mode shows the file's path rather than a file manager, because the
  recording session has nothing else installed in it.

## Still open

- `to-market.md` in the private tree (`~/kvit-notes-pro`) describes "paste
  crooked ASCII, get a repaired diagram" as a demo no competitor can show. The
  gallery now shows exactly that, so the line is fair, but it is worth
  checking against what `asciipaste.gif` actually demonstrates before it goes
  into launch copy: the characters are squared up, not redrawn as a picture.
- The three longest clips run 10.4 to 10.8 seconds (`query`, `search`,
  `wikilinks`). That is within the range the approved five set, so they were
  left alone, but they are where to cut first if the page feels slow.
- `theme.gif` and `export.gif` are the two large ones, at 408 and 456 KB,
  because both repaint most of the window. Lowering the capture rate for those
  two would trade smoothness for size if the total ever matters.
