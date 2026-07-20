// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDAST_H
#define MERMAIDAST_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

// Typed AST and diagnostics for the Mermaid-compatible parser (diagrams-prd.md
// §8.2). Kvit renders a documented Mermaid-compatible subset natively; this is
// not an embedding of Mermaid.js. Version 1 implements the flowchart family
// (§9); every other family parses only far enough to name itself and emit the
// "unsupported family" diagnostic, so its source is never discarded.
namespace Mermaid {

// The families Kvit can name. Only Flowchart is rendered in v1; the rest carry
// the unsupported-family diagnostic (diagrams-prd.md §9 deferred families).
enum class DiagramType {
    Unknown,
    Flowchart,
    Sequence,
    Class,
    State,
    Er,
    // Deferred (mindmap, timeline, gantt, pie, ...) collapse to Unsupported.
    Unsupported,
};

enum class Direction { TB, BT, LR, RL };

// Node outline shapes drawn by the layout/scene (diagrams-prd.md §9 "common
// node shapes"), aligned with the pinned flow.jison vertex productions. A
// shape Kvit does not recognize falls back to Rect.
enum class NodeShape {
    Rect,             // A[text]
    RoundRect,        // A(text)
    Stadium,          // A([text])
    Subroutine,       // A[[text]]
    Cylinder,         // A[(text)]
    Circle,           // A((text))
    DoubleCircle,     // A(((text)))
    Ellipse,          // A(-text-)
    Rhombus,          // A{text}
    Hexagon,          // A{{text}}
    Parallelogram,    // A[/text/]   (lean_right)
    ParallelogramAlt, // A[\text\]   (lean_left)
    Trapezoid,        // A[/text\]   (narrow top)
    TrapezoidAlt,     // A[\text/]   (narrow bottom)
    Odd,              // A>text]
};

enum class EdgeStroke { Solid, Dotted, Thick };

// A UTF-16 span into the fence source as stored on the block (§20.2: edit
// spans are computed from AST source offsets, never by regex over the body).
struct SourceSpan {
    int start = -1;
    int length = 0;
    bool valid() const { return start >= 0; }
    int end() const { return start + length; }
    bool contains(int offset) const
    {
        return valid() && offset >= start && offset <= end();
    }
};

struct Node {
    QString id;
    QString label;
    NodeShape shape = NodeShape::Rect;
    QStringList classes;   // classDef names applied via `class`/`:::`
    int order = 0;         // first-encounter order, a stable layout tie-breaker
    // §20 source mapping.
    SourceSpan idSpan;              // the id token of the first declaration
    SourceSpan labelSpan;           // raw text between the shape brackets
    SourceSpan shapeSpan;           // the whole bracket construct `[label]`
    QList<SourceSpan> refSpans;     // every id reference, declaration included
};

struct Edge {
    QString from;
    QString to;
    QString label;
    QString id;                // optional `e1@-->` edge id (kept, not rendered)
    EdgeStroke stroke = EdgeStroke::Solid;
    bool arrowStart = false;   // an arrowhead at the `from` end (`<-->`)
    bool arrowEnd = true;      // an arrowhead at the `to` end (`-->`)
    bool invisible = false;    // `~~~` link: ranks like an edge, draws nothing
    int minLen = 1;            // rank span from extra dashes (`--->`)
    int order = 0;
    // §20 source mapping.
    SourceSpan opSpan;         // the arrow token (incl. any inline label)
    SourceSpan pipeSpan;       // a `|label|` construct after the arrow, if any
    SourceSpan stmtSpan;       // the enclosing statement's token range
};

struct Subgraph {
    QString id;
    QString title;
    QStringList nodeIds;       // members declared inside the subgraph
    Direction direction = Direction::TB;
    bool hasDirection = false;
};

// A safe visual style set (diagrams-prd.md §9 "restricted syntax"): only fill /
// stroke colors, stroke width/style, and font emphasis are honoured; arbitrary
// CSS is ignored.
struct ClassDef {
    QColor fill;
    QColor stroke;
    qreal strokeWidth = 0;     // 0 => use the theme default
    bool dashed = false;
    bool bold = false;
    bool hasFill = false;
    bool hasStroke = false;
};

struct Diagnostic {
    enum Severity { Error, Warning };
    int line = 1;              // one-based, editor-ready
    int column = 1;            // one-based
    QString message;
    Severity severity = Error;
};

// ---- Sequence family (sequenceDiagram.jison @ the pinned version) ----

enum class SeqLine { Solid, Dotted };
enum class SeqHead { Filled, Open, Cross, Point };

struct SeqParticipant {
    QString id;
    QString label;
    bool actorFigure = false;   // declared with `actor`: drawn as a stick figure
    int boxIndex = -1;          // grouping `box`, -1 = none
    int order = 0;
    SourceSpan srcSpan;         // declaring / first-referencing statement
};

struct SeqBox {
    QString title;
    QColor color;               // invalid => theme tint
};

struct SeqEvent {
    enum Kind {
        Message, Note, Activate, Deactivate,
        BlockStart, BlockDivider, BlockEnd, Autonumber,
    };
    enum Block { Loop, Alt, Opt, Par, Critical, Break, Rect };
    enum Placement { LeftOf, RightOf, Over };

