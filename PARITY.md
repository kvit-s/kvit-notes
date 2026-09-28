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
rest of the app is step 6, in the order `PLAN.md` gives; the vault is in
`vault` and the window in `app`. Test names below are in
`cmd/kvit-notes/scenarios_test.go` (`TestScenarios/<name>`, which also runs
in the three other themes), in the `*_test.go` files of `editor`, `vault`
and `app`, in those of the ported packages (`mermaid`, `diagram`,
`textdiagram`, `mathtex`, `mathcmd`, `export` and the others), and in
kvit-ui-go's `platform` package for the tray.

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
    indentation, Ctrl+Enter leaves, Copy button, syntax colouring in Kvit's
    sixteen languages (the `highlight` package, with the Qt tests), the
    language menu (with Plain code, Mermaid and Text diagram), line numbers
    from View. Evidence: `20_caret_nav`, `31_code`, `visual_31_code_*`,
    `TestLanguageMenuDeclaresAndOptsOut`. Missing: horizontal scrolling (long
    lines wrap), the Ctrl+Enter hint in the footer, Tab to the next
    four-column stop over a selection
  - [x] 1.2.8 Image and media: an image line drawn as its picture (beside the
    note, from the top of the vault, or from the site folder for a path
    starting with "/"), at its width, with its caption; the line shown for
    editing while the caret is in it; a sound or video drawn as a card; a
    new picture copied into the vault's picture folder and named after the
    note. Evidence: `TestPicturesAreDrawn`, `TestAPictureFromOutsideIsCopiedIntoAssets`,
    `TestPictureFolders`. Missing: playing media, the lightbox, image effects
  - [x] 1.2.9 Divider. Evidence: `07_block_types`, `13_select`
  - [x] 1.2.10 Callout: Kvit's six types and any other, the header's fold
    arrow, type menu, title (edited in place) and colour dot, the "color"
    attribute. Evidence: `35_callouts`, `visual_35_callouts_*`, `TestCalloutsRoundTrip`
  - [~] 1.2.11 Table: drawn as a grid away from the caret, its Markdown with
    the caret in it; column alignment; inline formats in cells. Evidence:
    `TestTablesAreReadAsKvitReadsThem`. Missing: editing cells in the grid,
    sorting, the grid picker, column widths
  - [~] 1.2.12 Task board: the `kanban` fence drawn as columns and cards
    (labels, due dates, descriptions with their `$…$` math typeset), ticking
    a card, adding cards and editing them in place: a press on a card's text
    edits its line, where "#label" and "📅 date" are written as the file
    holds them, and a press on its description edits the description. Tab
    goes from the line to the description and out, Shift+Tab back, Enter
    keeps what was typed, Shift+Enter breaks a description's line, and
    Escape leaves the card as it was; both fields have the math typing aids
    of 1.2.15. Renaming, moving, folding, adding and deleting columns,
    moving a card to another column from its menu or by dragging it (with a
    line where it will go), deleting it, and the label and Hide done
    filters; the Markdown is the `kanban` package's port, which keeps every
    line it does not change. Evidence: `38_kanban`, `visual_38_kanban_*`,
    `TestCardDescriptionHasTheMathAids`,
    `TestCardLineHasTheMathAidsAndTabGoesToTheDescription`,
    `TestCellsAndCardsTypesetMath`. Missing: dragging columns (‹ and › move
    them), the card drawn under the pointer while dragged, the date picker
    and label chips for adding, the card details popover, the dates at a
    card's foot, a press under a card without a description opening an empty
    one (Tab from its line does). Escape drops what was typed, where the Qt
    card editor has already written it
  - [x] 1.2.13 Toggle: a callout of type "toggle", folded and opened by its
    arrow, the fold kept in the file. Evidence: `35_callouts`,
    `visual_35_callouts_03_toggle_collapsed`, `visual_35_callouts_04_toggle_expanded`
  - [~] 1.2.14 Embed: a web page's card, read only when Load preview is
    pressed (title, description and picture from its tags), its title
    opening the page. Evidence: `TestAnEmbedCardLoadsItsPreviewOnRequest`.
    Missing: Edit URL, dimensions, the video players' previews
  - [~] 1.2.15 Math: typeset by MicroTeX, the Qt app's engine, kept in C++
    (`third_party/microtex`) and built as a shared library that the
    `mathtex` package loads without cgo; without the library the TeX shows
    as source. A `$$ … $$` fence is a display equation, typeset centred away
    from the caret and edited as its TeX over a live preview, the error shown
    when it does not parse, Enter a line break and Ctrl+Enter a new
    paragraph, named in the block's corner. View, Equation numbers numbers
    the display equations at their right. Inline `$…$` is typeset in prose,
    table cells and task-board cards, a formula taller than its line making
    the line taller, and shows its source while the caret is in it or when
    it does not parse. Typing math: `$` puts in the closing dollar with the
    caret between (Backspace on the empty pair takes out both, Delete leaves
    a literal dollar, a second `$` steps over the closer, a selection is put
    between dollars); `\` in math opens the command menu, categories with
    typeset pictures on a bare backslash and a ranked list once letters
    follow (the `mathcmd` package, with the Qt tests); choosing puts the
    template in with the caret in its first slot, Tab and Shift+Tab move
    between slots, Ctrl+Space opens the list again; the commands chosen lead
    the menu, kept in `math.recentCommands`. Evidence:
    `TestMathFencesAreEquations`, `TestDisplayEquationShowsTypesetUntilEdited`,
    `TestPressOnEquationOpensItsTeX`, `TestEquationErrorsShowSourceAndMessage`,
    `TestEquationNumbersCountDisplayEquations`,
    `TestInlineMathIsTypesetAwayFromTheCaret`, `TestTallInlineMathGrowsItsLine`,
    `TestInvalidInlineMathStaysSource`, `TestCellsAndCardsTypesetMath`,
    `TestMathPrints`, `TestDollarAutoPairAndInlineMenu`,
    `TestMathCommandMenuPopupModes`, `TestMathCommandMenuInMathBlock`,
    `TestTabWalksTemplateSlots`, `TestCtrlSpaceCompletesAgain`,
    `TestBrowseTheCategories`, `TestTableCellMath`,
    `TestDisplayMathCtrlEnterLeavesTheBlock`, `TestMathRecentCommandsRoundTrip`,
    the `mathcmd` and `mathtex` tests, the storyboards `39_math`,
    `49_inline_math`, `36b_table_math` and `62_math_canary`, and
    `kvit-notes --math-selftest` run from the Windows and Linux packages.
    An inline formula away from the caret is heard as its TeX, with its
    lines, runs, caret and selection mapped onto that text (evidence:
    `TestInlineMathIsHeardAsItsTeX`). Missing: the PDF
    export draws no equation numbers; `[[` completion opens inside `$…$`,
    where the Qt app's does not. The macOS library is built and packaged but
    has not been loaded on a Mac
  - [x] 1.2.16 Drop cap: `dropcap=<lines>` with `dropcapcolor` and
    `dropcapfont`, from the / menu and the block menu, drawn as the Qt app
    approximates it (the paragraph indented, its first letter blank in place)
  - [x] 1.2.17 Diagrams: a `mermaid` fence is drawn natively. The `mermaid`
    package reads the flowchart, sequence, class, state and ER families by
    the mermaid@11.16.0 grammars (`flowchart-elk` as a flowchart, with a
    warning), keeping restricted syntax with a warning; `diagram` lays them
    out off the interface thread, with a cache and the Qt app's limits on
    nodes, edges, depth, labels and source size; any other family shows an
    "Unsupported" note over its source. Away from the caret the drawing fits
    the window (at most 720 px tall), with Fit, 100%, zoom out and in, the
    zoom level, panning, Copy, Copy as text (box-drawing characters from
    `textdiagram`), Reset layout, PNG (at twice the size, on the theme's
    background), Edit and As code (the fence retagged `plain`, one undo
    step). Editing shows the coloured source over a live preview that keeps
    the last good drawing and names the error's line and column; Tab puts
    in two spaces, Enter keeps the indentation, Ctrl+Enter leaves, named in
    the corner. On the drawing: selecting a node or an edge (Tab and the
    arrows go through the nodes, Escape clears) with its source line in the
    status bar, dragging a node (grid, guides, one `%% mermaid-flow:pos`
    line, curved edges), editing a label in place (double-click, F2), shape,
    colour and edge style from the context menu, renaming an id everywhere,
    deleting nodes and edges (chains split), drawing an edge from a side
    anchor, adding a connected node, and moving a sequence diagram's
    messages and participants (Ctrl+arrows, the menu, a drag); each gesture
    is one undo step that edits only its part of the source, and one whose
    result would not parse is refused with a message. `$$…$$` labels are
    typeset. A screen reader is told the drawing is an image named by what
    it shows or by the selection, with `accTitle` and `accDescr` as its
    description. Diagrams are drawn in the PDF export, and HTML export writes
    them for the pinned Mermaid module with their source. Character
    diagrams: an untagged, `text`, `plaintext` or `ascii` fence holding one
    is tagged `diagram` when a note is opened or Markdown pasted, and a
    diagram fence is straightened (box sides and connectors lined up by
    moving characters through spaces and edge fill only), also when text is
    pasted into a code block or Text diagram is chosen from the language
    menu; the one-time `.md.bak` follows the change, and a `plain` fence
    (Plain code, As code) is never tagged (the `textdiagram` package, with
    the Qt tests). Evidence: the `mermaid`, `diagram` and `textdiagram`
    tests, `TestDiagramCanvasSelectionAndLinking`,
    `TestZzy2DiagramFitFitsTallFlowchartAndShowsZoom`, `TestDiagramZoomAndPan`,
    `TestDiagramCopyCopyAsTextAndPNG`, `TestDiagramSavePNGWritesImage`,
    `TestDiagramResetLayout`, `TestDiagramAsCode`,
    `TestDiagramSwitchesBetweenDrawingAndSource`,
    `TestDiagramPreviewKeepsTheLastValidDrawing`, `TestDiagramSourceKeys`,
    `TestZzy5MermaidSourceCtrlEnterLeavesTheBlock`, `TestDiagramDragNodeArranges`,
    `TestDiagramEditsALabel`, `TestDiagramContextMenu`, `TestDiagramDeletes`,
    `TestDiagramDrawsAnEdgeFromAnAnchor`, `TestDiagramReordersASequence`,
    `TestDiagramReadOnlyRefusesGestures`, `TestDiagramMathLabels`,
    `TestDiagramTellsWhatItShows`, `TestDiagramPrints`,
    `TestDiagramHooksReachTheWindow`, `TestHTMLExport`,
    `TestIngestTagsCharacterDiagram`, `TestIngestStraightensDiagramFences`,
    `TestPasteIntoCodeBlockStraightensDiagram`,
    `TestDeclaringATextDiagramStraightensIt`, `TestAsCodeOptsOut`,
    `TestOpeningADiagramRetagsItAndTheFirstSaveKeepsABak`. Tested only at the
    source level: the colour and edge-style menu entries, and the sequence
    menu's moves; the caret and the preview marking each other's element is
    not tested
  - [x] 1.2.18 Collection query: the `query` fence's spec (the `query`
    package, with the Qt tests) over the vault's notes and their front
    matter, as a table or a board, its errors shown in the block, a row
    opening its note, worked out again as notes change. Evidence:
    `TestAQueryBlock`

## 2. Text editing and formatting

- [x] 2.1 Inline formatting: bold, italic, bold italic, strike, highlight,
  underline, inline code, links, wiki-links, bare URLs, nested spans. Evidence:
  `TestInlineSpans`, `03_types`, `04_nested`, `05_links`
- [x] 2.2 Markers shown only around the caret or selection (2.2.1–2.2.7). Evidence:
  `TestRevealFollowsCaret`, `TestClickMapsInsideSpan`, `01_reveal` (caret
  offsets after 14 and 17 presses of Right match Kvit's), `visual_01_reveal_*`,
  `visual_03_types_*`, `visual_04_nested_*`, `visual_05_links_*`
- [~] 2.3 Markdown syntax: everything listed, with superscript `^x^`,
  subscript `~x~` (no spaces inside, Pandoc's rule), text colour
  `<span style="color:…">` by Kvit's exact grammar, and inline math. The
  `[[note|alias]]` and `[[note#heading|alias]]` forms go to their note and
  heading. Evidence: `TestMarkdownRoundTrip`, `09_prefix`,
  `TestInlineSupSubMathColor`, `TestFollowingLinksAndTheLinkDialog`.
  Missing: away from the caret an aliased wiki-link shows its whole inside,
  where the Qt app shows only the alias
- [x] 2.4 Links: Ctrl+K inserts or edits a link (with a heading to link to,
  and Remove link); Ctrl+click, or a click in a block not being edited,
  follows it: a wiki link to the note its name resolves to (the `links`
  package's port), at its heading, a missing note made where the Qt app
  makes it, a shared name offering the notes to choose from; a web address
  in the browser; `#heading` in the note; `[[` offers the notes, and after
  "#" their headings. Evidence: `TestFollowingLinksAndTheLinkDialog`,
  `TestWikiLinkCompletion`
- [~] 2.5 Text selection: drag, double- and triple-click, Shift+arrows,
  Shift+click, across blocks, Ctrl+A. Evidence: `15_xsel`, `13_select`. Missing: the
  selection of its own that a block drawing its text has (an embed card,
  a collection query's results, a table of contents)
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
  `10_menu`, `29_block_menu`; Ctrl+Enter on selected blocks makes a
  paragraph after them with the caret in it, the keyboard's way below a table
  or a board, and "Copy as HTML" is in the block menu. Evidence:
  `TestCtrlEnterAfterSelectedBlocks`. Missing: a caret in the space between
  blocks before anything is typed (a paragraph is made at once), "Export" in
  the block menu

## 4. Slash commands and the block menu

- [x] 4.1 Activation, filtering, arrows, Enter, Escape, a press outside. Evidence:
  `10_menu`, `11_menu_flip`, `visual_10_menu_*`, `visual_11_menu_05`
- [~] 4.2 Contents: the kinds above, and image, table, callout, toggle,
  math (a display equation), Mermaid diagram, table of contents, task board,
  collection query and drop cap, with the Qt app's names, words and starter
  text. Missing: an embed from a typed address
- [x] 4.3 Behaviour: fuzzy matching, symbols, descriptions, groups, scrolling,
  placed under the caret or above it near the bottom. Evidence: `10_menu`, `11_menu_flip`

## 5. Clipboard

- [~] 5.1 Copy: text in a block, across blocks as Markdown, whole blocks,
  with HTML beside the Markdown on the clipboard. Evidence: `15_xsel`,
  `TestHTMLOnTheClipboard`. Missing: an internal format
- [x] 5.2 Cut, with the same formats. Evidence: `15_xsel`
- [~] 5.3 Paste: text at the caret; Markdown with blank lines becomes
  blocks, and so do several lines opening a code fence, which becomes the
  block the fence names (a character diagram in it tagged and straightened,
  as when a note is opened, in the same undo step); text pasted into a code
  block goes through that step too; HTML becomes Markdown (the export
  package's port of Qt's converter); Ctrl+Shift+V pastes plain text.
  Evidence: `TestHTMLOnTheClipboard`, `TestPastedFenceBecomesItsBlock`,
  `TestPasteIntoCodeBlockStraightensDiagram`,
  `TestCtrlVStraightensADiagramPastedIntoCode`. Missing: images, URLs,
  pasting after selected blocks, several lines of plain text becoming a
  paragraph each, and taking a fence from the plain text when the clipboard
  also holds HTML
- [~] 5.4 Drag and drop: blocks by their handle, Escape cancels. Missing:
  files, images and text from other applications

## 6. Undo and redo

- [x] 6.1–6.3 One history for the note; typing in one block merges while
  keystrokes are under 500 ms apart and under 20 characters; a click or a caret
  move ends the merge; every structural change is its own step. Evidence:
  `TestUndoMergesTyping`, `09_prefix`, `14_ops`, `16_drag`

## 7. Search and replace
- [x] 7.1 Find: Ctrl+F, matches in what the reader sees (across markers),
  the current one in its own colour, Enter and Shift+Enter, match case,
  whole word, regular expressions, an invalid pattern said so; Escape puts
  the caret at the match. A block selection or a cross-block text selection
  arms the "in selection" toggle, shown only then, which keeps the matches
  inside it. Evidence: `TestFindAndReplace`, `TestFindInSelection`
- [x] 7.2 Find and replace: Ctrl+H, Replace moving to the next match, All
  with the list of changes first, preserve case, one undo step, and Replace
  All inside the in-selection domain. Evidence:
  `TestFindAndReplace`, `TestFindInSelection`

## 8. Document organisation

- [~] 8.1 Folders: nested folders in the sidebar, opened and closed as
  `collection.json` records, their colours shown; new folder, rename, move to
  the trash; notes moved by dragging them onto a folder. Evidence:
  `TestTheWindowShowsTheVault`, `TestDragANoteOntoAFolder`,
  `TestCollectionStateRoundTrips`, and a colour chosen from the folder's
  menu. Missing: dragging folders
- [~] 8.2 Tags: a note's tags from its front matter, added and removed in the
  tag strip with the vault's tags offered as they are typed, the vault's
  tags with counts in the sidebar, their colours shown, and the note list
  filtered by one; renaming a tag on every note (merging when the new name
  is a tag already, after asking), deleting it from every note, and its
  colour, from its menu in the sidebar. Evidence: `TestNewNoteAndTags`,
  `TestFrontMatterEditsKeepForeignLines`, `TestTagsAreRenamedMergedAndDeleted`,
  `TestTagManagementFromTheSidebar`
- [~] 8.3 Note list: "Untitled N" notes named from their first block once it is
  finished, each row's title, snippet, date and word count as the Qt app
  derives them, sorting by modified, created or title either way (kept in
  the settings), pinned notes first, pin and favourite from the row's menu. Evidence:
  `TestAnUntitledNoteIsNamedAfterItsFirstBlock`, `TestPinFromTheNotesMenu`,
  `TestOpenScansNotesAndSkipsKvitsOwnFolders`; the Manual order in a
  folder, changed by dragging a note within the list and kept in
  `collection.json`; picking notes with Ctrl and Shift and pinning, marking,
  tagging or trashing them together. Evidence: `TestManualOrder`,
  `TestBulkActionsAndManualOrder`. Missing: renaming in the row itself (a
  dialog does it, with F2)
- [~] 8.4 Search across notes: the sidebar's field searches the index (the
  `search` package, kept up to date as notes change) within the scope shown,
  the results grouped by note with the lines found and a date menu, a line
  opening its note at the match. Evidence: `TestSearchAcrossNotes`,
  `TestTheWindowShowsTheVault`. Missing: recent searches
- [~] 8.5 Linked-note navigation: back and forward (Alt+Left, Alt+Right and
  the toolbar's arrows) and the quick switcher (Ctrl+P), which makes a note
  from words no note matches. Evidence: `TestBackForwardAndTheQuickSwitcher`.
  Following links and the backlinks pane (Ctrl+Shift+B, View), renames that
  update the links (Update links, Rename only, through
  `.kvit/redirects.json`). Evidence: `TestTheBacklinksPane`,
  `TestRenamingUpdatesLinks`

## 9. User interface

- [x] 9.1 Main layout: the sidebar, the note list, the editor and the
  outline side by side, resizable, their widths remembered; each side pane
  hidden on its own (its « button, View) and both with Ctrl+\\. Evidence:
  `TestTheWindowShowsTheVault`, `TestCtrlBackslashHidesTheSidePanes`,
  `TestPanesHideOneAtATime`
- [~] 9.2 Toolbar: File and View menus, back and forward, the block type
  list, the inline formats with superscript, subscript and text colour,
  alignment, and Insert, as flat buttons. Evidence:
  `TestTheToolbarChangesAndInsertsBlocks`, `TestANoteFromATemplate`.
  Missing: Link (with the link dialog), hiding groups from its menu
- [x] 9.3 Formatting bar: over a selection in one block, once it is made.
  Evidence: `TestTheFormattingBar`
- [x] 9.4 Gutter: + and × over the handle and the menu button, shown on hover,
  drawn as Kvit draws them. Evidence: `12_plus`, `visual_12_plus_01`
- [~] 9.5 Context menus: the block menu (with Align, Drop cap and Copy as
  HTML) and the text menu on right-click. Missing: Export in the block menu,
  the link menu
- [~] 9.6 Keyboard navigation: every editing action above has a key, and the
  menus have the Qt app's access keys (Alt+F, Alt+V, Alt+I for the toolbar's
  menus, a line's letter inside a menu). Evidence: `TestMenuAccessKeys`.
  Missing: F6 between panes
- [~] 9.7 Status bar: block, kind, path, counts, save state, the word count
  opening the statistics and the writing goal; hidden from View. Missing:
  the Qt layout of the bar, line and column in the vault window

## 10. Themes and appearance

- [x] 10.1 The four themes. Evidence: `TestScenariosInTheOtherThemes`
- [x] 10.2 Typography: family, base size, line height, block spacing, the
  maximum content width (centred) and the code family, from kvit-ui's
  typography settings. Evidence: `TestSettingsChangeTheEditor`
- [~] 10.3 Customisation: the settings dialog's Appearance (theme, accent and
  highlight colours, interface size, motion), Typography, General (the tray,
  where there is one) and This vault (the site and picture folders in
  `.kvit/settings.json`). The Go app keeps
  its own settings file, which starts as a copy of the Qt app's. Evidence:
  `TestSettingsChangeTheEditor`, `TestPictureFolders`. Missing: a list of the
  installed fonts (the family is typed), remote content and updates

## 11. Performance

- [x] 11.1 Only the rows in view are drawn; rows are measured once and cached. Evidence:
  `kvit-notes --bench`: 1,237 blocks open in 0.12 s, a wheel notch takes
  1.3–1.4 ms and a keystroke 3.2–3.7 ms, drawing included
- [ ] 11.2 Image optimisation
- [x] 11.3 Responsive editing, by the same measurement
- [~] 11.4 Targets: the Go figures are within the Qt targets above; memory in
  daily use not yet measured

## 12. Storage

- [~] 12.1 Local storage: a vault folder read and written in the Qt app's
  formats, atomic saves, the Qt app's lock on `.kvit/vault.lock` (flock, and
  the same byte range on Windows), a vault that cannot be written opened read
  only. Evidence: `TestTheVaultLockExcludesOtherOpeners`,
  `TestSaveBacksUpAndKeepsABakOnlyWhenTheEditorReshapes`; the scan leaves
  out what `.git/info/exclude`, each folder's `.gitignore` and the patterns
  set for the vault exclude (the `ignore` package, with the Qt tests), and
  what Windows marks hidden. Evidence: `TestTheScanFollowsGitignore`.
  Missing: the read-only mode said aloud
- [~] 12.2 Auto-save: two seconds after the typing stops, and on opening
  another note and closing the window. Evidence: `TestAutoSaveAfterTypingStops`,
  `TestEditingSavesTheNote`. Missing: the interval as a setting
- [x] 12.3 Manual save with Ctrl+S. Evidence: `TestEditingSavesTheNote`
- [x] 12.4 Backup and recovery: a backup in `.kvit/backups` before a save (at
  most one every ten minutes, the ten newest kept) and a one-time `.md.bak`
  when the editor rewrites a note's Markdown in its own form, both as the Qt
  app keeps them; the restore dialog showing each version; the recovery
  journal in `.kvit/recovery` offered back after an interruption; a note
  changed by another program reloaded, or the choice offered when there are
  unsaved changes; the trash shown read-only, put back or deleted for good.
  Evidence: `TestSaveBacksUpAndKeepsABakOnlyWhenTheEditorReshapes`,
  `TestRestoringAnEarlierVersion`, `TestUnsavedChangesAreOfferedBackAfterAnInterruption`,
  `TestTheJournalFollowsUnsavedChanges`, `TestAChangeByAnotherProgramReloadsTheNote`,
  `TestTheTrashShowsNotesReadOnlyAndPutsThemBack`, `TestTheRecoveryJournal`,
  `TestTheTrashCanBeListedRestoredAndEmptied`
- [x] 12.5 Export: the open note (as the editor holds it) or the whole
  vault, as Markdown, HTML in the theme's colours or plain text, one file a
  note or one combined file (the `export` package's port), with math
  written for MathJax and diagrams for the Mermaid module in HTML; and the
  open note as a PDF, drawn onto A4 pages by the editor itself in the light
  theme, with its fonts, its math typeset and its diagrams drawn. Evidence:
  `TestExportAndImport`, `TestPDFExport`, `TestHTMLExport`, `TestMathPrints`,
  `TestDiagramPrints`
- [x] 12.6 Import: files or a folder of Markdown and text, into the folder
  shown, after a summary; never over a note. Evidence: `TestExportAndImport`

## 13. Keyboard shortcuts

- [x] 13.1 Text formatting: Ctrl+B, I, U, E, Ctrl+Shift+S, Ctrl+K
- [x] 13.2 Block operations: Ctrl+D, Ctrl+Shift+D, Alt+Up/Down, Tab, Shift+Tab
- [x] 13.3 Block conversion: Ctrl+0–4, Ctrl+T, Ctrl+Shift+T
- [~] 13.4 General: undo, redo, save, select all, new note, Ctrl+\\, F11,
  Ctrl+P, Alt+Left and Right, Ctrl+Alt+N, Ctrl+F, Ctrl+H, Ctrl+Shift+B,
  and the list in File, Keyboard shortcuts

## 14. Accessibility

- [~] 14.1 Keyboard accessibility: all editing from the keyboard. Missing: the
  app's own focus order and chrome
- [~] 14.2 Screen readers: every block is an editable text named by its kind
  ("Heading 2 block"), a to-do has its state, the keyboard focus is
  reported on the block with the caret, with its text, lines, styled runs,
  caret and selection, and a screen reader can move the caret, select, replace
  text and tick a to-do; the gutter's controls are buttons. A diagram is an
  image named by what it shows, and a display equation a text holding its
  TeX; an inline formula away from the caret is heard as its TeX. Evidence: `TestBlocksAreEditableTextsNamedByKind`,
  `TestTheCaretsBlockReportsTextCaretAndRuns`, `TestScreenReaderActions`,
  `TestInlineMathIsHeardAsItsTeX`,
  `TestGutterControlsAreButtons`, `TestDiagramTellsWhatItShows`, the check
  for unnamed controls after every scenario, and on Windows through UI
  Automation with `./build.sh --win-check`. Not yet heard
  through NVDA, Narrator, VoiceOver or Orca
- [x] 14.3 Visual accessibility: the high-contrast theme, and colours from the
  theme's tokens only. Evidence: `TestSourceRules`

## 15–19. Integration and extras

- [x] 15.1 Quick capture: the window (File, Ctrl+Alt+N in the app), the note
  named from its first line and written in one write. Evidence:
  `TestQuickCapture`. The Qt app has no hotkey from other applications
  either: its `src/platform/globalhotkey.cpp` has no system backend, so the
  chord works only inside the app there too
- [x] 15.2 System tray: the icon with the tooltip "Kvit Notes" and the Qt
  app's menu (New Note, Quick Capture…, Show Kvit, Quit), a click on the icon
  showing the window, and `tray.closeToTray` in Settings, General (shown only
  where there is a tray), which makes closing the last window hide it with
  its vault open. The icon is kvit-ui's `platform.Tray`: Shell_NotifyIcon on
  Windows, StatusNotifierItem and dbusmenu on Linux (none under WSLg, which
  has no StatusNotifierWatcher, as for the Qt app), NSStatusItem on macOS
  (compiled, not yet run). New Note also shows the window, which the Qt app
  leaves where it is. Evidence: `TestTheTrayIconAndItsMenu`,
  `TestTheTrayMenuActsOnTheVaultWindow`,
  `TestClosingTheLastWindowHidesItInTheTray`,
  `TestClosingToTheTrayIsOnlyForTheLastWindowAndOnlyWhenAsked`,
  `TestQuitFromTheTrayEndsTheApp`, `TestTheTraySettingIsShownOnlyWithATray`,
  and on Windows `kvit-notes --tray-check` (the menu, a menu choice, a click
  and quitting)
- [x] One running copy: a second start hands its folder or file to the
  running app through a socket and exits. Evidence:
  `TestASecondCopyHandsOverToTheFirst`
- [x] 15.3 File associations: a file named on the command line opens on its
  own, a second start hands it to the running copy, and a Markdown or text
  file dropped on a window opens, in that window when it is one of the
  vault's notes. The packages associate `.md` files as the Qt app's do: the
  Windows installer's optional task writes the ProgID `KvitNotes.md` and
  `.md\OpenWithProgids` under HKCU (`packaging/windows/kvit-notes.iss`,
  tried through a test copy of the installer with a ProgID of its own,
  `packaging/windows/test-windows.sh`); the Linux `.desktop` file declares
  `text/markdown`; the macOS bundle's `Info.plist` declares Markdown
  documents, which reach the app through unison's `OpenFilesCallback`
  (compiled, not yet run on a Mac). Evidence:
  `TestPathsOpenAsOnTheCommandLine`, `TestASecondCopyHandsOverToTheFirst`,
  `TestDroppedNoteFilesOpen`
- [x] 15.4 Notifications: `platform.Tray.Notify`, with an ID that comes back
  when the notification is clicked and the four authorization states:
  Shell_NotifyIcon balloons on Windows, the desktop's notifications over
  D-Bus on Linux, UNUserNotificationCenter on macOS (compiled, not yet run).
  The Qt app posts none of its own (only its tests do), so this app posts
  none either. Evidence: kvit-ui's `TestNotificationsNeedAuthorizationAndTheIcon`,
  `TestClicksReachTheCallbacks`, `TestAuthorizationIsAskedOnce`, and on
  Windows the balloon `kvit-notes --tray-check` posts, shown as a toast
- [~] 16.1 Focus mode: F11 or View leaves the editor alone with its text in
  a centred column; Escape leaves. The window is maximised rather than made
  full screen, which unison v0.108.0 has no call for. Evidence: `TestFocusMode`
- [x] 16.2 Typewriter mode: the caret's line kept in the middle of the view,
  the other blocks faded
- [x] 17.1 Outline panel: headings by level, the current section
  highlighted, folding, the levels shown chosen, a heading gone to on a
  click. Evidence: `TestTheOutline`
- [x] 17.2 Table of contents block: kept in step with the headings in the
  file, drawn as a card whose entries go to their headings. Evidence:
  `43_toc`, `visual_43_toc_*`
- [x] 18.1 Note templates: New from template, the three built-ins written
  when a vault has none, `{{title}}`, `{{date}}`, `{{time}}` and Qt date
  formats, the tags and favourite mark kept, and the templates
  dialog. A second note from one template takes the next free name, where
  the Qt app refuses. Evidence: `TestANoteFromATemplate`,
  `TestTemplatesAreSeededOnceAndFilledIn`, `TestQtDateFormats`
- [x] 19.1 Statistics: words, characters with and without spaces,
  paragraphs, blocks and reading time for the note or the selection, and the
  words written this session. Evidence: `TestStatisticsAndTheWritingGoal`,
  `TestStatisticsCountWhatTheReaderSees`
- [x] 19.2 Writing goals: set from the status line, kept as `goal` in the
  front matter, shown as a share of the goal. Evidence:
  `TestStatisticsAndTheWritingGoal`

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
