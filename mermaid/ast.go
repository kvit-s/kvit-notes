// Package mermaid reads the Mermaid diagram notation Kvit draws itself: the
// flowchart, sequence, class, state and entity-relationship families, each
// read by a parser written against the Jison grammar of mermaid@11.16.0, the
// version Kvit's HTML export loads. It is a port of the app's
// src/content/diagrams/mermaid*.cpp, and its tests are the app's
// tests/test_mermaid*.cpp.
//
// The package knows nothing of drawing: Parse turns a fence's text into a
// syntax tree with diagnostics, and the diagram package lays the tree out.
// Every position the tree records is a rune offset into the fence's text,
// which is how the editor counts positions in a block, and equals the
// app's UTF-16 offsets for text outside the astral planes.
//
// ast.go is mermaidast.h. lexer.go is mermaidlexer, the flowchart's tokens,
// which the on-diagram edits read as well. parser.go is mermaidparser: the
// resource limits, front matter, the choice of family from the header, the
// flowchart parser and its `%% mermaid-flow:pos` lines. sequence.go,
// class.go, state.go and er.go are mermaidsequence, mermaidclass,
// mermaidstate and mermaider. edits.go is mermaidedits, the edits the
// reader's gestures on a drawn diagram make to its text. color.go reads
// colours as colour's string constructor does, and text.go has the string
// behaviour the parsers rely on (trimming, case-insensitive keywords, toInt
// and toDouble).
package mermaid

// DiagramType is the family a fence's header names.
type DiagramType int

const (
	Unknown DiagramType = iota
	Flowchart
	Sequence
	Class
	State
	Er
	// Unsupported is every family Kvit can name but does not draw (gantt,
	// pie, mindmap, timeline and the rest).
	Unsupported
)

// Direction is the way a diagram's ranks run.
type Direction int

const (
	TB Direction = iota
	BT
	LR
	RL
)

// NodeShape is a flowchart node's outline, one per vertex production of
// flow.jison. A shape Kvit does not know falls back to Rect.
type NodeShape int

const (
	ShapeRect             NodeShape = iota // A[text]
	ShapeRoundRect                         // A(text)
	ShapeStadium                           // A([text])
	ShapeSubroutine                        // A[[text]]
	ShapeCylinder                          // A[(text)]
	ShapeCircle                            // A((text))
	ShapeDoubleCircle                      // A(((text)))
	ShapeEllipse                           // A(-text-)
	ShapeRhombus                           // A{text}
	ShapeHexagon                           // A{{text}}
	ShapeParallelogram                     // A[/text/]   (lean right)
	ShapeParallelogramAlt                  // A[\text\]   (lean left)
	ShapeTrapezoid                         // A[/text\]   (narrow top)
	ShapeTrapezoidAlt                      // A[\text/]   (narrow bottom)
	ShapeOdd                               // A>text]
)

// EdgeStroke is how a flowchart link is drawn.
type EdgeStroke int

const (
	StrokeSolid EdgeStroke = iota
	StrokeDotted
	StrokeThick
)

// Span is a stretch of the fence's text, in runes. Edits to the source are
// worked out from these spans, never by searching the text again.
type Span struct {
	Start  int // -1 when the span is not set
	Length int
}

// NoSpan is the span of something the source does not hold.
var NoSpan = Span{Start: -1}

// Valid reports whether the span is set.
func (s Span) Valid() bool { return s.Start >= 0 }

// End is the offset just past the span.
func (s Span) End() int { return s.Start + s.Length }

// Contains reports whether offset is inside the span or at either end of it.
func (s Span) Contains(offset int) bool {
	return s.Valid() && offset >= s.Start && offset <= s.End()
}

// Color is a colour written in the source, such as a classDef's fill. The
// zero Color is a colour not given, as an invalid colour is in the app.
type Color struct {
	R, G, B, A uint8
	Set        bool
}

