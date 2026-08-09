# Kvit Notes — feature specification

This is the behaviour specification for Kvit Notes, a native block editor for
Markdown notes built on Qt 6 and QML. A note is stored as an ordinary `.md` file
and shown fully rendered, with the raw markdown of a span revealed only while the
cursor is inside it. The sections below state, feature by feature, what the
application is specified to do: the block system and the twenty-one block kinds,
text editing and formatting, the notes collection, the user interface, and the
platform integration around them.

Four documents divide this ground up, because they answer different questions and
go stale at different rates:

- **This document** states intended behaviour.
- **[usage.md](usage.md)** explains how to use the application, and is the better
  starting point for a reader who has not run it yet.
- **[docs/backlog.md](docs/backlog.md)** records the known differences between this
  specification and what the code does today, so that no paragraph here has to
  hedge about whether it is already shipped.
- **[docs/adr/](docs/adr/)** holds the decisions the code is constrained by, and
  **[devel.md](devel.md)** covers building, debugging and testing.

---

## 1. Block System

### 1.1 Core Block Concept
- Each piece of content (text, image, heading, etc.) is an independent, self-contained block
- Blocks can be individually selected, moved, deleted, and transformed
- Blocks maintain their own state and rendering logic
- Blocks can be nested within other blocks (indentation hierarchy)
- Each block has a unique identifier for reference and manipulation
- Blocks can be collapsed/expanded when they contain nested content

### 1.2 Block Types

#### 1.2.1 Text Block (Paragraph)
- Default block type for regular text content
- Supports inline formatting (bold, italic, underline, strikethrough, code)
- Supports inline links with URL and display text
- Auto-continuation: pressing Enter creates a new text block
- Empty text blocks can be converted to other types via slash commands
- Support for text alignment (left, center, right, justify)

#### 1.2.2 Heading Blocks
- **Heading 1**: Largest heading, typically for document titles
- **Heading 2**: Section headings
- **Heading 3**: Subsection headings
- **Heading 4**: Minor headings
- Each heading level has distinct font size and weight
- Headings contribute to document outline/table of contents generation
- Pressing Enter at end of heading creates a new paragraph block

#### 1.2.3 Todo/Checkbox Block
- Checkbox element at the beginning of the block
- Clicking checkbox toggles completion state
- Completed items show strikethrough text styling
- Support for due dates attached to todo items
- Support for priority levels (high, medium, low)
- Keyboard shortcut to toggle checkbox state
- Indented todos create sub-tasks with parent-child relationship
- Progress indicator showing completed/total sub-tasks

#### 1.2.4 Bulleted List Block
- Bullet point marker at the beginning
- Pressing Enter creates new list item at same level
- Pressing Tab indents to create nested list item
- Pressing Shift+Tab outdents list item
- Pressing Enter on empty list item exits list mode
- Different bullet styles for different nesting levels (disc, circle, square)

#### 1.2.5 Numbered List Block
- Sequential numbering at the beginning
- Auto-numbering updates when items are added, removed, or reordered
- Support for different numbering styles (1,2,3 / a,b,c / i,ii,iii)
- Nested numbered lists restart numbering or continue based on settings
- Same indentation behavior as bulleted lists

#### 1.2.6 Quote Block
- Visual indicator (vertical bar, background color, or both)
- Italic or distinct text styling
- Support for attribution line
- Can contain multiple paragraphs within single quote block
- Nested quotes for multi-level quotation

#### 1.2.7 Code Block
- Monospace font rendering
- Syntax highlighting for common programming languages
- Language selector dropdown
- Line numbers (optional, toggleable)
- Copy code button
- Horizontal scrolling for long lines
- Preserve whitespace and indentation exactly as entered
- Enter inserts a newline, blank lines included, and carries the current line's
  indentation onto the new one; Ctrl+Enter leaves the block and starts a
  paragraph below, named in the block's bottom-right corner while the caret is
  inside, opposite the copy button
- Tab indents to the next four-column stop and Shift+Tab takes one stop back;
  with lines selected both work on every line the selection touches.
  Indentation is written as spaces, never as tab characters
- Support for common languages: Python, JavaScript, C++, Java, HTML, CSS, SQL, Bash, JSON, XML, Markdown

#### 1.2.8 Image Block
- Display images inline within document
- Support for common formats: PNG, JPG, JPEG, GIF, WebP, SVG, BMP
- Resize handles for adjusting image dimensions
- Maintain aspect ratio option when resizing
- Alignment options (left, center, right)
- Caption text below image, wrapping to the image's width; Shift+Enter adds a
  line to it and Enter leaves for a new block below
- Alt text for accessibility
- Click to view full-size in lightbox/modal
- Lazy loading for performance with many images
- Drag and drop image files from file system
- Drag and drop images from web browsers
- Paste images from clipboard
- Image effects: rounded corners, shadow, border

#### 1.2.9 Divider/Separator Block
- Horizontal line to visually separate content sections
- Multiple styles: solid line, dashed line, dotted line, decorative
- Adjustable thickness and color
- Full-width or partial-width options

#### 1.2.10 Callout/Alert Block
- Highlighted block for important information
- Different types: info, warning, success, error, tip, note
- Icon associated with each type
- The type is chosen from the header: the icon opens a list of the kinds, and
  picking one keeps the body, title, fold state and any custom colour
- Enter leaves the callout for a new block, splitting the body when the caret
  is inside it; Shift+Enter adds a line to the body; Ctrl+Enter folds it
- Customizable background color
- Can contain multiple paragraphs and other inline content

#### 1.2.11 Table Block
- Grid of cells organized in rows and columns
- Add/remove rows and columns
- Resize column widths by dragging borders
- Header row with distinct styling
- Cell text alignment (left, center, right)
- Merge cells horizontally or vertically
- Sort by column (ascending/descending)
- Tab to move between cells. The arrow keys move within the cell's text and
  cross to the next cell at its edge: Up and Down along the column from the
  cell's first and last line, Left and Right along the row from the two ends
  of the text, wrapping to the next or previous row and leaving the table at
  the grid's own edges
- Enter is done with a cell's value and moves down the column, as Down does;
  Shift+Enter breaks a line inside the cell; Ctrl+Enter makes a new block
  below the table, which the hint under the grid names while a cell is live
- Dragging from one cell to another selects the rectangle of cells between
  them. Ctrl+C copies that rectangle as a table of its own — the selected
  cells under the header cells of the columns they came from, since markdown
  has no notation for part of a table — Ctrl+X copies and empties it,
  Backspace or Delete empties it, and Escape or a click into a cell drops it.
  The same two commands are on the right-click menu while cells are selected.
  A selection and a live cell are exclusive: one ends the other
- Support for inline formatting within cells
- Multi-line cells: Shift+Enter breaks a line inside a cell, stored as `<br>`
  so the row stays one line of the file. A code fence an LLM opened inside a
  row is folded into its cell and keeps its lines and indentation

#### 1.2.12 Kanban/Task Board Block
- Multiple columns representing workflow stages
- Cards within each column representing tasks
- Drag and drop cards between columns
- Drag and drop to reorder cards within columns
- Drag and drop to reorder columns, and left/right controls that do the same
  without a pointer gesture
