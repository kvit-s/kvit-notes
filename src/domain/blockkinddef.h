// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKKINDDEF_H
#define BLOCKKINDDEF_H

#include "block.h"
#include "blockkind.h"
#include "menuentry.h"

#include <QList>
#include <QString>

struct RenderContext;

// One instance per block kind: everything a feature needs to know about a
// kind, in one class, with nothing optional.
//
// The problem this solves is stated plainly by the defect that prompted it.
// About twenty kinds of block exist, and thirteen places decided something
// per kind — the serializer switched on a type enum, the exporter compared
// fence-language strings, the outline, the typography and the toolbar each
// carried a list of their own. Nothing checked that a new kind reached all of
// them, and the last one added did not: a `query` fence matched none of the
// exporter's four language branches, so every HTML and PDF export printed the
// query's `from:`/`where:` spec as a code listing — the one part of the block
// a reader never sees on screen. The block was otherwise complete. It parsed,
// it saved, it rendered live, it sat in the slash menu, it had tests.
//
// Here, a kind that has not answered something does not compile.
//
// THE RULE THAT KEEPS THIS WORKING: a virtual added here is added PURE.
// Never with a body on the base that returns the raw source so the tree keeps
// building. That body is the export defect, written once and inherited by
// every kind at once.
//
// These objects describe kinds and hold no content. One instance per kind,
// stateless, owned by the registry; a Block points at the one describing it,
// so a conversion writes a pointer beside the State swap it already does. No
// allocation, no destroyed object, the block id survives, undo is untouched
// and the Block* QML cached stays valid.
class BlockKindDef
{
public:
    virtual ~BlockKindDef() = default;

    // ================= identity =================

    // The registry key. A module's def returns its assigned number cast to
    // BlockKind; only the built-ins are enumerators.
    virtual BlockKind kind() const = 0;

    // A stable lowercase identifier — "paragraph", "heading1", "query".
    // Names a kind in a diagnostic and keys the guard corpus's samples.
    // Never shown to a reader.
    virtual QString id() const = 0;

    // The fence info string that selects this kind, empty when the kind is
    // not fence-backed. This is the one table the built-in fence languages
    // live in: the registry seeds itself by walking the built-in defs and
    // reading this, so `kanban` is written down once.
    virtual QString fenceLanguage() const = 0;

    // ================= storage =================

    // One block's markdown, WITHOUT its <!--kvit …--> attribute tag: the
    // caller attaches that, on the opening line or the last one according to
    // attributeTagRidesOpeningLine() below.
    //
    // `ordinal` is the document-level numbering the model cached; only the
    // numbered list reads it. What separates one block's markdown from the
    // next — the blank line, and the single newline that makes two adjacent
    // list items a tight list — belongs to the caller, because it depends on
    // the neighbour.
    virtual QString serialize(const Block::State &state, int ordinal) const = 0;

    // Whether the attribute tag rides the block's OPENING line rather than
    // trailing its last one. True for exactly four kinds — code fence, math
    // fence, table and callout — and the reason is not that they are
    // multi-line. A quote and a wrapped list item are multi-line too and take
    // the trailing form. It is that the LAST line of those four is a
    // terminator the parser requires to be bare: a tagged closing fence never
    // closes its block, so on the next load the rest of the note is read as
    // that block's content.
    virtual bool attributeTagRidesOpeningLine() const = 0;

    // ================= the three text projections =================
    //
    // One cached string used to answer all three, with two type tests in
    // front of it. They are separated because their consumers fail
    // differently, and that is the useful definition of the boundary:
    //
    //   displayText     wrong output is cosmetic — a mangled outline entry.
    //   statisticsText  wrong output is PERSISTED — the word count is
    //                   written into the vault's sidecar index.
    //   searchText      wrong output CORRUPTS CONTENT — a replace splices by
    //                   position, so a projection whose positions do not map
    //                   back onto the markdown rewrites the wrong span.

    // What the reader sees. Feeds the display role, the outline node text and
    // so the heading slugs, the drop cap's initial, and the note-list snippet.
    virtual QString displayText(const Block::State &state) const = 0;

    // What the word and character counters count.
    virtual QString statisticsText(const Block::State &state) const = 0;

    // What find-in-note and the vault index match over.
    virtual QString searchText(const Block::State &state) const = 0;

    // ================= predicates =================

