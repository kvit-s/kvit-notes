# Parity with Qt Kvit Notes

This is the checklist for kvit-notes-go reaching parity with the Qt/QML Kvit
Notes in `~/kvit-notes` (see `~/kvit-shirei/go-ui-plan.md`, section 6). It
follows the sections of the Qt repository's `features.md`, at the commit
`918f539` (2026-09-25).

Each line is marked:
- `[x]` done, with the evidence: a test, or a pair of screenshots from
  `./build.sh --shots` (Go below Kvit's storyboard image of the same name,
  in `build/shots/compare/`);
- `[~]` partly done, saying what is missing;
- `[ ]` not started.

The editor itself is the plan's step 3 and is in the `editor` package. The
rest of the app (vaults, the note list, search, export, settings, the
window's chrome) is step 6. Test names below are in
`cmd/kvit-notes/scenarios_test.go` (`TestScenarios/<name>`, which also runs
in the three other themes) and `editor/*_test.go`.

## 1. Block system

- [x] 1.1 Core block concept: a note is a flat list of blocks with a kind, an
  indent level and Markdown source. Evidence: `TestMarkdownRoundTrip`, `TestEnterAndBackspace`
- 1.2 Block types
  - [x] 1.2.1 Paragraph. Evidence: `01_reveal`, `visual_01_reveal_*`
  - [~] 1.2.2 Headings 1–4. Evidence: `07_block_types`, `visual_07_types_01`. Heading 1's
    row is 1 px taller than Qt's (see "Differences" below)
  - [x] 1.2.3 To-do with check box, click and Ctrl+Enter to tick. Evidence: `07_block_types`,
    `visual_07_types_02_todo_toggled`
  - [x] 1.2.4 Bulleted list, three bullet shapes by depth. Evidence: `07_block_types`, `08_listflow`
  - [x] 1.2.5 Numbered list, restarting when nested. Evidence: `07_block_types`
  - [x] 1.2.6 Quote. Evidence: `07_block_types`
  - [~] 1.2.7 Code block: monospace, whitespace kept, Enter keeps the line's
    indentation, Ctrl+Enter leaves, Copy button. Evidence: `20_caret_nav`,
    `visual_07_types_01`. Missing: syntax colouring, the language menu, line
    numbers, horizontal scrolling (long lines wrap), the Ctrl+Enter hint in the
    footer, Tab to the next four-column stop over a selection
  - [ ] 1.2.8 Image
  - [x] 1.2.9 Divider. Evidence: `07_block_types`, `13_select`
  - [ ] 1.2.10 Callout
  - [ ] 1.2.11 Table (kept verbatim as a raw block, so it survives a save)
  - [ ] 1.2.12 Task board
  - [ ] 1.2.13 Toggle
  - [ ] 1.2.14 Embed
  - [ ] 1.2.15 Math (display math kept verbatim)
  - [ ] 1.2.16 Drop cap
  - [ ] 1.2.17 Diagrams
  - [ ] 1.2.18 Collection query

## 2. Text editing and formatting

- [x] 2.1 Inline formatting: bold, italic, bold italic, strike, highlight,
  underline, inline code, links, wiki-links, bare URLs, nested spans. Evidence:
  `TestInlineSpans`, `03_types`, `04_nested`, `05_links`
- [x] 2.2 Markers shown only around the caret or selection (2.2.1–2.2.7). Evidence:
  `TestRevealFollowsCaret`, `TestClickMapsInsideSpan`, `01_reveal` (caret
  offsets after 14 and 17 presses of Right match Kvit's), `visual_01_reveal_*`,
  `visual_03_types_*`, `visual_04_nested_*`, `visual_05_links_*`
- [~] 2.3 Markdown syntax: everything listed except images and the
  `[[note|alias]]` and `[[note#heading]]` forms, which parse as wiki-links but
  are not resolved. Evidence: `TestMarkdownRoundTrip`, `09_prefix`
- [ ] 2.4 Links: Ctrl+K dialog, following links, completion of `[[`,
  resolving wiki targets
- [~] 2.5 Text selection: drag, double- and triple-click, Shift+arrows,
  Shift+click, across blocks, Ctrl+A. Evidence: `15_xsel`, `13_select`. Missing: the
  selection of blocks that draw rather than edit their text (none exist yet)
- [x] 2.6 Caret: blinking (steady with motion reduced), goal column, Home/End,
  Ctrl+Home/End to the ends of the note, word moves, Page Up/Down, the space
  below the last block. Evidence: `20_caret_nav`, `TestCtrlHomeAndEndReachTheEndsOfTheNote`

## 3. Block manipulation

- [x] 3.1 Block selection: handle click, Shift+click, Ctrl+click, Ctrl+A twice,
  Escape, Ctrl+Shift+Up/Down. Evidence: `13_select`, `visual_13_select_*`
- [~] 3.2 Reordering: drag by the handle with rows making room, one undo step,
  Escape cancels, Alt+Up/Down for a selection. Evidence: `16_drag`, `14_ops`,
  `visual_16_drag_*`. Missing: the animation of the moved row, dragging a
  multi-block selection by one handle
- [x] 3.3 Indentation: Tab and Shift+Tab, on a selection too, up to four levels
- [x] 3.4 Conversion: typed prefixes, the / menu, the block menu, Ctrl+0–4,
  Ctrl+T, Ctrl+Shift+T, as their own undo step. Evidence: `09_prefix`, `10_menu`,
  `29_block_menu`, `TestTypedConversionUndoesToLiteral`
- [x] 3.5 Deletion: Backspace and Delete rules, Delete/Backspace on a
  selection, the gutter's ×, the block menu. Evidence: `TestEnterAndBackspace`
- [x] 3.6 Duplication: Ctrl+D, below the original. Evidence: `14_ops`
- [~] 3.7 Creation: Enter, "/", the gutter's +, the block menu from its
  button, Shift+F10 and the Menu key, recently used kinds first. Evidence: `12_plus`,
  `10_menu`, `29_block_menu`. Missing: the caret in the space between blocks,
  Ctrl+Enter on selected blocks, "Copy as HTML" and "Export" in the block menu

## 4. Slash commands and the block menu

- [x] 4.1 Activation, filtering, arrows, Enter, Escape, a press outside. Evidence:
  `10_menu`, `11_menu_flip`, `visual_10_menu_*`, `visual_11_menu_05`
- [~] 4.2 Contents: the eleven kinds above. Missing: image, table, callout,
  toggle, task board, embed, math, diagram, collection query
- [x] 4.3 Behaviour: fuzzy matching, symbols, descriptions, groups, scrolling,
  placed under the caret or above it near the bottom. Evidence: `10_menu`, `11_menu_flip`

## 5. Clipboard

- [~] 5.1 Copy: text in a block, across blocks as Markdown, whole blocks. Evidence:
  `15_xsel`. Missing: HTML and an internal format beside the plain text
- [~] 5.2 Cut. Evidence: `15_xsel`. Same formats missing
- [~] 5.3 Paste: text at the caret, Markdown with blank lines becomes blocks.
  Missing: HTML, images, URLs, pasting after selected blocks, Ctrl+Shift+V
- [~] 5.4 Drag and drop: blocks by their handle, Escape cancels. Missing:
  files, images and text from other applications

## 6. Undo and redo

- [x] 6.1–6.3 One history for the note; typing in one block merges while
  keystrokes are under 500 ms apart and under 20 characters; a click or a caret
  move ends the merge; every structural change is its own step. Evidence:
  `TestUndoMergesTyping`, `09_prefix`, `14_ops`, `16_drag`

## 7. Search and replace

- [ ] 7.1 Find
- [ ] 7.2 Find and replace

## 8. Document organisation

- [ ] 8.1 Folders
- [ ] 8.2 Tags
- [ ] 8.3 Note list
- [ ] 8.4 Search across notes
- [ ] 8.5 Linked-note navigation

## 9. User interface

- [ ] 9.1 Main layout (one window with a stand-in toolbar and status line so far)
- [~] 9.2 Toolbar: block type and six inline formats as buttons. Missing: the
  File and View menus, superscript, subscript, link, colour, alignment, Insert
- [ ] 9.3 Formatting bar
- [x] 9.4 Gutter: + and × over the handle and the menu button, shown on hover,
  drawn as Kvit draws them. Evidence: `12_plus`, `visual_12_plus_01`
- [~] 9.5 Context menus: the block menu on right-click. Missing: the text
  menu (cut, copy, paste, formats) on a right-click in text
- [~] 9.6 Keyboard navigation: every editing action above has a key. Missing:
  menu access keys (9.6.1)
- [~] 9.7 Status bar: block, line and column, kind, counts, save state.
  Missing: the Qt layout of the bar

## 10. Themes and appearance

- [x] 10.1 The four themes. Evidence: `TestScenariosInTheOtherThemes`
- [~] 10.2 Typography: family, base size, line height and the code family are
  read from kvit-ui's typography settings. Missing: paragraph spacing and the
  maximum content width
- [ ] 10.3 Customisation

## 11. Performance

- [x] 11.1 Only the rows in view are drawn; rows are measured once and cached. Evidence:
  `kvit-notes --bench`: 1,237 blocks open in 0.12 s, a wheel notch takes
  1.3–1.4 ms and a keystroke 3.2–3.7 ms, drawing included
- [ ] 11.2 Image optimisation
- [x] 11.3 Responsive editing, by the same measurement
- [~] 11.4 Targets: the Go figures are within the Qt targets above; memory in
  daily use not yet measured

## 12. Storage

- [~] 12.1 Local storage: one Markdown file, read and written. Missing: vaults
- [ ] 12.2 Auto-save
- [x] 12.3 Manual save with Ctrl+S (in `cmd/kvit-notes`)
- [ ] 12.4 Backup and recovery
- [ ] 12.5 Export
- [ ] 12.6 Import

## 13. Keyboard shortcuts

- [~] 13.1 Text formatting: Ctrl+B, I, U, E, Ctrl+Shift+S. Missing: Ctrl+K
- [x] 13.2 Block operations: Ctrl+D, Ctrl+Shift+D, Alt+Up/Down, Tab, Shift+Tab
- [x] 13.3 Block conversion: Ctrl+0–4, Ctrl+T, Ctrl+Shift+T
- [~] 13.4 General: undo, redo, save, select all. Missing: the app-level ones
  (new note, find, quick switcher and so on)

## 14. Accessibility

- [~] 14.1 Keyboard accessibility: all editing from the keyboard. Missing: the
  app's own focus order and chrome
- [x] 14.2 Screen readers: every block is an editable text named by its kind
  ("Heading 2 block"), a to-do carries its state, the keyboard focus is
  reported on the block with the caret, with its text, lines, styled runs,
  caret and selection, and a screen reader can move the caret, select, replace
  text and tick a to-do; the gutter's controls are buttons. Evidence:
  `TestBlocksAreEditableTextsNamedByKind`,
  `TestTheCaretsBlockReportsTextCaretAndRuns`, `TestScreenReaderActions`,
  `TestGutterControlsAreButtons`, the check for unnamed controls after every
  scenario, and on Windows through UI Automation with `./build.sh --win-check`.
  Not yet heard through NVDA, Narrator, VoiceOver or Orca
- [x] 14.3 Visual accessibility: the high-contrast theme, and colours from the
  theme's tokens only. Evidence: `TestSourceRules`

## 15–19. Integration and extras

- [ ] 15.1 Quick-capture hotkey
- [ ] 15.2 System tray
- [ ] 15.3 File associations
- [ ] 15.4 Notifications
- [ ] 16.1 Focus mode
- [ ] 16.2 Typewriter mode
- [ ] 17.1 Outline panel
- [ ] 17.2 Table of contents block
- [ ] 18.1 Note templates
- [~] 19.1 Statistics: block, word and character counts in the status line
- [ ] 19.2 Writing goals

## Differences from Kvit's storyboard screenshots

- **Text is about 7% narrower.** Kvit's screenshots were taken on Linux, where
  Qt rounds each glyph's advance to a whole pixel; kvit-ui's text layer keeps
  the font's own advances, as Qt does on Windows with DirectWrite. Lines wrap
  at different words as a result.
- **Heading 1's row is 1 px taller** (39 px of text at 30 px against Qt's
  38), so every row below a Heading 1 sits 1 px lower.
- **The note is narrower by the scroll bar's strip.** kvit-ui's region keeps
  its scroll bar in a strip of its own; Kvit's document draws its bar over the
  text.
- **Selected text is dark on the accent**, where Kvit's is white: kvit-ui's
  theme chooses the label colour with more contrast against the accent.
- **After a drag across blocks the caret is where the drag ended**; Kvit leaves
  it in the block where the drag started.
- **The code panel** follows the current Qt code (a 26 px header, an 18 px
  footer), where the older storyboard images show a smaller panel.
- **Storyboard 30** drew an input method's composition at the caret; input
  methods are not a requirement of the Go apps (the owner's decision of
  2026-09-26), so `30_input_method_text` checks only the text an input method
  commits.
