package editor

// Column widths much as a browser's automatic table layout gives them
// (Embedding.FitTables), for the columns the reader has not dragged:
//
//   - a column's natural width is its widest cell on one line, and its least
//     width its longest piece that cannot be broken (a word, a path, a link),
//     each with the cell's padding and at least minColumn;
//   - when the natural widths fit the room, the columns take them, and the
//     table is as wide as its content;
//   - when only the least widths fit, every column takes its least width and
//     the room left over is shared out by how much more each column wants
//     (natural minus least): a column that wants no more than an equal share
//     of what is left gets all it wants, the smallest wants first, and the
//     columns still wanting more share the rest in proportion to their wants;
//   - when even the least widths do not fit, a table that may scroll
//     (Embedding.ScrollTables) takes them and scrolls sideways
//     (tablescroll.go), and any other is squeezed by the note's own rule,
//     every column scaled by the same factor.
//
// A browser shares the whole room in proportion to the wants, which wraps a
// short cell with a space in it, such as "95 ms", beside a long sentence.
// Satisfying the small wants first keeps such a column on one line and
// leaves only the columns of prose to wrap.
//
// A dragged column keeps its width and takes no share. A piece runs from one
// space or line break to the next, so a path, a link or a hyphenated name is
// one piece, even though the text layer may break it after a hyphen when its
// column is narrower. Text written without spaces, such as Chinese or
// Japanese, is broken where the text layer breaks it: Unicode's line
// breaking rules (UAX #14), through go-text's segmenter.

import (
	"math"
	"slices"
	"unicode"

	"github.com/go-text/typesetting/segmenter"
	"github.com/kvit-s/kvit-ui/text"
)

// measuresLeast reports whether a table's least column widths are wanted:
// when its columns are fitted, or it may scroll once they no longer fit.
func (e *Editor) measuresLeast() bool {
	return e.seams.fitTables() || (e.seams.scrollTables() && !e.Printing)
}

// longestPiece is the width of the widest piece of a laid-out line, as the
// file's comment says, trailing spaces left out.
func longestPiece(l *text.Layout) float32 {
	rs := []rune(l.Text())
	if len(rs) == 0 {
		return 0
	}
	var seg segmenter.Segmenter
	seg.Init(rs)
	it := seg.LineIterator()
	var widest float32
	start := 0
	for it.Next() {
		line := it.Line()
		end := line.Offset + len(line.Text)
		if end < len(rs) && !pieceEndsAt(rs, end) {
			continue
		}
		last := end
		for last > start && unicode.IsSpace(rs[last-1]) {
			last--
		}
		if last > start {
			x0, _, _ := l.CaretAt(start)
			x1, _, _ := l.CaretAt(last)
			widest = max(widest, abs32(x1-x0))
		}
		start = end
	}
	return widest
}

// pieceEndsAt reports whether a break the text layer may make before rs[at]
// ends a piece: after a space or a line break, or between two characters
// outside ASCII, as in text written without spaces.
func pieceEndsAt(rs []rune, at int) bool {
	before := rs[at-1]
	return unicode.IsSpace(before) || (before > unicode.MaxASCII && rs[at] > unicode.MaxASCII)
}

// fitColumns gives the columns not pinned by a drag their widths in room, as
// the file's comment says, and reports whether it did: false leaves them to
// the note's own rule, which a table gets with FitTables off or when its
// least widths do not fit and it may not scroll.
func (e *Editor) fitColumns(cols []float32, pinned []bool, natural, least []float32, room float32) bool {
	fit, scroll := e.seams.fitTables(), e.seams.scrollTables() && !e.Printing
	var fixed, sumNatural, sumLeast float32
	for c := range cols {
		if pinned[c] {
			fixed += cols[c]
			continue
		}
		sumNatural += natural[c]
		sumLeast += least[c]
	}
	free := room - fixed
	switch {
	case sumNatural <= free:
		if !fit {
			return false
		}
		for c := range cols {
			if !pinned[c] {
				cols[c] = natural[c]
			}
		}
	case sumLeast <= free:
		if !fit {
			return false
		}
		shareRoom(cols, pinned, natural, least, free-sumLeast)
	default:
		if !scroll {
			return false
		}
		for c := range cols {
			if !pinned[c] {
				cols[c] = least[c]
			}
		}
	}
	return true
}

// shareRoom gives the columns not pinned their least widths and shares extra,
// the room left over, among those that want more: a column whose want is no
// more than an equal share of what is left takes all of it, the smallest
// wants first, and the rest share what remains in proportion to their wants.
// The wants add up to more than extra, so at least one column is left
// wanting and none passes its natural width.
func shareRoom(cols []float32, pinned []bool, natural, least []float32, extra float32) {
	var open []int
	for c := range cols {
		if pinned[c] {
			continue
		}
		cols[c] = least[c]
		if natural[c] > least[c] {
			open = append(open, c)
		}
	}
	want := func(c int) float32 { return natural[c] - least[c] }
	slices.SortStableFunc(open, func(a, b int) int {
		switch {
		case want(a) < want(b):
			return -1
		case want(a) > want(b):
			return 1
		}
		return 0
	})
	// Satisfying a want no larger than the equal share leaves the others a
	// share at least as large, so the smallest wants are taken in order.
	for len(open) > 0 && want(open[0]) <= extra/float32(len(open)) {
		extra -= want(open[0])
		cols[open[0]] = natural[open[0]]
		open = open[1:]
	}
	var wanted float32
	for _, c := range open {
		wanted += want(c)
	}
	if wanted <= 0 {
		return
	}
	for _, c := range open {
		cols[c] = least[c] + float32(math.Floor(float64(extra*want(c)/wanted)))
	}
}
