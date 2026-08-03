# Manual QA checklist

Run per platform, per release candidate. Real Windows
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

       *Expect:* one process and one tray icon. The second and third
       launches forward their request to the running instance and exit, so
       no extra processes and no extra tray icons accumulate. A launch
       naming a different folder opens (or raises) that vault's window in
       the running instance; a bare launch raises the existing window.

       *Resolved (2026-07):* the single-instance channel
       (`src/platform/singleinstance.*`) routes every launch to the first
       process, which the window registry turns into open-or-raise (see
       ADR 0005's Update). The earlier "decision pending" pile-up is
       retired; confirm no extra processes accumulate.

2. [ ] **Hover chrome on every block type.** Hover slowly over each block
       type in turn, moving onto the block's own controls (the plus
       button, the drag handle, the gutter).

       *Expect:* the chrome appears once and stays steady while the
       pointer is anywhere over the block, including over the controls
       themselves. Buttons are clickable on the first click.

       *Known suspect:* the math block showed blinking chrome and an
       unclickable plus button because hover state came from a
       `MouseArea` that the chrome's own areas stole hover from. Every
       delegate now takes its chrome from the one `BlockGutter.qml` and
       folds that component's `hovered` back into its own hover state, so
       the stealing case is handled in one place rather than eleven — but
       the result has not been checked on real Windows for Embed, Image,
       Table, Diagram, Media, Query, Editable, Divider, Kanban or Toc.

