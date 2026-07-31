# Block architecture

## What this is

A note is a list of blocks, and there are twenty-one kinds of them. Everything
decided per kind is a pure virtual function on one class, `BlockKindDef`, with
one subclass per kind. The exporter, the serializer, the search indexer, the
outline, the typography and the block menu call through the base pointer and
do not know which kinds exist:

```cpp
html += b.kind()->toHtml(b.state(), ctx);      // instead of switch (b.type())
```

A kind that has not answered something does not compile.

Those classes describe kinds and hold no content. `Block` is one concrete
class holding the data, and a conversion swaps a pointer rather than replacing
an object. The arrangement where `QueryBlock` derives from `Block` and owns
the content was the first design considered; it is recorded under "Rejected:
one subclass per kind, deriving from Block" with the reasons it does not fit
this tree.

This document is the reasoning. `src/domain/blockkinddef.h` is the interface
itself, and each virtual there names the call sites it answers for.

## Why

### The defect that prompted it

`DocumentExporter::buildHtmlBody` used to render a code block by testing its
fence language:

```cpp
case Block::CodeBlock:
    if (b.language == "mermaid")      { /* diagram */ }
    else if (b.language == "diagram") { /* preformatted */ }
    else if (b.language == "kanban")  { /* board */ }
    else if (b.language == "toc")     { /* anchor list */ }
    else                              { /* highlighted code */ }
```

A `query` fence matched none of those branches, so every HTML and PDF export
printed the query's `from:`/`where:` spec as a code listing, which is the one
part of the block a reader never sees on screen. The block was otherwise
complete: it parsed, it saved, it rendered live, it sat in the slash menu, it
had tests.

Plain-text export had the same hole and it was wider. `buildPlainText` emitted
a code block's content verbatim, so a `.txt` export wrote the query spec, the
raw kanban markdown and the Mermaid source.

Neither was a decision anyone made. Both are what a switch with a `default:`
label does when a kind is added and the person adding it does not know the
switch is there.

### A block existed as three types, and one of them had lost a field

```cpp
// live editing
class Block { BlockType m_type; QString m_content; ...; QString m_attributes; };

// parse output
struct DocumentSerializer::BlockData { BlockType type; ...; QString attributes; };

// export input
struct DocumentExporter::Blk { BlockType type; ...; /* no attributes */ };
```

`Blk` was a copy of `BlockData` with one member left out, and both loops that
built it dropped `attributes` silently. That is why alignment, drop caps,
divider styles, image effects, embed sizes and table column widths were
missing from every export. A struct was copied and a field was not.

There is one snapshot type now: `Block::State`, which the model holds, the
parser produces and the exporter renders. `DocumentSerializer::BlockData` is
an alias for it.

### Where block knowledge used to live

Thirteen places had to agree about a kind, and each of them was a separate
opportunity to forget one:

| What is decided | Where it was |
|---|---|
| Recognising a kind while reading a file | `DocumentSerializer::parse`, a line scanner |
| Writing it back to markdown | `DocumentSerializer::serializeBlock`, a switch |
| Which QML delegate draws it | `BlockKindRegistry` plus seventeen `DelegateChoice` blocks in `main.qml` |
| What the delegate must implement | `BlockDelegateBase.qml` |
| Its slash-menu and turn-into entry | `BlockMenuModel`, a 23-entry catalog literal |
| HTML and PDF export | `DocumentExporter::buildHtmlBody` |
| Plain-text export | `DocumentExporter::buildPlainText` |
| The text word counts, snippets and search see | `Block::displayText` |
| Whether its text is verbatim | `type == Block::CodeBlock`, in nine places across four modules |
| Whether "remove line breaks" applies | `foldsLineBreaks` in `blockmodel.cpp` |
| Its heading level | `headingLevel` in `documentoutline.cpp` |
| Its font size | `Typography::sizeForBlockType` |
| Whether the alignment buttons apply | `alignableTypes: [0, 1, 2, 3, 10, 11]` in `Toolbar.qml`, and the same literal again in `EditorContextMenus.qml` |

Every row is now a virtual on the kind. `parse` is the exception and is
discussed at the end.

## The design

### Two questions, answered separately

These are easy to run together, so they are stated apart:

- **Where does per-kind behaviour live?** In one class per kind, with pure
  virtuals, so the compiler refuses a kind that has not answered everything.
- **What holds a block's data?** One concrete `Block`, as before.

Each subclass describes one kind. None derives from `Block` or holds a block's
content. A block points at the singleton describing its kind:

```cpp
class Block : public QObject {
    const BlockKindDef *m_kind;    // never null, never owned
    ...
};
```

### The kind space is one enumeration