    Kind kind = Message;
    // Message: from/to participants; Note: from(,to) actors; Activate: from.
    QString from;
    QString to;
    QString text;               // message text / note text
    SeqLine line = SeqLine::Solid;
    SeqHead head = SeqHead::Filled;
    bool bidirectional = false; // `<<->>` / `<<-->>`
    bool activateTarget = false;   // `->>+`
    bool deactivateSource = false; // `->>-`
    Placement placement = Over;
    Block block = Loop;
    QString blockLabel;         // block condition / divider label / rect color
    bool autonumberVisible = true;
    int autonumberStart = 1;
    int autonumberStep = 1;
    int srcLine = 1;            // one-based source line of the statement
    SourceSpan srcSpan;         // the statement's span in the fence source
};

struct SequenceAst {
    QList<SeqParticipant> participants;
    QList<SeqBox> boxes;
    QList<SeqEvent> events;
    QString title;
    QString accTitle;
    QString accDescr;

    int indexOfParticipant(const QString &id) const
    {
        for (int i = 0; i < participants.size(); ++i)
            if (participants.at(i).id == id)
                return i;
        return -1;
    }
    int messageCount() const
    {
        int n = 0;
        for (const SeqEvent &e : events)
            if (e.kind == SeqEvent::Message)
                ++n;
        return n;
    }
};

// One `id=x,y` entry of a `%% mermaid-flow:pos` arrangement line (§20.3).
// Coordinates are node centers in logical pixels, origin at the scene's
// top-left (the obsidian-mermaid-flow convention, locked by the cross-tool
// fixtures).
struct PosEntry {
    QString id;
    double x = 0;
    double y = 0;
};

struct FlowchartAst {
    Direction direction = Direction::TB;
    QList<Node> nodes;         // first-encounter order
    QList<Edge> edges;
    QList<Subgraph> subgraphs;
    QHash<QString, ClassDef> classDefs;
    QString accTitle;
    QString accDescr;
    // §20.3 manual arrangement: the fence's single recognized pos line.
    bool hasPosLine = false;
    QList<PosEntry> posEntries;
    SourceSpan posLineSpan;    // the whole line, newline excluded

    int indexOfNode(const QString &id) const
    {
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes.at(i).id == id)
                return i;
        return -1;
    }
};

// ---- Class family (classDiagram.jison @ the pinned version) ----

enum class ClassRelEnd {
    None,
    Extension,     // <| / |> hollow triangle
    Composition,   // * filled diamond
    Aggregation,   // o hollow diamond
    Dependency,    // < / > open arrow
    Lollipop,      // () circle
};

struct ClassNode {
    QString id;
    QString label;         // display name; `["label"]` overrides the id
    QString annotation;    // <<interface>> etc., without the chevrons
    QStringList attributes;  // raw member text, visibility prefix included
    QStringList methods;
    QStringList cssClasses;
    int namespaceIndex = -1;
    int order = 0;
    SourceSpan srcSpan;      // declaring / first-referencing statement
};

struct ClassRelation {
    QString from;
    QString to;
    ClassRelEnd fromEnd = ClassRelEnd::None;
    ClassRelEnd toEnd = ClassRelEnd::None;
    bool dotted = false;
    QString label;
    QString fromCard;      // quoted cardinality next to `from`
    QString toCard;
    int order = 0;
    SourceSpan srcSpan;    // the relation statement
};

struct ClassNamespace {
    QString name;
    QStringList classIds;
};

