# Manual QA checklist

Run per platform, per release candidate (launch-plan.md B5). Real Windows
and macOS hardware runs this for **every** RC, not only the first; Linux
runs it on one X11 and one Wayland desktop session (non-WSL). Record each
pass as `docs/qa-runs/<version>-<platform>.md` with a checked copy of this
list, the artifact checksum, and any deviations.

## How to use this list

Run against the installed artifact with a fresh user profile, never a
build tree, unless a step says otherwise. Every item gives the action and
the expected result. Judge against the expected result rather than
against "nothing looked broken": several of the defects found so far are
visible only if you know what correct looks like.

Start with the platform watch-list. Those items are known-suspect on
specific platforms, either because automated tests caught them and no
human has seen them on real hardware, or because they cannot be tested
automatically at all. Everything below the watch-list is the standing
pass.

## Platform watch-list

### macOS

1. [ ] **Inline math spacing.** In a note, type each of these on its own
       line: `Text before $x^2$ text after`, `A fraction $\frac{a}{b}$
       inline`, and a line with three formulas in one sentence. Also put
       one inline formula inside a list item and one inside a heading.

       *Expect:* each rendered formula sits on the text baseline with
       normal word spacing on both sides. The following word starts just
       after the formula, neither overlapping it nor separated by a gap
       wider than a space.

       *Recently fixed, so worth confirming by eye:* the reservation used
       `QFont::setStretch()`, which is exact on Linux and was not on
       macOS, where `x^2` reserved 13.45 px against a rendered 16.00 and
       `\frac{a}{b}` reserved 1.47 against 12.00, leaving formulas to
       collide with the following text. It now uses an additive
       per-character advance, and the macOS unit tests pass. Automated
       tests only compare numbers, so confirm it looks right. Fractions
       and long TeX sources showed the worst error, so check those first,
       and change the content font size in Settings and re-check, since
       the reservation scales with it.

       *Also check:* click into a formula so the TeX source is revealed,
       then click away. The line should return to exactly its previous
       layout with no leftover gap.

2. [ ] **Sort by created time.** Make two notes: one with front matter
       `created: 2026-03-01`, one with no `created:` field that you then
       edit so its modification time is recent. Sort the note list by
       Created, then by Modified, and compare the two orderings.

       *Expect:* Created ordering reflects creation, so it differs from
       Modified ordering for the note you edited.

       *Known behavior, verified on macOS CI:* a note with no
       front-matter `created:` date takes its created time from the file.
       APFS reports a birth time, but setting a modification time earlier
       than it pulls the birth time back to match, so a note whose mtime
       moves backward also moves in the created ordering. A restore from
       backup, an rsync that preserves times, or a sync client are the
       realistic ways this happens to a user.

       *Also check:* copy a note file into the vault with `cp -p` from an
       old original, then look at the Created ordering.

       *Expect:* the copied note sorts by that old time rather than by
       when it arrived. Judge whether that reads as correct; if it does
       not, the fix is to prefer front-matter dates more aggressively,
       which is a product decision rather than a defect.

3. [ ] **Gatekeeper first open.** Launch the downloaded artifact before
       any `xattr` clearing.

       *Expect:* one Gatekeeper prompt, then the app opens and does not
       prompt again on later launches.

### Windows

1. [ ] **Second launch while the first is open.** Launch the app, close
       the window, then launch it again. Repeat three times, watching the
       tray overflow area.

       *Expect:* documented, decided behavior, whichever the owner picks.

       *Known issue, decision pending:* there is no single-instance
       guard, and Windows is the first platform where close-to-tray
       actually engages. Each launch starts a new process and each closed
       window parks another icon in the tray; three accumulated within
       minutes of ordinary use. It is data-safe, since the file watcher
       and conflict banner cover concurrent edits, but it reads as "the
       app will not close". Record how many processes accumulate.

2. [ ] **Hover chrome on every block type.** Hover slowly over each block
       type in turn, moving onto the block's own controls (the plus
       button, the drag handle, the gutter).

       *Expect:* the chrome appears once and stays steady while the
       pointer is anywhere over the block, including over the controls
       themselves. Buttons are clickable on the first click.

       *Known suspect:* the math block showed blinking chrome and an
       unclickable plus button because hover state came from a
       `MouseArea` that the chrome's own areas stole hover from. That one
       is fixed. Ten delegates still derive hover the same way and have
       not been checked on real Windows: Embed, Image, Table, Diagram,
       Media, Query, Editable, Divider, Kanban, Toc.

3. [ ] **Right-click every block type.** Right-click the rendered body of
       each block and its drag handle.

       *Expect:* the shared block menu opens, including Turn-into and
       Delete, so every block is removable by mouse alone.

       *Known suspect:* of the special delegates only Image, Diagram and
       Math wire right-button handling. Table, Kanban, Query, Media,
       Embed, Toc and Divider likely have no mouse route to the block
       menu.

4. [ ] **Fonts and emoji in the real GUI.** Open a note mixing prose,
       code, math, and color emoji.

       *Expect:* text renders through DirectWrite with correct weights,
       and color emoji are in color rather than monochrome outlines. CI
       proves nothing here, since the runner has no display.

5. [ ] **SmartScreen.** Launch the downloaded installer.

       *Expect:* one SmartScreen warning, cleared through "More info →
       Run anyway", matching what the README tells users. Builds ship
       unsigned for now.

### Linux