// Node is a flowchart vertex.
type Node struct {
	ID      string
	Label   string
	Shape   NodeShape
	Classes []string // classDef names applied by `class` or `:::`
	Order   int      // the order it was first met in, which layout breaks ties by

	IDSpan    Span   // the id of its first declaration
	LabelSpan Span   // the text between the shape's brackets
	ShapeSpan Span   // the whole bracket construct, `[label]`
	RefSpans  []Span // every mention of its id, the declaration included

	// An `A@{ shape: …, label: … }` block declares the node instead of
	// brackets, or as well as them, and wins wherever both appear. Editing
	// such a node has to rewrite the block, because adding brackets gives a
	// source this parser and mermaid.js read differently. LabelSpan then
	// covers the block's label value, quotes included.
	HasShapeData     bool
	LabelInShapeData bool
}

// Edge is a flowchart link.
type Edge struct {
	From, To   string
	Label      string
	ID         string // an `e1@-->` edge id, kept and not drawn
	Stroke     EdgeStroke
	ArrowStart bool // an arrowhead at the From end (`<-->`)
	ArrowEnd   bool // an arrowhead at the To end (`-->`); true unless the link says otherwise
	Invisible  bool // a `~~~` link: ranked like an edge, drawn as nothing
	MinLen     int  // the ranks it spans, from extra dashes (`--->`); 1 unless longer
	Order      int

	OpSpan   Span // the arrow, with any label written inside it
	PipeSpan Span // a `|label|` after the arrow
	StmtSpan Span // the whole statement it is part of
}

// Subgraph is a flowchart subgraph.
type Subgraph struct {
	ID           string
	Title        string
	NodeIDs      []string // the nodes declared inside it
	Direction    Direction
	HasDirection bool
}

// ClassDef is the restricted style a classDef, class, style or `:::` can
// give: fill and stroke colours, stroke width and dashes, and bold text.
// Any other CSS is ignored.
type ClassDef struct {
	Fill, Stroke Color
	StrokeWidth  float64 // 0 keeps the theme's width
	Dashed       bool
	Bold         bool
	HasFill      bool
	HasStroke    bool
}

// Severity says whether a diagnostic stops the diagram being drawn.
type Severity int

const (
	SeverityError Severity = iota
	SeverityWarning
)

// Diagnostic is an error or warning at a place in the fence's text.
type Diagnostic struct {
	Line     int // one-based
	Column   int // one-based, in runes
	Message  string
	Severity Severity
}

// ---- Sequence family (sequenceDiagram.jison) ----

// SeqLine is a message's line.
type SeqLine int

const (
	SeqSolid SeqLine = iota
	SeqDotted
)

// SeqHead is a message's arrowhead.
type SeqHead int

const (
	HeadFilled SeqHead = iota
	HeadOpen
	HeadCross
	HeadPoint
)

// SeqParticipant is a participant or actor.
type SeqParticipant struct {
	ID          string
	Label       string
	ActorFigure bool // declared with `actor`, drawn as a stick figure
	BoxIndex    int  // the `box` it is grouped in, -1 for none
	Order       int
	SrcSpan     Span // the statement declaring it, or first mentioning it
}

// SeqBox is a `box` grouping participants.
type SeqBox struct {
	Title string
	Color Color // not set: the theme's tint
}

// SeqEventKind is what a sequence statement is.
type SeqEventKind int

const (
	EventMessage SeqEventKind = iota
	EventNote
	EventActivate
	EventDeactivate
	EventBlockStart
	EventBlockDivider
	EventBlockEnd
	EventAutonumber
)

// SeqBlock is the kind of a fragment.
type SeqBlock int

const (
	BlockLoop SeqBlock = iota
	BlockAlt
	BlockOpt
	BlockPar
	BlockCritical
	BlockBreak
	BlockRect
)

// SeqPlacement is where a note sits.
type SeqPlacement int

const (
	PlaceLeftOf SeqPlacement = iota
	PlaceRightOf
	PlaceOver
)

