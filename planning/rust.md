# Rewriting Kvit Notes in Rust on GPUI: an investigation

Written 2026-08-09. The claims about GPUI were checked against the Zed
repository and crates.io on that date and are given with the dates that
matter, because GPUI is pre-1.0 and several of them changed during 2026.

## What this document is

Kvit Notes is a block editor for Markdown notes, written in C++ against Qt 6,
with its user interface written in QML and Qt Quick. It ships on Linux,
Windows and macOS, and it is the open half of an open-core product: a premium
agent module lives in a separate private repository and plugs into this tree
through a documented extension seam.

This document answers one question: what would it cost to rewrite the whole
application in Rust, drawing its interface with **GPUI**, the user-interface
framework the Zed editor is built on, and would the result be better or worse
than what exists. It is an investigation rather than a plan of record. Nothing
in it commits the project to anything, and the last two sections say plainly
what the evidence does and does not support. One section in the middle, "The
experiment: a text-only block editor in Rust", is different in kind: it
specifies a bounded piece of work that could be started tomorrow and that
would answer the single question the rest of the document has to estimate.

Two words are used throughout with specific meanings, following the sibling
document `planning/qwidgets.md`. **Shell** means the presentation layer:
everything that draws, accepts input, and routes it to a service. Today that
is 37,117 lines of QML across 95 files, plus 4,678 lines of binding code in
`src/qml`. **Services** means the C++ libraries underneath it — `content`,
`domain`, `search`, `platform`, `repository` and `application` — roughly
65,000 lines holding the block model, Markdown parsing and serialization, the
vault repository, the search index, the Mermaid diagram engine and everything
else with no pixels in it.

A related experiment already exists on this branch. `planning/qwidgets.md`
planned a replacement of the QML shell with one built from QWidgets, and
`planning/qwidgets-findings.md` records what the implemented vertical slice
measured. That experiment is the most useful comparison available for this
one, and the section "What the QWidgets experiment already settled" below uses
its results rather than repeating its reasoning.

## The short answer, before the detail

A Rust rewrite is a different kind of project from the QWidgets experiment,
and the difference is not a matter of degree. Replacing the QML shell with
widgets keeps roughly 65,000 lines of tested C++ services, keeps the hardest
component in the product unchanged, and keeps 48,390 lines of C++ tests
running as a regression net. Replacing Qt with Rust keeps none of that. Every
line of the 72,072 lines of C++ under `src/`, the 37,117 lines of QML, and the
67,203 lines of tests is either rewritten or discarded, and the vendored
40,169-line MicroTeX math typesetter has to be bridged or replaced.

Three findings shape the answer.

**GPUI has the element the document view needs.** Its `list` element does
variable-height, lazily-measured, recycled scrolling over a `SumTree`, which
is precisely the property that made `QListView` the risky choice in the
QWidgets plan and that Qt Quick's `ListView` provides today. The document
surface is not the thing most likely to fail here.

**The hybrid editor has no foundation to land on.** The reveal behaviour —
showing a span's raw Markdown only while the caret is inside it — is
implemented by `BlockEditorEngine` in terms of `QTextDocument` and
`QTextCursor`. The QWidgets experiment moved it to a `QTextEdit` and it worked
unchanged, because both surfaces are the same Qt document model. Rust has
nothing of that shape. GPUI gives shaped lines, styled runs and hit testing;
it does not give an editable rich-text document with cursors, undo-aware
mutation and per-run formatting. That layer would be new code underneath the
hardest component in the product. The reveal *semantics*, on the other hand,
are already pure functions over strings in `src/content/inlinemarkdown.cpp`,
which means a Rust port of them can be checked against the C++ original
exactly. That separation is what makes the experiment described below small
enough to be worth running.