3. [ ] **Right-click every block type.** Right-click the rendered body of
       each block, its drag handle, and its left gutter well below the
       handle, beside the last line of a tall block. Do the same on an
       indented list item, whose gutter is one indent step wider.

       *Expect:* the shared block menu opens, including Turn-into and
       Delete, so every block is removable by mouse alone. On a block that
       is part of a block selection, the selection menu opens instead.

       *Known suspect:* two routes reach the menu. The shared
       `BlockGutter.qml` handles the right button on the drag handle, and
       a `MouseArea` at the back of the list's content item (`main.qml`,
       beside the gap cursor) answers a right press anywhere else in the
       gutter for every block type. The rendered body is a separate
       question: Image, Diagram and Math wire right-button handling of
       their own, and a board's card opens its own menu, so right-clicking
       the body of a table, query, media card, embed card, TOC or divider
       — or a board anywhere other than a card — still does nothing.

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

       *Confirm the backend:* for the Wayland pass, launch once with
       `QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=wayland` and confirm the output
       loads `libqwayland.so` from inside the AppImage. For the X11 pass, use
       `QT_QPA_PLATFORM=xcb`. A Wayland login running the XCB plugin is an
       XWayland pass, not a native Wayland pass.

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

       *Also check the four blocks that edit a fenced source: code, math,
       Mermaid diagram, and collection query. Make each one the last block
       in the note, type into it, and press Ctrl+Enter.*

       *Expect:* while the caret is in the source, the block's bottom-right
       corner reads "Ctrl+Enter: new block"; Enter adds a line inside the
       block rather than leaving it; Ctrl+Enter keeps everything typed,
       makes a paragraph below, and puts the caret in it. Being last in the
       note is the case that matters: an arrow key has no neighbour to land
       on there, so a missing exit strands the caret.
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
6. [ ] Render one diagram of each Mermaid family. Insert a Mermaid block
       with `/mermaid` for each source below and type or paste the source
       into it — the ` ```mermaid ` markers here are this document's
       formatting, not part of what goes in the block. Then drag the
       flowchart's `C` node to a clear spot and click away.

       ```mermaid
       flowchart LR
           A([Start]) --> B{Vault set?}
           B -- yes --> C[Open collection]
           B -- no --> D[(Seed Welcome)]
           D --> C
           C --> E[/Render note/]
       ```

       ```mermaid
       sequenceDiagram
           autonumber
           participant U as User
           participant E as Editor
           participant S as Serializer
           U->>E: type a heading
           activate E
           E->>S: block changed
           S-->>E: markdown
           deactivate E
           Note over S: debounced save
       ```

       ```mermaid
       classDiagram
           class Block {
               +BlockType type
               +QString content
               +render() void
           }
           class CodeBlock {
               +QString language
           }
           Block <|-- CodeBlock
           Block "1" o-- "0..*" Attribute
       ```

       ```mermaid
       stateDiagram-v2
           [*] --> Idle
           Idle --> Editing: keypress
           Editing --> Saving: debounce
           Saving --> Idle: written
           Saving --> Conflict: file changed
           Conflict --> Idle: resolved
           Conflict --> [*]: discarded
       ```

       ```mermaid
       erDiagram
           COLLECTION ||--o{ NOTE : contains
           NOTE ||--o{ BLOCK : "is made of"
           NOTE }o--o{ NOTE : links-to
           NOTE {
               string title PK
               date created
               string tags "comma separated"
           }
       ```

       *Expect:* all five render as drawings, with no blank block, no
       diagnostic line, and no fallback to editable source. Each family
       carries something specific to judge by: the flowchart draws four
       distinct shapes (stadium, rhombus, cylinder, parallelogram) with
       `yes` and `no` on the branch edges; the sequence diagram numbers
       its messages, puts an activation bar on Editor, returns on a
       dashed arrow, and places the note over Serializer; the class
       diagram gives Block a three-compartment box, a hollow-triangle end
       on the inheritance line, and `1` / `0..*` beside the aggregation
       diamond; the state diagram has a filled start dot, an end target,
       and a label on every transition; the ER diagram shows crow's-foot
       ends and NOTE's attribute table with its `PK` marker and quoted
       comment.

       Three of these sources contain an edge whose ends are more than one
       rank apart — `B` to `C` in the flowchart, the two transitions back
       to `Idle` in the state diagram, and NOTE's relationship to itself.
       Each has to travel past whatever stands between its ends, so look
       at those three specifically: the line curves around the boxes in
       the way rather than crossing them, and its label sits in open space
       rather than printed over a box or over another label. This is the
       part of diagram layout that has broken before.

       *Expect, from the drag:* the node follows the pointer, and the fence
       source gains one comment line that starts `%% mermaid-flow:pos` and
       lists every node as `id=x,y` — read it back through the block's Copy
       source or its Edit source menu item. Nothing else in the source
       changes. The node is still where you left it after a reopen, and
       Ctrl+Z undoes the move in one step.
7. [ ] Paste a crooked ASCII diagram; accept the repair. Paste this into an
       empty paragraph, backtick fence included — the fence has no language
       on purpose, since detection is what tags it.

       ````
       ```
       ┌──────────┐
       │ Editor     │
       │ (QML)      │
       └────┬───────┘
             │
       ┌─────▼──────┐        ┌───────────┐
       │ Serializer │ ─────► │ Markdown    │
       │ blocks     │        │ file       │
       └────────────┘        └───────────┘
       ```
       ````

       Three flaws are built in: the first box's top edge stops two
       columns short of its own walls, the tee below it sits one column
       left of the connector it feeds, and the Markdown box's right wall
       juts out on both text rows.

       *Expect:* the block arrives already straightened, reading exactly

       ```
       ┌────────────┐
       │ Editor     │
       │ (QML)      │
       └─────┬──────┘
             │
       ┌─────▼──────┐        ┌───────────┐
       │ Serializer │ ─────► │ Markdown  │
       │ blocks     │        │ file      │
       └────────────┘        └───────────┘
       ```

       so the corners meet their walls and the tee, the connector and the
       arrowhead share one column. Label text keeps the column it was
       pasted at; only border characters move. The block is an ordinary
       editable code block with **Text diagram** ticked in its language
       menu, and the saved file holds it as a ` ```diagram ` fence of
       plain text. Ctrl+Z undoes the whole paste in one step, rather than
       peeling the repair off it or leaving a half-repaired drawing.

       Then repeat the paste the other way round: insert an empty code
       block with `/code`, click into it, and paste the same drawing on its
       own, without the fence lines.

       *Expect:* the same straightened result, again with **Text diagram**
       ticked and one Ctrl+Z to undo it. Then paste an ordinary function
       into a fresh code block and confirm it is left exactly as copied and
       still says **Plain text**: the detection has to claim drawings
       without claiming code.

       *Also check:* type a crooked box by hand into a code block. It stays
       exactly as typed, since the repair runs on text arriving from
       outside rather than on every keystroke. Choosing **Text diagram**
       from the language menu straightens it then and there.
8. [ ] Wiki-links: `[[` completion, follow, backlinks panel, quick
       switcher (Ctrl+P).
       *Expect:* completion lists existing notes, following opens the
       target, and the backlink appears on the other side.
9. [ ] Global search across the collection returns snippets; sidebar scope
       filters apply.
       *Expect:* results appear as you type, with matches highlighted in
       the snippet.
10. [ ] Export to HTML and to PDF from a note carrying every kind of
        rendering. Build the note first, so one export exercises all of it:
        prose with bold, a `==highlight==`, an inline `$x^2$` and a `$$…$$`
        block; a bulleted and a numbered list with one level of nesting; a
        to-do with a due date and a priority; a quote; a callout; a divider;
        a table with a formula in a header cell; a syntax-highlighted code
        block; a character diagram; a Mermaid diagram; a task board with a
        card carrying a label, a due date and a description; a query block in
        table view and another in board view; a local image; a remote image;
        an audio or video block; a web embed; a `[[wiki-link]]`; and a table
        of contents. Open the HTML in a browser and the PDF in a viewer.

        *Expect:* each block arrives as the thing the editor draws, never as
        the text it is stored as. Specifically: the query blocks are a table
        of matching notes and a board of cards, not their `from:`/`where:`
        specs; the task board's card shows its label and due date as chips
        with its description under the title; the embed is a link carrying
        the page title where the preview card had already fetched one; the
        Mermaid diagram is a drawing; the character diagram keeps its column
        alignment in a monospace font; math is typeset. Nothing runs off the
        right edge of the page, and highlighted text is legible — check that
        under a dark theme as well as a light one, since the export carries
        the theme's colours.

        *Known limits, so they are not reported as new defects:* an audio or
        video block exports as a link to its source path rather than a
        player, and that path is relative to the note, so it only resolves if
        the export sits beside the media. A `[[wiki-link]]` exports as plain
        text, since the export has no note to point it at. Per-block
        presentation set through the block's own menu — alignment, drop cap,
        divider style, image rounding and shadow, table column widths — is
        not carried into either format. A combined ("single file") export
        writes no per-note titles, so notes run together unless each body
        starts with its own heading. The PDF is printed by Qt's document
        engine, which runs no JavaScript and fetches nothing: it rasterizes
        the five Mermaid families the app draws natively (flowchart,
        sequence, class, state, ER) and prints any other family as source,
        and a remote image is blank in it. The HTML instead loads MathJax and
        Mermaid from a CDN, so its maths and diagrams need the viewer to be
        online.

        *Shortcut for the markup itself:* the unit suite writes a reference
        export of the blocks that render from something other than their own
        text to `build/screenshots/rich_blocks_export.html`. Open that in a
        browser when you want to judge the output without building the note
        by hand; it does not replace the pass above, which is the only place
        a real image, a real media file and a real embed are involved.
11. [ ] Open a ~1 MB / 100k-word markdown file.
        *Expect:* loads under a second, typing has no perceptible lag,
        and scrolling end-to-end stays smooth.
12. [ ] Switch all five themes; toggle high-contrast and reduced motion.
        *Expect:* every block type stays legible in each theme, with no
        invisible text or unstyled chrome.
13. [ ] Quit and relaunch.
        *Expect:* theme, typography, window geometry, and layout persist,
        and the crash journal is clean.
14. [ ] Single-file mode: open a lone `.md` via the file association with
        no vault configured.
        *Expect:* fast start, no collection chrome, math/diagrams/tables
        render, and the status-bar "Create vault from this folder…"
        affordance works.
15. [ ] In a table, type a formula into a header cell and into a body cell by
        typing `$`, then a backslash, then picking `\frac` from the menu and
        filling both slots with Tab. Click away from each. Type a sentence
        with spaces into a third cell.
        *Expect:* the `$` inserts its closing partner with the caret between
        them, the backslash opens the command menu, and Tab walks the
        template's empty slots, exactly as when typing a formula in a
        paragraph.
        Both cells show the rendered equation once the caret leaves them, and
        the equation survives clicking back in and out. Every space appears
        on the press that typed it, with no press that seems to do nothing.
        A header cell that grows to fit a formula keeps the header shading
        across the whole row, including the shorter cells beside it.
16. [ ] In a table cell, press Shift+Enter and type a second line, then press
        Enter. Reopen the note, and open it in a plain markdown editor.
        *Expect:* Shift+Enter breaks the line inside the cell and Enter is
        done with the cell rather than adding a line. The cell still shows
        two lines after the reopen, and the file holds the break as `<br>`
        with the row still on one line.
17. [ ] Paste an LLM answer whose table contains a fenced code block inside a
        row (an explanation with a code listing in one cell).
        *Expect:* the table renders as one table, with the listing in its own
        cell over several lines and its indentation intact. Nothing after the
        table is swallowed into a code block.
18. [ ] Render a Mermaid flowchart whose node label is `"$$\frac{a}{b}$$"` and
        whose edge label is `"$$x^2$$"`, and a sequence diagram with a formula
        as a message, a participant name and a note. Export both to PDF and to
        HTML.
        *Expect:* every formula is typeset rather than shown as `$$…$$`, each
        node box has grown to hold its formula, and a message arrow sits below
        its formula rather than through it. PDF shows the same, sharp at any
        zoom. HTML renders through Mermaid.js in the browser.
19. [ ] Drag the border between two table column headers, then reopen the
        note; drag the last column's right border too. Right-click a cell
        and choose "Reset column widths".
        *Expect:* the column follows the pointer, the width is still there
        after the reopen, and the reset restores content-measured widths.
        Ctrl+Z undoes a drag in one step.
20. [ ] On a task board, drag a card into another column and drop it between
        two cards there, then drag a column by its name past its neighbour.
        Click a card's text and type `#urgent` on its line; click under the
        title and type a two-line description with `$x^2$` and a
        `[[wiki-link]]` in it.
        *Expect:* the card follows the pointer with a line showing the slot
        it will take, and lands there; the column does the same. The typed
        label appears as a chip, the description shows both lines with the
        formula typeset and the link followable, and the file holds all of
        it as ordinary markdown. Ctrl+Z undoes each move in one step.
21. [ ] On the same card, click "+ tag" in the strip under the title, type the
        first letters of a label the board already uses and take it from the
        list; then click "+ due date" and pick a day from the calendar. Open
        the note in a plain markdown editor.
        *Expect:* the label and the date appear as chips, the card's foot says
        when it was added and when it was last changed, and the file holds the
        label and the date on the card's own line with the two dates in an
        HTML comment at the end of it. Clicking a day in the calendar does not
        also click the card behind it.
22. [ ] Select text with the mouse: press inside a paragraph and drag straight
        down over several of its lines, then across to the right, then on
        through the blocks below it. Do it again in a block you have just been
        typing in, and once in a code block. Then scroll the note with the
        wheel and by dragging the scrollbar.
        *Expect:* every drag highlights from the character it started on to the
        one under the pointer, in the block it started in as well as the ones
        it runs into. The list scrolls normally afterwards. The document does
        not scroll under a selection drag unless the pointer reaches the top or
        bottom edge of the viewport, where it scrolls to follow.
23. [ ] Open every chooser that drops out of a button while its block sits at
        the very bottom of a long note, so the space below the button is
        smaller than the list: a callout's kind and its colour dot, a
        divider's style, an image's effects, a to-do's due date, the label
        list on a task-board card, and the text colour on the formatting bar.
        Then shrink the window until it is barely taller than one of them and
        repeat.
        *Expect:* each one is wholly on screen, with its last row reachable.
        A chooser that will not fit below its button is moved up rather than
        cut off at the window edge, so it may overlap the block it belongs
        to.

## Distribution

Run against the installed artifact, never a build tree.

1. [ ] Clean install → launch → upgrade-in-place from the previous
       version → uninstall.
       *Expect:* the upgrade preserves settings and collections; the
       uninstall leaves no running process; `.md` association opens the
       app.
2. [ ] Signature verification: `signtool verify` (Windows, once signed),
       `spctl -a` and Gatekeeper first-open (macOS).
       *Expect:* the macOS Developer ID signature, notarization ticket and
       Gatekeeper assessment all pass. Windows remains unsigned as documented
       in the README.
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
