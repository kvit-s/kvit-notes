# Kvit Notes — User Guide

This guide explains how to use Kvit Notes. It assumes no prior knowledge of the project and
reads on its own: it introduces what the editor is, then walks through every feature grouped
by task. If you have used Obsidian, Notion, or Bear, the shape will feel familiar; if you
have not, everything you need is described here.

## What Kvit Notes is

Kvit Notes is a markdown editor and notes application. It stores your writing as ordinary
markdown files in folders on your disk — there is no database and no proprietary format. A
note you write in Kvit opens correctly in any other markdown editor, and a markdown file
written elsewhere opens correctly in Kvit.

Its defining behavior is **hybrid editing**, also called live preview. In a plain markdown
editor you type `**bold**` and see the asterisks; in a fully rendered viewer you see **bold**
but cannot edit the source. Kvit does both at once: text is shown fully rendered, and the raw
markdown markers of a span appear only while your text cursor is inside that span. Move the
cursor away and the markers hide again, leaving clean rendered text. You are always editing
real markdown, but you rarely see the syntax unless you are working on it.

The document is built from **blocks**. Every paragraph, heading, list item, quote, code
block, table, image, and so on is an independent block that you can convert, move, duplicate,
and style on its own. This is what makes the `/` menu, block drag-and-drop, and per-block
styling possible.

## Getting started

Launch the application:

```bash
./build/kvit-notes
```

The first time you run it with no arguments, Kvit creates a notes collection at
`~/Documents/Kvit`, seeds it with a Welcome note, and opens it. On later runs it reopens the
note you last had open. You can also point Kvit at a specific location:

- `kvit-notes /path/to/folder` opens that folder as your notes root.
- `kvit-notes /path/to/note.md` opens a single file directly, without the collection panels
  — useful for a one-off file or a file association.

## The interface

The window has three panes, left to right:

1. **The sidebar** holds the global search field, the All Notes and Favorites scopes, the
   folder tree, and the tag list. Its footer shows the trash.
2. **The note list** shows the notes in the current scope, with each note's title, a text
   snippet, a date, and a word count.
3. **The editor** is where you read and write the open note.

Press **Ctrl+\\** to hide both side panels and give the editor the full window; press it
again to bring them back. You can also collapse each panel independently by clicking the
chevron in its header, and resize a panel by dragging the thin seam on its right edge.

## Working with notes

**Create a note** with **Ctrl+N**; it lands in the folder you currently have in scope.
**Rename** a note by pressing **F2** or double-clicking its title in the list — the file on
disk renames to match, because a note's file name *is* its title.

**Organize into folders** by dragging notes onto folders in the sidebar. Folders are real
directories on disk. Hover a folder for actions to create a subfolder, rename, or delete it,
and give it a color from the palette.

**Tag notes** from the tag strip above the editor: type in the add-field, and an
autocomplete popup filters the tags you already have or creates a new one on the fly. Tags
are stored in each note's front-matter, and the sidebar lists every tag with a count.
Clicking a tag filters the note list to notes carrying it, composing with the folder scope.
You can rename, merge, or delete tags from the tag list; a merge tells you how many notes it
will rewrite before you confirm.

**Pin and favorite** notes by hovering a row in the note list. Pinned notes float to the top
of any sort; favorites appear under the Favorites scope. **Sort** the note list by modified
date, created date, title, or a manual drag order, in either direction, from the list header.

**Select several notes at once** the same way you select files in a file manager: click to
open and select one, Ctrl+Click to toggle individual rows, Shift+Click to select a range. An
action bar then lets you pin, favorite, tag, or delete the whole selection, and dragging any
selected row carries all of them to a folder.

**Delete** a note or folder and it goes to the trash (`.kvit/trash/`), not to permanent
oblivion — you get a confirmation, and the sidebar footer shows how many items are in the
trash. Emptying the trash from that footer is the one permanent deletion in the application,
and it names the count before it acts.

## Linking notes together

**Wiki-links** connect notes: type `[[` and a completion popup lists your notes, filtered as
you type; Enter inserts the link and closes it with `]]`. The full grammar is
`[[note]]`, `[[note|shown text]]`, `[[note#Heading]]`, and `[[note#Heading|shown text]]` —
a bare name finds the note anywhere in the collection (case-insensitive, `.md` implied), and
you add path segments (`[[folder/note]]`) only when two notes share a name. Typing `#` after
the target narrows the popup to that note's headings; `[[#Heading]]` links within the
current note.

