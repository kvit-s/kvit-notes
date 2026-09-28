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
  - [x] 1.2.2 Headings 1–4. Evidence: `07_block_types`, `visual_07_types_01`. Heading 1's
    row is 1 px taller than Qt's (see "Differences" below)
  - [x] 1.2.3 To-do with check box, click and Ctrl+Enter to tick. Evidence: `07_block_types`,
    `visual_07_types_02_todo_toggled`
  - [x] 1.2.4 Bulleted list, three bullet shapes by depth. Evidence: `07_block_types`, `08_listflow`
  - [x] 1.2.5 Numbered list, restarting when nested. Evidence: `07_block_types`
  - [x] 1.2.6 Quote. Evidence: `07_block_types`
  - [x] 1.2.7 Code block: monospace, whitespace kept, Enter keeps the line's
    indentation, Ctrl+Enter leaves, Copy button, the Ctrl+Enter hint in the
    footer while the caret is in it, Tab padding to the next four-column stop
    (a selection over lines indenting or outdenting every line it touches),
    syntax colouring in Kvit's
    sixteen languages (the `highlight` package, with the Qt tests), the
    language menu (with Plain code, Mermaid and Text diagram), line numbers
    from View. Long lines do not wrap: the panel clips them, the caret
    follows along them, and the wheel (horizontal, or Shift+vertical) and a
    scrollbar under the panel move them, as Kvit's `codeChrome` does.
    Evidence: `20_caret_nav`, `31_code`,
    `visual_31_code_01_light`, `visual_31_code_02_line_numbers`,
    `visual_31_code_03_language_menu`,
    `visual_31_code_04_long_line_scrolled`,
    `TestLanguageMenuDeclaresAndOptsOut`, `TestTabStopsInCode`,
    `TestCodeFooterHint`, `TestCodeLongLinesDoNotWrap`,
    `TestCodeCaretScrollsIntoView`, `TestCodeBarDragScrolls`,
    `TestPrintingWrapsCode`.
  - [x] 1.2.8 Image and media: an image line drawn as its picture (beside the
    note, from the top of the vault, or from the site folder for a path
    starting with "/"), at its width, with its caption; the line shown for
    editing while the caret is in it; a sound or video drawn as a card opening
    externally; a new picture copied into the vault's picture folder and named
    after the note; the effects (rounded with its radius, shadow, border with
    an optional colour) from the block menu, drawn as Kvit draws them; a click
    on a resolved picture opening it full-size over the note (Escape or a press
    closing it). Evidence: `TestPicturesAreDrawn`, `TestAPictureFromOutsideIsCopiedIntoAssets`,
    `TestPictureFolders`, `TestImageEffectsRoundTrip`, `TestLightboxOpensAndCloses`,
    `TestBlockMenuHasExport`. Playing inline has no Go toolkit behind it, so a
    sound or video opens in the reader's player instead; see "Differences" below
  - [x] 1.2.9 Divider. Evidence: `07_block_types`, `13_select`
  - [x] 1.2.10 Callout: Kvit's six types and any other, the header's fold
    arrow, type menu, title (edited in place) and colour dot, the "color"
    attribute. Evidence: `35_callouts`, `visual_35_callouts_*`, `TestCalloutsRoundTrip`
  - [x] 1.2.11 Table: drawn as a grid (the header set apart, column
    alignment, inline formats and math in cells); a press makes a cell live
    for editing in place (Enter moves down the column, Tab walks the grid
    and adds a row past the last cell, Shift+Enter breaks the line as
    `<br>` so the row stays one line of the file, Ctrl+Enter leaves for a
    new block below, Escape leaves the cell, the arrows cross at the edges);
    a double press on a header sorts by it, again going the other way, with
    ▲ or ▼ and one undo step; the right-click menu inserts and deletes rows
    and columns, sorts, aligns and resets widths; a dragged border pins that
    column in the block's `cols` attribute; the / menu and Insert open the
    grid picker (3×3 first, arrows, Enter, Escape). Evidence: `36_tables`,
    `visual_36_tables_01_rendered`, `visual_36_tables_02_cell_editing`,
    `visual_36_tables_03_sorted`, `visual_36_tables_04_grid_picker`,
    `36b_table_math` (`visual_36_tables_05_inline_math`),
    `TestTablesAreReadAsKvitReadsThem`, the `tabledata` tests (Qt's
    `test_tabledata.cpp` ported), `TestTablePressMakesCellLive`,
    `TestTableCellCommitAndUndo`, `TestTableEscapeLeavesCell`,
    `TestTableTabWalksAndAddsRow`,
    `TestTableEnterMovesDownAndCtrlEnterLeaves`,
    `TestTableShiftEnterBreaksLine`, `TestTableArrowsMoveBetweenCells`,
    `TestTableSortTogglesAndMarks`, `TestTableHeaderDoubleClickSorts`,
    `TestTableStructureOps`, `TestTableColumnWidths`,
    `TestTablePickerInserts`, `TestTableCellMath`. Dragging from one cell
    to another sweeps its rectangle (one cell stays an ordinary press):
    Ctrl+C copies it as a table of its own, under its columns' headers,
    Ctrl+X copies and empties it, Backspace or Delete empties it, Escape or
    a press elsewhere drops it, and the right-click menu copies and clears
    it; a selection and a live cell are exclusive. Evidence: `36_tables`
    (`visual_36_tables_03b_cell_selection`), `TestTableSweepSelectsRectangle`,
    `TestTableSweepCopy`, `TestTableSweepCut`,
    `TestTableSweepDeleteAndEscape`, `TestTableSweepMenu`,
    `TestTableSweepEndsLiveCell`. A live cell shows Kvit's + Row / + Column
    buttons under the grid (one undo step each, the live cell kept for the
    next edit, named to screen readers). Evidence: `visual_36_tables_02_cell_editing`,
    `TestTableAddButtons`.
  - [x] 1.2.12 Task board: the `kanban` fence drawn as columns and cards
    (labels, due dates, descriptions with their `$…$` math typeset), ticking
    a card, adding cards and editing them in place: a press on a card's text
    edits its line, where "#label" and "📅 date" are written as the file
    holds them, and a press on its description edits the description. Tab
    goes from the line to the description and out, Shift+Tab back, Enter
    keeps what was typed, Shift+Enter breaks a description's line, and
    Escape leaves the card as it was; both fields have the math typing aids
    of 1.2.15. Renaming, moving, folding, adding and deleting columns, and
    moving a column by dragging its header (a click renames instead, with a
    gap indicator while dragged, Escape cancels) as well as with ‹ and ›.
    Moving a card to another column from its menu or by dragging it (with a
    line where it will go and the card drawn under the pointer, Escape
    cancels), deleting it, and the label and Hide done filters; the Markdown
    is the `kanban` package's port, which keeps every line it does not
    change. The days a card was added and last changed show at its foot, and
    a press under a card without a description opens an empty one. A strip
    under the title holds the card's labels and its due date, and is where
    both are set: each label chip removes itself, + tag adds one offering the
    board's labels for reuse, and the date chip and + due open a calendar
    for the due date. The card-details popover holds the labels, the due
    date (an invalid date refused, staying open), moving to another column
    and deleting the card, from the card's menu. Evidence: `38_kanban`,
    `visual_38_kanban_*` (column drag, ghost and label removal added),
    `TestCardDescriptionHasTheMathAids`,
    `TestCardLineHasTheMathAidsAndTabGoesToTheDescription`,
    `TestCellsAndCardsTypesetMath`, `TestCardFootDates`,
    `TestColumnDropIndex`, `TestColumnDragMovesColumn`,
    `TestColumnClickRenamesWithoutMoving`, `TestCardGhostFollowsPointer`,
    `TestLabelChipsAddRemoveAndOffer`, `TestChipPressRemovesLabel`,
    `TestTagFieldTakesHighlightAndTyped`, `TestSetCardDue`,
    `TestMonthGridMondayFirst`, `TestDuePickerPickAndClear`,
    `TestDueChipOpensPicker`, `TestCardDetailsApply`, `TestCardMenuHasDetails`.
    Escape drops what was typed, where the Qt card editor has already
    written it; see "Differences" below
  - [x] 1.2.13 Toggle: a callout of type "toggle", folded and opened by its
    arrow, the fold kept in the file. Evidence: `35_callouts`,
    `visual_35_callouts_03_toggle_collapsed`, `visual_35_callouts_04_toggle_expanded`
  - [x] 1.2.14 Embed: a web page's card, read only when Load preview is
    pressed (title, description and picture from its tags), its title
    opening the page; Edit URL from the block menu, the configured width and
    height from the block menu kept in the block's attributes, and a play
    badge over a video host's thumbnail opening externally. Evidence:
    `TestAnEmbedCardLoadsItsPreviewOnRequest`, `TestSetEmbedURLKeepsAlt`,
    `TestSetEmbedSizeAndVideoHosts`, `TestNormalizeEmbedURL`
  - [x] 1.2.15 Math: typeset by MicroTeX, the Qt app's engine, kept in C++
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
    `TestInlineMathIsHeardAsItsTeX`); the `[[` list stays shut inside
    `$…$` (evidence: `TestWikiMenuStaysShutInMath`); the PDF export numbers
    its equations with View, Equation numbers (evidence:
    `TestEquationNumbersPrint`). The macOS library is built and packaged but
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
- [x] 2.3 Markdown syntax: everything listed, with superscript `^x^`,
  subscript `~x~` (no spaces inside, Pandoc's rule), text colour
  `<span style="color:…">` by Kvit's exact grammar, and inline math. The
  `[[note|alias]]` and `[[note#heading|alias]]` forms go to their note and
  heading, and show only the alias away from the caret. Evidence: `TestMarkdownRoundTrip`, `09_prefix`,
  `TestInlineSupSubMathColor`, `TestFollowingLinksAndTheLinkDialog`,
  `TestWikiAliasShowsAlias`
- [x] 2.4 Links: Ctrl+K inserts or edits a link (with a heading to link to,
  and Remove link); Ctrl+click, or a click in a block not being edited,
  follows it: a wiki link to the note its name resolves to (the `links`
  package's port), at its heading, a missing note made where the Qt app
  makes it, a shared name offering the notes to choose from; a web address
  in the browser; `#heading` in the note; `[[` offers the notes, and after
  "#" their headings. Evidence: `TestFollowingLinksAndTheLinkDialog`,
  `TestWikiLinkCompletion`
- [x] 2.5 Text selection: drag, double- and triple-click, Shift+arrows,
  Shift+click, across blocks, Ctrl+A. Evidence: `15_xsel`, `13_select`; a block
  drawing its text carries a selection of its own over what it drew (an embed
  card, a collection query's results, a table of contents): drag for a character
  span, double-click for a word, a third click for a whole line, Ctrl+A for the
  block before the document, Ctrl+C to copy the text on screen as plain text
  (tabs between cells, newlines between lines), Escape to drop. Evidence:
  `TestDrawnTextOfTocQueryEmbed`, `TestDrawnWordAndLine`
- [x] 2.6 Caret: blinking (steady with motion reduced), goal column, Home/End,
  Ctrl+Home/End to the ends of the note, word moves, Page Up/Down, the space
  below the last block. Evidence: `20_caret_nav`, `TestCtrlHomeAndEndReachTheEndsOfTheNote`

## 3. Block manipulation

- [x] 3.1 Block selection: handle click, Shift+click, Ctrl+click, Ctrl+A twice,
  Escape, Ctrl+Shift+Up/Down. Evidence: `13_select`, `visual_13_select_*`
- [x] 3.2 Reordering: drag by the handle with rows making room, one undo step,
  Escape cancels, Alt+Up/Down for a selection; dragging the handle of a
  selected block moves the whole selection to the drop gap, one undo step
  with the selection following it. Evidence: `16_drag`, `14_ops`,
  `visual_16_drag_*`, `TestMoveBlocksToMovesARun`,
  `TestMultiBlockDragMovesTheSelection`. The moved row takes its new position
  directly without animating, as the surrounding rows do in Qt; see
  "Differences" below
- [x] 3.3 Indentation: Tab and Shift+Tab, on a selection too, up to four levels
- [x] 3.4 Conversion: typed prefixes, the / menu, the block menu, Ctrl+0–4,
  Ctrl+T, Ctrl+Shift+T, as their own undo step. Evidence: `09_prefix`, `10_menu`,
  `29_block_menu`, `TestTypedConversionUndoesToLiteral`
- [x] 3.5 Deletion: Backspace and Delete rules, Delete/Backspace on a
  selection, the gutter's ×, the block menu. Evidence: `TestEnterAndBackspace`
- [x] 3.6 Duplication: Ctrl+D, below the original. Evidence: `14_ops`
- [x] 3.7 Creation: Enter, "/", the gutter's +, the block menu from its
  button, Shift+F10 and the Menu key, recently used kinds first. Evidence: `12_plus`,
  `10_menu`, `29_block_menu`; Ctrl+Enter on selected blocks makes a
  paragraph after them with the caret in it, the keyboard's way below a table
  or a board, "Copy as HTML" is in the block menu, and "Export…" opens the
  export dialog scoped to the blocks. Evidence:
  `TestCtrlEnterAfterSelectedBlocks`, `TestBlockMenuHasExport`,
  `TestBlockExportOpensScopedDialog`. The blank space between two blocks takes
  a caret of its own: pointing at the seam draws a line, clicking turns it
  into a blinking caret, typing makes a paragraph holding it ("/" opens the
  block menu, Enter leaves it empty), Up/Down moves it, Escape leaves for the
  end of the block above, and Ctrl+V pastes there (flat text as paragraphs,
  structured keeping its types, plain stripped). Evidence:
  `TestGapCaretInsertsOnTyping`, `TestGapCaretKeys`,
  `TestGapCaretMovesAndLeaves`, `TestGapCaretSlashOpensTheMenu`,
  `TestGapCaretPaste`

## 4. Slash commands and the block menu

- [x] 4.1 Activation, filtering, arrows, Enter, Escape, a press outside. Evidence:
  `10_menu`, `11_menu_flip`, `visual_10_menu_*`, `visual_11_menu_05`
- [x] 4.2 Contents: the kinds above, and image, table, callout, toggle,
  math (a display equation), Mermaid diagram, table of contents, task board,
  collection query, drop cap and web embed, with the Qt app's names, words and starter
  text. A typed web address offers a Web Embed of that address first.
  Evidence: `10_menu`, `11_menu_flip`, `TestSlashMenuEmbedFromTypedAddress`
- [x] 4.3 Behaviour: fuzzy matching, symbols, descriptions, groups, scrolling,
  placed under the caret or above it near the bottom. Evidence: `10_menu`, `11_menu_flip`

## 5. Clipboard
- [x] 5.1 Copy: text in a block, across blocks as Markdown, whole blocks,
  with HTML beside the Markdown on the clipboard, and the internal
  `application/x-kvit-markdown` type beside those, so a copy pastes back as
  its text and never round-trips through the HTML converter. Evidence: `15_xsel`,
  `TestHTMLOnTheClipboard`, `TestInternalFormatPastesAsText`
- [x] 5.2 Cut, with the same formats. Evidence: `15_xsel`
- [x] 5.3 Paste: text at the caret; Markdown with blank lines becomes
  blocks, and so do several lines opening a code fence, which becomes the
  block the fence names (a character diagram in it tagged and straightened,
  as when a note is opened, in the same undo step); several flat lines become
  a paragraph each (plain stripped to display text); text pasted into a code
  block goes through that step too; HTML becomes Markdown (the export
  package's port of Qt's converter), except a payload whose plain text opens
  a fence uses that text so a copied fence does not arrive wrapped in a
  second code block; a lone URL over words links them; pasting after selected
  blocks inserts after them and selects the new blocks; a picture on the
  clipboard saves into the vault's picture folder as an image block;
  Ctrl+Shift+V pastes plain text. Evidence: `TestHTMLOnTheClipboard`,
  `TestPastedFenceBecomesItsBlock`, `TestPasteIntoCodeBlockStraightensDiagram`,
  `TestCtrlVStraightensADiagramPastedIntoCode`,
  `TestPastedFlatLinesBecomeParagraphs`, `TestPastedURLLinksTheSelection`,
  `TestPastedURLLinksThroughTheKeyboard`, `TestInsertAtInsertsBlocks`,
  `TestPasteAfterSelectedBlocks`, `TestPastedImageInsertsAnImageBlock`,
  `TestPastedPictureIsSavedAsAnImageBlock`, `TestFenceFromPlainTextBeatsHTML`
- [x] 5.4 Drag and drop: blocks by their handle, Escape cancels; files, images
  and text from other applications land where they are dropped (an image file
  as an image block, copied into the vault's picture folder first, a web
  address as an embed, text as blocks the way pasted text goes), with a drop
  indicator at the insertion row. Note files still open in the window.
  Evidence: `TestDropFilesSplitNotesImagesOther`,
  `TestCanAcceptExternalDeclinesNoteOnly`, `TestDropExternalInsertsImageAtIndex`,
  `TestDropExternalInsertsText`, `TestDropExternalLoneURLBecomesEmbed`,
  `TestDroppedNoteFilesOpen`

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

- [x] 8.1 Folders: nested folders in the sidebar, opened and closed as
  `collection.json` records, their colours shown; new folder, rename, move to
  the trash; notes moved by dragging them onto a folder, and folders moved by
  dragging them onto another folder (or All Notes for the top, never into
  themselves). Evidence: `TestTheWindowShowsTheVault`,
  `TestDragANoteOntoAFolder`, `TestDragAFolderOntoAFolder`, `TestMoveFolder`,
  `TestCollectionStateRoundTrips`, and a colour chosen from the folder's
  menu
- [x] 8.2 Tags: a note's tags from its front matter, added and removed in the
  tag strip with the vault's tags offered as they are typed, the vault's
  tags with counts in the sidebar, their colours shown, and the note list
  filtered by one; renaming a tag on every note (merging when the new name
  is a tag already, after asking), deleting it from every note, and its
  colour, from its menu in the sidebar. Evidence: `TestNewNoteAndTags`,
  `TestFrontMatterEditsKeepForeignLines`, `TestTagsAreRenamedMergedAndDeleted`,
  `TestTagManagementFromTheSidebar`
- [x] 8.3 Note list: "Untitled N" notes named from their first block once it is
  finished, each row's title, snippet, date and word count as the Qt app
  derives them, sorting by modified, created or title either way (kept in
  the settings), pinned notes first, pin and favourite from the row's menu,
  and renaming in the row itself with F2 (a field over the row, Enter keeps
  it, Escape leaves it). Evidence:
  `TestAnUntitledNoteIsNamedAfterItsFirstBlock`, `TestPinFromTheNotesMenu`,
  `TestOpenScansNotesAndSkipsKvitsOwnFolders`, `TestRenameInRow`; the Manual order in a
  folder, changed by dragging a note within the list and kept in
  `collection.json`; picking notes with Ctrl and Shift and pinning, marking,
  tagging or trashing them together. Evidence: `TestManualOrder`,
  `TestBulkActionsAndManualOrder`
- [x] 8.4 Search across notes: the sidebar's field searches the index (the
  `search` package, kept up to date as notes change) within the scope shown,
  the results grouped by note with the lines found and a date menu, a line
  opening its note at the match, and recent searches under the empty field,
  kept newest-first in the settings. Evidence: `TestSearchAcrossNotes`,
  `TestTheWindowShowsTheVault`, `TestRecentSearches`
- [x] 8.5 Linked-note navigation: back and forward (Alt+Left, Alt+Right and
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
- [x] 9.2 Toolbar: File and View menus, back and forward, the block type
  list, the inline formats with superscript, subscript and text colour, Link
  (with the link dialog), alignment, and Insert, as flat buttons, each group
  hidden from its menu and kept in the settings. Evidence:
  `TestTheToolbarChangesAndInsertsBlocks`, `TestANoteFromATemplate`,
  `TestToolbarGroupsHide`
- [x] 9.3 Formatting bar: over a selection in one block, once it is made.
  Evidence: `TestTheFormattingBar`
- [x] 9.4 Gutter: + and × over the handle and the menu button, shown on hover,
  drawn as Kvit draws them. Evidence: `12_plus`, `visual_12_plus_01`
- [x] 9.5 Context menus: the block menu (with Align, Drop cap, Copy as
  HTML and Export, which opens the export dialog scoped to the blocks) and
  the text menu on right-click, with Link…; a press on a link opens the link
  menu (Open link, Edit link…, Remove link). Evidence: `TestBlockMenuHasExport`,
  `TestBlockExportOpensScopedDialog`, `TestLinkMenuItems`, `TestRemoveLinkAt`
- [x] 9.6 Keyboard navigation: every editing action above has a key, F6 moves
  between the panes drawn, and the
  menus have the Qt app's access keys (Alt+F, Alt+V, Alt+I for the toolbar's
  menus, a line's letter inside a menu). Evidence: `TestMenuAccessKeys`,
  `TestF6CyclesPanes`
- [x] 9.7 Status bar: block, kind, line and column, path, counts, save state with its
  last-saved time, the word count opening the statistics and the writing goal;
  hidden from View. In the Qt app's order (qml/EditorStatusBar.qml): the save
  state with its dot and time (just now, N min ago, hh:mm), Block N \u00b7 Ln X,
  Col Y, the block type, the file path, the block count, and the word and
  character counts with the goal; the passive update notice leads when a newer
  release is found, and a lone file offers its folder as a vault. Evidence:
  `TestStatusShowsLineAndColumn`, `TestStatusFollowsQtOrderWithSavedTimeAndUpdateNotice`

## 10. Themes and appearance

- [x] 10.1 The four themes. Evidence: `TestScenariosInTheOtherThemes`
- [x] 10.2 Typography: family, base size, line height, block spacing, the
  maximum content width (centred) and the code family, from kvit-ui's
  typography settings. Evidence: `TestSettingsChangeTheEditor`
- [x] 10.3 Customisation: the settings dialog's Appearance (theme, accent and
  highlight colours, interface size, motion), Typography (with a list of the
  installed fonts beside the family field), General (the tray where there is
  one, the auto-save wait in seconds, remote content and the daily update
  check) and This vault (the site and picture folders in
  `.kvit/settings.json`). The Go app keeps its own settings file, which starts
  as a copy of the Qt app's. Remote content follows the Qt keys
  (`network.autoLoadRemoteContent`, off by default, with per-origin approval in
  `network.allowedOrigins`): opening a note is not consent, a remote picture
  offers Load from its origin, Load preview approves its page's origin, an
  unfetchable address fails with its reason, redirects ask again except the
  same-site hop, and special-use addresses never connect. The update check is
  the Qt one (`updates.checkEnabled`, opt-out daily, `updates.lastCheck`): one
  daily read of the releases feed, no telemetry, no download, a passive
  status-bar notice opening its page, never a popup. Evidence:
  `TestSettingsChangeTheEditor`, `TestPictureFolders`, `TestInstalledFontsListed`,
  `TestOriginOf`, `TestEgressPolicyDefaultsToClosed`,
  `TestEgressPolicyPerOriginConsent`, `TestSameSiteRedirect`,
  `TestAddressIsBlocked`, `TestUpdateCheckIsOptOutDailyAndPassive`,
  `TestCompareVersions`, `TestParseLatestPayload`,
  `TestRemotePictureNeedsApprovalThenLoads`

## 11. Performance

- [x] 11.1 Only the rows in view are drawn; rows are measured once and cached. Evidence:
  `kvit-notes --bench`: 1,237 blocks open in 0.12 s, a wheel notch takes
  1.3–1.4 ms and a keystroke 3.2–3.7 ms, drawing included
- [x] 11.2 Image optimisation: pictures load lazily when drawn, are downscaled
  to the display width (1400 px) on load, and decoded pictures are kept over a
  64 MB budget, oldest first. Evidence: `TestLargePicturesAreDownscaledOnLoad`,
  `TestImageCacheEvictsOverBudget`
- [x] 11.3 Responsive editing, by the same measurement
- [~] 11.4 Targets: the Go figures are within the Qt targets above; memory in
  daily use not yet measured

## 12. Storage

- [x] 12.1 Local storage: a vault folder read and written in the Qt app's
  formats, atomic saves, the Qt app's lock on `.kvit/vault.lock` (flock, and
  the same byte range on Windows), a vault that cannot be written opened read
  only and said aloud. Evidence: `TestTheVaultLockExcludesOtherOpeners`,
  `TestSaveBacksUpAndKeepsABakOnlyWhenTheEditorReshapes`,
  `TestAVaultThatCannotBeWrittenOpensReadOnly`, `TestReadOnlyAnnounced`; the scan leaves
  out what `.git/info/exclude`, each folder's `.gitignore` and the patterns
  set for the vault exclude (the `ignore` package, with the Qt tests), and
  what Windows marks hidden. Evidence: `TestTheScanFollowsGitignore`.
- [x] 12.2 Auto-save: after the typing stops (two seconds unless the settings
  name another wait), and on opening
  another note and closing the window. Evidence: `TestAutoSaveAfterTypingStops`,
  `TestEditingSavesTheNote`, `TestSaveIntervalSetting`
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
  theme, with its fonts, its math typeset and its diagrams drawn, numbered
  with View, Equation numbers. Evidence:
  `TestExportAndImport`, `TestPDFExport`, `TestHTMLExport`, `TestMathPrints`,
  `TestDiagramPrints`, `TestEquationNumbersPrint`
- [x] 12.6 Import: files or a folder of Markdown and text, into the folder
  shown, after a summary; never over a note. Evidence: `TestExportAndImport`

## 13. Keyboard shortcuts

- [x] 13.1 Text formatting: Ctrl+B, I, U, E, Ctrl+Shift+S, Ctrl+K
- [x] 13.2 Block operations: Ctrl+D, Ctrl+Shift+D, Alt+Up/Down, Tab, Shift+Tab
- [x] 13.3 Block conversion: Ctrl+0–4, Ctrl+T, Ctrl+Shift+T
- [x] 13.4 General: undo, redo, save, select all, new note, Ctrl+\\, F11,
  Ctrl+P, Alt+Left and Right, Ctrl+Alt+N, Ctrl+F, Ctrl+H, Ctrl+Shift+B,
  and the list in File, Keyboard shortcuts

## 14. Accessibility

- [x] 14.1 Keyboard accessibility: all editing from the keyboard, and the app's
  own focus order: F6 walks the panes drawn (search, note list, editor,
  backlinks, outline, toolbar), skipping hidden panes, and the menus carry the
  Qt app's access keys. Evidence: `TestF6CyclesPanes`, `TestMenuAccessKeys`
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
- [x] 16.1 Focus mode: F11 or View leaves the editor alone with its text in
  a centred column; Escape leaves. The window is maximised rather than made
  full screen, which unison v0.108.0 has no call for; see "Differences" below.
  Evidence: `TestFocusMode`
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
- **Unsized table columns fill differently.** Kvit shares the width by
  content weight (each column at least 92 px); the Go grid measures each
  column from its content and gives the last column the slack, so a
  right-aligned last column shows a wide empty run
  (`visual_36_tables_01_rendered`). Dragged widths are kept as Kvit keeps
  them, in the block's `cols` attribute.
- **A code block's scrollbar is Kvit's thumb**, in the border colour (strong
  under the pointer or while dragged) with no track of its own, where Qt's
  QQC2 bar leaves no visible mark in its own screenshot
  (`visual_31_code_04_long_line_scrolled`). The footer hint draws above it
  in both, so it stays readable where the two overlap.
- **A dragged row takes its new position directly without animating.**
  Qt's ListView animates the moved row while the surrounding rows take their
  new positions at once; unison has no such transition, and headless tests
  check the final order and the single undo step instead
  (`TestMultiBlockDragMovesTheSelection`, `visual_16_drag_*`). Reduced motion
  stills Qt's animation rather than removing the move.
- **A press far below the last block makes a paragraph at once.** Within
  reach of the last seam the press arms the gap caret as above; further down,
  in the editor's tail space Qt has no equivalent of, the caret goes to a new
  paragraph at the end, as the Go editor always did.
- **Sounds and videos open outside the note.** No Go toolkit plays video in
  place, so a media card opens its file in the reader's player (a remote page
  in the browser), where Qt plays it inside the block.
- **Focus mode maximises instead of going full screen.** Qt takes the window
  full screen; unison v0.108.0 has no call for that, so the Go app maximises,
  hides the panes and centres the column.
- **Escape in a card editor drops what was typed.** Qt's card editor writes as
  typed, so Escape keeps it; the Go field writes on Enter, so Escape leaves the
  card as it was.