- Add new columns; rename a column by clicking its name; delete a column with
  its cards (undoable, so no confirmation)
- Add new cards to any column, which open their editor empty rather than
  arriving named "New card"
- Card content: title, description, labels, due date. A card is edited in
  place, not in a dialog: clicking its text opens an editor on the card's own
  line, where `#label` and `📅 YYYY-MM-DD` are written as the file holds them,
  and clicking under it opens one on the description
- A strip under the title holds the card's labels and its due date, and is
  where both are set: a chip adds a label, offering the labels the board
  already uses so one can be reused rather than retyped, and another opens a
  calendar for the due date. Each label chip removes itself
- Cards record the day they were added and the day they were last changed,
  shown at the foot of the card. The two dates ride in an HTML comment at the
  end of the card's line, so they stay out of the text being edited and out of
  every other markdown tool's way, and a card written before they were kept
  says only what it knows
- Card descriptions are shown on the card, hold as many lines as they are
  given (Shift+Enter breaks a line), and render inline math and
  `[[wiki-links]]` as the prose blocks do
- A card-details popover for the structured fields (the labels, and the due
  date the storage grammar is strict about), plus "Move to column" and
  "Delete card", reached from the card's context menu
- Column headers with card count, a collapse triangle, and controls that
  highlight and name themselves under the pointer
- Collapse/expand columns
- Color-coded labels/tags on cards
- Filter cards by label or status: a chip row above the board narrows it to
  one label or hides finished cards. The cards that do not match leave the
  columns rather than dimming, the column counts read "2 of 5", and a line
  beside the chips says how much of the board is showing

#### 1.2.13 Toggle/Collapsible Block
- Header text always visible
- Expandable/collapsible content section
- Click header or arrow icon to toggle
- Remember expanded/collapsed state
- ~~Can contain any other block types within collapsed content~~ *(declined
  2026-07-18: a toggle body is a single block's text — multiple paragraphs
  work, nested code blocks/tables/lists do not)*
- Keyboard shortcut to toggle

#### 1.2.14 Embed Block
- Embed external content via URL
- Support for video embeds (YouTube, Vimeo)
- Support for audio embeds *(served by the media path, not an embed player
  card — `isEmbedUrl` excludes recognized media extensions by design;
  player-card variant declined 2026-07-18)*
- ~~Support for document embeds (PDF viewer)~~ *(declined 2026-07-18)*
- Configurable dimensions
- Fallback display when embed fails to load