**Follow a link** by clicking it (or Ctrl+Enter with the cursor on it). A link to a note
that doesn't exist yet renders muted, and following it creates the note — a bare name in
the current note's folder, a path-qualified name at its own path.

**Backlinks**: toggle the backlinks pane with **Ctrl+Shift+B** (or View → Backlinks) to see
every note linking to the open one, with the lines the links appear on; it updates live,
including when files change on disk. **Rename safety**: renaming or moving a note through
the app rewrites the links that point to it in your other notes (aliases and heading anchors
preserved) and reports "Updated N links in M notes" in the status bar.

**Navigate your trail** with **Alt+Left / Alt+Right** (also the mouse back/forward buttons,
or the toolbar arrows) — history remembers scroll positions. The **quick switcher**
(**Ctrl+P**) jumps to any note by fuzzy name match; Shift+Enter there creates a note with
the typed name.

## Writing: the hybrid editing model

Type markdown as you normally would. As you type a recognized span, Kvit renders it and hides
the markers once your cursor leaves. Place the cursor back inside a rendered span and its
markers reappear, muted, so you can edit them. Selecting text, or placing the cursor at
either boundary of a span, also reveals it.

The recognized inline formats, with their markdown and shortcuts:

| Format | Markdown | Shortcut |
|---|---|---|
| Bold | `**bold**` or `__bold__` | Ctrl+B |
| Italic | `*italic*` or `_italic_` | Ctrl+I |
| Bold italic | `***both***` | (combine Ctrl+B and Ctrl+I) |
| Underline | `++underline++` | Ctrl+U |
| Strikethrough | `~~strike~~` | Ctrl+Shift+S |
| Highlight | `==highlight==` | (toolbar / formatting bar) |
| Superscript | `^sup^` | (toolbar / formatting bar) |
| Subscript | `~sub~` | (toolbar / formatting bar) |
| Inline code | `` `code` `` | Ctrl+E |
| Text color | `<span style="color:VALUE">…</span>` | (toolbar / formatting bar / menu) |
| Link | `[text](url)` | Ctrl+K |

A few rules worth knowing. The underscore variants (`_italic_`, `__bold__`) match only at
word boundaries, so `snake_case_names` stay literal. Superscript and subscript content must
have no spaces, following the Pandoc convention, so stray tildes and carets in ordinary prose
stay literal. Spans nest — `**bold with *italic* inside**` works — and inside inline code
nothing else parses. A bare `http(s)://…` URL becomes a clickable link automatically without
changing the stored text.

The shortcuts work on a selection (wrapping it in markers) or on a collapsed cursor (inserting
an empty marker pair so you can type inside), and pressing a shortcut on already-formatted
text removes that format. Highlight, superscript, and subscript have markdown syntax but no
keyboard shortcut, by the specification's design; reach them from the toolbar, the floating
formatting bar that appears over a selection, or the text right-click menu.

**Links.** Press **Ctrl+K** to open the link dialog — prefilled from your selection when
inserting, or from the link under the cursor when editing, with a button to remove the link
while keeping its text. While a block is being edited, a plain click places the cursor;
**Ctrl+Click** always opens the link. When a block is not focused, a plain click on a link
opens it, matching the reading state.

## Block types

Every block has a type, and you can convert between types freely. There are three ways to set
a block's type:

- **The `/` menu.** On an empty block, type `/` to open a floating menu of every block type,
  grouped and searchable. Keep typing to filter (`/h1`, `/code`, `/table`), use Up/Down to
  move the highlight and Enter to apply, or click an entry. Escape closes it and leaves your
  typed text intact.
- **The gutter plus-button.** Hover any block to reveal a `+` in the left gutter; click it to
  insert a new block below and open the menu for it.