A kind used to be three unrelated things at once: a stored block type, a fence
language compared as a raw string, and a plain `int` spanning three ranges that
the QML delegate chooser watched. A switch over an `int` gets no checking from
a compiler.

`BlockKind` (`src/domain/blockkind.h`) is the one enumeration, at the values
already in use, so nothing persisted changed:

```cpp
enum class Kind : int {
    Paragraph = 0, Heading1 = 1, Heading2 = 2, Heading3 = 3,
    BulletList = 4, NumberedList = 5, Todo = 6, Quote = 7,
    CodeBlock = 8, Divider = 9, Heading4 = 10, Image = 11,
    Callout = 12, MathBlock = 13, Media = 14, Table = 15,
    Kanban = 100, Toc = 101, Embed = 102, Mermaid = 103, Query = 104,
    // module kinds are >= 200 and never enumerators here
};
```

Resolution takes the whole `Block::State`, not a type and a language: one kind
is decided by content. An image expression whose URL names a web page rather
than an image file is a preview card.

`BlockKind` is a key that identifies a kind. It is not what features switch
on, and the only switch left over it is `Typography::sizeForRole`, over the
font role rather than the kind.

**The delegate value is derived separately, and deliberately.** `Paragraph`
and all four heading levels share one delegate and publish `0` between them.
Those five zeros are what keep paragraph-to-heading — the most common
conversion in the editor, run every time someone types `# ` at the start of a
line — from destroying the delegate the caret is sitting in.
`BlockKindDef::delegateKind()` is that derivation, and it is the only place it
exists.

### One class per kind

`src/domain/blockkinddef.h` holds the interface. It has nineteen pure
virtuals, grouped as identity, storage, the three text projections, the
predicates, output and UI. Two of them are not in the obvious list and both
are forced by a call site:

- `unfoldableTail()` — the trailing run of the content a line fold must leave
  alone. Only the quote has one: its attribution sits on its own last line as
  chrome, and folding the newline in front of it turns it into body text. A
  bare `foldsLineBreaks()` bool would have left that case behind in the caller
  as an orphaned comparison against one block type.
- `attributeTagRidesOpeningLine()` — where the block's `<!--kvit …-->` tag
  goes. True for exactly four kinds, and the reason is not that they are
  multi-line. It is that their LAST line is a terminator the parser requires
  to be bare.

One instance per kind, stateless, held as a function-local static and reached
through `BlockKindDefs::builtin()`. That is what lets a vault scan construct a
`Block` on the stack, off the GUI thread, once per block per note, with no
registry to hand and nothing to allocate.

**The rule that keeps this working: a virtual added here is added pure.**
Never with a body on the base that returns the raw source so the tree keeps
building. That body is the export defect, written once and inherited by
everything.

### Call sites

No switch, no `default:`, no dependence on a warning flag:

```cpp
html += b.kind()->toHtml(b.state(), ctx);            // exporter
md   += b.kind()->serialize(b.state(), ordinal);     // serializer
if (b.kind()->isVerbatim()) ...                      // search, selection
```

Changing a block's kind is a pointer write beside the existing `State` swap.
No allocation, no destroyed object, the block id survives, undo is untouched
and the `Block*` four QML files cache stays valid.

### The module boundary

`Block` and `BlockModel` live in `kvit-domain` and must reach every
definition, so every definition lives in `kvit-domain` too. A kind therefore
cannot reach the theme, the open collection, the embed cache or the image
context, and it cannot rasterise.

`BlockRenderServices` (`src/domain/rendercontext.h`) is the seam: an abstract
interface declared in `domain` and implemented by `DocumentExporter` in
`application`. It carries the embedded-asset URIs, the syntax highlighter, the
query answer, the embed card and the table of contents. The rule it encodes is
that rasterisation, anything shaped like network state, and anything that
reads a block other than this one stays above `domain`.

The alternative was a `RenderContext` of forward-declared pointers, with kinds
that dereference them implemented higher up. That does not survive contact
with `BlockModel`: the model asks a kind for its display text from `domain`,
so every kind has to be constructible there, including the query.

### Document renderers keep sequencing

Rendering is not block-local and does not pretend to be. The exporter keeps
ownership of everything that spans blocks:

- contiguous list runs and their nesting, so a kind supplies the `<li>` inner
  markup and the run supplies the tags around it;
- heading slugs, which are collision-suffixed across the whole document, and
  which each heading receives through the context rather than computing;
- the table of contents, which reads every heading in the document and the
  whole slug table, and which the toc kind delegates back to the renderer for;
- shared assets, where one MathJax and one Mermaid script tag are emitted for
  the whole file however many blocks asked;
