# Step 6: the full Kvit Notes app in Go

This is the order of work for the plan's step 6 (`~/kvit-shirei/go-ui-plan.md`,
section 5): taking kvit-notes-go from the block editor, which step 3 finished,
to parity with the Qt app in `~/kvit-notes`. `PARITY.md` is the checklist
parity is judged by; this file says in what order its open items are done and
why. Each part below ends with its tests passing, `PARITY.md` updated with the
evidence, and an entry in `~/kvit-shirei/migration-log.md`.

The Qt app is 134k lines of C++ and 61k of QML, with 78k lines of tests. Its
Qt repository was tagged `qt-port-start` on 2026-09-27 at `918f539`; from then
on it takes fixes only, and `git log qt-port-start..` in `~/kvit-notes` lists
what the Go port must also absorb before parity.

## What stays the same

**The vault on disk.** A vault is a folder of Markdown notes with a `.kvit/`
directory of Kvit's own state beside them. The Go app reads and writes the
same files in the same formats (front matter, `collection.json`, trash,
backups, the recovery journal, templates, `assets/`), so the Qt and Go apps
can be used on the same vault in turn during the migration, and takes the
same lock, so they can never have one vault open at the same time. Caches under
`.kvit/cache/` are rebuildable, so the Go app may keep its own there.

**The settings.** kvit-ui's settings file (`ui.json`) is already shared with
the Qt app through kvit-ui-go. The app's own settings follow the Qt keys.

## Order of work

1. **The vault and the three-pane window.** Open a vault (a folder given on
   the command line, the last one used, or `Documents/Kvit`), scan it, and
   show Kvit's window: the sidebar (search field, All Notes, Favorites, the
   folder tree, tags, trash), the note list (sort, snippet, date, word count,
   pinned and favourite notes) and the editor with its tag strip. Opening and
   switching notes, auto-save, creating, renaming (including the automatic
   title from the first block), deleting to the trash, folders, and moving
   notes. Resizable and collapsible panes, Ctrl+\\. This is first because
   everything after it happens inside it, and because it is what makes the Go
   app usable for daily notes at all.
2. **Keeping work safe.** Backups before each overwrite, the crash-recovery
   journal, noticing a note changed by another program, restoring from the
   trash and from backups, a vault that cannot be written.
3. **Links and navigation.** Wiki-links resolved and followed, Ctrl+click on
   links, the link dialog (Ctrl+K), `[[` completion, back and forward, the
   quick switcher (Ctrl+P), the backlinks pane, and rename redirects.
4. **Finding things.** Find and replace in a note, and search across notes.
   The Qt app searches with SQLite's full-text index; whether the Go app uses
   a pure-Go SQLite or an index of its own is decided here (plan section 10
   lists it as unverified).
5. **The editor's remaining parts.** Code syntax colouring, the language menu
   and line numbers; the text context menu; the caret between blocks; HTML on
   the clipboard; then the other block kinds in order of use: images (and
   `assets/`), callouts, toggles, tables, divider styles, drop caps, embeds,
   task boards, collection queries, the table of contents, media. Math is a
   C++ helper program (MicroTeX) and diagrams a Go port of the Mermaid
   renderer (plan section 3).
6. **The rest of the app.** Settings, export (Markdown, HTML, PDF, plain text)
   and import, templates, focus and typewriter modes, the outline panel,
   statistics and writing goals, quick capture with its global hotkey, the
   tray icon, one running copy with several windows and vaults, file
   associations, notifications, and menu access keys.
7. **The Qt tests as the specification.** The Qt app's 113 test programs and
   its remaining storyboards (`tests/tst_visual.qml`), ported part by part
   alongside the work above, and finally the measurements for the migration
   log's before-and-after table and daily use on Windows until nothing is
   missed.

Parts 1 and 2 together are the point where the Go app can replace the Qt one
for writing notes. After that the order can bend to what the owner uses most.

## Where it stands

`PARITY.md` has the detail, item by item, with the test behind each. In
short, on 2026-09-27:

- **Part 1** is done, with manual order, picking several notes, colours,
  tag renaming and the `.gitignore` rules in the scan. Panes can be hidden
  one at a time and their widths are remembered.
- **Part 2** is done: backups and their restore dialog, the recovery journal,
  other programs' changes, the trash, read-only vaults.
- **Part 3** is done: following links, the link dialog, `[[` completion,
  back and forward, the quick switcher, backlinks, and renames that update
  links through `.kvit/redirects.json`.
- **Part 4** is done: find and replace in a note, and search across notes
  with an index of the Go app's own (the `search` package). No SQLite.
- **Part 5**: code colouring, the language menu and line numbers, the text
  menu and the formatting bar, superscript, subscript, text colour, images,
  embeds, callouts, toggles, tables (as grids), divider styles, drop caps,
  the table of contents, task boards, collection queries and HTML on the
  clipboard exist, with cards dragged between columns. Left: math rendering
  and Mermaid diagrams.
- **Part 6**: settings, export (PDF too) and import, templates, focus and
  typewriter modes, the outline, statistics and writing goals, quick capture
  (the Qt app's hotkey also works only inside the app), one running copy,
  menu access keys. Left: the tray icon with its notifications, and file
  associations, which need operating-system code unison does not have.
- **Part 7** has started: the ported packages carry the Qt tests of the
  logic they port, and 21 storyboards are replayed as scenarios.