- **Markdown prefixes.** Type a structural prefix at the start of a paragraph and it converts
  automatically: `- ` or `* ` for a bullet, `1. ` for a numbered item, `> ` for a quote,
  `# ` through `#### ` for headings, `` ``` `` for a code fence, `---` or `***` for a
  divider, and `- [ ] ` for a to-do. One Ctrl+Z restores the literal text you typed.

The available block types:

- **Paragraph** — plain body text.
- **Headings 1–4** — Ctrl+1 through Ctrl+3 reach the first three; Heading 4 is reached from
  the menu or the `#### ` prefix.
- **Bulleted list** — nests with Tab / Shift+Tab; the bullet glyph cycles disc, circle,
  square with depth.
- **Numbered list** — nests, and numbers itself automatically from document position.
- **To-do** — a checkbox you click (or toggle with Ctrl+Enter); completed items strike
  through. A to-do can carry a **due date** (a 📅 chip, turning red when overdue) and a
  **priority** (⏫ / 🔼 / 🔽), and a parent to-do shows its sub-tasks' progress.
- **Quote** — an accent bar and muted text, with optional **attribution** (a trailing
  `— name` line) and nesting via `>>`.
- **Code block** — a monospace panel with **syntax highlighting** for eleven languages, a
  language selector, a copy button, and an optional line-number gutter. Content is verbatim,
  so nothing inside a code block is interpreted as markdown.
- **Divider** — a horizontal rule, with configurable styles (see presentation below).
- **Callout / toggle** — a titled, tinted, foldable panel (info, note, tip, warning, danger,
  quote, or a plain toggle). The fold state is saved in the file. Ctrl+Enter folds it.
- **Table** — a pipe table edited cell by cell, with Tab to move across a row, add/remove of
  rows and columns, per-column alignment, and sort-by-column.
- **Image** — inserted from a file dialog, a paste, or a drag from your file manager or
  browser. The bytes are copied into an `assets/` folder beside your notes and linked by a
  relative path, so moving a note never breaks the image. Drag the corner to resize, click to
  zoom, and add an alt text and a caption.
- **Kanban board** — columns of cards with labels, due dates, and descriptions, with card
  drag between columns (or a "Move to column" control), stored as readable markdown.
- **Math block** — a `$$ … $$` LaTeX equation rendered centered; focus it to edit the source
  with a live preview. Inline math is `$x$` within a line: typing `$` inserts the closing `$`
  too with the caret between them (Backspace on the empty pair removes both; forward Delete
  removes just the closer when you meant a literal dollar sign). While editing math — either
  kind — typing `\` opens a command menu: browse symbols by category (Greek, arrows,
  relations, matrices, …) with each glyph rendered, or keep typing to autocomplete the
  command name; Enter or Tab inserts it, with the caret placed in the first argument slot
  and Tab hopping to the next. Ctrl+Space reopens completion for the command under the
  caret.
- **Mermaid diagram** — a ` ```mermaid ` fenced block rendered natively, entirely offline.
  Kvit implements a documented Mermaid-compatible subset of five families — flowcharts/graphs
  (all the flow.jison node shapes incl. `@{ shape: … }` data blocks, labelled/chained/invisible
  links, subgraphs, safe styling), sequence diagrams (participants/actors, every arrow form,
  activations, notes, loop/alt/opt/par/critical/break fragments, boxes, autonumber), class
  diagrams (compartment boxes, all UML relation ends with cardinalities, annotations,
  namespaces, notes), state diagrams (transitions, `[*]` start/end, composite states,
  fork/join/choice, notes), and ER diagrams (entity tables with keys and comments, crow's-foot
  cardinalities, roles) — each parser aligned with the pinned `mermaid@11.16.0` grammar (the
  full matrix lives in features.md §1.2.17). Focus the block to edit the source with a
  debounced live preview. While the new source is invalid the last good render stays on
  screen with a line/column diagnostic, and a deferred diagram family (gantt, pie, mindmap, …)
  falls back to editable source rather than a blank block — the Markdown is never discarded.
  Interactivity such as `click` is ignored with a warning. The read view offers
  **Fit**/**100%**/zoom, **Copy** source, **Copy as text** (put a Unicode box-drawing
  rendition of the rendered diagram on the clipboard — the same `┌─┐│└┘▼►` vocabulary the
  crooked-diagram repair recognizes, so a copied diagram pastes anywhere plain text goes and
  round-trips through Kvit's own repair untouched; flowcharts and sequence diagrams come out
  best, class/state/ER are best-effort with UML and crow's-foot markers simplified to
  `△ ◇ o < >`), **PNG** (save the rendered diagram as a 2× PNG image), and **As code**.

  **Flowcharts are directly editable on the rendered diagram**.
  Click a node or connection to select it (Tab/arrows cycle, Escape clears); the status bar
  shows its source line. Double-click (or F2/Enter) edits the label in place; drag a node to
  arrange it — the arrangement persists inside the fence as one `%% mermaid-flow:pos` comment
  (the obsidian-mermaid-flow format, ignored by browsers, removable with **Reset layout**);
  drag one of the selected node's round side-anchors onto another node to draw a connection,
  or click an anchor to add a new connected node. The right-click menu offers Rename id,
  Edit label, Shape, Edge style, Color, Add connected node, Delete, Reset layout, and Edit
  source. Every gesture rewrites only the touched span of the fence source — comments,
  formatting, and everything else stay byte-identical — as a single undo step, and a gesture
  that cannot keep the source valid is refused with an explanation. While the source is
  invalid (last-good preview), on-diagram editing is disabled except Edit source.
  Nothing in the diagram fetches the
  network, reads files, or runs scripts. HTML export ships the original source to the reader's
  browser with one pinned Mermaid.js module, so exported pages can render families the in-app
  preview does not yet support; PDF export embeds a natively-rendered raster. Insert one from the
  block menu (`/mermaid`).
