package textdiagram

import "strings"

// The characters Repair recognizes as parts of boxes and connectors, from
// the app's diagramglyphs.h. Canvas draws only characters from these
// sets, which is why Repair leaves a Canvas's text unchanged.

// IsTopLeft reports whether r is a box's top-left corner.
func IsTopLeft(r rune) bool { return strings.ContainsRune("┌┏╔╭", r) }

// IsTopRight reports whether r is a box's top-right corner.
func IsTopRight(r rune) bool { return strings.ContainsRune("┐┓╗╮", r) }

// IsBottomLeft reports whether r is a box's bottom-left corner.
func IsBottomLeft(r rune) bool { return strings.ContainsRune("└┗╚╰", r) }

// IsBottomRight reports whether r is a box's bottom-right corner.
func IsBottomRight(r rune) bool { return strings.ContainsRune("┘┛╝╯", r) }

// IsHFill reports whether r fills a horizontal edge between its corners.
func IsHFill(r rune) bool { return strings.ContainsRune("─━═┄┅┈┉╌╍-=", r) }

// IsEdgeJunction reports whether r is a junction or an arrowhead that can
// sit inside a horizontal edge. Such a character never ends an edge and is
// never trimmed over; Repair may slide it along the edge to line it up with
// a connector.
func IsEdgeJunction(r rune) bool {
	return strings.ContainsRune("┬┳╦╤╥┴┻╩╧╨┼╋▼▲+", r)
}

// IsWall reports whether r is a box's side bar.
func IsWall(r rune) bool {
	return strings.ContainsRune("│┃║╎╏┆┇┊┋├┤┣┫╠╣╞╡╟╢|", r)
}

// IsConnector reports whether r is a cell of a connector standing on its own
// between boxes.
func IsConnector(r rune) bool { return strings.ContainsRune("│┃║▼▲|", r) }