struct ClassNote {
    QString text;
    QString forClass;      // empty = free-standing note
};

struct ClassAst {
    Direction direction = Direction::TB;
    QList<ClassNode> classes;
    QList<ClassRelation> relations;
    QList<ClassNamespace> namespaces;
    QList<ClassNote> notes;
    QHash<QString, ClassDef> classDefs;
    QString title;
    QString accTitle;
    QString accDescr;

    int indexOfClass(const QString &id) const
    {
        for (int i = 0; i < classes.size(); ++i)
            if (classes.at(i).id == id)
                return i;
        return -1;
    }
};

// ---- State family (stateDiagram.jison @ the pinned version) ----

enum class StateKind {
    Normal,
    Start,    // a `[*]` used as a transition source
    End,      // a `[*]` used as a transition target
    Fork,     // <<fork>> / [[fork]]
    Join,
    Choice,
};

struct StateNode {
    QString id;
    QString label;             // display name / long description via `as`
    QStringList descriptions;  // `s1 : text` lines
    StateKind kind = StateKind::Normal;
    int parentIndex = -1;      // enclosing composite state, -1 = root
    bool composite = false;
    QStringList cssClasses;
    int order = 0;
    SourceSpan srcSpan;        // declaring / first-referencing statement
};

struct StateTransition {
    QString from;
    QString to;
    QString label;
    int order = 0;
    SourceSpan srcSpan;    // the transition statement
};

struct StateNote {
    QString stateId;   // empty = floating note
    bool leftOf = false;
    QString text;
};

struct StateAst {
    Direction direction = Direction::TB;
    QList<StateNode> states;
    QList<StateTransition> transitions;
    QList<StateNote> notes;
    QHash<QString, ClassDef> classDefs;
    QString title;
    QString accTitle;
    QString accDescr;

    int indexOfState(const QString &id) const
    {
        for (int i = 0; i < states.size(); ++i)
            if (states.at(i).id == id)
                return i;
        return -1;
    }
};

// ---- ER family (erDiagram.jison @ the pinned version) ----

enum class ErCardinality {
    ZeroOrOne,   // |o / o|
    ZeroOrMore,  // }o / o{
    OneOrMore,   // }| / |{
    OnlyOne,     // ||
    MdParent,    // `u` (rendered as only-one)
};

struct ErAttribute {
    QString type;
    QString name;
    QStringList keys;    // PK / FK / UK
    QString comment;
};

struct ErEntity {
    QString id;
    QString label;       // `NAME["label"]` alias, defaults to the id
    QList<ErAttribute> attributes;
    QStringList cssClasses;
    int order = 0;
    SourceSpan srcSpan;  // declaring / first-referencing statement
};

struct ErRelationship {
    QString from;
    QString to;
    ErCardinality fromCard = ErCardinality::OnlyOne;   // drawn at `from`
    ErCardinality toCard = ErCardinality::OnlyOne;     // drawn at `to`
    bool identifying = true;   // solid line; non-identifying is dashed
    QString label;
    int order = 0;
    SourceSpan srcSpan;        // the relationship statement
};

struct ErAst {
    Direction direction = Direction::TB;
    QList<ErEntity> entities;
    QList<ErRelationship> relationships;
    QHash<QString, ClassDef> classDefs;
    QString title;
    QString accTitle;
    QString accDescr;

    int indexOfEntity(const QString &id) const
    {
        for (int i = 0; i < entities.size(); ++i)
            if (entities.at(i).id == id)
                return i;
        return -1;
    }
};

struct ParseResult {
    DiagramType type = DiagramType::Unknown;
    bool supported = false;         // true only when Kvit renders this family
    QString familyName;             // header keyword, for the unsupported message
    FlowchartAst flowchart;
    SequenceAst sequence;
    ClassAst classDiagram;
    StateAst stateDiagram;
    ErAst er;
    QList<Diagnostic> diagnostics;

    bool hasErrors() const
    {
        for (const Diagnostic &d : diagnostics)
            if (d.severity == Diagnostic::Error)
                return true;
        return false;
    }
    Diagnostic firstError() const
    {
        for (const Diagnostic &d : diagnostics)
            if (d.severity == Diagnostic::Error)
                return d;
        return Diagnostic{};
    }
};

} // namespace Mermaid

#endif // MERMAIDAST_H
