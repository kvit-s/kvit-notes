# Block architecture

## What this is

A note is a list of blocks, and there are about twenty kinds of them. Every
feature that has to do something per kind decides for itself which kinds exist:
the serializer switches on a type enum, the exporter tests fence-language
strings, the outline and the typography and the toolbar each carry their own
list. Nothing checks that a new kind reached all of them, and the last one added
did not.

This document proposes one class per kind, `BlockKindDef`, with pure virtual
functions for everything a feature needs to know. The exporter, the serializer,
the search indexer and the outline call through the base pointer and stop
knowing which kinds exist. A kind that has not answered something does not
compile.

```cpp
html += b.kind()->toHtml(b.state(), ctx);      // instead of switch (b.type())
```

Those classes describe kinds and hold no content. `Block` stays one concrete
class holding the data, and a conversion swaps a pointer rather than replacing
an object. The arrangement where `QueryBlock` derives from `Block` and owns the
content was the first proposal here; it is recorded under "Rejected: one
subclass per kind, deriving from Block" with the reasons it does not fit.

Two of the defects below can be fixed in a day, ahead of any of this, and the
migration section puts those first.

The sections up to "The design" describe the current tree. Everything after is
the target.

## Why

### The defect that prompted it

`DocumentExporter::buildHtmlBody` renders a code block by testing its fence
language:

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
complete: it parsed, saved, rendered live, sat in the slash menu, had tests. A
`query` branch has since been added, which is the fix this shape invites and the
reason the shape is worth changing.

### Plain-text export has the same hole, still open

`DocumentExporter::buildPlainText` emits a code block's content verbatim, so a
`.txt` export writes the query spec, the raw kanban markdown and the Mermaid
source.

### A block exists as three types, and one of them lost a field

```cpp
// live editing
class Block { BlockType m_type; QString m_content; ...; QString m_attributes; };

// parse output
struct DocumentSerializer::BlockData { BlockType type; ...; QString attributes; };

// export input
struct DocumentExporter::Blk { BlockType type; ...; /* no attributes */ };
```

`Blk` is a copy of `BlockData` with one member left out, and both loops that
build it (`blocksFromModel`, `blocksFromMarkdown`) drop `attributes` silently.
That is why alignment, drop caps, divider styles and image effects are missing
from every export. Nobody decided that. A struct was copied and a field was not.

This one is a mapping bug rather than a dispatch bug, and it is fixed by using
one snapshot type rather than by anything else in this document.

### Where block knowledge lives

Thirteen places have to agree about a kind:

| What is decided | Where | Module |
|---|---|---|
| Recognising a kind while reading a file | `DocumentSerializer::parse`, a line scanner | domain |
| Writing it back to markdown | `DocumentSerializer::serializeBlock`, a switch | domain |
| Which QML delegate draws it | `BlockKindRegistry` plus the `DelegateChooser` in `main.qml` | domain, QML |
| What the delegate must implement | `BlockDelegateBase.qml`, eleven functions | QML |
| Its slash-menu and turn-into entry | `BlockMenuModel`, a 23-entry catalog | application |
| HTML and PDF export | `DocumentExporter::buildHtmlBody` | application |
| Plain-text export | `DocumentExporter::buildPlainText` | application |
| The text word counts, snippets and search see | `Block::displayText` | domain |
| Whether its text is verbatim | `type == Block::CodeBlock` in five files, and again as `verbatimEditing` in QML | four modules, QML |
| Whether "remove line breaks" applies | `foldsLineBreaks` in `blockmodel.cpp` | domain |
| Its heading level | `headingLevel` in `documentoutline.cpp` | domain |
| Its font size | `Typography::sizeForBlockType` | platform |
| Whether the alignment buttons apply | `alignableTypes: [0, 1, 2, 3, 10, 11]` in `Toolbar.qml` | QML |

## The finding: the closed set already exists

The obvious reading of the export defect is that fence languages are strings, so
no compiler can check them. That is wrong. The kinds that render differently are
already a closed enumeration:

```cpp
namespace BlockKinds {
enum Kind { Kanban = 100, Toc = 101, Embed = 102, Mermaid = 103, Query = 104 };
}
static constexpr int BlockKindRegistry::FirstRegisteredKind = 200;
```

`BlockKindRegistry::kindForLanguage` already maps `"query"` to `Query`, and
`BlockModel::delegateKindForContent` already folds block type, fence language
and content into one number. The set was closed the whole time; the exporter
compares raw strings only because it never asks the registry.

Two further things are switched off rather than absent:

- The project sets no `-Wall`, `-Wextra` or `-Werror` in `CMakeLists.txt` or the
  presets, so `-Wswitch` is not running at all.
