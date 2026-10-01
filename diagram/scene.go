// Package diagram lays out a Mermaid syntax tree (package mermaid) into a
// Scene: shapes, paths and text in logical pixels, with colours named by
// their role rather than resolved. The editor draws a scene on screen and
// into PDF and PNG exports, and package textdiagram turns one into
// box-drawing text. It knows nothing of the toolkit: text is measured
// through LayoutOptions.
package diagram

import "github.com/kvit-s/kvit-notes/mermaid"

// Point is a position in logical pixels.
type Point struct{ X, Y float64 }

// Size is a width and a height in logical pixels.
type Size struct{ W, H float64 }

// Rect is a rectangle with its top left at X, Y.
type Rect struct{ X, Y, W, H float64 }

// Role is what a colour is for; the theme gives the colour when the scene
// is drawn.
type Role int

const (
	RoleBackground Role = iota
	RoleNodeFill
	RoleNodeStroke
	RoleEdgeStroke
	RoleLabel
	RoleEdgeLabel
	RoleSubgraphFill
	RoleSubgraphStroke
	RoleNoteFill   // sequence-diagram notes
	RoleNoteStroke // sequence-diagram notes
	RoleActivation // a sequence diagram's activation bars
)

// Marker is drawn at one end of a path.
type Marker int

const (
	MarkerNone          Marker = iota
	MarkerArrow                // a filled triangle (`-->`, `->>`)
	MarkerOpenArrow            // two stroked lines (`->`)
	MarkerCross                // `-x`
	MarkerDot                  // `-)`
	MarkerTriangleOpen         // UML extension, `<|--`
	MarkerDiamondFilled        // UML composition, `*--`
	MarkerDiamondOpen          // UML aggregation, `o--`
	MarkerCircleOpen           // UML lollipop, `()--`
	MarkerErOne                // crow's foot `||`
	MarkerErZeroOne            // crow's foot `|o`
	MarkerErMany               // crow's foot `}|`
	MarkerErZeroMany           // crow's foot `}o`
)

// ShapeKind is a shape's outline.
type ShapeKind int

const (
	KindRect ShapeKind = iota
	KindRoundRect
	KindStadium
	KindCircle
	KindDoubleCircle
	KindEllipse
	KindRhombus
	KindHexagon
	KindCylinder
	KindSubroutine
	KindParallelogram
	KindParallelogramAlt
	KindTrapezoid
	KindTrapezoidAlt
	KindOdd
	KindActor // a sequence diagram's stick figure; Rect is its bounds
)

// Shape is a node's outline.
type Shape struct {
	Kind           ShapeKind
	Rect           Rect
	FillRole       Role
	StrokeRole     Role
	StrokeWidth    float64
	FillOverride   mermaid.Color // from classDef or style; not set: the role's colour
	StrokeOverride mermaid.Color
	NodeID         string // for screen readers, hit testing and selection
	Src            mermaid.Span
}

// SegmentKind is one step of a path.
type SegmentKind int

const (
	MoveTo  SegmentKind = iota // Pts[0]
	LineTo                     // Pts[0]
	QuadTo                     // control Pts[0], end Pts[1]
	CubicTo                    // controls Pts[0] and Pts[1], end Pts[2]
	Close
)

// Segment is one step of a path.
type Segment struct {
	Kind SegmentKind
	Pts  [3]Point
}

// Outline is a sequence of path steps: moves, lines, curves and closes.
type Outline struct {
	Segs []Segment
}

// LineStyle is how a path is stroked.
type LineStyle int

const (
	LineSolid LineStyle = iota
	LineDashed
	LineDotted // a flowchart's dotted link, `-.->`, drawn as dots
)

// Path is an edge, a lifeline or a message line.
type Path struct {
	Outline     Outline
	StrokeRole  Role
	StrokeWidth float64
	Style       LineStyle
	StartMarker Marker
	EndMarker   Marker
	StartPoint  Point // where the start marker sits
	EndPoint    Point // where the end marker sits
	StartDir    Point // a unit vector pointing into the start node
	EndDir      Point // a unit vector pointing into the end node
	// EdgeIndex is the syntax tree's edge this path draws, counted within
	// its family, or -1 for a path that cannot be selected.
	EdgeIndex int
	Src       mermaid.Span
}

// Align places text within its rectangle.
type Align int

const (
	AlignCenter Align = iota
	AlignLeft         // at the left, centred from top to bottom
)

// Text is a label.
type Text struct {
	Rect Rect // alignment applies within it
	Text string
	// Tex is the label's TeX when the whole label is one $$…$$ expression,
	// and empty otherwise. Layout has sized Rect from the typeset formula,
	// so the formula is drawn instead of Text, which still holds the
	// label's source for screen readers, for search, and to draw when the
	// formula cannot be typeset.
	Tex           string
	Role          Role
	FontSize      float64
	Bold          bool
	Italic        bool
	Align         Align
	HasBackground bool // edge labels sit on a small backdrop
}

// Group is a subgraph's frame, or a band behind a sequence diagram's
// participants.
type Group struct {
	Rect         Rect
	Title        string
	FillOverride mermaid.Color // not set: RoleSubgraphFill
	NoBorder     bool
}

// Scene is a laid-out diagram. Groups are drawn first, then shapes, paths
// and texts.
type Scene struct {
	Groups []Group
	Shapes []Shape
	Paths  []Path
	Texts  []Text
	Bounds Rect
	// For screen readers.
	AccTitle string
	AccDescr string
	Summary  string // made by layout, such as "Mermaid flowchart, 8 nodes, 9 edges"
}

// Empty reports whether the scene has nothing to draw.
func (s *Scene) Empty() bool { return len(s.Shapes) == 0 && len(s.Paths) == 0 }

// Measurer measures a diagram's text in the font it will be drawn in, at
// LayoutOptions.FontSize, regular weight.
type Measurer interface {
	// Advance is the width of s.
	Advance(s string) float64
	// Height is the height of a line.
	Height() float64
}

// MathMeasurer typesets a label that is one $$…$$ expression. Size is the
// formula's size set beside text of the diagram's font, and ok is false
// when the TeX does not typeset.
type MathMeasurer interface {
	Size(tex string) (size Size, ok bool)
}

// LayoutOptions are what a layout depends on besides the syntax tree.
type LayoutOptions struct {
	// FontFamily names the font, for the render cache's key; Measure
	// measures in it.
	FontFamily string
	FontSize   float64 // pixels; 14 unless set
	Direction  mermaid.Direction
	Measure    Measurer
	Math       MathMeasurer // nil: math labels are drawn as their source
}