// SeqEvent is one statement of a sequence diagram, in source order.
type SeqEvent struct {
	Kind SeqEventKind
	// A message's participants; a note's one or two; the one activated.
	From, To         string
	Text             string // a message's or a note's text
	Line             SeqLine
	Head             SeqHead
	Bidirectional    bool // `<<->>` or `<<-->>`
	ActivateTarget   bool // `->>+`
	DeactivateSource bool // `->>-`
	Placement        SeqPlacement
	Block            SeqBlock
	BlockLabel       string // a fragment's condition, a divider's label, a rect's colour
	AutonumberShown  bool
	AutonumberStart  int
	AutonumberStep   int
	SrcLine          int  // the statement's line, one-based
	SrcSpan          Span // the statement
}

// SequenceAst is a sequence diagram.
type SequenceAst struct {
	Participants []SeqParticipant
	Boxes        []SeqBox
	Events       []SeqEvent
	Title        string
	AccTitle     string
	AccDescr     string
}

// IndexOfParticipant is the index of the participant with id, or -1.
func (a *SequenceAst) IndexOfParticipant(id string) int {
	for i := range a.Participants {
		if a.Participants[i].ID == id {
			return i
		}
	}
	return -1
}

// MessageCount is the number of messages.
func (a *SequenceAst) MessageCount() int {
	n := 0
	for i := range a.Events {
		if a.Events[i].Kind == EventMessage {
			n++
		}
	}
	return n
}

// PosEntry is one `id=x,y` of a `%% mermaid-flow:pos` line: a node's centre
// in pixels from the scene's top left, as obsidian-mermaid-flow writes it.
type PosEntry struct {
	ID     string
	X, Y   float64
	IDSpan Span // where the id is, so a rename can take its position along
}

// FlowchartAst is a flowchart.
type FlowchartAst struct {
	Direction  Direction
	Nodes      []Node // in the order first met
	Edges      []Edge
	Subgraphs  []Subgraph
	ClassDefs  map[string]ClassDef
	AccTitle   string
	AccDescr   string
	HasPosLine bool // the fence has a pos line: positions are pinned
	PosEntries []PosEntry
	PosLine    Span // the whole pos line, without its line break
}

// IndexOfNode is the index of the node with id, or -1.
func (a *FlowchartAst) IndexOfNode(id string) int {
	for i := range a.Nodes {
		if a.Nodes[i].ID == id {
			return i
		}
	}
	return -1
}

// ---- Class family (classDiagram.jison) ----

// ClassRelEnd is what one end of a class relation is drawn with.
type ClassRelEnd int

const (
	RelNone        ClassRelEnd = iota
	RelExtension               // <| or |>, a hollow triangle
	RelComposition             // *, a filled diamond
	RelAggregation             // o, a hollow diamond
	RelDependency              // < or >, an open arrow
	RelLollipop                // (), a circle
)

// ClassNode is a class.
type ClassNode struct {
	ID             string
	Label          string // the name shown; `["label"]` overrides the id
	Annotation     string // <<interface>> and the like, without the chevrons
	Attributes     []string
	Methods        []string
	CSSClasses     []string
	NamespaceIndex int // -1 for none
	Order          int
	SrcSpan        Span
}

// ClassRelation is a relation between two classes.
type ClassRelation struct {
	From, To         string
	FromEnd, ToEnd   ClassRelEnd
	Dotted           bool
	Label            string
	FromCard, ToCard string // quoted cardinalities beside each end
	Order            int
	SrcSpan          Span
}

// ClassNamespace is a namespace, one level deep.
type ClassNamespace struct {
	Name     string
	ClassIDs []string
}

// ClassNote is a note, for a class or free standing.
type ClassNote struct {
	Text     string
	ForClass string // empty for a free-standing note
}

// ClassAst is a class diagram.
type ClassAst struct {
	Direction  Direction
	Classes    []ClassNode
	Relations  []ClassRelation
	Namespaces []ClassNamespace
	Notes      []ClassNote
	ClassDefs  map[string]ClassDef
	Title      string
	AccTitle   string
	AccDescr   string
}

// IndexOfClass is the index of the class with id, or -1.
func (a *ClassAst) IndexOfClass(id string) int {
	for i := range a.Classes {
		if a.Classes[i].ID == id {
			return i
		}
	}
	return -1
}

