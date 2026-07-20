# Block Editor Features Specification

A comprehensive features document for a Qt-based block editor inspired by Daino Notes and similar modern note-taking applications.

The collection query block, Mermaid text export, and linked-note navigation additions were
audited against the implementation on 2026-07-12. Their entries below describe the behavior
currently present in the repository; remaining hardening work is recorded in §21.9.

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
- Support for common languages: Python, JavaScript, C++, Java, HTML, CSS, SQL, Bash, JSON, XML, Markdown

#### 1.2.8 Image Block
- Display images inline within document
- Support for common formats: PNG, JPG, JPEG, GIF, WebP, SVG, BMP
- Resize handles for adjusting image dimensions
- Maintain aspect ratio option when resizing
- Alignment options (left, center, right)
- Caption text below image
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
- Tab to move between cells
- Support for inline formatting within cells

#### 1.2.12 Kanban/Task Board Block
- Multiple columns representing workflow stages
- Cards within each column representing tasks
- Drag and drop cards between columns
- Drag and drop to reorder cards within columns
- Drag and drop to reorder columns
- Add new columns
- Add new cards to any column
- Card content: title, description, labels, due date
- Column headers with card count
- Collapse/expand columns
- Color-coded labels/tags on cards
- Filter cards by label or status

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
  | Flowchart / graph (also `flowchart-elk`) | `TB`/`TD`/`BT`/`LR`/`RL` (and `>` `<` `^` `v`) directions; every flow.jison vertex shape — rectangle, rounded, stadium, circle, double circle, ellipse, rhombus, hexagon, cylinder, subroutine, both parallelograms, both trapezoids, odd/flag — plus `@{ shape: …, label: … }` shape data; solid/dotted/thick, labelled, chained, `A & B` lists, `e1@` edge ids, `~~~` invisible links; subgraphs with multi-word titles and local direction; `classDef`/`class`/`style`/`:::` safe styling; markdown-string labels; `accTitle`/`accDescr` (incl. multiline) |
  | Sequence | participants/actors with aliases, every arrow form (open/filled/cross/async-point heads, dotted, bidirectional), `+`/`-` activation shorthand and `activate`/`deactivate`, notes (left of/right of/over pairs), `loop`/`alt`-`else`/`opt`/`par`-`and`/`critical`-`option`/`break`/`rect` fragments, `box` participant groups with colors, `autonumber`, titles, `#code;` entity escapes |
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
  diagnostic. Resource limits (nodes/edges/depth/label length, 256 KiB source) bound the
  renderer.
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
- "Popping" animation where blocks make room for dragged block
- Keyboard shortcut: Alt+Up / Alt+Down to move selected blocks
- Move multiple selected blocks together
- Smooth animation during reordering

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
- Block type menu with search/filter
- Recently used block types shown first
- Keyboard navigation through block type menu

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
- **Table**: Insert table with row/column configuration
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
  - Plain text: insert as text
  - HTML: convert to appropriate blocks
  - Images: create image block
  - URLs: create link or embed
  - Internal format: recreate block structure
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
- Renaming or moving a note through Kvit automatically rewrites stale matching wiki targets in
  indexed note files while preserving aliases and heading anchors, then reports the changed
  link and note counts in the status bar
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

### 9.6 Keyboard Navigation
- Tab/Shift+Tab to navigate UI elements
- Arrow keys for menu navigation
- Enter to activate/select
- Escape to cancel/close dialogs and menus
- Comprehensive keyboard accessibility

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
- Recover from backup
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

### 14.2 Screen Reader Support
- ARIA labels on interactive elements
- Semantic HTML structure
- Alt text for images
- Announce dynamic content changes

### 14.3 Visual Accessibility
- High contrast theme option
- Adjustable font sizes
- Reduced motion option
- Sufficient color contrast ratios

---

## 15. System Integration

### 15.1 Global Hotkey
- System-wide keyboard shortcut to summon application
- Configurable shortcut combination
- Quick note capture from any application

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

## 20. Sync & Collaboration (Future Consideration)

### 20.1 Cloud Sync
- Sync notes across devices
- Conflict resolution
- Offline support with sync when online
- Selective folder sync

### 20.2 Real-time Collaboration
- Multiple users editing same document
- Presence indicators showing other cursors
- User attribution for changes
- Comment/annotation system

---

## 21. Implementation Considerations