- the blank line between plain-text blocks, and the per-indent-level numbering
  counter a nested numbered list restarts from.

`toHtml` is handed one block and returns its markup. A `RenderContext` that
grew a mutable list cursor and a slug table would be the exporter again,
wearing a different name.

## What is checked, and by what

| Failure | Caught by |
|---|---|
| A new kind is added and an existing operation forgets it | the kind's class does not compile until every pure virtual is implemented |
| A new operation is added and forgets a kind | the new pure virtual breaks every kind that has not answered it |
| A `BlockKind` enumerator is added with no class | `BlockKindDefTests`, walking the enumeration from its metaobject; `tools/check-block-kinds.py` as the cheap version that needs no build |
| Two kinds share one definition, or two definitions claim one fence language | `BlockKindDefTests` |
| A kind's markdown does not survive a save and a reload | `BlockKindDefTests`, one sample per kind, with a second round trip to catch a shape that is not a fixed point |
| A kind's attribute tag lands where the parser cannot read it back | `BlockKindDefTests`, which puts a sentinel block after the sample: a tag that stops a fence closing swallows it |
| A kind exports as nothing | `BlockKindDefTests`, in both formats |
| A kind has no delegate, or names one that is not in the binary | `check-block-kinds.py`, and `ShellTests`, which loads each delegate the registry names |
| Two kinds publish one delegate value | `ShellTests` and `BlockKindDefTests`, which allow it for the five text kinds and nothing else |
| A snapshot type quietly loses a field | there is one snapshot type |

The first two rows are the point of the design, and neither depends on a
warning flag or on nobody writing `default:`.

Warnings are on regardless: `-Wall -Wextra -Werror=switch` for every module
under `src/` and every test target. `-Wswitch` is an error rather than a
warning because a switch over a block kind that has forgotten an enumerator is
not a style question, it is the block being dropped from whatever that switch
decides. The flag only works where the cases are written out, which is why it
is a second line rather than the guarantee.

## Adding a kind

1. Add the enumerator to `BlockKind` and write its `BlockKindDef` subclass in
   `src/domain/blockkinds/`. It does not compile until every pure virtual is
   implemented.
2. Return it from its group's accessor in `kindgroups.h`, which is what puts
   it in the registry and so in the block menu and the delegate chooser.
3. Write the QML delegate, inheriting `BlockDelegateBase`, add it to
   `resources.qrc` and `tests/integration_tests.qrc`, and name it from
   `delegateUrl()`.
4. Add a sample to `tests/test_blockkinddef.cpp`. The suite fails naming your
   kind until you do.

A linked module does steps 1 and 3 in its own sources and calls
`BlockKindRegistry::registerKind` instead of step 2. A module that only wants
its own delegate can still call `registerFenceLanguage`, and its blocks behave
as code blocks in every other respect — which is what that call has always
meant, and is now stated by an adapter rather than left to a default.

## Decisions

### What a query block contributes to search

**The spec, unchanged, and the code says so.**

That was already the answer, and it was not a considered one: a query fence is
a `CodeBlock` by stored type, and a code block's text is its content, so the
raw `from:`/`where:` text is what the vault index and the in-note find match
over.

Indexing the *results* is the option that sounds right and is the one to
refuse. `CollectionSearchIndex::parseNote` is a pure function of one file's
text, which is what lets a differential oracle rebuild its rows without a
database; `QueryData::evaluate` scans every note in the vault. Result-indexing
needs a dependency graph and a reindex model, and would make a front-matter
edit in one note invalidate query blocks in unrelated ones. That is a product
decision with an architecture attached.

Indexing nothing is the only other defensible answer and it is worse: a reader
who searches for text they can see on screen finds nothing either way, and the
spec at least finds the block by its folder name.

The cost of deferring is one line in one class, which is the property
`searchText()` was introduced to buy.

### Three text projections, and the fourth that needs no virtual

Four things exist. The fourth is the block's `content`, the editable source
every serializer works on, and it needs no virtual.

The three that do are distinguished by how their consumer fails, which is the
useful definition:

- `displayText` — what a reader sees. Wrong output is cosmetic: a mangled
  outline entry, a wrong note-list snippet.
- `statisticsText` — what the counters count. Wrong output is *persisted*:
  `NoteEntry::wordCount` is written into the vault's sidecar index and read
  back on a cold start.
- `searchText` — what a match is found in. Wrong output *corrupts content*: a
  replace splices by position, so a projection whose positions do not map back
  onto the markdown rewrites the wrong span.

They diverge in two places today and both are preserved rather than tidied: a
divider has no search text, and a to-do strips its metadata tail in all three.

### `State::type` survives