#### 1.2.15 Math/Equation Block
- LaTeX syntax support for mathematical notation
- Inline math within text blocks
- Block-level equations for complex formulas
- Real-time preview of rendered equation
- Equation numbering option
- Math entry assistance: typing `$` in prose auto-pairs
  the closing `$` with the caret between (forward Delete keeps a literal
  dollar; Backspace on the empty pair removes both; a second `$` types
  over the closer); typing `\` in a math context opens the command menu —
  a browsable, LyX-toolbar-style category panel with rendered glyphs on a
  bare `\`, collapsing into ranked autocompletion as letters follow.
  Accepting inserts the command's template with the caret in its first
  empty slot; Tab hops between slots; Ctrl+Space re-triggers completion
  for the backslash-word at the caret
- In a block equation Enter is a line break, since an `aligned` or `cases`
  body is several lines of TeX; Ctrl+Enter leaves the block and starts a
  paragraph below, named in the block's bottom-right corner while the source
  is open, the same as in a code block

#### 1.2.16 Drop Cap Block
- Enlarged first letter spanning multiple lines
- Customizable number of lines to span
- Font and color customization for drop cap letter *(via the `dropcapfont`/
  `dropcapcolor` markdown attributes only; a UI for setting them declined
  2026-07-18)*
- Text wraps around the drop cap *(approximated: the inset applies to the
  whole block, not only the first N lines — QQuickTextEdit limitation;
  exact wrap declined 2026-07-18. Drop cap is also not preserved in
  HTML/PDF export — declined 2026-07-18)*

#### 1.2.17 Diagram Blocks
Two diagram families, both stored as ordinary fenced code so Markdown remains the
source of truth and round-trips through other editors.

- **Mermaid diagram** — a ` ```mermaid ` fence rendered natively (no browser, JavaScript,
  Node, or network dependency inside the app). Kvit renders a documented Mermaid-compatible
  subset of five families, each parser built grammar-first against the Jison grammars of the
  pinned export version `mermaid@11.16.0`:

  | Family | Native support |
  |---|---|
  | Flowchart / graph (also `flowchart-elk`) | `TB`/`TD`/`BT`/`LR`/`RL` (and `>` `<` `^` `v`) directions; every flow.jison vertex shape — rectangle, rounded, stadium, circle, double circle, ellipse, rhombus, hexagon, cylinder, subroutine, both parallelograms, both trapezoids, odd/flag — plus `@{ shape: …, label: … }` shape data; solid/dotted/thick, labelled, chained, `A & B` lists, `e1@` edge ids, `~~~` invisible links; subgraphs with multi-word titles and local direction; `classDef`/`class`/`style`/`:::` safe styling; markdown-string labels; `accTitle`/`accDescr` (incl. multiline); LaTeX node and edge labels (`A["$$\frac{a}{b}$$"]`, Mermaid's `$$` delimiter) typeset natively |
  | Sequence | participants/actors with aliases, every arrow form (open/filled/cross/async-point heads, dotted, bidirectional), `+`/`-` activation shorthand and `activate`/`deactivate`, notes (left of/right of/over pairs), `loop`/`alt`-`else`/`opt`/`par`-`and`/`critical`-`option`/`break`/`rect` fragments, `box` participant groups with colors, `autonumber`, titles, `#code;` entity escapes; LaTeX participant, message and note labels typeset natively |
  | Class | classes with labels, backquoted names, `~T~` generics; member/method compartments as text; extension/composition/aggregation/dependency/lollipop ends on solid or dotted lines with cardinalities and labels; `<<annotations>>`; one-level namespaces; notes; direction; safe styling |
  | State (`stateDiagram`/`-v2`) | transitions with labels, `[*]` start/end scoped per composite, long descriptions (`state "…" as id`, `id : text`), composite states (nested), `<<fork>>`/`<<join>>`/`<<choice>>`, notes (single-line, `end note` blocks, floating), direction, safe styling |
  | ER | entities (quoted names, `NAME["alias"]`), attribute tables with types/keys (PK/FK/UK)/comments, every cardinality spelling (crow's-foot symbol pairs and the verbose `one or more` forms), identifying vs non-identifying lines, relationship roles, direction, safe styling |

  Restricted syntax — interactivity (`click`/`href`/callbacks/links), participant/actor
  config blocks, `create`/`destroy` lifecycles, central `()` connections, `linkStyle`,
  `scale`, node properties, and unknown `@{…}` keys — is retained with a warning, never an
  unrecognized-token failure. Deferred families (gantt, pie, mindmap, timeline, …) show an
  "unsupported family" diagnostic over editable source rather than a blank block. Parsing and
  layout run off the UI thread with an LRU cache; identical source, font, and direction
  produce an identical scene. Read state shows the rendered diagram fit to the visible
  window in both dimensions (a diagram taller than the 720 px read window scales down rather
  than clipping), with hover zoom controls, the current zoom level indicated bottom-right,
  and panning when zoomed in past the window; a per-block **PNG** control saves the scene as
  a 2× raster on the theme background. Focus shows the source plus a debounced live preview
  that keeps the last valid render while the new source is invalid, with a line/column
  diagnostic. The source editor is syntax-coloured through the same scanners and the same
  five theme tokens a code block is coloured with (§1.2.7), covering `%%` comments and
  `%%{…}%%` directives, diagram and statement keywords, layout directions, link runs, and
  bracketed, quoted or post-colon label text. Tab inserts two spaces, and Enter opens the new
  line at the current line's indentation; Ctrl+Enter leaves the block and starts a paragraph
  below, named in the block's bottom-right corner while the source is open, the same as in a
  code block. Resource limits (nodes/edges/depth/label length,
  256 KiB source) bound the renderer.
- **On-diagram editing** — supported flowcharts are edited directly on
  the rendered diagram; every gesture becomes a surgical edit of the fence source (one undo
  step), so bytes outside the edited span — comments, formatting, statement order — survive
  exactly, and a gesture that cannot reparse cleanly is refused with a status message.
  Clicking a node or edge selects it (Tab/arrows cycle, Escape clears) and surfaces its
  source line; in the editor, cursor and preview highlight each other's element both ways.
  Dragging a node (grid snap, alignment guides) persists as a single
  `%% mermaid-flow:pos` comment line — the obsidian-mermaid-flow format — which switches the
  block into arranged mode: pinned coordinates replace auto-layout and edges route as curves;
  **Reset layout** deletes the line and restores the exact prior source. The gesture set
  covers inline label editing (double-click / F2), shape and color changes, edge restyling
  (solid/dotted/thick), renaming an id across all references, deleting nodes or edges
  (chains split so unaffected links survive), drawing an edge by dragging a side anchor onto
  another node, and quick-adding a connected node. Sequence diagrams reorder instead:
  messages move up/down and participants left/right (Ctrl+arrows, context menu, or a
  one-position drag), swapping the underlying statements.
- **Character-cell (text) diagram** — a `diagram` (or `text-diagram`/`ascii-diagram`) fence.
  The tag carries no special rendering: the block is an ordinary unhighlighted code block,
  and the tag marks the fence for the ingest pass family. A
  conservative classifier runs at ingest (file open / paste) and retags a high-confidence
  untagged (or `text`/`plaintext`/`ascii`) fence to `diagram` — an info-string change that
  arms the same one-time `.bak` backup as the other ingest normalizations; directory
  listings, console tables, code, stack traces, and prose stay code. Diagram fences are then
  straightened at ingest (§7.5): ragged box edges align to their dominant column and jogged
  connectors line up under their tees, via zero-shift whitespace/fill swaps that never move
  label text — idempotent, `.bak`-armed, undoable on paste, all-or-nothing per box side, and
  skipped entirely by the `plain` opt-out. After ingest the stored text is preserved
  character-for-character.

Both diagram families export to Markdown (verbatim), HTML (Mermaid via one pinned
`mermaid@11.16.0` CDN module under `securityLevel: 'strict'`, with a collapsed source
disclosure; text diagrams as escaped `<pre>`), PDF (native raster for Mermaid, preformatted
text for character diagrams), and plain text (the source body). Reverse the auto-tag with the
block's **As code** control or the language menu's **Plain code**, which is never re-examined.

Every successfully rendered Mermaid family also exposes **Copy as text** beside the existing
source-copy and PNG controls. It converts the current scene to a deterministic Unicode
box-drawing diagram. Flowcharts and sequence diagrams receive the highest-fidelity layout;
class, state, and ER diagrams use the same generic renderer with simplified UML and
crow's-foot markers. The output is recognized by Kvit's text-diagram classifier and is already
a fixed point of its straightening pass.

#### 1.2.18 Collection Query Block

A `query` fenced block is a live, read-only projection over the indexed front-matter and
built-in properties of every note in the open collection. The fence stores only its spec;
rendered rows and cards are never serialized.

````markdown
```query
from: projects/
where: status = active
view: table
columns: title, status, due, priority
sort: due asc
```
````

- `view: table` renders rows; `view: board` plus `group-by: FIELD` renders a kanban-style board
- `where:` accepts `=`, `!=`, `<`, `>`, `<=`, `>=`, `contains`, `has`, and `exists`; repeated
  or comma-separated conditions are ANDed
- Date and number comparisons are typed; other comparisons are case-insensitive strings
- `from:` scopes by folder, `sort:` accepts multiple ascending/descending keys, and `limit:`
  caps the result count
- User-defined first-level front-matter keys are queryable alongside `title`, `path`, `folder`,
  `modified`, `created`, `words`, and `tags`
- Clicking a row or card opens its note; results refresh after in-app saves and external file
  changes through the collection revision index
- Invalid or unknown spec keys produce an in-block error instead of being ignored
- The launch implementation is read-only: editing metadata by changing cells or dragging cards
  is deferred
- In the spec editor Enter is a line break, since the spec is a list of lines; Ctrl+Enter
  leaves the block and starts a paragraph below, named in the block's bottom-right corner
  while the editor is open, the same as in a code block

---

## 2. Text Editing & Formatting

### 2.1 Inline Formatting
- **Bold**: Ctrl+B / Cmd+B
- **Italic**: Ctrl+I / Cmd+I
- **Underline**: Ctrl+U / Cmd+U
- **Strikethrough**: Ctrl+Shift+S / Cmd+Shift+S
- **Inline Code**: Ctrl+E / Cmd+E (monospace background)
- **Highlight/Mark**: Background color highlight
- **Text Color**: Foreground color selection
- **Superscript**: Raised smaller text
- **Subscript**: Lowered smaller text

### 2.2 Hybrid WYSIWYG/Markdown Approach

The editor uses a hybrid approach that bridges WYSIWYG and Markdown editing, providing the best of both worlds.

#### 2.2.1 Core Concept
- Text is stored as plain Markdown internally
- Text is displayed as rendered/formatted to the user (WYSIWYG appearance)
- Markdown syntax is revealed only when the cursor enters a formatted region
- No explicit mode switching required - behavior is automatic based on cursor position

#### 2.2.2 Display States

**Cursor Outside Formatted Region (Reading/Navigating):**
- All formatting appears rendered
- Bold text appears bold (no asterisks visible)
- Italic text appears italic (no asterisks visible)
- Links show as clickable text (no brackets or URLs visible)
- User experience similar to Word or Google Docs

**Cursor Inside Formatted Region (Editing):**
- Only the specific formatted span reveals its Markdown syntax
- Rest of the line/document remains rendered
- Example: `This is **important** text` - only "**important**" shows syntax when cursor is within that word

#### 2.2.3 Interaction Examples

**Example 1: Navigating through formatted text**
```
Display when cursor at start of line:
    "This is important information here"
              ↑ bold    ↑ italic (both rendered)

Display when cursor moves into "important":
    "This is **important** information here"
              ↑ syntax visible  ↑ still rendered

Display when cursor moves into "information":
    "This is important *information* here"
            ↑ rendered   ↑ syntax visible
```

**Example 2: Creating new formatted text**
- User types: `**hello**`
- While typing, asterisks are visible
- When cursor moves away (arrow key, click elsewhere, continue typing after closing `**`)
- Text transforms to rendered bold: **hello**

**Example 3: Editing existing formatted text**
- User sees: **important**
- User clicks/arrows into the word
- User sees: `**important**`
- User can now: edit the word, remove asterisks to unformat, add more asterisks

#### 2.2.4 Syntax Reveal Triggers
- Cursor entering a formatted span via arrow keys
- Cursor entering via mouse click within the span
- Cursor entering via text selection that includes the span
- Backspace/Delete operations that reach the boundary of formatted text

#### 2.2.5 Syntax Hide Triggers
- Cursor leaving the formatted span via arrow keys
- Cursor leaving via mouse click outside the span
- Pressing Escape (optional: force hide and move cursor out)
- Completing the formatting syntax (e.g., typing closing `**`)

#### 2.2.6 Benefits of This Approach
- **Non-technical users**: See clean formatted text, can use toolbar/shortcuts for formatting
- **Power users**: Can type Markdown directly, see and edit raw syntax when needed
- **Data portability**: Underlying data is plain Markdown, easily exported/migrated
- **No context switching**: Single editor surface, no split panes or mode toggles
- **Discoverable**: Users learn Markdown naturally by seeing syntax when editing

#### 2.2.7 Edge Cases and Behavior

**Nested formatting (e.g., bold italic):**
- Cursor in region reveals all applicable syntax: `***bold italic***`

**Adjacent formatted regions:**
- Each region reveals independently
- `**bold***italic*` - cursor in "bold" shows `**bold**`, "italic" stays rendered

**Partially selected formatted text:**
- Selection that starts/ends within formatted region reveals that region's syntax
- Allows precise editing of formatting boundaries

**Empty formatted regions:**
- `****` (empty bold) - cursor between shows `****`, allows deletion or text insertion
- Useful for "format then type" workflow

**Multi-line formatted regions (if supported):**
- Syntax revealed at start and end markers when cursor is anywhere within

### 2.3 Markdown Syntax Support
- `**bold**` or `__bold__` for bold text
- `*italic*` or `_italic_` for italic text
- `~~strikethrough~~` for strikethrough
- `` `code` `` for inline code
- `[link text](url)` for hyperlinks
- `[[note]]`, `[[note|alias]]`, and `[[note#heading|alias]]` for wiki-links
- `![alt](url)` for images
- `# ` through `#### ` for headings
- `- ` or `* ` for bulleted lists
- `1. ` for numbered lists
- `> ` for block quotes
- `---` or `***` for horizontal dividers
- ``` ``` ``` for code blocks
- `[ ]` and `[x]` for todo items

### 2.4 Links
- Create links via Ctrl+K / Cmd+K
- Link dialog with URL field and display text field
- Auto-detect URLs typed in text and convert to links
- Click link to open in external browser
- Ctrl+Click / Cmd+Click to follow link
- Edit link on hover or via context menu
- Remove link formatting while keeping text
- Support for internal document links (jump to heading)
- Wiki-links use `[[note]]`, `[[note|alias]]`, `[[note#heading]]`, and
  `[[note#heading|alias]]`; `[[#heading]]` targets the current note
- Typing `[[` in prose opens fuzzy note completion; typing `#` after a resolved target switches
  completion to that note's headings. Completion does not open in code blocks or inline math
- Wiki targets are case-insensitive and imply `.md`; a path suffix disambiguates duplicate
  titles. The current resolver otherwise chooses the shortest matching path, then
  alphabetically on equal-length ties
- Following an unresolved target creates it: bare names use the current note's folder and
  path-qualified targets create their folder chain
- `![[note]]` remains a literal `!` followed by a normal wiki-link; transclusion is not part of
  the current feature

### 2.5 Text Selection
- Click and drag to select text within a block
- Double-click to select word
- Triple-click to select entire block
- Shift+Arrow keys to extend selection
- Ctrl+A / Cmd+A to select all content in document
- Multi-block selection: click and drag across multiple blocks
- Shift+Click to extend selection to clicked position
- Selection highlighting with distinct color
- Blocks that draw their text rather than editing it (the web embed card, a
  collection query's results, a table of contents) carry a selection of their
  own over what they drew: drag for a character span, double-click for a word,
  a third click for a whole line, Ctrl+A for the block before the document,
  Ctrl+C to copy, Escape to drop. It stays inside the one block, and what it
  copies is the text on screen as plain text (tabs between cells on a line,
  newlines between lines). A document-level range across such a block still
  takes it whole and still carries its markdown source.

### 2.6 Cursor Behavior
- Blinking cursor indicator
- Cursor maintains horizontal position when moving vertically between lines
- Home key moves to beginning of line
- End key moves to end of line
- Ctrl+Home / Cmd+Up moves to beginning of document
- Ctrl+End / Cmd+Down moves to end of document
- Ctrl+Left / Option+Left moves by word
- Ctrl+Right / Option+Right moves by word
- Page Up/Down scrolls by viewport height
- The note scrolls past its last block, by about a third of the window, so the
  end of a note can be worked on in the middle of the window rather than
  against its bottom edge
- A part of a block that appears when it is edited — a task-board card's
  description field, a table row growing around its live cell — is scrolled
  into view when it opens and while it grows, so it cannot open below the
  window's edge

---

## 3. Block Manipulation

### 3.1 Block Selection
- Click on block to select it
- Click on block handle/grip to select entire block
- Shift+Click to select range of blocks
- Ctrl+Click / Cmd+Click to toggle block in selection
- Keyboard navigation: Up/Down arrows move between blocks when at block boundaries
- Ctrl+Shift+Up/Down to extend block selection
- Visual indication of selected blocks (border, background)

### 3.2 Block Reordering
- Drag and drop blocks to new positions
- Visual feedback during drag (placeholder, drop indicator)
- Blocks make room for the dragged block as it moves
- Keyboard shortcut: Alt+Up / Alt+Down to move selected blocks
- Move multiple selected blocks together
- The moved block animates to its new row. Surrounding rows take their new
  positions directly, so a variable-height block that finishes rendering
  asynchronously cannot leave them overlapped at stale animated coordinates

### 3.3 Block Indentation
- Tab key to indent block (increase nesting level)
- Shift+Tab to outdent block (decrease nesting level)
- Indent/outdent multiple selected blocks simultaneously
- Maximum indentation depth limit
- Visual indentation with consistent spacing per level
- Indented blocks form parent-child hierarchy

### 3.4 Block Conversion
- Convert existing block to different type
- Slash command menu to change type
- Context menu option to convert
- Keyboard shortcuts for common conversions:
  - Ctrl+0 / Cmd+0: Convert to paragraph
  - Ctrl+1 / Cmd+1: Convert to Heading 1
  - Ctrl+2 / Cmd+2: Convert to Heading 2
  - Ctrl+3 / Cmd+3: Convert to Heading 3
  - Ctrl+T / Cmd+T: Convert to Todo
  - Ctrl+Shift+T / Cmd+Shift+T: Convert to Quote
- Preserve text content during conversion when applicable

### 3.5 Block Deletion
- Backspace at beginning of empty block deletes the block
- Delete key at end of block merges with next block (for text blocks)
- Delete selected blocks with Delete or Backspace key
- Context menu delete option
- Undo available after deletion

### 3.6 Block Duplication
- Duplicate selected block(s)
- Keyboard shortcut: Ctrl+D / Cmd+D
- Duplicated blocks appear directly below original
- Deep copy: nested content is also duplicated

### 3.7 Block Creation
- Enter key at end of block creates new block below
- Slash command (/) opens block type menu
- Plus (+) button in gutter to add new block
- Menu button in the gutter opens the block context menu. It can copy the
  block, copy it as Markdown, plain text or HTML, or export just that block;
  when the block is part of a block selection, those commands use the whole
  selection. Menu or Shift+F10 opens the same menu for the focused block
- Block type menu with search/filter
- Recently used block types shown first
- Keyboard navigation through block type menu
- The blank space between two blocks takes a caret of its own. Pointing at that
  space draws a line in it; clicking turns the line into a blinking caret, and
  the next character typed becomes a paragraph in that space holding it. "/"
  makes the paragraph and opens the block type menu, and Enter leaves it empty.
  The space above the first block and below the last one are both such places
- Up and Down move that caret to the next space; Escape leaves it for the end
  of the block above
- Ctrl+V / Cmd+V pastes at that caret. Flat text makes paragraphs, structured
  Clipboard content keeps its block types, and Ctrl+Shift+V / Cmd+Shift+V
  pastes the source's plain text as paragraphs with inline formatting removed
- Ctrl+Enter on selected blocks puts the caret in the space after them. This is
  the keyboard route to the space below a block whose own Enter belongs to it —
  a table's Enter moves down the column and a code block's starts a line

---

## 4. Slash Commands & Block Menu

### 4.1 Slash Command Activation
- Type "/" at beginning of empty block or new line
- Opens floating menu with block type options
- Type to filter/search available block types
- Arrow keys to navigate menu
- Enter to select and insert block type
- Escape to close menu without selection
- Click outside menu to close

### 4.2 Block Type Menu Contents
- **Text**: Plain paragraph block
- **Heading 1-4**: Various heading levels
- **Bulleted List**: Unordered list item
- **Numbered List**: Ordered list item
- **Todo**: Checkbox item
- **Quote**: Block quote
- **Code**: Code block with language selection
- **Divider**: Horizontal separator
- **Image**: Insert image (opens file dialog or URL input)
- **Table**: Insert table with row/column configuration (grid picker driven by pointer or
  arrow keys, Enter to accept, Escape to cancel)
- **Callout**: Alert/notification block
- **Toggle**: Collapsible section
- **Task Board**: Kanban board
- **Embed**: External content embed
- **Math**: Equation block
- **Mermaid Diagram**: Native rendered `mermaid` fence
- **Collection Query**: Live table or board over note metadata

### 4.3 Menu Behavior
- Fuzzy search matching (typing "h1" matches "Heading 1")
- Icons for each block type
- Descriptions/hints for each option
- Grouped by category (Basic, Media, Advanced, etc.)
- Scrollable when many options
- Position menu near cursor, adjust to stay in viewport

---

## 5. Clipboard Operations

### 5.1 Copy
- Ctrl+C / Cmd+C to copy selected content
- Copy text selection within a block
- Copy entire selected blocks
- Copy multiple blocks preserving structure
- Copy to system clipboard in multiple formats (plain text, HTML, internal format)

### 5.2 Cut
- Ctrl+X / Cmd+X to cut selected content
- Remove content from document after copying
- Works with text selections and block selections

### 5.3 Paste
- Ctrl+V / Cmd+V to paste from clipboard
- Smart paste detection:
  - Plain text: insert as text, except a payload that opens a Markdown fence,
    which is parsed into the block type named by the fence
  - HTML: convert to appropriate blocks
  - Images: create image block
  - URLs: create link or embed
  - Internal format: recreate block structure
- When an external editor supplies both plain text and preformatted HTML for
  fenced Markdown, use the plain-text fence as the structure source instead
  of wrapping that fence in another code block
- Paste at cursor position within text
- Paste after selected block when blocks are selected
- Ctrl+Shift+V / Cmd+Shift+V: Paste as plain text (strip formatting)

### 5.4 Drag and Drop
- Drag selected blocks to reorder
- Drag files from system to insert (images, etc.)
- Drag images from web browser to insert
- Drag text from external applications
- Visual drop indicator showing insertion point
- Cancel drag with Escape key

---

## 6. Undo/Redo System

### 6.1 Undo
- Ctrl+Z / Cmd+Z to undo last action
- Unlimited undo history (within session)
- Grouped operations: multiple keystrokes while typing undo together
- Undo after timeout or explicit action separates groups
- Undo stack persists during session

### 6.2 Redo
- Ctrl+Y / Cmd+Shift+Z to redo undone action
- Redo stack cleared when new action is performed after undo
- Full redo of complex operations (block moves, formatting changes)

### 6.3 Operation Merging
- Consecutive typing merged into single undo operation
- Timer-based separation (pause in typing creates new undo point)
- Explicit action (clicking elsewhere, using tool) creates new undo point
- Formatting changes are separate undo operations
- Block structure changes (move, delete, convert) are separate operations

---

## 7. Search & Replace

### 7.1 Find
- Ctrl+F / Cmd+F opens find bar
- Search across all blocks in document
- Real-time highlighting of matches as you type
- Match count display (e.g., "3 of 15")
- Navigate between matches with Enter or arrow buttons
- F3 / Cmd+G for next match
- Shift+F3 / Cmd+Shift+G for previous match
- Scroll to and highlight current match
- Case-sensitive search option
- Whole word match option
- Regular expression search option

### 7.2 Find and Replace
- Ctrl+H / Cmd+Option+F opens find and replace
- Replace current match
- Replace all matches
- Preview of replacements before confirming
- Replace in selection only option
- Preserve case option (match case of replaced text)

---

## 8. Document Organization

### 8.1 Folders
- Hierarchical folder structure for organizing notes
- Create, rename, delete folders
- Drag and drop notes between folders
- Nested folders (subfolders)
- Folder icons or colors for visual distinction
- Expand/collapse folders in sidebar
- Folder-level search

### 8.2 Tags
- Apply multiple tags to any note
- Create new tags on the fly
- Tag autocomplete when typing
- Tag colors for visual organization
- Filter notes by tag
- Tag management (rename, delete, merge tags)
- Tag sidebar showing all tags with note counts

### 8.3 Note List
- A note created without a name arrives as "Untitled N" and takes its name
  from its first block once that block is finished — the heading in the usual
  case, the first 60 characters of the text otherwise, sanitized to a valid
  file name. It happens once: only a note still carrying the automatic name is
  renamed this way, so a name the reader typed, or one this produced, stays
  put however the first block is edited afterwards. A name already taken, or
  first-block text that cannot be a file name (a table, an image, a code
  fence), leaves the note called what it was called
- List view of notes in current folder/filter
- Sort by: date modified, date created, title, manual order
- Sort ascending/descending
- Preview snippet of note content
- Note metadata: created date, modified date, word count
- Pin important notes to top
- Star/favorite notes
- Bulk selection and operations

### 8.4 Search Across Notes
- Global search across all notes
- Search in titles and content
- Filter by folder, tag, date range
- Search results with context snippets
- Click result to open note at match location
- Recent searches history

### 8.5 Linked-Note Navigation

- A toggleable right-side backlinks pane lists referring notes, link counts, and context lines;
  clicking an entry opens the referrer
- Backlinks refresh through the collection revision index, including after external file changes
- Back and forward navigation are available from toolbar buttons, Alt+Left / Alt+Right, and mouse
  back/forward buttons; history restores the saved scroll position
- Ctrl+P opens a fuzzy quick switcher over note titles and paths; Enter opens the selected note
  and Shift+Enter creates a note from the typed title in the current folder scope
- Renaming or moving a note through Kvit keeps existing wiki targets working immediately: the
  vault records a redirect from the old name to the new one, so every `[[old name]]` still
  resolves from the moment the rename completes, however many notes point at it
- The referring notes are then rewritten in the background, a few at a time, preserving aliases
  and heading anchors, and the changed link and note counts are reported in the status bar when
  the pass finishes. The redirect is dropped once nothing links through it. Because the rename
  itself only writes one small control file, it takes the same time whether one note links to it
  or a thousand, and an interrupted pass cannot break a link: the redirect outlives the crash and
  the rewrite resumes at the next open
- A redirect is recorded only when link updating was requested; a rename asked to leave links
  alone still leaves them alone. A redirect never shadows a real note, so creating a note at the
  old name takes that name back
- Redirects are local to the vault, so a vault opened in another tool before the background pass
  finishes sees the not-yet-rewritten links as unresolved
- Graph view, unlinked mentions, and wiki transclusion are not included

---

## 9. User Interface

### 9.1 Main Layout
- Sidebar for navigation (folders, tags, search)
- Note list panel (optional, hideable)
- Editor panel (main content area)
- Resizable panels with drag handles
- Collapsible sidebar for focused writing
- Full-screen/distraction-free mode

### 9.2 Toolbar
- Formatting buttons (bold, italic, etc.)
- Block type dropdown
- Text alignment buttons
- Insert menu (image, table, etc.)
- View options (outline, backlinks, word count)
- Back/forward navigation buttons with enabled-state feedback
- Toolbar customization (show/hide buttons)
- On macOS the File and View menus are in the system menu bar at the top of
  the screen instead of on toolbar buttons; the rest of the toolbar is the
  same on every platform

### 9.3 Formatting Bar
- Floating toolbar appears on text selection
- Quick access to common formatting options
- Context-sensitive options based on selection
- Position near selection without obscuring it

### 9.4 Block Handle/Gutter
- Handle appears on hover at left of each block
- Drag handle to reorder blocks
- Click handle to select entire block
- Plus button to insert new block
- Context menu on right-click

### 9.5 Context Menus
- Right-click on text: cut, copy, paste, formatting options
- Right-click on block: block-specific options, convert, delete, duplicate
- Right-click on selection: options relevant to selection type
- Right-click on link: open link, edit link, remove link
- Every entry carries an access key on Windows and Linux (§9.6.1)

### 9.6 Keyboard Navigation
- Tab/Shift+Tab to navigate UI elements
- Arrow keys for menu navigation
- Enter to activate/select
- Escape to cancel/close dialogs and menus
- Comprehensive keyboard accessibility

#### 9.6.1 Menu access keys (Windows and Linux)
- Every command in every menu has an access key: one letter of its label,
  drawn underlined, that runs the command when it is typed while the menu is
  open. A submenu's own entry has one too, so a whole path is reachable by
  typing — in the block menu, `A` then `C` is Align → Center.
- The File, View and Insert menus have no menu bar to hang from, so their
  toolbar buttons carry the access key instead and Alt+F, Alt+V and Alt+I open
  them from anywhere in the window.
- Pressing Alt on its own does nothing. Qt Quick's menus bind the key
  combination but do not implement the Windows behaviour of activating the
  menu bar on a bare Alt press.
- Every command has a key, and the keys are distinct within one menu. Both are
  checked rather than reviewed (`tools/check-menu-access-keys.py`).
- The underlines are drawn from the start rather than appearing only while Alt
  is held, because Qt does not read the Windows "Underline access keys"
  setting.
- The code-block language chooser is the one menu without them: its entries
  are values to pick from rather than commands, and most of them share their
  first letters.
- macOS has no access-key convention, so no underlines appear there and the
  markers are removed from the labels; its menus are reached through the
  system menu bar and the standard Command shortcuts.

### 9.7 Status Bar
- Current line/column position
- Word count for document or selection
- Character count
- Last saved time
- Sync status (if applicable)

---

## 10. Themes & Appearance

### 10.1 Built-in Themes
- Light theme: bright background, dark text
- Dark theme: dark background, light text
- Sepia theme: warm, paper-like background
- System theme: follow OS light/dark setting

### 10.2 Typography Settings
- Font family selection for editor
- Font size adjustment
- Line height/spacing adjustment
- Paragraph spacing
- Maximum content width option
- Monospace font selection for code blocks

### 10.3 Customization
- Accent color selection
- Custom CSS/styling (advanced users)
- Block-specific styling options
- Highlight color selection

---

## 11. Performance & Optimization

### 11.1 Virtualized Rendering
- Only render blocks visible in viewport
- Efficient scrolling with many blocks
- Lazy loading of off-screen content
- Smooth scrolling performance
- Handle documents with thousands of blocks

### 11.2 Image Optimization
- Lazy load images as they scroll into view
- Thumbnail generation for previews
- Image caching
- Progressive image loading

### 11.3 Responsive Editing
- No lag during typing
- Instant formatting application
- Smooth cursor movement
- Efficient undo/redo operations
- Background save operations

### 11.4 Targets
- Load time: under 1 second for documents up to 500,000 words
- Typing latency: imperceptible, meaning under 16 ms so a 60 fps frame is never missed
- Scrolling: 60 fps through delegate virtualization, at any document size
- Memory: scales linearly with document size

Two of these are asserted by the test suite rather than left as intentions: the
500-note collection open and the 500-note search query each run against a budget
on every merge. Both are measured in process CPU time rather than wall-clock,
because wall-clock on a shared machine measures the neighbouring processes; the
reasoning and the measurements behind the thresholds are in `tests/timingbudget.h`
and summarized in [CONTRIBUTING.md](CONTRIBUTING.md).

---

## 12. Data Storage & Persistence

### 12.1 Local Storage
- Notes stored locally on user's device
- SQLite database or file-based storage
- Plain text/Markdown as underlying format
- Automatic saving
- Save indicator (saved/unsaved state)

### 12.2 Auto-Save
- Automatic save after changes
- Configurable auto-save interval
- Save on blur (when editor loses focus)
- Debounced saving during continuous typing

### 12.3 Manual Save
- Ctrl+S / Cmd+S to force save
- Visual confirmation of save

### 12.4 Backup & Recovery
- Automatic backup creation
- Backup rotation (keep last N backups)
- Recover from backup — the restore dialog draws the version under the cursor
  as rendered blocks beside the list of timestamps, pictures included, and
  that preview can be swept with the pointer and copied out as markdown
  without restoring anything; a link in it opens the note or page it names,
  and a remote image in it waits for the reader to approve its origin the way
  one in a note does (see selection.md, "A document drawn read-only")
- Crash recovery (restore unsaved changes)

### 12.5 Export Options
- Export as Markdown (.md)
- Export as HTML
- Export as PDF
- Export as plain text
- Export selected notes or entire collection
- Include images in export

### 12.6 Import Options
- Import Markdown files
- Import text files
- Import from other note applications (if feasible)
- Batch import multiple files
- Preserve folder structure during import

---

## 13. Keyboard Shortcuts

### 13.1 Text Formatting
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Bold | Ctrl+B | Cmd+B |
| Italic | Ctrl+I | Cmd+I |
| Underline | Ctrl+U | Cmd+U |
| Strikethrough | Ctrl+Shift+S | Cmd+Shift+S |
| Inline Code | Ctrl+E | Cmd+E |
| Link | Ctrl+K | Cmd+K |

### 13.2 Block Operations
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Move block up | Alt+Up | Option+Up |
| Move block down | Alt+Down | Option+Down |
| Duplicate block | Ctrl+D | Cmd+D |
| Delete block | Ctrl+Shift+D | Cmd+Shift+D |
| Indent | Tab | Tab |
| Outdent | Shift+Tab | Shift+Tab |

### 13.3 Block Conversion
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Paragraph | Ctrl+0 | Cmd+0 |
| Heading 1 | Ctrl+1 | Cmd+1 |
| Heading 2 | Ctrl+2 | Cmd+2 |
| Heading 3 | Ctrl+3 | Cmd+3 |
| Todo | Ctrl+T | Cmd+T |
| Quote | Ctrl+Shift+T | Cmd+Shift+T |

### 13.4 General
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Save | Ctrl+S | Cmd+S |
| Undo | Ctrl+Z | Cmd+Z |
| Redo | Ctrl+Y | Cmd+Shift+Z |
| Find | Ctrl+F | Cmd+F |
| Find & Replace | Ctrl+H | Cmd+Option+F |
| Select All | Ctrl+A | Cmd+A |
| New Note | Ctrl+N | Cmd+N |
| Quick Switcher | Ctrl+P | Ctrl+P |
| Back | Alt+Left | Alt+Left |
| Forward | Alt+Right | Alt+Right |
| Toggle Backlinks | Ctrl+Shift+B | Ctrl+Shift+B |
| Toggle Sidebar | Ctrl+\ | Cmd+\ |
| Distraction-free | F11 | Cmd+Ctrl+F |

---

## 14. Accessibility

### 14.1 Keyboard Accessibility
- All features accessible via keyboard
- Logical tab order
- Visible focus indicators
- Skip navigation links
- Underlined menu access keys on Windows and Linux (§9.6.1)

### 14.2 Screen Reader Support
- A role and a name on every interactive element, published through
  `QAccessible`. Narrator and NVDA read that over UI Automation, VoiceOver over
  NSAccessibility, and Orca over AT-SPI. A control drawn as a rectangle
  with a tap handler on it carries them explicitly; `qml/IconButton.qml` is the
  shared component that supplies them for a glyph-labelled button.
- State published alongside the name where a control has one: checked on a
  to-do and on every toggle, selected on a list row, the current value on a
  slider.
- Alt text for images
- Announce dynamic content changes

### 14.3 Visual Accessibility
- High contrast theme option, followed automatically when the operating system
  is in a high-contrast mode and no theme has been chosen explicitly
- Adjustable font sizes: the editor font (§10.2) sizes the document, and a
  separate interface size sizes the sidebar, note list, toolbar, status bar,
  dialogs and block furniture
- Reduced motion: on, off, or following the operating system's own setting
  (the default)
- Colour contrast to WCAG 2.1 level AA: 4.5:1 for text, and 3:1 for the parts
  of a control that show where it is and what state it is in. The contrast
  floors in `tests/test_theme.cpp` hold both across all four themes.

`accessibility.md` is the full account: what the application supplies, how each
claim here is verified, and the manual screen-reader checks no automated suite
in this repository can perform.

---

## 15. System Integration

### 15.1 Quick-capture Hotkey
- Keyboard shortcut to open the quick-capture window, active while the app has
  focus. `Ctrl+Alt+N` by default, read from the `hotkey.quickCapture` setting;
  the tray menu opens the same window.
- ~~System-wide keyboard shortcut to summon application~~ ~~Quick note capture
  from any application~~ *(deferred 2026-07-20. No system-wide grab is
  implemented on any platform. `GlobalHotkey` is a wired seam with nothing
  behind it: X11 `XGrabKey`, the XDG GlobalShortcuts portal, Win32
  `RegisterHotKey` and the macOS equivalent are all unwritten, and
  `AppContext` reports the feature unsupported everywhere. It stays a seam
  rather than a shipped claim, because a configurable hotkey that silently
  never fires is worse than an honest absence. Implementing it needs one
  backend per platform and a way to verify each; this machine (WSLg) grants no
  system-wide grab at all, so none of it could be tested here.)*
- The chord is configurable by editing `hotkey.quickCapture` in the settings
  file; there is no settings-dialog control for it yet.

### 15.2 System Tray
- Minimize to system tray
- Tray icon with context menu
- Quick actions from tray

### 15.3 File Associations
- Associate .md files with editor
- Open files via double-click or drag-drop
- Command line argument to open file

### 15.4 Native Notifications
- Optional notifications for reminders
- Sync status notifications

---

## 16. Distraction-Free Mode

### 16.1 Focus Mode
- Hide all UI except editor content
- Full-screen or windowed
- Centered text with maximum width
- Subtle, minimal interface elements
- Fade non-focused blocks (typewriter mode option)
- Single-click or hotkey to exit

### 16.2 Typewriter Mode
- Keep current line vertically centered
- Smooth scrolling as you type
- Reduce visual noise above/below current position

---

## 17. Document Outline

### 17.1 Outline Panel
- Auto-generated from headings in document
- Collapsible outline tree
- Click heading to navigate to that section
- Current section highlighted in outline
- Configurable heading levels to include

### 17.2 Table of Contents Block
- Insert generated TOC into document
- Auto-update as headings change
- Clickable links to sections

---

## 18. Templates

### 18.1 Note Templates
- Create new notes from templates
- Built-in templates (meeting notes, project plan, daily journal)
- Custom user-defined templates
- Template management (create, edit, delete)
- Template variables (date, time, etc.)

---

## 19. Word Count & Statistics

### 19.1 Document Statistics
- Word count
- Character count (with and without spaces)
- Paragraph count
- Block count
- Estimated reading time
- Selection statistics when text selected

### 19.2 Writing Goals
- Set target word count
- Progress indicator toward goal
- Session word count tracking

---

## 20. Sync and collaboration are out of scope

Kvit Notes does not synchronize notes between devices and does not support
several people editing one note at the same time. Neither is planned, so neither
appears in [docs/backlog.md](docs/backlog.md): this is a boundary of the product
rather than unfinished work inside it, and [CONTRIBUTING.md](CONTRIBUTING.md)
lists both among the directions the project declines.

### 20.1 Why sync is left to the filesystem

A vault is an ordinary folder of `.md` files, so any general-purpose file sync
already running on the machine moves notes between devices without the editor
participating: Syncthing, Dropbox, iCloud Drive, or a git remote all work, and
each of them handles the offline case and the selective-folder case better than
an editor-specific implementation would.

What the editor does have to handle is a file changing underneath an open note,
and it does. An external write to the open file surfaces as a keep-mine or
load-theirs banner rather than being silently overwritten or silently discarded
(§12), which is the point where sync and the editor actually meet.

### 20.2 Why real-time collaboration is a different decision

Concurrent editing is not a feature that could be added later behind a setting.
It needs a server, a persistent identity per editor, and a conflict-resolution
model such as operational transformation or a conflict-free replicated data type
living in the document core. That last requirement contradicts the decision the
rest of the design rests on, which is that the file on disk is the single
authoritative copy of a note and everything else is rebuildable from it
([docs/adr/0001](docs/adr/0001-files-on-disk-are-authoritative.md)). Comments and
annotations are declined for the same reason: markdown has no notation for them,
so storing them means either a sidecar database or a syntax that other editors
would render as stray text.

---

## 21. Implementation notes

The techniques below are the non-obvious ones the editor rests on. Each solves a
problem that is easy to hit and hard to diagnose when building a block editor on
Qt Quick: a cursor whose position must be read against three different
representations of the same text, delegates the view is entitled to destroy while
a drag is in flight, an undo stack that has to merge keystrokes the way a person
expects. They are recorded beside the behaviour they produce so that a later
reader can tell an arbitrary-looking choice from a considered one.

### 21.1 Cursor Position Detection in Hybrid Markdown Mode

Detecting when the cursor enters a formatted region is non-trivial because Qt's TextArea uses RichText (HTML) internally, not Markdown.

**Challenge**: The internal HTML representation differs from the displayed text and from the stored Markdown.

**Solution approach**:
1. Convert the full TextArea HTML content to Markdown
2. Convert HTML from position 0 to cursor position to Markdown (using `getFormattedText()`)
3. Find the longest common prefix between both Markdown strings
4. Use regular expressions to detect if cursor position falls within Markdown syntax markers (e.g., `**`, `*`, `` ` ``)
5. If cursor is within syntax markers, reveal the raw Markdown for that region

**Libraries needed**:
- HTML normalizer (Qt's HTML output is non-standard)
- HTML-to-Markdown converter
- Robust regex patterns for Markdown syntax detection

### 21.2 ListView Virtualization and Delegate Management

Qt's ListView destroys off-screen delegates for memory efficiency, which creates challenges for large documents and drag operations.

**Delegate Reuse Pool**:
- Use ListView's `reuseItems` property to move off-screen delegates to a pool instead of destroying
- Implement `isPooled` property on each delegate to disable bindings and signals when pooled
- Re-enable bindings when delegate returns to visible area
- Significant memory savings for documents with thousands of blocks

**Caching Strategy**:
- Avoid increasing `cacheBuffer` during drag operations (memory intensive)
- Instead, create invisible replicas of dragged blocks (see Drag & Drop section)

### 21.3 Cross-Block Text Selection

Implementing text selection that spans multiple discrete block delegates requires coordination across the ListView.

**Implementation approach**:
1. Track `selectionStartIndex` and `selectionEndIndex` at the document/model level
2. Each visible delegate checks if it falls within the selection range
3. Emit `selectionChanged()` signal to trigger rechecks across all visible delegates
4. Update selection state on every cursor movement during mouse drag
5. Implement smooth accelerated scrolling when selection drag reaches viewport edges

**Selection features to support**:
- Forward and backward selection
- Word selection (double-click and drag)
- Line/block selection (triple-click)
- Keyboard extension (Shift+Arrow keys)

### 21.4 Drag and Drop Without Delegate Destruction

During drag operations, if a block is moved far from its original position, ListView may destroy it mid-drag.

**Solution - Invisible Replica Technique**:
1. When drag starts, create an invisible replica of the dragged block at its original position
2. The replica occupies space in the ListView, preventing layout collapse
3. Move the visible drag representation freely (can be OS drag image or custom)
4. Other blocks make room based on the invisible replica's position; they do
   not animate through cached y coordinates because delegate heights may
   change asynchronously
5. On drop, remove replica and insert block at new position
6. On cancel, remove replica and restore original block visibility

**External drag (files from OS)**:
- QML's DragEvent lacks QMimeData access
- May need to extend drag-drop functionality or use platform-specific solutions
- Create invisible placeholder block during external drag to show drop position
- Use OS-provided drag image as visual feedback

### 21.5 Undo/Redo with Operation Merging

Users expect typing "hello" then pressing undo to remove all 5 characters, not one at a time.

**Data Structure**:
- Simple structs storing old and new plaintext for each operation
- Prioritize simplicity over complex diff algorithms
- Each block can reference its undo stack, or use a central document-level stack

**Operation Merging (CompoundAction)**:
- Consecutive character insertions merge into a single undoable action
- Timer-based separation: pause in typing creates new undo point
- Explicit actions (click, format change) create new undo point

**Operations that should merge**:
- Sequential typing without pause
- Indenting/outdenting multiple selected blocks together
- Kanban card moves (remove from source + insert at destination)
- Batch formatting changes

**Complex block considerations**:
- Advanced blocks (Kanban, Table) maintain undo state at the document/model level
- Prevents undo stack loss when parent block is removed and recreated

### 21.6 Complex Block Serialization

Complex blocks like Kanban boards need a syntax that remains human-readable in plain text.

**Recommended syntax pattern**:
```
{{blockType "param1":"value1","param2":"value2"}}
[block content in readable format]
{{/blockType}}
```

**Example - Kanban Board**:
```
{{kanban "title":"Project Tasks"}}
## Todo
- [ ] Design UI mockups
- [ ] Write documentation

## In Progress
- [ ] Implement drag and drop

## Done
- [x] Set up project structure
{{/kanban}}
```

**Benefits**:
- Human-readable without rendering
- Easy to parse programmatically
- Content degrades gracefully in plain text editors
- Nested content uses familiar Markdown syntax

### 21.9 Known gaps

Differences between shipped behavior and this specification are tracked in
[docs/backlog.md](docs/backlog.md) rather than inside the specification itself,
so that a reader can tell which of the two a given paragraph describes. The
follow-ups this subsection once listed are recorded there under their original
heading, with where each one ended up.