This section documents key technical challenges and solutions discovered in existing block editor implementations, particularly from Daino Notes.

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
4. Other blocks animate to "make room" based on the invisible replica's position
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

### 21.7 Performance Benchmarks

Reference benchmarks from Daino Notes testing with "War and Peace" (561,693 words):

| Application | Load Time | Notes |
|-------------|-----------|-------|
| Daino Notes | 0.33s | Qt/QML with virtualization |
| AppFlowy | 2.20s | Crashes on large documents |
| MarkText | 19.90s | Electron-based |
| Notion | N/A | Too slow to measure |

**Performance targets**:
- Load time: < 1 second for documents up to 500,000 words
- Typing latency: imperceptible (< 16ms for 60fps)
- Scroll performance: smooth 60fps with virtualization
- Memory: scale linearly with document size

**Known bottlenecks to address**:
- Bulk block deletion can block UI (runs on main thread)
- Saving entire document on each change vs. incremental saves
- Memory pooling for delegate reuse optimization

### 21.8 Qt-Specific Considerations

**Known limitations**:
- ListView scrolling may not match native platform feel exactly
- RichText span styling (border, border-radius) not natively supported
- Blurred/translucent window backgrounds require platform-specific handling

**Useful libraries**:
- QWindowKit: frameless windows with native controls (macOS, Windows, Linux)
- QSimpleUpdater: cross-platform application updates
- Custom HTML exporter: normalize Qt's non-standard HTML output

**Licensing note**:
- Qt LGPL permits static linking when providing object files and relinking capability
- Dynamic linking is not the only option for LGPL compliance

### 21.9 Pre-Launch Implementation Follow-ups

The launch-facing behavior of all three additions is implemented and covered by focused unit
and integration tests. The 2026-07-12 audit found these remaining differences from the
specification they were built to:

- Query blocks currently evaluate synchronously on every relevant content or collection
  revision. The planned 150 ms coalescing timer, `(spec, revision)` result cache, and explicit
  1,000-note / 25 ms performance gate have not been implemented.
- Wiki-link backlink extraction skips fenced code, but does not yet share the formatter's full
  opaque-region rules for inline code and inline/display math. Those literal examples can be
  indexed or rewritten as links even though the editor does not render them as wiki-links.
- Rename-safe rewriting is atomic per file but currently automatic: there is no preflight
  confirmation, modified-stamp conflict check, partial-failure report, open-document undo path,
  or folder-rename target rewrite.
- Bare duplicate wiki targets currently resolve deterministically to the shortest path instead
  of remaining unresolved until the user supplies enough path to make the suffix unique.
- The backlinks integration test covers revision-driven live updates, but not the plan's exact
  external `FileWatcher::feedChange` panel path. The query-block integration test does cover that
  external watcher path.
- Mermaid text export is unit-tested across all five families and exposed in QML, but
  `DiagramCanvas::textDiagram()` checks for a non-empty scene rather than using the stricter
  `sceneCurrent()` guard specified by the plan; the visible action itself is hidden while a
  render is pending or errored.

---

## References

### Primary Sources

- **Daino Notes Block Editor Article**: [Developing a Beautiful and Performant Block Editor in Qt C++ and QML](https://rubymamistvalove.com/block-editor) - Comprehensive technical deep-dive into building a block editor with Qt

- **Daino Notes GitHub**: [github.com/nuttyartist/daino-notes-public](https://github.com/nuttyartist/daino-notes-public) - Open source Qt/QML note-taking application

- **Daino Notes Website**: [get-notes.com](https://www.get-notes.com/) - Official product page

### Related Block Editor Implementations

- **BlockNote**: [blocknotejs.org](https://www.blocknotejs.org/) - React-based block editor built on Prosemirror and Tiptap

- **BlockNote GitHub**: [github.com/TypeCellOS/BlockNote](https://github.com/TypeCellOS/BlockNote) - Open source Notion-style editor

### Comparison and Alternatives

- **AlternativeTo - Daino Notes**: [alternativeto.net/software/plume-notes](https://alternativeto.net/software/plume-notes/) - List of similar note-taking applications

- **Best Block-Based Editors**: [makeuseof.com/best-block-based-editors-note-taking](https://www.makeuseof.com/best-block-based-editors-note-taking/) - Comparison of Notion, Workflowy, RemNote, and others
