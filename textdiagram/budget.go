package textdiagram

// The limits on a Canvas, from the app's diagrambudget.h. A note is
// untrusted input, and when a diagram is turned into text its coordinates
// decide how many cells the canvas asks for: a node pinned far away by an
// arrangement comment would otherwise make a grid of any size. Drawing
// outside these limits is dropped, so the text is cut off rather than the
// memory unbounded.
//
// The row and column limits alone do not bound the storage: a box spanning
// both of them has side walls that grow every row it touches out to the far
// column, 4 × 10^8 cells at 20,000 by 20,000. MaxCanvasCells, the total of
// every row's length, is therefore the limit that binds. Every real diagram
// is far inside all three.
const (
	MaxCanvasRows  = 20000
	MaxCanvasCols  = 20000
	MaxCanvasCells = 2 * 1000 * 1000
)