- Every block-type switch carries a `default:` label, which suppresses
  `-Wswitch` even when it is running. That applies to `serializeBlock`,
  `buildHtmlBody`, `buildPlainText`, `foldsLineBreaks`, `headingLevel` and
  `sizeForBlockType`.

So compile-time completeness is available for every built-in kind today, without
any refactor, by resolving the kind once and switching on it with no `default:`.
The only part that cannot be checked that way is a kind a linked module
registers at runtime, which gets a number from 200 upward and cannot be an
enumerator in a switch the core compiles.

**This is interim enforcement rather than the design.** It rests on a warning
flag staying enabled and on nobody writing a `default:` label, and it does not
reach module kinds at all. The target below removes the switches entirely, and
the enumeration survives only as the key that identifies a kind. Migration step
3 says what to do with it and why it is worth writing even though most of it is
deleted later.

### The kind space needs to be one enumeration

`delegateKindForContent` returns a plain `int` covering three ranges: block-type
values below 20, the built-in fence and embed kinds from 100, and module kinds
from 200. A switch over an `int` gets no checking. The first step is therefore to
make the kind space one enum holding both fixed ranges at their current values:

```cpp
enum class BlockKind : int {
    Text = 0, BulletList = 4, NumberedList = 5, Todo = 6, Quote = 7,
    CodeBlock = 8, Divider = 9, Image = 11, Callout = 12, MathBlock = 13,
    Media = 14, Table = 15,
    Kanban = 100, Toc = 101, Embed = 102, Mermaid = 103, Query = 104,
    // module kinds are >= 200 and never enumerators here
};
```

The values are the ones already in use, so nothing persisted changes.

## The design

### Two questions, answered separately

These get run together easily, so they are stated apart:

- **Where does per-kind behaviour live?** In one class per kind, with pure
  virtuals, so the compiler refuses a kind that has not answered everything.
- **What holds a block's data?** One concrete `Block`, as today.

Each subclass describes one kind, and none of them derives from `Block` or holds
a block's content. A block points at the singleton describing its kind:

```cpp
class Block : public QObject {
    const BlockKindDef *m_kind;    // never null, never owned
    State m_state;                 // unchanged
};
```

"Rejected: one subclass per kind" at the end covers the other arrangement, where
`QueryBlock` derives from `Block` and owns the content. That is what does not
work here.

### One class per kind

```cpp
// src/domain/blockkinddef.h
class BlockKindDef {
public:
    virtual ~BlockKindDef() = default;

    virtual BlockKind kind() const = 0;
    virtual QString   id() const = 0;              // "query"

    // Storage.
    virtual QString serialize(const Block::State &, int ordinal) const = 0;

    // The three text projections. They differ on the query block, and today
    // Block::displayText answers all three with one string.
    virtual QString displayText(const Block::State &) const = 0;
    virtual QString statisticsText(const Block::State &) const = 0;
    virtual QString searchText(const Block::State &) const = 0;

    // The predicates currently scattered across five modules.
    virtual bool isVerbatim() const = 0;
    virtual bool foldsLineBreaks() const = 0;
    virtual int  headingLevel() const = 0;
    virtual FontRole fontRole() const = 0;
    virtual bool isAlignable() const = 0;

    // Output. One block's markup; sequencing belongs to the caller.
    virtual QString toHtml(const Block::State &, const RenderContext &) const = 0;
    virtual QString toPlainText(const Block::State &, const RenderContext &) const = 0;

    // UI.
    virtual MenuEntry menu() const = 0;            // or MenuEntry::none()
    virtual QString  delegateUrl() const = 0;
};
```

One instance per kind, stateless, owned by the registry, which absorbs today's
`BlockKindRegistry`. `BlockMenuModel` and the `DelegateChooser` read from it, so
a kind reaches the slash menu and gets its delegate by being registered.

**The rule that keeps this working: a virtual added here is added pure.** Never
with a body on the base that returns the raw source so the tree keeps building.
That body is the export defect, written once and inherited by everything.

### Call sites

No switch, no `default:`, no dependence on a warning flag:

```cpp
html += b.kind()->toHtml(b.state(), ctx);            // exporter
md   += b.kind()->serialize(b.state(), ordinal);     // serializer
if (b.kind()->isVerbatim()) ...                      // search, selection
```

Changing a block's kind is a pointer write beside the existing `State` swap:

```cpp
m_kind = registry.def(BlockKind::Heading1);
```

No allocation, no destroyed object, undo untouched, QML references still valid.

### The module boundary

`RenderContext` is a struct of pointers, defined in `domain`, whose members are
forward-declared:

```cpp
// src/domain/rendercontext.h
class Theme; class NoteCollection; class EmbedMetadata;

struct RenderContext {
    enum Target { Browser, Pdf } target;
    Theme *theme;
    NoteCollection *collection;      // null in single-file mode
    EmbedMetadata *embeds;
    QString noteDir, collectionRoot;
};
```

A kind implemented in `domain` (paragraph, heading, quote, divider) never
dereferences those pointers, so it compiles against the declarations alone. A
kind that does dereference them is implemented higher up: `QueryKindDef` lives
in `application` and includes `notecollection.h`. No module includes upward and
`check-layering.py` stays satisfied.

The strict alternative is to split the interface in two, with storage and text
in `domain` and rendering in `application`, giving two small classes per kind
paired at composition time. It removes the nominal coupling above at the cost of
a pairing the compiler cannot check, which then needs the guard test. The single
interface is the recommendation; this is recorded because the trade is close.

### Document renderers keep sequencing

Rendering is not block-local and must not pretend to be. The exporter keeps
ownership of everything that spans blocks:

- contiguous list runs and their nesting (`buildHtmlBody`'s list branch);
- heading slugs, which are collision-suffixed across the whole document
  (`headingSlugs`);
- the TOC block, which reads every heading in the document;
- shared assets, where one MathJax and one Mermaid script tag are emitted for
  the whole file however many blocks asked.

`toHtml` is handed one block and returns its markup. Anything that needs to look
at neighbours stays in the document renderer. A `RenderContext` that grew a
mutable list cursor and a slug table would be the exporter again, wearing a
different name.

## What is checked, and by what

| Failure | Caught by |
|---|---|
| A new kind is added and an existing operation forgets it | the kind's class does not compile until every pure virtual is implemented |
| A new operation is added and forgets a kind | the new pure virtual breaks every kind that has not answered it |
| A kind is registered under a number nothing implements | `registry.def()` returns the one instance or the registration does not compile |
| A kind has no delegate, or a delegate names no kind | guard test comparing the registry against `main.qml` |
| A kind does not round-trip through markdown | guard test over one sample per kind |
| A snapshot type quietly loses a field | there is one snapshot type |

The first two rows are the point of the design, and neither depends on a warning
flag or on nobody writing `default:`.

Whatever switches over block type survive the move should still switch on the
resolved kind and carry no `default:` label, with `-Werror=switch` on. That is
the interim mechanism described under "The finding", and it stays useful as a
second line afterwards.

## Rejected: one subclass per kind, deriving from Block

To be clear about what is being rejected: the virtual functions and the one
class per kind stay. What is rejected is putting the block's data inside that
class, so that `QueryBlock` derives from `Block` and a conversion changes the
object's type.

The first version of this document proposed exactly that. It reads well and it
is what most people picture. It does not fit this tree, for reasons that are
specific rather than stylistic:

**A `content` class cannot derive from a `domain` class.** `domain` depends on
`content`, so `TableBlock` and `KanbanBlock` cannot live beside the parsers they
use. Every kind would have to move up into `domain` or higher.

**There is no way to keep a block's identity across a conversion.**
`Block::State` is documented as "everything that defines a block besides its
identity", and no `setBlockId` exists. Replacing the object means either
inventing one or losing the id that focus, selection and the drag layer track.

**The common conversion currently touches no QML, and replacement would change
that.** `delegateKindFor` returns 0 for `Paragraph` and all four headings, so
paragraph-to-heading leaves the delegate alone today. Object replacement forces
a `BlockObjectRole` republish, a rebind of anything holding the old pointer, and
a refocus, on the path that runs when someone types `# ` at the start of a line.

**QML holds `Block*`.** `blockAt(int)` is `Q_INVOKABLE`, four QML files call it
and cache the result, and there is a `blockObject` role. Destroying the object
leaves those references null rather than crashing, which is the harder failure
to trace.

**Worker paths depend on blocks being copyable values.**
`collectionsearchindex.cpp`, `vaultscan.cpp` and `notecollection.cpp` construct
a `Block` on the stack to compute display text while scanning every note in the
vault, and `BlockData` is a copyable value used by parsers, scans, tests and the
performance corpus. Owning polymorphic QObjects there adds allocation, thread
affinity and registry injection to code that is currently a pure function of one
file's text.

**Two sources of truth.** `State::type` cannot go away, so a `QueryBlock` whose
restored state says `CodeBlock` would have to be either impossible or precisely
coerced.

What the subclass design was reaching for, one place per kind holding everything
about it, is what the registry entry provides. What it added on top, moving the
data as well, is what breaks.

## Migration

Sorted by what survives the work rather than by phase number, because one of
these steps is deliberately temporary.

### Worth doing whatever happens next

**1. One snapshot type.** Delete `DocumentExporter::Blk` and use
`Block::State`. This restores alignment, drop caps, divider styles and image
effects to exports, and the same fix is needed under either design.

**2. Plain-text export.** Give the fence-backed kinds a plain-text form instead
of emitting their source. The code written here moves into a virtual later
unchanged; only its call site changes.

Both are small and both fix defects a reader can see today.

### Temporary, and knowingly so

**3. Interim compiler enforcement.** Make the kind space one enumeration,
resolve it once, turn on `-Wall` with at least `-Werror=switch`, and delete the
`default:` labels from the six block-type switches, writing the cases out.
`foldsLineBreaks` becomes eight explicit `return false` cases rather than one
default.

Half of this survives: the enumeration stays as the registry key and the QML
delegate role, and the warning flags are worth having across the project. The
expanded switch cases do not survive, since steps 5 to 7 delete those switches.
That is roughly a hundred lines written to be thrown away.

It is still worth writing, for one reason. Steps 4 to 7 are incremental and will
take weeks, the switches exist throughout, and a kind added in that window would
meet exactly the hole this document is about. A hundred lines to hold the line
during the transition is a fair price.

Skip this step only if step 4 starts immediately. Skipping it and then deferring
the redesign for a month is the one ordering that leaves the tree no better than
it is now.

### The design

**4. `BlockKindDef` with the predicates only.** Introduce the base class with
`isVerbatim`, `foldsLineBreaks`, `headingLevel`, `fontRole` and `isAlignable`,
one subclass per kind, and have `Block` hold a `const BlockKindDef *`. Delete
the five `== Block::CodeBlock` comparisons, `foldsLineBreaks`, `headingLevel`,
`sizeForBlockType` and the `alignableTypes` literal in `Toolbar.qml`. Nothing
renders through the new class yet, so a mistake here shows up in the existing
unit suites rather than in output nobody looks at.

This is the bulk of the mechanical work: about twenty small classes and the
registry that owns them.

**5. Rendering.** Add `toHtml` and `toPlainText` as pure virtuals, implement
them per kind, and delete both exporter switches. One kind per commit, since the
exporter's tests already cover each kind separately.

**6. Storage and text.** Add `serialize`, `displayText`, `statisticsText` and
`searchText`, then delete `serializeBlock`'s switch and the `Todo` special case
in `Block::displayText`. This is where the risk is, because round-trip fidelity
protects users' files, so run it with the round-trip corpus green.

**7. Menu and delegate.** Move `BlockMenuModel`'s catalog and the
`DelegateChooser`'s built-in choices onto `menu()` and `delegateUrl()`.

**8. Guard tests.** `tools/check-block-kinds.py`, in the style of the existing
`check-qrc-sync.py` and `check-layering.py`: every registered kind round-trips
its markdown and produces non-empty HTML and plain text for a sample; every
`DelegateChoice` names a registered kind and every kind with a delegate URL has
a choice.

After step 6 the `-Werror=switch` from step 3 is protecting almost nothing,
which is how you know the move worked.

## Adding a kind afterwards

1. Add the enumerator to `BlockKind` and write its `BlockKindDef` subclass. It
   does not compile until every pure virtual is implemented.
2. Register the instance in `AppContext`, or in `KvitExtension::registerBlockKinds`
   for a linked module.
3. Write the QML delegate, inheriting `BlockDelegateBase`, and add it to
   `resources.qrc`.

Step 1 is the point of the exercise, and it is the same step whether the kind is
built in or comes from a module.

## Open questions

**What does a query block contribute to search?** Indexing its rendered answer
means a metadata change in one note invalidates query blocks in unrelated notes,
and `CollectionSearchIndex` is currently a pure function of one file's text.
Indexing its spec means searching for a note title visible on screen finds
nothing. The three options are nothing, the spec, or the results with an
explicit dependency and reindex model. This is a product decision, and deciding
it after `searchText` is written means writing it twice.

**Are three text projections enough?** Editable source, visible text,
statistics text and searchable text are four distinct things; the entry above
names three and treats the source as the block's content. Worth confirming
against the query and to-do blocks, which are the two that differ.

**Does `State::type` survive?** Once kind resolution is central, the type enum
is an input to it and nothing else reads it directly. Removing it touches
persisted model roles, `insertBlock`, the menu catalog and QML, so it is a
later decision rather than part of this work.

**Should `DocumentSerializer::parse` change at all?** It is a line scanner with
lookahead and run accumulation, and it is what protects users' files. Moving
recognition onto the kinds is the tidier end state and is how markdown-it and
similar parsers work, but nothing else here depends on it and the risk is
concentrated there.