// ---- State family (stateDiagram.jison) ----

// StateKind is what a state is.
type StateKind int

const (
	StateNormal StateKind = iota
	StateStart            // a [*] a transition leaves
	StateEnd              // a [*] a transition arrives at
	StateFork             // <<fork>> or [[fork]]
	StateJoin
	StateChoice
)

// StateNode is a state.
type StateNode struct {
	ID           string
	Label        string   // its name, or the long description given with `as`
	Descriptions []string // `s1 : text` lines
	Kind         StateKind
	ParentIndex  int // the composite state it is inside, -1 for the top
	Composite    bool
	CSSClasses   []string
	Order        int
	SrcSpan      Span
}

// StateTransition is a transition.
type StateTransition struct {
	From, To string
	Label    string
	Order    int
	SrcSpan  Span
}

// StateNote is a note beside a state, or floating.
type StateNote struct {
	StateID string // empty for a floating note
	LeftOf  bool
	Text    string
}

// StateAst is a state diagram.
type StateAst struct {
	Direction   Direction
	States      []StateNode
	Transitions []StateTransition
	Notes       []StateNote
	ClassDefs   map[string]ClassDef
	Title       string
	AccTitle    string
	AccDescr    string
}

// IndexOfState is the index of the state with id, or -1.
func (a *StateAst) IndexOfState(id string) int {
	for i := range a.States {
		if a.States[i].ID == id {
			return i
		}
	}
	return -1
}

// ---- Entity-relationship family (erDiagram.jison) ----

// ErCardinality is one end of a relationship.
type ErCardinality int

const (
	ZeroOrOne  ErCardinality = iota // |o or o|
	ZeroOrMore                      // }o or o{
	OneOrMore                       // }| or |{
	OnlyOne                         // ||
	MdParent                        // u, drawn as only one
)

// ErAttribute is one row of an entity's attribute table.
type ErAttribute struct {
	Type    string
	Name    string
	Keys    []string // PK, FK, UK
	Comment string
}

// ErEntity is an entity.
type ErEntity struct {
	ID         string
	Label      string // a `NAME["label"]` alias; the id unless given
	Attributes []ErAttribute
	CSSClasses []string
	Order      int
	SrcSpan    Span
}

// ErRelationship is a relationship between two entities.
type ErRelationship struct {
	From, To    string
	FromCard    ErCardinality // drawn at From
	ToCard      ErCardinality // drawn at To
	Identifying bool          // a solid line; a non-identifying one is dashed
	Label       string
	Order       int
	SrcSpan     Span
}

// ErAst is an entity-relationship diagram.
type ErAst struct {
	Direction     Direction
	Entities      []ErEntity
	Relationships []ErRelationship
	ClassDefs     map[string]ClassDef
	Title         string
	AccTitle      string
	AccDescr      string
}

// IndexOfEntity is the index of the entity with id, or -1.
func (a *ErAst) IndexOfEntity(id string) int {
	for i := range a.Entities {
		if a.Entities[i].ID == id {
			return i
		}
	}
	return -1
}

// ParseResult is what Parse makes of a fence. Only the tree of Type is
// filled in.
type ParseResult struct {
	Type        DiagramType
	Supported   bool   // Kvit draws this family
	FamilyName  string // the header's keyword, for the unsupported message
	Flowchart   FlowchartAst
	Sequence    SequenceAst
	Class       ClassAst
	State       StateAst
	Er          ErAst
	Diagnostics []Diagnostic
}

// HasErrors reports whether any diagnostic is an error.
func (r *ParseResult) HasErrors() bool {
	for _, d := range r.Diagnostics {
		if d.Severity == SeverityError {
			return true
		}
	}
	return false
}

// FirstError is the first error. When there is none it is an empty error at
// line 1, column 1, as a default Diagnostic is in the app.
func (r *ParseResult) FirstError() Diagnostic {
	for _, d := range r.Diagnostics {
		if d.Severity == SeverityError {
			return d
		}
	}
	return Diagnostic{Line: 1, Column: 1}
}