Three reasons, and the field is required for each. It is the persisted value, and `BlockKind` is
derived from it in a way that is lossy in one direction: `Embed` and `Kanban`
both map back to a stored type the enum alone cannot recover. It is the
conversion vocabulary, which the slash menu, the toolbar, the context menu and
live markdown typing all speak from QML as an int. And removing it buys
nothing: `kind()->…` is what deleted every call site this design wanted gone,
so the field's removal would delete nothing further.

### `DocumentSerializer::parse` is unchanged

Nothing in the design depends on it. Three of its recognitions cannot be
expressed as a per-kind line test without redesigning them — callout-versus-
quote is decided at flush time from the first accumulated line of a quote run,
a pipe table needs a line of lookahead plus a run, and a list continuation line
needs the previous line's classification and write access to the block already
emitted — and its branch order decides the outcome, with fences first
because their content may contain any marker and `todoRe` before `bulletRe`
because `- [ ] x` also matches the bullet pattern.

It is also the code that protects users' files. `DocumentManager` copies a note
to `note.md.bak` the first time the bytes it would write differ from the bytes
it read, so a parse change that regroups lines does not fail loudly. It starts
writing backup copies across vaults, misaligns the block indexes the search
index stored against a freshly parsed document, and breaks the perf corpus
oracles that use `parse` to count blocks.

The one thing worth doing on the read side was much smaller and is done:
`parse` returns `QList<Block::State>`.

## Known differences from what came before

Three changes to behaviour are deliberate and worth stating rather than
discovering.

**A `.txt` export now writes what the editor draws.** A task board exports as
its columns and cards, a table as an aligned text table, a query as its
answer, a callout as a labelled aside, an image as what it is and where it
lives. Only a Mermaid diagram still exports its source, labelled, because
there is no text rendering of a diagram; a character diagram's source *is* the
picture and is unchanged. Two smaller fixes ride along: a to-do's metadata
tail is stripped as every other projection strips it, and a nested numbered
list restarts its numbering instead of counting 1, 2, 3, 4 through both levels.

**Presentation attributes reach exports.** Alignment, drop caps, divider
styles, image effects, an embed's stored size, a table's column widths and a
callout's colour override are all rendered. A colour or font family that is
not one is dropped rather than written: a note is untrusted input, and its
bytes end up inside a style attribute of a document the reader may pass on.

**The block menu's Advanced group is in a slightly different order.** Its rows
now come from the kinds, in the order the kinds are registered, so "Drop Cap"
and "Math Block" sit earlier in that group than they did. The four group
headings, their contents and every row's text are unchanged.

## Rejected: one subclass per kind, deriving from Block

To be clear about what is rejected: the virtual functions and the one class
per kind stay. What is rejected is putting the block's data inside that class,
so that `QueryBlock` derives from `Block` and a conversion changes the
object's type.

That was the first version of this design. It reads well and it is what most
people picture. It does not fit this tree, for reasons that are specific
rather than stylistic:

**A `content` class cannot derive from a `domain` class.** `domain` depends on
`content`, so `TableBlock` and `KanbanBlock` could not live beside the parsers
they use. Every kind would have to move up into `domain` or higher.

**There is no way to keep a block's identity across a conversion.**
`Block::State` is documented as "everything that defines a block besides its
identity", and no `setBlockId` exists. Replacing the object means either
inventing one or losing the id that focus, selection and the drag layer track.

**The common conversion touches no QML, and replacement would change that.**
Paragraph and all four headings publish delegate kind 0, so paragraph-to-
heading leaves the delegate alone. Object replacement forces a
`BlockObjectRole` republish, a rebind of anything holding the old pointer, and
a refocus, on the path that runs when someone types `# ` at the start of a
line.

**QML holds `Block*`.** `blockAt(int)` is `Q_INVOKABLE`, four QML files call
it and cache the result, and there is a `blockObject` role. Destroying the
object leaves those references null rather than crashing, which is the harder
failure to trace.

**Worker paths depend on blocks being copyable values.**
`collectionsearchindex.cpp`, `vaultscan.cpp` and `notecollection.cpp`
construct a `Block` on the stack to compute display text while scanning every
note in the vault, and `Block::State` is a copyable value used by parsers,
scans, tests and the performance corpus. Owning polymorphic QObjects there
would add allocation, thread affinity and registry injection to code that is
currently a pure function of one file's text.

**Two sources of truth.** `State::type` cannot go away, so a `QueryBlock`
whose restored state said `CodeBlock` would have to be either impossible or
precisely coerced.

What the subclass design was reaching for, one place per kind holding
everything about it, is what the definition provides. What it added on top,
moving the data as well, is what breaks.