    // Display coordinates equal markdown coordinates: the block's content IS
    // its text, so a range can be spliced straight into it.
    //
    // This is a correctness claim rather than a styling one. A kind that
    // answers true while its searchText() is not literally its content
    // corrupts the reader's content on a replace.
    virtual bool isVerbatim() const = 0;

    // Whether serialize() writes the block's content down at all.
    //
    // False for exactly one built-in kind: the divider serializes as three
    // characters that say nothing about the state they came from. A
    // conversion INTO such a kind must therefore drop the text it is handed
    // rather than keep it in the model, because a block holding text its own
    // markdown does not carry looks right on screen until the note is saved
    // and reopened, and then the text is simply gone — the worst shape a loss
    // can take, since nothing reports it and undo has long since been
    // cleared.
    virtual bool holdsContent() const = 0;

    // Whether "remove line breaks" applies: the block's newlines are wrapping
    // that a reader would want folded away, rather than content the breaks
    // are part of. A code block's newlines are the code.
    virtual bool foldsLineBreaks() const = 0;

    // The trailing run of the content a line fold must leave alone, its
    // leading newline included. Empty for every kind but the quote, whose
    // attribution sits on its own last line as chrome: folding the newline in
    // front of it silently turns it into body text.
    //
    // This exists because a bare foldsLineBreaks() bool loses that case,
    // which would be left behind in the caller as an orphaned comparison
    // against one block type — exactly the shape being deleted.
    virtual QString unfoldableTail(const Block::State &state) const = 0;

    // 1 to 4 for a heading, 0 for everything else. Replaces the same mapping
    // written out by hand in nine places.
    virtual int headingLevel() const = 0;

    // Which entry of the frozen type scale this kind renders at.
    virtual FontRole fontRole() const = 0;

    // Whether the alignment buttons apply to this kind.
    virtual bool isAlignable() const = 0;

    // ================= output =================
    //
    // One block's markup. The caller owns everything that spans blocks: the
    // contiguous list run and its nesting, the collision-suffixed slug table,
    // the one MathJax and one Mermaid script tag for the whole file, the
    // blank line between plain-text blocks, and a combined export's per-note
    // wrappers.
    //
    // Blocks are concatenated with NO separator at all, so an implementation
    // must emit no leading or trailing whitespace: several exporter
    // assertions span two blocks' output.
    //
    // The three list kinds return the item's INNER html with no <li> wrapper.
    // The run branch defers every closing </li> so that a deeper item can
    // open its sublist inside the still-open one, which is where HTML wants a
    // nested list.
    virtual QString toHtml(const Block::State &state,
                           const RenderContext &ctx) const = 0;

    // One block's plain text, with no trailing newline and no blank-line
    // separator; the caller puts one blank line after every block.
    virtual QString toPlainText(const Block::State &state,
                                const RenderContext &ctx) const = 0;

    // ================= UI =================

    // The slash-menu and turn-into rows this kind contributes, in catalog
    // order. A list rather than one entry, because the mapping is many-to-one
    // and must stay so: a paragraph contributes "Text" and "Drop Cap", a
    // callout "Callout" and "Toggle". A kind with no menu presence — the
    // embed card, which is reached through a URL prompt — returns nothing.
    virtual QList<MenuEntry> menuEntries() const = 0;

    // The value the model publishes as the delegate-kind role and the QML
    // DelegateChooser watches.
    //
    // EXACTLY ONE RULE, and it is worth stating at every implementation:
    // return 0 for Paragraph and all four headings, which share one delegate,
    // and static_cast<int>(kind()) for everything else. Those five zeros are
    // what keep paragraph-to-heading — the most common conversion in the
    // editor, run every time someone types "# " at the start of a line — from
    // destroying the delegate and the caret inside it.
    virtual int delegateKind() const = 0;

    // The QML file that draws this kind. Every kind names its own, and the
    // shell builds one DelegateChoice per registered kind from these, so a
    // kind reaches the screen by being registered rather than by someone
    // remembering to edit main.qml.
    //
    // Empty is legal and means "this kind shares another kind's delegate":
    // the four headings return nothing, because kind 0 is the paragraph's.
    virtual QString delegateUrl() const = 0;

protected:
    BlockKindDef() = default;
    BlockKindDef(const BlockKindDef &) = delete;
    BlockKindDef &operator=(const BlockKindDef &) = delete;
};

#endif // BLOCKKINDDEF_H