- **Text diagram** — an ASCII/Unicode box-and-arrow drawing (the kind assistants often
  produce) stored in a `diagram`-tagged fence and shown as an ordinary code block, edited
  like any other text. What makes it special happens when the note is opened or pasted: an
  untagged fence (or one tagged `text`/`plaintext`/`ascii`) whose body is a high-confidence
  character diagram is retagged `diagram` automatically, and the diagram is straightened
  like the other LLM repairs — box edges that miss their corners by a column or two and
  connectors that jog sideways are aligned by swapping border characters with adjacent
  spaces, so labels and everything else stay exactly where they were; anything ambiguous is
  left as written. The first repairing save backs the file up to `.bak`, and a repaired
  paste is one undo step. Pick **Plain code** in the language menu to opt a fence out of
  diagram detection and repair for good, or **Text diagram** to opt it in.
- **Collection query** — a ` ```query ` fenced block rendering a live table or kanban-style
  board over the front-matter of all notes in the collection (insert with `/query`). The
  fence body is a small spec:

  ```
  from: projects/
  where: status = active
  view: table
  columns: title, status, due
  sort: due asc
  ```

  `where:` supports `=`, `!=`, `<`, `>`, `<=`, `>=`, `contains`, `has` (list fields), and
  `field exists`, with several conditions ANDed across lines or commas; comparisons are
  type-aware (dates as dates, numbers as numbers). Besides your own front-matter keys, the
  built-in fields `title`, `path`, `folder`, `modified`, `created`, `words`, and `tags` are
  queryable. `view: board` with `group-by: status` renders columns of cards instead of rows.
  Clicking a row or card opens that note. Results update live — edit any note's
  front-matter, in the app or in another program, and the block re-evaluates; only the spec
  is saved in the file, never the results. The view is read-only at this stage; a typo in
  the spec shows as an error message in the block rather than being silently ignored.
- **Audio / video** — a local media file rendered as a player with play/pause, a seek bar,
  and a volume control.
- **Embed card** — an `![](url)` pointing at a web page or video host renders as a preview
  card with a thumbnail, title, and description, opening the URL in your browser on click.

## Moving and manipulating blocks

**Select whole blocks** by clicking the drag-handle (the dots) in a block's left gutter.
Shift+Click extends the selection as a range, Ctrl+Click toggles individual blocks, and
Ctrl+Shift+Up/Down extends the selection from the keyboard. Ctrl+A is a ladder: the first
press selects the current block's text, the second selects every block.

With blocks selected, one keystroke acts on all of them, each as a single undo step:

- **Delete** or **Backspace** removes them.
- **Ctrl+D** duplicates them below.
- **Alt+Up / Alt+Down** moves them up or down.
- **Tab / Shift+Tab** indents or outdents list items.
- **Ctrl+C / Ctrl+X / Ctrl+V** copy, cut, and paste them as markdown, so they round-trip
  through the clipboard and into other editors.

**Reorder by dragging** a block's handle: a floating proxy lifts, the document makes room as
you move, and dropping commits the move. Dragging the handle of a selected block carries the
whole selection. Escape cancels a drag.

**Select text across blocks** by pressing in one block and dragging through others — the
selection is character-precise across boundaries. Double-click-drag snaps to words,
triple-press-drag to whole blocks. Shift+Arrows extend the selection across block edges.

## Clipboard

Copy (**Ctrl+C**) puts markdown on the clipboard: a fully selected span keeps its markers, a
partially selected one is reconstructed so it renders the same, and partial link text keeps
its target. Cut (**Ctrl+X**) removes exactly what copy would capture, so cut-and-paste
round-trips. Paste (**Ctrl+V**) inserts markdown at the cursor, splitting multi-line text
into blocks. **Paste-plain (Ctrl+Shift+V)** strips all formatting first.

## Finding and replacing

Press **Ctrl+F** to open the find bar in the editor's top-right corner. It highlights every
match live across all blocks and shows a "3 of 15" count. Enter and Shift+Enter (or F3 and
Shift+F3) step through matches with wrap-around, scrolling the current match into view.
Case-sensitive, whole-word, and regular-expression options compose; an invalid pattern shows
a recoverable error rather than breaking. Escape closes the bar and places the cursor at the
current match; the query survives so F3 resumes.

Press **Ctrl+H** to add the replace row. Replace substitutes the current match and advances;
Replace All opens a preview of every pending change first and applies only on confirm, as one
undo step. The replacement string is itself markdown, so replacing with `**loud**` produces
bold text. In regex mode you can reference capture groups (`$1`, `$&`). A "preserve case"
option adapts each replacement to its match's casing, and an "in selection only" option arms
automatically when you open the bar over a selection.

**Global search** across the whole collection lives in the sidebar's search field. Results
group by note with context snippets; clicking a result opens that note and hands off to the
find bar seeded at the exact occurrence. The folder scope, the active tag, and a
modified-date filter all narrow the results.

## The writing environment

**Document outline.** Press **Ctrl+Shift+O** to toggle a right-side outline pane that lists
the note's headings as a navigable, collapsible tree and highlights the section you are
currently in. Clicking a heading jumps to it.

**Table of contents.** Insert a TOC block (from the `/` menu) to place a live, clickable
heading list inside the note itself; it regenerates whenever the headings change.

**Internal links.** A link of the form `[text](#heading-name)` jumps to that heading within
the note. Ctrl+K offers a "link to heading" mode. A link to a heading that no longer exists
renders muted rather than breaking.

**Focus mode** (**F11**) hides the toolbar, panels, and status bar, goes full-screen, and
centers the text column for distraction-free writing; Escape exits. **Typewriter mode** keeps
the line you are editing centered on screen and fades the other blocks; the two modes compose.

**Statistics and goals.** Click the word count in the status bar for a live popover of word,
character, paragraph, and reading-time counts for the document or your selection, plus a
per-session word delta. Set a per-note **writing goal** and the status bar shows a progress
ring that turns green when you reach it.

**Templates.** The Templates menu creates a note from a template and manages your templates
(three built-ins are provided). A template is just a note, and it can expand `{{date}}`,
`{{time}}`, `{{title}}`, and `{{date:FORMAT}}` placeholders when you instantiate it. You can
also save the current note as a template.

**Export and import.** Export the open note, the note-list selection, or the whole collection
to **Markdown, HTML, PDF, or plain text**. HTML export is self-contained — theme styles,
images, and rendered math are embedded — and headings carry anchors so internal links work.
Import copies markdown or text files, a batch, or a whole folder tree into your collection,
preserving structure and front-matter (so importing an Obsidian vault is lossless). Ctrl+O in
collection mode offers open-standalone or import.

## Per-block presentation

Beyond content, individual blocks carry presentation options, stored as a small
`<!--kvit key=value-->` comment on the block's own line so a foreign markdown editor renders
the content and drops the comment invisibly:

- **Alignment** — left, center, or right for text and images, from a toolbar group or the
  Align context submenu.
- **Divider styles** — solid, dashed, dotted, or decorative, with thickness, color, and full
  or partial width.
- **Callout colors** — a custom accent color for a callout, recoloring the whole panel.
- **Image effects** — rounded corners, a drop shadow, a border, and aspect-ratio control.
- **Drop cap** — an enlarged initial letter on a paragraph, with its own color and font.
- **Embed dimensions** — a resizable embed card.

## Appearance and settings

Open settings with **Ctrl+,**. You can choose a **theme** — light, dark, sepia,
high-contrast, or "system" (which follows your OS light/dark setting live) — and override the
accent and highlight colors. **Typography** settings control the editor font, base size, line
height, block spacing, maximum content width, and the monospace font; one base-size slider
scales the whole document coherently. Everything you set persists between sessions.

The **toolbar** offers a block-type dropdown, formatting toggles that light up to reflect the
cursor's formatting, an insert menu, and a view menu; right-click it to show or hide button
groups. A **floating formatting bar** appears over a text selection. The **status bar** shows
your caret position, selection-aware word and character counts, and the last-saved time. All
of these can be toggled from the view menu.

## Accessibility

Kvit supports keyboard-only use: **F6** cycles between panes, dialogs trap and return focus,
and a visible focus ring marks the focused block. Screen readers receive names and roles on
controls, note rows, and blocks, image alt text is exposed, and a live region announces save
state, search counts, and mode changes. A **high-contrast theme** (white on black, strong
borders) and a **reduced-motion** setting are available for visual accessibility.

## Where your data lives

Your collection is a plain directory of markdown files. Everything is readable and editable
with any text editor:

- **Folders** are directories; **notes** are `.md` files whose name is the title.
- **Per-note metadata** (tags, created date, pinned, favorite, writing goal) lives in a YAML
  front-matter block at the top of each note. Front-matter from other tools is preserved
  byte-for-byte.
- **Collection-level state** (tag and folder colors, folder expand state, manual order, the
  last open note) lives in one `collection.json` inside a `.kvit/` directory.
- The `.kvit/` directory also holds the **trash**, rotating **backups** (taken before each
  overwrite; restore them from the ↺ button by the tag strip), a **crash-recovery journal**
  (which offers to restore unsaved work if the app was interrupted), the **templates**, and
  the **embed cache**. Images ingested into notes live in an `assets/` folder.

Every save is atomic, so an interrupted write never truncates a note, and auto-save writes
your changes a few seconds after you stop typing. If you edit a note in another program while
Kvit has it open, Kvit notices the external change and, if you also have unsaved edits, offers
to keep yours or load the other version rather than silently overwriting either.

## Keyboard shortcuts

**Text formatting**

| Action | Shortcut |
|---|---|
| Bold | Ctrl+B |
| Italic | Ctrl+I |
| Underline | Ctrl+U |
| Strikethrough | Ctrl+Shift+S |
| Inline code | Ctrl+E |
| Insert / edit link | Ctrl+K |

**Blocks**

| Action | Shortcut |
|---|---|
| Move block up / down | Alt+Up / Alt+Down |
| Duplicate block | Ctrl+D |
| Delete block | Ctrl+Shift+D |
| Indent / outdent | Tab / Shift+Tab |
| Convert to Paragraph / H1 / H2 / H3 | Ctrl+0 / Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Convert to To-do / Quote | Ctrl+T / Ctrl+Shift+T |
| Toggle to-do / fold callout | Ctrl+Enter |
| Open the block menu | `/` (on an empty block) |

**Navigation and documents**

| Action | Shortcut |
|---|---|
| Save | Ctrl+S |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Find / find & replace | Ctrl+F / Ctrl+H |
| Next / previous match | F3 / Shift+F3 |
| Select all (text, then blocks) | Ctrl+A |
| New note | Ctrl+N |
| Open / import | Ctrl+O |
| Toggle side panels | Ctrl+\\ |
| Toggle outline pane | Ctrl+Shift+O |
| Distraction-free (focus) mode | F11 |
| Settings | Ctrl+, |
| Quick capture | Ctrl+Alt+N |
| Shortcut reference | F1 |
| Cycle panes | F6 |

Press **F1** at any time for the in-app shortcut reference.
