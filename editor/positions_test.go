package editor

import "testing"

// The core's tests/test_blockpositions.cpp, case for case. Blocks: 0 a
// formatted paragraph, 1 a code block, 2 a divider, 3 a paragraph whose
// first character is a marker.

const (
	positionsParagraph       = "This is **bold** text"
	positionsParagraphLength = 21
	positionsDisplayLength   = 17
	positionsCode            = "let x = **not markdown**\nf()"
)

func positionsDoc() *Doc {
	return NewDoc([]Block{
		NewBlock(Paragraph, positionsParagraph),
		{ID: NewBlock(Code, "").ID, Kind: Code, Text: positionsCode},
		NewBlock(Divider, ""),
		NewBlock(Paragraph, "**bold** tail"),
	})
}

func expectPos(t *testing.T, what string, got, want int) {
	t.Helper()
	if got != want {
		t.Errorf("%s = %d, want %d", what, got, want)
	}
}

func TestMarkdownPositionSkipsHiddenMarkers(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "display 0", d.MarkdownPosition(0, 0), 0)
	expectPos(t, "display 5", d.MarkdownPosition(0, 5), 5)
	expectPos(t, "display 8", d.MarkdownPosition(0, 8), 10)
	expectPos(t, "display 11", d.MarkdownPosition(0, 11), 13)
	expectPos(t, "display 12", d.MarkdownPosition(0, 12), 16)
	expectPos(t, "the end", d.MarkdownPosition(0, positionsDisplayLength), positionsParagraphLength)
}

func TestMarkdownPositionInVerbatimBlockIsIdentity(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "code 0", d.MarkdownPosition(1, 0), 0)
	expectPos(t, "code 12", d.MarkdownPosition(1, 12), 12)
	expectPos(t, "code end", d.MarkdownPosition(1, len(positionsCode)), len(positionsCode))
}

func TestMarkdownPositionPastEndClampsToContent(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "one past", d.MarkdownPosition(0, positionsDisplayLength+1), positionsParagraphLength)
	expectPos(t, "far past", d.MarkdownPosition(0, 500), positionsParagraphLength)
	expectPos(t, "code far past", d.MarkdownPosition(1, 500), len(positionsCode))
}

func TestMarkdownPositionNegativeMapsToStartOfText(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "-1", d.MarkdownPosition(0, -1), 0)
	expectPos(t, "-500", d.MarkdownPosition(0, -500), 0)
	expectPos(t, "code -1", d.MarkdownPosition(1, -1), 0)
	expectPos(t, "marker first, 0", d.MarkdownPosition(3, 0), 2)
	expectPos(t, "marker first, -4", d.MarkdownPosition(3, -4), 2)
}

func TestMarkdownPositionOutOfRangeBlockIsZero(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "count", d.MarkdownPosition(len(d.Blocks), 5), 0)
	expectPos(t, "99", d.MarkdownPosition(99, 5), 0)
	expectPos(t, "-1", d.MarkdownPosition(-1, 5), 0)
}

func TestDisplayPositionSkipsHiddenMarkers(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "md 0", d.DisplayPosition(0, 0), 0)
	expectPos(t, "md 5", d.DisplayPosition(0, 5), 5)
	expectPos(t, "md 10", d.DisplayPosition(0, 10), 8)
	expectPos(t, "md 13", d.DisplayPosition(0, 13), 11)
	expectPos(t, "md 16", d.DisplayPosition(0, 16), 12)
	expectPos(t, "the end", d.DisplayPosition(0, positionsParagraphLength), positionsDisplayLength)
}

func TestDisplayPositionInsideMarkerClampsToContentEdge(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "md 8", d.DisplayPosition(0, 8), 8)
	expectPos(t, "md 9", d.DisplayPosition(0, 9), 8)
	expectPos(t, "md 14", d.DisplayPosition(0, 14), 12)
	expectPos(t, "md 15", d.DisplayPosition(0, 15), 12)
}

func TestDisplayPositionInVerbatimBlockIsIdentity(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "code 0", d.DisplayPosition(1, 0), 0)
	expectPos(t, "code 12", d.DisplayPosition(1, 12), 12)
	expectPos(t, "code end", d.DisplayPosition(1, len(positionsCode)), len(positionsCode))
}

func TestDisplayPositionPastEndClampsToDisplayText(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "one past", d.DisplayPosition(0, positionsParagraphLength+1), positionsDisplayLength)
	expectPos(t, "far past", d.DisplayPosition(0, 500), positionsDisplayLength)
	expectPos(t, "code far past", d.DisplayPosition(1, 500), len(positionsCode))
}

func TestDisplayPositionNegativeIsZero(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "-1", d.DisplayPosition(0, -1), 0)
	expectPos(t, "-500", d.DisplayPosition(0, -500), 0)
	expectPos(t, "code -1", d.DisplayPosition(1, -1), 0)
	expectPos(t, "marker first, -1", d.DisplayPosition(3, -1), 0)
}

func TestDisplayPositionOutOfRangeBlockIsZero(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "count", d.DisplayPosition(len(d.Blocks), 5), 0)
	expectPos(t, "99", d.DisplayPosition(99, 5), 0)
	expectPos(t, "-1", d.DisplayPosition(-1, 5), 0)
}

func TestEmptyBlockAnswersZero(t *testing.T) {
	d := positionsDoc()
	expectPos(t, "markdown 0", d.MarkdownPosition(2, 0), 0)
	expectPos(t, "markdown 7", d.MarkdownPosition(2, 7), 0)
	expectPos(t, "display 0", d.DisplayPosition(2, 0), 0)
	expectPos(t, "display 7", d.DisplayPosition(2, 7), 0)
}

func TestRoundTripOverEveryDisplayPosition(t *testing.T) {
	d := positionsDoc()
	for display := 0; display <= positionsDisplayLength; display++ {
		md := d.MarkdownPosition(0, display)
		expectPos(t, "paragraph round trip", d.DisplayPosition(0, md), display)
	}
	for display := 0; display <= len(positionsCode); display++ {
		md := d.MarkdownPosition(1, display)
		expectPos(t, "code round trip", d.DisplayPosition(1, md), display)
	}
	if got := d.DisplayText(0); got != "This is bold text" {
		t.Errorf("display text %q", got)
	}
}
