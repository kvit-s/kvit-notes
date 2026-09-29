// Package textdiagram is Kvit Notes' character-cell diagrams: drawings made
// of box-drawing characters, arrows and boxed labels, kept in a code fence
// tagged `diagram` (or `text-diagram` or `ascii-diagram`). It has four
// parts, each a port of the app's src/content/diagrams/:
//
//   - Classify (diagramclassifier.cpp) decides whether a fence body is such
//
// a drawing. When a note is opened or Markdown is pasted, the app
// retags an untagged, `text`, `plaintext` or `ascii` fence to `diagram`
// when Classify says yes.
//   - Repair (diagramrepair.cpp) straightens a diagram fence at the same
//
// moments: a box's corners and side bars that stray from the column
// most of them share are moved onto it, and a connector that jogs
// sideways between rows is lined up under its tee. Every move swaps a
// character with spaces or edge fill, so nothing else on the line
// changes column.
//   - Canvas (textcanvas.cpp) is a grid of characters that boxes and lines
//
// are drawn onto, with crossings drawn as the right junction. It draws
// only the characters Repair recognizes (glyphs.go, diagramglyphs.h), so
// Repair leaves its output unchanged.
//   - FromScene (textdiagram.cpp, in fromscene.go) draws a laid-out Mermaid
//
// diagram, a diagram.Scene, on a Canvas: the text "Copy as text" puts
// on the clipboard.
//
// budget.go holds the limits of diagrambudget.h that Canvas uses.
//
// Positions are rune offsets into a line. The app counts UTF-16 code
// units, so the numbers differ after a character outside the Basic
// Multilingual Plane (most emoji), but they name the same characters. The
// size limits on a fence body (InspectionCapChars, RepairCapChars) are in
// UTF-16 code units, as the app counts them, so both apps refuse the same
// fences.
//
// The package knows nothing of the editor or the toolkit.
package textdiagram

// utf16Len is the length of s in UTF-16 code units, which is what string's
// size() counts.
func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		if r > 0xFFFF {
			n += 2
		} else {
			n++
		}
	}
	return n
}
