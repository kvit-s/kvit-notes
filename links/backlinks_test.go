package links

import (
	"reflect"
	"strings"
	"testing"
)

// From TestNoteCollection::testBacklinks: two links on one line are two
// links and one context line, and a link in a fence is no link.
func TestBacklinks(t *testing.T) {
	bodies := map[string]string{
		"A.md": "Links to [[B]] twice: [[b#Heading|alias]]\n",
		"B.md": "No outgoing links\n",
		"C.md": "```\n[[B]]\n```\nOnly a fenced mention\n",
	}
	ix := NewIndex([]string{"A.md", "B.md", "C.md"})
	want := []Backlink{{Path: "A.md", Count: 2, Contexts: []string{"Links to [[B]] twice: [[b#Heading|alias]]"}}}
	if got := ix.Backlinks("B.md", bodies); !reflect.DeepEqual(got, want) {
		t.Errorf("Backlinks(B.md) = %+v, want %+v", got, want)
	}
	if got := ix.Backlinks("A.md", bodies); len(got) != 0 {
		t.Errorf("Backlinks(A.md) = %+v", got)
	}
}

// From tst_integration's test_wiki2_backlinksPanelListsAndUpdatesLive,
// with the rest of the pane's rules: rows sorted by path with capitals
// first, a note's links to itself left out, each line once, and a line cut
// to 200 UTF-16 code units without splitting an emoji.
func TestBacklinksRows(t *testing.T) {
	long := strings.Repeat("x", 197) + "🙂🙂 [[Welcome]]"
	bodies := map[string]string{
		"Welcome.md":       "Self [[Welcome]]\n",
		"Ideas/Reading.md": "See [[Welcome]] and [[welcome#Intro|w]].",
		"Ideas/Plans.md":   "  Also [[Welcome]].  \nmiddle\nand [[/welcome]] again\n",
		"ideas/lower.md":   long,
		"Other.md":         "[[Nope]] `[[Welcome]]`\n",
	}
	ix := NewIndex([]string{"Welcome.md", "Ideas/Reading.md", "Ideas/Plans.md", "ideas/lower.md", "Other.md"})
	want := []Backlink{
		{Path: "Ideas/Plans.md", Count: 2, Contexts: []string{"Also [[Welcome]].", "and [[/welcome]] again"}},
		{Path: "Ideas/Reading.md", Count: 2, Contexts: []string{"See [[Welcome]] and [[welcome#Intro|w]]."}},
		{Path: "ideas/lower.md", Count: 1, Contexts: []string{strings.Repeat("x", 197) + "🙂"}},
	}
	if got := ix.Backlinks("Welcome.md", bodies); !reflect.DeepEqual(got, want) {
		t.Errorf("Backlinks = %+v\nwant %+v", got, want)
	}
}

// The context of a link at the very start of a note is the note's first
// line. The Qt app shows the last line instead, or nothing when the body
// ends with a newline, because QString::lastIndexOf with a start of -1
// searches from the end; that is not reproduced.
func TestBacklinkAtTheStartOfANote(t *testing.T) {
	bodies := map[string]string{"A.md": "[[B]] first line\nsecond\n", "B.md": ""}
	got := NewIndex([]string{"A.md", "B.md"}).Backlinks("B.md", bodies)
	if len(got) != 1 || !reflect.DeepEqual(got[0].Contexts, []string{"[[B]] first line"}) {
		t.Errorf("Backlinks = %+v", got)
	}
}