**Accessibility became possible in Rust this year and is not yet finished.**
AccessKit support merged into GPUI on 2026-05-27, which removes what would
have been a decisive objection twelve months ago. Zed's own tracking issue for
missing Windows screen-reader support (#41138) was still open on 2026-05-22.
This repository holds a 37,000-word accessibility review, 427 explicit
accessibility assignments in QML and a guard test that fails a build without
them, so the standard being ported to is high and the platform being ported
onto is early.

## What GPUI is, and where it stood on 2026-08-09

GPUI is the user-interface framework Zed Industries wrote for the Zed editor
and now publishes for others to use. Applications describe an element tree in
Rust; layout is flexbox through the `taffy` crate; styling uses a
Tailwind-shaped builder API; rendering goes to the GPU. It is not a widget
toolkit in the Qt sense, in that it ships primitives — `div`, `text`,
`StyledText`, `img`, `svg`, `canvas`, `list`, `uniform_list`, `anchored`,
`deferred`, `animation` — and leaves buttons, menus, dialogs and text fields
to the application or to a third-party component library.

Facts worth having exactly right:

- **Publication.** The `gpui` crate on crates.io is at version 0.2.2,
  published 2025-10-22 under Apache-2.0, with no release since; that was still
  true on 2026-08-09. Standalone users therefore track the Zed monorepo as a
  git dependency, so updates arrive from a repository whose release tags mark
  Zed versions and say nothing about the framework's own compatibility.
- **Stability.** The crate README states GPUI "is still pre-1.0. There will
  often be breaking changes between versions."
- **Platform split.** Work through 2026 broke the framework into
  `gpui`, `gpui_platform`, `gpui_linux`, `gpui_macos`, `gpui_windows`,
  `gpui_wgpu` and `gpui_web`. macOS renders through Metal; Windows uses Win32
  and DirectWrite; Linux and FreeBSD need a Wayland or X11 backend selected by
  cargo feature.
- **Linux rendering.** The Blade graphics library was removed and the Linux
  renderer reimplemented on `wgpu` in pull request #46758, merged 2026-02-13.
  This matters here because Blade's Vulkan requirements were the cause of the
  "no supported device found" failures Zed users hit on machines without a
  hardware GPU, and `wgpu` widens the set of drivers that work.
- **Accessibility.** Pull request #56065, "gpui: Accesskit support", merged
  2026-05-27 across 27 files (+2,781/−58). The API it introduced is
  `Element::a11y_role()`, `on_a11y_action()` for dispatching accessible
  actions, `a11y_synthetic_children()` for elements that must present several
  accessibility nodes, and automatic identifiers for text with
  `Text::new_inaccessible()` as the opt-out. A follow-up exposing
  accessibility identifiers to platform clients closed 2026-08-07.
- **Video.** There is no media-playback element. `surface.rs` exists for
  screen-capture and call surfaces, and Zed does not play audio or video.
- **Testing.** `crates/gpui/src/platform/visual_test.rs` provides a platform
  that pairs real rendering with a deterministic test scheduler, and its own
  header says "macOs-only for now". A pull request adding offscreen headless
  rendering to the wgpu backend (#62341) was open on 2026-08-08.

A component library, `gpui-component` from Longbridge, supplies about sixty
desktop components on top of GPUI under Apache-2.0, including a text input, a
code editor with language-server support, virtualized tables and dock layouts.
Its documentation makes no accessibility claims. Whether to depend on it is a
real decision for the chrome, discussed under Stage 3.

## What the code already gives you, and what it does not

These figures come from the tree as it stands on 2026-08-09 and are what every
estimate later in this document rests on.

| Layer | Lines | Status under a QWidget shell | Status under a Rust rewrite |
|---|---:|---|---|
| `src/content` | 23,983 | Unchanged | Rewritten |
| `src/domain` | 12,903 | Unchanged | Rewritten |
| `src/repository` | 10,053 | Unchanged | Rewritten |
| `src/application` | 8,713 | Unchanged | Rewritten |
| `src/platform` | 6,341 | Three image providers replaced | Rewritten |
| `src/search` | 2,980 | Unchanged | Rewritten |
| `src/qml` | 4,678 | Mostly replaced | Rewritten |
| `qml/*.qml` | 37,117 | Replaced | Replaced |
| `third_party/microtex` | 40,169 | Unchanged | Bridged or replaced |
| `tests/test_*.cpp` | 48,390 | Nearly all unchanged | Rewritten |
| `tests/tst_*.qml` | 18,269 | Rewritten | Rewritten |

The middle column is the QWidgets experiment; the right column is this one.
The two columns are the whole argument in miniature.

Some parts of the C++ port cleanly and some do not, and the distinction is
worth stating before the risks:

- **The Mermaid diagram engine ports mechanically.** `src/content/diagrams`
  is 13,729 lines of lexer, parser, classifier, layout and repair for Mermaid
  source, plus a painter. Almost all of it is text in, geometry out, with no
  Qt dependency beyond `QString`, `QPointF` and `QRectF`. It is large, and it
  is the kind of large that a competent translation handles without design
  decisions.
- **The block model, commands and undo stack port mechanically.**
  `src/domain` is 12,903 lines of block kinds, a command pattern over them and
  an undo stack. The one structural change is that Qt's signal/slot mechanism
  disappears: 72 files declare `Q_OBJECT`, and the tree contains 273
  `Q_PROPERTY` declarations, 208 `connect()` calls and 499 `emit` statements.
  GPUI has its own reactivity — entities, `Context::notify()` and
  subscriptions — so this becomes a translation of one observer model into
  another rather than a search for a replacement.
- **The search index ports to `rusqlite`.** Only `src/search/searchindexdb`
  touches Qt SQL, and SQLite's FTS5 full-text engine is reachable from Rust
  through `rusqlite` with the `fts5` feature. This is 2,980 lines and the
  least risky part of the whole exercise.
- **The 511 `Q_INVOKABLE` methods are not a porting cost.** The macro exists
  only so QML can find the method; in Rust the question does not arise. What
  does arise is that every one of them is a place where QML calls into C++,
  and the shell rewrite has to re-establish each call site.
- **The platform layer becomes a collection of crates.** File watching goes to
  `notify`, HTTP to `reqwest` behind the same single egress policy that ADR
  0003 requires, the vault's advisory lock to `rustix` or `fs2` with the same
  kernel semantics, tray and global hotkey to the `tray-icon` and
  `global-hotkey` crates, images to `image` and `resvg`. None of these is
  exotic; all of them are separate integrations where Qt supplied one.

Three things have no comfortable Rust answer, and they are the ones that
should drive the decision.

**Rich text.** `QTextDocument` is used for far more than the editor. The
HTML-to-Markdown importer parses through it, the exporter lays out HTML and
prints through `QPdfWriter`, the inline Markdown renderer builds its output
as HTML for it, and the code highlighter is a `QSyntaxHighlighter` over it.
Rust has parsers (`html5ever`) and rasterizers, and it has GPUI's shaped-text
primitives, without anything that plays the role of an editable, styled,
laid-out document that all four of those uses share. The PDF path is the
sharpest instance: today it is "render this HTML through the same layout
engine the editor uses, then write a PDF", and in Rust it becomes "lay the
document out yourself, then emit PDF with `printpdf`".

**Math.** MicroTeX is 40,169 lines of C++ vendored under `third_party`, and
this repository has patched it in at least ten files, including generated font
definition tables prefixed `kvit_`. Rewriting it is not proportionate. The
practical answer is to keep it as C++ and bridge it, and the tree makes that
easier than it sounds: MicroTeX already abstracts its drawing behind a
`Graphics2D` interface with four backends in `src/platform` (`cairo`,
`gdi_win`, `qt`, `skia`), and the Qt backend is 506 lines. A Rust port writes
a fifth backend that draws into a Rust-owned pixel buffer, or reuses the cairo
one and takes the surface across, then binds the parse-and-render entry points
with `cxx`. The bridge is a bounded piece of work on a seam that already
exists. It does mean the "Rust rewrite" ships a C++ compiler dependency and a
C++ library forever, which is worth stating out loud before anyone describes
the result as an all-Rust application. The Rust alternatives were checked:
ReX describes itself as not intended for use beyond testing and debugging,
`pulldown-latex` targets MathML output rather than a rasterized formula, and
RaTeX claims wide KaTeX coverage but would be a new and unproven dependency
under the product's most visually exacting feature.

**Media playback.** `qml/MediaBlock.qml` uses Qt Multimedia's `MediaPlayer`
for audio and video blocks. GPUI has no element for this and Zed has no such
feature, so the options are binding GStreamer or FFmpeg, shelling out to an
external player, or dropping the block kind. Of the three, dropping it is a
product decision rather than an engineering one.

## The five risks

Ordered by how likely each is to end the project. Everything in the staged
plan further down exists to answer one of them.

**1. There is no rich-text document model to build the reveal editor on.**
`BlockEditorEngine` (`src/qml/blockeditorengine.{h,cpp}`) attaches to a
`QTextDocument` and inserts or removes marker characters as the cursor moves,
which is how a span shows `**bold**` while the caret is inside it and renders
as bold when the caret leaves. Its correctness depends on the document model
underneath: cursor positions that survive edits, character formats per run,
and a layout that can be re-measured cheaply. In Rust that model is the
application's to build — a rope for the text, a run-based format layer over
it, mapping between byte offsets and shaped-glyph positions, and cursor
movement that respects grapheme clusters and bidirectional text. GPUI provides
the pieces the layer sits on: `StyledText` with `TextRun`s, `TextLayout::index_for_position`,
and `LineLayout::{index_for_x, closest_index_for_x, x_for_index}` for hit
testing. It does not provide the layer. This is the risk that can end the
project, and it is the exact place where the QWidgets experiment got its
biggest reuse.

**2. Accessibility parity is unproven on GPUI.** The QML tree carries 427
explicit `Accessible.*` assignments, `tools/check-accessible-names.py`
enforces them as the `AccessibleNameGuard` test, and `accessibility.md`
records seven findings with a work plan against them. GPUI's AccessKit
integration is ten weeks old as of this writing and its own documentation
module is silent on live regions, focus management and accessible names as
distinct from roles. The specific question this project has to answer is
narrower than "is GPUI accessible": it is whether a document view of several
thousand painted blocks can present a useful accessibility tree, and whether
the announcement path that `AccessibilityAnnouncer` uses today has an
equivalent.

**3. Verification would start from nothing.** The tree has 1,382 C++ test
functions across 83 files and 479 interface test cases in QML. Under the
QWidgets experiment nearly all of the C++ half kept running, which is what
made that branch safe to work on. Under a Rust rewrite none of it runs, and
the new code has no oracle other than the old code's behaviour, most of which
is written down in `features.md` but some of which lives only in the tests.
Rust's own testing story is good and `cargo test` is faster and steadier than
the Qt Quick suites on this machine; the problem is the interval during which
the rewrite has neither the old tests nor a complete set of new ones.

There is one exception, and it is large enough to change how the riskiest part
of the port should be approached. Wherever the C++ is a pure function of its
inputs, the existing binary can be run as a reference implementation and the
Rust port compared against it case by case. The inline Markdown mapping,
which is exactly the component risk 1 is about, is pure in this sense. The
experiment section below builds that comparison harness and treats it as the
main deliverable.

**4. Screenshot and visual verification would get harder before they get
easier.** CLAUDE.md records that no automated suite here can see
rendering-driver bugs because every test renders on CPU, and that the real
interface has to be observed through `kvit-uidriver` with 46 scenarios. GPUI's
deterministic visual-test platform is macOS-only by its own description, and
offscreen headless rendering for the wgpu backend was an open pull request on
2026-08-08. On this machine the picture is mixed: the `GALLIUM_DRIVER=llvmpipe`
pin exists because Mesa's `d3d12` driver corrupts Qt Quick text under WSLg,
and GPUI does not use OpenGL at all, so that specific bug would not follow the
port. What follows instead is a dependency on Vulkan through `wgpu`, with
lavapipe (`/usr/share/vulkan/icd.d/lvp_icd.json` is present) as the software
fallback, and Zed's own documentation warns that software-emulated rendering
performs poorly.

**5. The premium module and its seam would have to be rebuilt.** ADR 0006
defines the extension seam as block kinds, QML context objects and three empty
`Loader` slots in `qml/main.qml`, and `src/main.cpp` reaches the private agent
module through two `#ifdef KVIT_AGENT` fragments. All of that is C++ and QML.
A Rust rewrite invalidates the seam's design and the private tree that plugs
into it, which is a cost outside this repository and outside any estimate made
from the line counts in it.

## What the QWidgets experiment already settled

The experiment on this branch built a vertical slice of the editor on
QWidgets and measured it against the Qt Quick shell using the repository's
`PerfCorpus::warAndPeace()` fixture of 6,241 blocks and about 562,900 words.
Its numbers are relevant here because they establish what problem, if any, a
rewrite would be solving.

| Measurement | Qt Quick shell | QWidget slice |
|---|---:|---:|
| Note/model load | 56.19 ms | 48.73 ms |
| Load through first frame | 817.00 ms | 58.11 ms |
| Scroll average | 15.72 ms | 8.29 ms |
| Scroll p95 | 18.98 ms | 9.40 ms |

The two scroll figures are measured differently and are not interchangeable
benchmarks, but the first-frame result is a fourteen-fold improvement obtained
without leaving C++, without touching the services and without rewriting a
single test. If the motivation for looking at Rust is that the application
feels slow to open a large note, that motivation is already addressed by a
change an order of magnitude smaller than a rewrite.

The experiment also produced two findings that apply to any future shell,
including a Rust one. `AppContext` composes and opens a vault with no QML
engine in the process, so a second binary can reuse the composition root
without carrying Qt Quick with it. And a
list of QML files — `NoteSession.qml`, `CrossBlockTextSelection.qml`,
`CrossBlockTextDrag.qml`, `RenderedTextSelection.qml`, `BlockSelectionKeys.qml`,
`AppShortcuts.qml`, `SessionPersistence.qml` — hold behaviour rather than
pixels and should move into `src/application`. Roughly 3,500 lines of QML
across seventeen files fall into that category. Moving them is worth doing
whatever happens here, and doing it first would make a Rust port's shell
smaller and its service port larger, which is the direction that favours a
mechanical translation.

## The decision that governs everything

How the document surface is built decides whether this is a large but ordinary
translation or an open-ended research project. There are four shapes.

**(a) Full rewrite: Rust services, GPUI shell, MicroTeX bridged.** Everything
under `src/` becomes Rust. The document view is GPUI's `list` element over a
Rust block model, with a purpose-built rich-text layer beneath the editing
surface. MicroTeX stays as C++ behind a `cxx` bridge with a new `Graphics2D`
backend. This is the shape the rest of this document estimates.

**(b) Rust shell over the existing C++ services, through an FFI bridge.**
Superficially attractive, because it keeps 65,000 tested lines. It does not
survive contact with the interface between them: the shell calls into
services at 511 `Q_INVOKABLE` entry points and observes them through 273
properties and 51 signal blocks, and every one of those crossings would need a
hand-written or generated binding, with Qt's event loop on one side and GPUI's
executor on the other. The bridge would be larger than the shell. It is worth
naming so that it can be ruled out deliberately.

**(c) Rust services under the existing QML shell.** The mirror image, and
worse: QML's binding system needs `QObject`s with properties and signals, so
the Rust side would have to impersonate the Qt object system through something
like `cxx-qt`. That trades a working C++ tree for a Rust tree that must
reproduce Qt's meta-object behaviour exactly.

**(d) Do not rewrite. Extract the behavioural QML into `src/application`, and
treat the QWidgets findings as the answer to whatever prompted the question.**

**Recommendation: (a) if the decision is taken, and (d) unless something not
yet stated makes (a) necessary.** The reasoning for preferring (a) over (b)
and (c) is that partial ports across a language boundary of this shape cost
more than they save, and both intermediate options leave the project
permanently maintaining two object models. The reasoning for (d) is in the
final section.

## Branch and build layout

If the investigation proceeds, it proceeds as a separate tree, not as a
directory inside this one. A Cargo workspace under `rust/` inside a Qt/CMake
repository would fight the layering guard, the resource-list guard and the
CI matrix without any of them being wrong. The right shape is a sibling
repository, `kvit-notes-rs`, with the vault format, the Markdown dialect and
`features.md` as the shared contract between the two.

Within it, a workspace whose crate names mirror the current libraries keeps
the two trees legible against each other:

```
crates/content/      markdown, html, tables, kanban, diagrams, math bridge
crates/domain/       blocks, kinds, commands, undo
crates/repository/   vault, files, index, trash, backups
crates/search/       rusqlite + FTS5
crates/platform/     settings, watcher, egress, tray, hotkey, theme
crates/application/  the controllers, including the ones extracted from QML
crates/text/         the rich-text document layer described under risk 1
crates/shell/        the GPUI element tree
crates/microtex-sys/ the cxx bridge and the new Graphics2D backend
```

`tools/check-layering.py` has an obvious Rust equivalent in Cargo itself: the
dependency direction the guard enforces is expressible as workspace
dependencies, and a cycle is a build error rather than a test failure. That is
a real and unglamorous advantage of the move, and it should be recorded as one.

## The experiment: a text-only block editor in Rust

Everything above says the outcome turns on one component. The experiment
described here builds that component and nothing else that can be avoided: a
working block editor in Rust and GPUI that handles plain Markdown text, with
the reveal behaviour that defines the product, and without math, diagrams,
tables, boards, images or media. It is scoped to be finished and judged, then
thrown away, in the same spirit as the QWidgets branch.

The question it answers is narrow and worth stating in one sentence: can the
hybrid reveal editor be rebuilt on GPUI's text primitives, correctly enough to
match the existing implementation character for character, and fast enough to
type in a large note?

### What it builds

A single-window application that takes a directory of `.md` files on the
command line, lists them, and opens one as a document of blocks. Within that
document:

- **Twelve block kinds**, the same set the QWidgets spike used because they
  need no custom chrome: paragraph, headings one through four, bullet and
  numbered lists, to-do, quote, code block, divider and callout. That is
  twelve of the twenty-one built-in kinds, and it covers the entire hybrid
  editing story.
- **The full inline grammar minus math.** The span registry in
  `markdownformatter.cpp` has eighteen rows producing fourteen format kinds:
  bold, italic, bold-italic, strikethrough, underline, highlight,
  superscript, subscript, inline code, colour spans, backslash escapes,
  wiki links, links and autolinks. Only `math` is dropped. Cutting the grammar
  further would make the experiment prove less than it costs, because most of
  the difficulty lives in nesting and in the marker-matching rules rather than
  in the number of kinds.
- **Reveal**, to the specification in `features.md` §2.2 and the comment
  block at the top of `src/qml/blockeditorengine.h`: a collapsed cursor
  reveals the span whose markdown range contains it with both edges inclusive;
  a selection reveals every span it touches; an unfocused block reveals
  nothing; the reveal unit is the top-level span, so a nested subtree's
  markers appear and disappear together; a code span's content is verbatim.
- **Editing across blocks**: Enter splits, Backspace at the start merges,
  arrow keys hand the caret between blocks at the same horizontal offset,
  and typing `# ` at the start of a paragraph converts it to a heading with
  the caret surviving.
- **Selection across blocks**, with copy producing the same Markdown the C++
  implementation produces for the same range.
- **A minimal undo stack** over block edits. This is in scope for one
  specific reason: the C++ engine's model-to-document sync exists to keep the
  cursor stable across undo, redo and load rebuilds, and an experiment that
  never exercises that path would miss a known source of difficulty.
- **Files on disk**, read and written directly. No vault repository, no
  index, no lock, no trash, no backups.

### What it leaves out, and why that is safe to leave out

The nine block kinds excluded are image, media, math, table, kanban board,
table of contents, embed, Mermaid diagram and query. Each of them is
predominantly chrome: a layout, a set of controls and a rendering path.
Skipping them removes the MicroTeX bridge entirely, which means the experiment
needs no foreign-function interface, no C++ toolchain and no vendored
third-party tree; it also removes the diagram engine's 13,729 lines, which are
a translation exercise with no unknowns in them.

What matters is that none of the nine changes the answer to the question being
asked. The reveal engine runs inside a text block. A table cell and a board
card each host the same editing surface, so if the surface works in a
paragraph it works in them, and if it does not work in a paragraph nothing
downstream matters.

Also out of scope: search, the sidebar and note-list panes, dialogs, settings
persistence, multiple windows, themes beyond one hard-coded palette, and
packaging.

### Why this is cheaper than it looks

Reading the engine closely produces a result that reshapes the estimate. The
reveal semantics are already separated from Qt's document model. The mapping
vocabulary lives in `src/content/inlinemarkdown.{h,cpp}` — 976 lines of pure
functions over strings, described in its own header as "the pure transition
tables this class drives a live `QTextDocument` with". They are:

```
displayText(md)                          markers stripped
documentText(md, revealed[])             what the document should hold
documentToMarkdown / markdownToDocument  position mapping in a reveal state
spanToReveal(md, mdPos)                  which span a cursor reveals
spansToRevealForRange(md, start, end)    which spans a selection reveals
formatRangesForState(md, revealed[])     the styled runs to draw
applyDocumentEdit(md, revealed[], …)     a document edit, back into markdown
markdownForRange / cutRangeResult        copy and cut
formatFlagsAt / linkSpanIn               what the caret is inside
```

Underneath them sits the parser in `src/content/markdownformatter.cpp`,
another 1,711 lines, which is table-driven: a registry of span types with
their markers and a documented scan that tries the rows in order at each
position. So the pure logic to port is roughly 2,700 lines with clear
boundaries, and the remaining 1,548 lines of `BlockEditorEngine` are the
driver that applies those functions to a live `QTextDocument`.

That split has two consequences. The pure part can be translated and then
checked against the original by running both, which is the subject of Step 1
below. And a good deal of the driver has no Rust counterpart: the minimal
prefix/suffix diff, the internal-edit guard that distinguishes the engine's
own edits from the user's, and the deferral of reveal evaluation off
`QQuickTextEdit`'s signal dispatch all exist because a `QTextDocument` is
mutable shared state that something else is also reading. A GPUI block that
re-renders from `(markdown, cursor, selection, revealed)` on every frame does
not have that problem to solve. It trades it for re-shaping text on each
keystroke, which is the thing Step 5 has to measure.

### Where it lives

A scratch repository, `kvit-reveal-spike`, outside this tree. It has no
CMake, no Qt and no relationship to the layering guard.

One addition is needed in *this* repository, and it is additive in the way
`KVIT_UI_DRIVER` and `KVIT_WIDGETS` already are: a `tools/oracle.cpp` behind
an `option(KVIT_ORACLE ... OFF)` block, linking `kvit-core`, modelled line for
line on the existing `kvit-uidriver` block in `CMakeLists.txt`. A default
configure builds exactly what it builds today.

### Step 1: the oracle

`kvit-oracle` reads newline-delimited JSON requests on standard input and
writes one JSON result per line. Each request names one of the pure functions
listed above and carries its arguments; each response carries the return
value. That is a few hundred lines over `kvit-content`, and it turns the
existing C++ implementation into a reference the Rust port can be tested
against.

Two corpora drive it.

**Harvested cases.** `tests/test_blockeditorengine.cpp` is 2,119 lines holding
85 test cases, and the Markdown strings, cursor positions and edits in them
are the accumulated knowledge of what this engine has to get right. Extract
them into a JSON vector file that both trees read.

**Generated cases.** A `proptest` generator over an alphabet weighted towards
markers — asterisk, underscore, backtick, tilde, equals, caret, brackets,
parentheses, backslash — producing strings of ten to two hundred characters,
each paired with a random cursor position and a random edit. Run millions of
them. Divergence between the two implementations is either a bug in the port
or an ambiguity in the specification, and both are worth finding.

The corpus file is worth committing to this repository whatever the experiment
concludes, because it documents the engine's behaviour in a form no test
framework owns.

### Step 2: the pure port

Translate `inlinemarkdown` and the parser from `markdownformatter` into a
Rust crate with no dependency on GPUI, running the differential harness from
Step 1 continuously. The unit here is character offsets: the C++ works in
UTF-16 code units because that is what `QString` indexes, and Rust strings
index bytes. Pick one representation for the port, convert at the oracle
boundary, and write that decision down, because getting it wrong produces
failures that look like parser bugs.

This step has a definite finish and an exact success criterion, which is
unusual for a rewrite and is the reason to do it before anything with pixels
in it.

### Step 3: the document layer

The part with no counterpart to copy. It has to:

- hold a block's display text and hand GPUI a `StyledText` whose `TextRun`s
  come from `formatRangesForState`;
- place the caret from a click through `TextLayout::index_for_position`, move
  it by grapheme cluster, and move it vertically with `x_for_index` and
  `closest_index_for_x` so up and down keep a column;
- accept keyboard input through a focus handle and GPUI's key dispatch,
  including the platform's input-method preedit for composed text;
- apply each edit by calling the ported `applyDocumentEdit` and re-deriving
  the render state, rather than mutating a document in place;
- recompute the revealed set from the cursor or selection after every change,
  and paint selection ranges within and across blocks.

Do not borrow `gpui-component`'s text input here. The question is whether this
layer can be built on the primitives; starting from someone else's input
answers a different question and leaves the estimate unimproved.

### Step 4: the block list and a window

GPUI's `list` element over the block model, one element per block, with the
twelve kinds' block-level styling: heading sizes, list markers, quote rule,
to-do checkbox, callout background, divider. A note-list pane down the left
side only so that switching notes exercises the load path.

The `list` element measures lazily through a `SumTree` and asks only that
off-screen elements not change height without telling it, which a block model
can honour. This step is expected to be uneventful; it exists so that Step 5
has something to measure and so that the block-boundary editing rules have a
place to run.

### Step 5: measure

Against baselines this repository already records, using the War and Peace
fixture in `tests/perf/corpus.cpp` — 6,241 blocks, about 562,900 words —
exported to Markdown so the Rust tree can open it.

| Measurement | Qt Quick shell | QWidget slice | Rust and GPUI |
|---|---:|---:|---|
| Note/model load | 56.19 ms | 48.73 ms | to fill in |
| Load through first frame | 817.00 ms | 58.11 ms | to fill in |
| Scroll average | 15.72 ms | 8.29 ms | to fill in |
| Scroll p95 | 18.98 ms | 9.40 ms | to fill in |
| Keystroke to frame, 10,000-char block | not recorded | not recorded | to fill in |

The last row is new and is the number that matters most for this component.
Neither existing measurement captures it, and it is where a
render-from-state design pays its cost, so capture it on the Qt Quick shell
too while the comparison is being set up.

Also record the size of what was written, split into the pure port, the
document layer and the shell, so the ratio can be compared with the QWidgets
experiment's figure of about 1,570 lines for its document view and editor.

One narrow accessibility check belongs here as well, at a cost of perhaps a
day: put a role and a name on the block list and on one block through
`a11y_role()`, then run the platform's screen reader and write down what it
says. This does not settle risk 2, and it converts an unknown into an
observation.

### The gate

Continue past the experiment only if all of these hold.

1. The differential harness reports zero divergences over the harvested
   corpus and over at least a million generated cases.
2. Reveal behaves correctly in all twelve block kinds, `# ` conversion
   preserves the caret, and Enter, Backspace and arrow handoff work at block
   boundaries.
3. A sweep across three blocks copies Markdown identical to what the C++
   `markdownForRange` returns for the same range.
4. Time to first frame on the large corpus is within twice the QWidget
   slice's 58.11 ms, no scroll frame exceeds 100 ms, and keystroke-to-frame
   in a 10,000-character block stays under 16 ms.
5. The document layer from Step 3 comes in under about 4,000 lines. If
   reaching this feature set costs more than that, the low-confidence row in
   the estimate table was optimistic, and the full grammar with math,
   tables and boards on top of it would be considerably worse.

Stop the experiment, rather than continuing under a caveat, if the document
layer cannot express reveal without diverging from the C++ semantics in cases
the harvested corpus covers, if input-method composition cannot coexist with
programmatic marker insertion, or if GPUI's pre-1.0 churn breaks the spike
more than once during its run.

### What the experiment costs

| Step | Estimated lines | Confidence |
|---|---:|---|
| Oracle binary in this repository | 300–500 | High |
| Corpus extraction and differential harness | 400–800 | High |
| Pure port: `inlinemarkdown` and the parser | 1,800–2,600 | Medium |
| Document layer | 2,000–4,000 | Low |
| Block list, twelve kinds, window | 1,200–2,000 | Medium |
| Measurement harness | 300–600 | Medium |

That is roughly 6,000 to 10,500 lines. The QWidgets experiment reached its
verdict in 2,644 lines including tests and its capture driver, and the
difference is almost entirely the two things that branch got for free: the
parser and mapping functions it reused, and the `QTextDocument` it attached
to.

### What to write down before deleting it

Into `planning/rust-findings.md`, following the shape of
`planning/qwidgets-findings.md`:

1. The differential result: how many cases ran, how many diverged, and what
   each divergence turned out to be.
2. The measurement table above, filled in, with the machine and the renderer
   in use recorded beside it.
3. The size of the document layer, and a judgement of what the full inline
   grammar including math would add to it.
4. Whether the render-from-state design removed the class of problem the
   `QTextDocument` guard and minimal-diff machinery exist for, or moved it
   somewhere else.
5. What the screen reader said.
6. How many times upstream GPUI changes broke the spike, and how long each
   took to repair. This is the only direct evidence anyone will have about
   the cost of tracking a pre-1.0 framework.
7. A plain judgement of how the Rust editor felt to type in beside the
   shipped one, written while both were open.

## Stage 1: the services

With the experiment's gate passed, port the services bottom-up in dependency
order, each
crate with its tests translated alongside it rather than afterwards.
`domain` and `content` first, because everything depends on them and because
the diagram engine and the block kinds are the largest mechanical translations
in the tree. `repository` and `search` next, where the vault format is the
contract that must not drift: the Rust tree must open a vault the C++ tree
wrote and write one the C++ tree can open, and there should be a test that
asserts it against a fixture vault committed to both repositories.

The `content` crate is where the math bridge lands, and it should land early
rather than late, because it is the one piece whose feasibility rests on an
FFI seam nobody here has built yet.

**Exit check:** a command-line binary that opens a vault, reads every note,
serializes each one back and reports zero differences against the C++ tree's
output over the full performance corpus.

## Stage 2: the document surface and editing

The experiment already produced the shape of this stage: the `list` element
over a block model, the document layer, and the twelve block kinds that need
no custom chrome. What Stage 2 adds is the work the experiment deliberately
skipped: math spans in the inline grammar, the reveal interaction with
inline formulas, and the connection to the real ported services in place of
the spike's direct file reads. Code blocks get syntax highlighting from
`tree-sitter` or `syntect`, both of which are stronger than the hand-written
`CodeHighlighter` they replace.

How much of the spike's code survives into this stage is itself a finding to
record, since a spike that has to be rewritten rather than absorbed changes
what the exercise was worth.

Cross-block selection is the part that transfers most directly from the
existing design, because `DocumentSelection` is a service and its
`portionForBlock()` shape is language-independent. The drawing changes; the
model does not.

## Stage 3: chrome, and the component-library decision

The sidebar, note list, toolbar, status bar, menus, splitters and the nine
dialogs. This is where the choice about `gpui-component` gets made, and the
trade is specific. Taking it saves a large amount of undifferentiated work —
menus, dialogs, tables, docks and a text input already exist there under
Apache-2.0. It also adds a second pre-1.0 dependency tracking a fast-moving
framework, and its documentation makes no accessibility claims, so anything it
draws would need auditing against the standard `accessibility.md` sets. The
recommendation is to take it for dialogs and chrome, and to own the document
surface and anything a screen reader must traverse.

Native menus are worth a note. Fusion's Qt Quick style draws every menu label
with `palette.text` whatever the entry's state, which is why this project has
`DiscoverableMenuItem` and a guard test for it. Whatever GPUI does here, it is
a different problem, and the specific workaround disappears.

## Stage 4: the specialized block kinds

In the order the QWidgets plan used, for the same reasons: divider, image and
media; math, which is the bridge plus a rendered image; Mermaid diagrams,
where the ported layout engine feeds a GPUI `canvas`; table of contents, embed
and query, which draw selectable rendered text and depend on the same hit
testing the editor uses; then the table, and last the task board, which at
2,627 lines of QML is the largest single file in the shell. If the project is
going to be abandoned, it should be abandoned before the task board.

The media block needs its product decision here rather than an engineering
one.

## Stage 5: accessibility, tests, tooling and packaging

Accessibility becomes real work at this point rather than a checkbox: roles
through `a11y_role()`, actions through `on_a11y_action()`, and a decision
about how a document of thousands of blocks presents itself, probably through
`a11y_synthetic_children()` on the list. The guard that replaces
`check-accessible-names.py` can be stricter in Rust than in QML, because the
element builders are typed and a lint can require a role on anything that
handles a click.

Packaging is the one place where the Rust tree is straightforwardly better. A
single static binary replaces `windeployqt`, `macdeployqt`, the AppImage
runtime and the Flatpak's Qt dependencies. Code signing and notarization stay
exactly as difficult as ADR 0007 describes, because those are organizational
rather than technical costs.

## Estimating the size

These are estimates derived from structure and line counts, and nothing here
was measured by building any of it. Rust
typically expresses the same logic in fewer lines than C++ with paired headers
and implementation files, which is where the reductions come from.

| Area | Replaced | Rust code, estimated | Confidence |
|---|---|---:|---|
| Spikes A and B | — | 500–1,500 | High |
| `domain` + `content` less diagrams | 23,157 | 14,000–18,000 | Medium |
| Diagram engine | 13,729 | 9,000–12,000 | Medium |
| `repository` + `search` | 13,033 | 8,000–11,000 | High |
| `platform` | 6,341 | 4,000–6,000 | Medium |
| `application` + extracted QML controllers | 12,213 | 7,000–10,000 | Medium |
| Rich-text document layer | nothing equivalent | 6,000–12,000 | Low |
| MicroTeX bridge and backend | 506 | 600–1,200 | Medium |
| Shell | 37,117 | 20,000–30,000 | Low |
| Tests | 67,203 | 25,000–45,000 | Low |

That totals roughly 94,000 to 147,000 lines of new Rust, against a tree of
about 176,000 lines of C++, QML and tests today. The confidence column matters
more than the numbers, and the two low-confidence rows that decide the outcome
are the rich-text layer, which has no counterpart to measure against, and the
tests, where nobody has yet written one and found out what it costs.

One number deliberately absent is calendar time. The git history here begins
with a single commit of 179,888 lines labelled "Initial public release", so it
records when the tree was published rather than how long it took to write, and
using it to estimate a rewrite would be reading something into it that is not
there.

## What would make us stop

- The experiment fails its gate, in any of the five ways that section lists.
  Reusing `BlockEditorEngine`'s design on top of a modest new document layer
  is the premise the whole estimate rests on.
- GPUI's accessibility API cannot represent a large document view in a form a
  screen reader can navigate, measured against what `accessibility.md`
  requires rather than against Zed's current state.
- The vault round-trip test at the end of Stage 1 shows the Rust tree writing
  notes the C++ tree reads differently. Silent divergence in the file format
  is worse than any performance result, because ADR 0001 makes the files on
  disk authoritative.
- The MicroTeX bridge turns out to require changes inside MicroTeX beyond a
  new `Graphics2D` backend, since the vendored copy is already patched and a
  second set of patches on a second seam compounds.
- GPUI's pre-1.0 churn costs more per month than the port advances. This is
  measurable: track how many hours a month go to updating against upstream
  breaking changes during Stages 1 and 2, when the shell surface is smallest,
  and extrapolate.

## The recommendation

Nothing found in this investigation says a Rust and GPUI rewrite is
impossible. GPUI has the list element the document needs, the text hit-testing
selection requires, an accessibility integration as of May 2026, and a
rendering backend on Linux that no longer depends on the Vulkan features that
gave Zed its worst installation failures. MicroTeX has a clean backend seam
that makes bridging it a bounded job rather than a rewrite, which is the most
useful thing this investigation turned up about the hardest-looking
dependency.

What the investigation does not find is a problem that the rewrite solves. The
performance question already has an answer on this branch, obtained in C++ at
a fraction of the cost. Packaging improves, dependency direction becomes a
compile error instead of a guard test, and the code-highlighting and search
stories get slightly better. Set against that: about 176,000 lines of working,
tested, shipped code stops being an asset, the single hardest component loses
the foundation it is built on, the accessibility standard this project holds
itself to would be re-established against a ten-week-old API, and the private
premium module and its seam would have to be redesigned.

The judgement offered here, which is the author's and not a measurement, is
that the case for (d) is stronger than the case for (a): extract the
behavioural QML into `src/application` as both this document and the QWidgets
findings recommend, keep the shell that works, and revisit Rust if a
motivation appears that the current tree cannot serve.

That judgement should not decide the question on its own, because it rests on
an estimate of one component nobody has built. The text-only experiment
described above is the cheapest way to replace the estimate with a
measurement. It runs in a scratch repository, adds one optional binary to this
one, and produces a differential correctness result and a keystroke latency
figure that would be worth having on file even if the answer is that the
rewrite should not happen.