1. [ ] **Confirm the real GPU path.** Launch with `QSG_INFO=1` and read
       the `qt.rhi.general ... RENDERER:` line.

       *Expect:* the actual GPU, not `llvmpipe`. On the NVIDIA machine
       cross-check with `nvidia-smi`.

       *Why it matters:* the WSL development environment pins
       `GALLIUM_DRIVER=llvmpipe` because the d3d12 driver corrupts glyph
       rendering. That pin must not reach shipped launchers, and this is
       the check that proves it. If the session falls back to software or
       the integrated GPU, force real-GPU rendering with an HDMI dummy
       plug or a streaming session before judging any rendering item.

2. [ ] **Both session types.** Run the full pass once on Wayland and once
       on X11, from the same machine's login screen.

       *Expect:* no difference in behavior. Note the distribution version
       in the run record, because the AppImage baseline claim in the
       README is only as good as the oldest distribution actually tested.

3. [ ] **Tray on GNOME.** Check tray behavior with no extension
       installed.

       *Expect:* documented degradation rather than a silent failure. The
       app must remain quittable and reachable without a tray icon.

## Features

Run against the installed artifact with a fresh user profile.

1. [ ] First run opens the seeded Welcome collection.
       *Expect:* content is present and readable before any setup.
2. [ ] Create each block type from the `/` menu: heading, list, todo,
       quote, code, divider, table, kanban, callout, toggle, image, embed,
       math, diagram, drop cap.
       *Expect:* each inserts at the cursor and is immediately editable.
3. [ ] Paste an image from the clipboard; drag-drop an image file.
       *Expect:* both land in the note and survive a reopen, with the file
       written into the collection rather than linked from its origin.
4. [ ] Play an audio block and a video block.
       *Expect:* playback with working transport controls. On Windows this
       exercises the FFmpeg media backend.
5. [ ] Render a formula inline (`$...$`) and as a block (`$$...$$`).
       *Expect:* both render; entering the formula with the cursor reveals
       the TeX source, leaving it re-renders. See the macOS watch-list for
       inline spacing.
6. [ ] Render one diagram of each Mermaid family (flowchart, sequence,
       class, state, ER); drag a flowchart node.
       *Expect:* all five render natively, and the drag rewrites the
       markdown source rather than only moving pixels.
7. [ ] Paste a crooked ASCII diagram; accept the repair.
       *Expect:* columns align after repair and the source stays plain
       text.
8. [ ] Wiki-links: `[[` completion, follow, backlinks panel, quick
       switcher (Ctrl+P).
       *Expect:* completion lists existing notes, following opens the
       target, and the backlink appears on the other side.
9. [ ] Global search across the collection returns snippets; sidebar scope
       filters apply.
       *Expect:* results appear as you type, with matches highlighted in
       the snippet.
10. [ ] Edit the open note in another editor while the app has it open.
        *Expect:* the conflict banner offers keep-mine and load-theirs,
        and both choices do what they say.
11. [ ] Export the note to PDF and to HTML.
        *Expect:* both open, carry images, and render math. HTML math
        comes from MathJax in the browser, so check it online.
12. [ ] Open a ~1 MB / 100k-word markdown file.
        *Expect:* loads under a second, typing has no perceptible lag,
        and scrolling end-to-end stays smooth.
13. [ ] Switch all five themes; toggle high-contrast and reduced motion.
        *Expect:* every block type stays legible in each theme, with no
        invisible text or unstyled chrome.
14. [ ] Quit and relaunch.
        *Expect:* theme, typography, window geometry, and layout persist,
        and the crash journal is clean.
15. [ ] Single-file mode: open a lone `.md` via the file association with
        no vault configured.
        *Expect:* fast start, no collection chrome, math/diagrams/tables
        render, and the status-bar "Create vault from this folder…"
        affordance works.

## Distribution

Run against the installed artifact, never a build tree.

1. [ ] Clean install → launch → upgrade-in-place from the previous
       version → uninstall.
       *Expect:* the upgrade preserves settings and collections; the
       uninstall leaves no running process; `.md` association opens the
       app.
2. [ ] Signature verification: `signtool verify` (Windows, once signed),
       `spctl -a` and Gatekeeper first-open (macOS).
       *Expect:* verification passes, or the unsigned state matches what
       the README documents.
3. [ ] `kvit-notes --math-selftest` from the installed location.
       *Expect:* passes, and math renders in the app with the build tree
       absent. This is the relocatability check in packaged form.
4. [ ] Audio/video play from the installed resources.
       *Expect:* playback works with no source tree present.
5. [ ] Offline: full feature pass with networking disabled.
       *Expect:* everything works except the update check. Enumerate
       outbound requests and confirm only the update check appears, and
       only when enabled.
6. [ ] Hostile environments: non-ASCII username and collection path; long
       paths (Windows); a read-only collection file; case-colliding
       filenames.
       *Expect:* clear errors, never data loss or a crash. Case-colliding
       names deserve attention on macOS and Windows, whose filesystems are
       case-insensitive by default.
7. [ ] First run with no settings file; then with a corrupted settings
       file.
       *Expect:* the app recovers to defaults and never crashes.
8. [ ] Release hygiene: checksums published; THIRD-PARTY-NOTICES.md and
       the Qt license folder present inside the artifact; the artifact
       traceable to the CI run that built the tag.

## Sign-off

| Platform | Version | Date | Runner | Result |
|---|---|---|---|---|
| Windows | | | | |
| macOS | | | | |
| Linux X11 | | | | |
| Linux Wayland | | | | |
