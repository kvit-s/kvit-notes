package links

import (
	"slices"
	"testing"
)

// From TestDocumentOutline::testBaseSlug.
func TestSlug(t *testing.T) {
	cases := []struct{ text, slug string }{
		{"Introduction", "introduction"},
		{"Getting Started", "getting-started"},
		{"HELLO World", "hello-world"},
		{"What's new?", "whats-new"},
		{"Section 2: Details (v1)", "section-2-details-v1"},
		{"snake_case_name", "snake-case-name"},
		{"a  --  b", "a-b"},
		{"  spaced  ", "spaced"},
		{"!!!bang", "bang"},
		{"Chapter 12", "chapter-12"},
		{"Café Ünïcode", "café-ünïcode"},
		{"***", ""},
		// sees a character beyond the Basic Multilingual Plane as two
		// surrogates, neither a letter, so it is dropped.
		{"𝐀bc 🙂 d", "bc-d"},
		{"\u0130x", "ix"},
	}
	for _, c := range cases {
		if got := Slug(c.text); got != c.slug {
			t.Errorf("Slug(%q) = %q, want %q", c.text, got, c.slug)
		}
	}
}

func headings(texts ...string) []Heading {
	out := make([]Heading, len(texts))
	for i, text := range texts {
		out[i] = Heading{Block: i * 2, Text: text}
	}
	return out
}

// From TestDocumentOutline::testCollisionSuffixesInDocumentOrder and
// testCollisionWithLiteralSuffixHeading: repeated headings are numbered in
// order, and no two headings ever share an anchor.
func TestAnchorsAreUnique(t *testing.T) {
	cases := []struct {
		texts []string
		want  []string
	}{
		{[]string{"Overview", "Overview", "Overview"}, []string{"overview", "overview-1", "overview-2"}},
		{[]string{"Foo", "Foo", "Foo-1"}, []string{"foo", "foo-1", "foo-1-1"}},
		{[]string{"Foo-1", "Foo", "Foo"}, []string{"foo-1", "foo", "foo-2"}},
		{[]string{"***", "Title", ""}, []string{"section-0", "title", "section-4"}},
	}
	for _, c := range cases {
		if got := Anchors(headings(c.texts...)); !slices.Equal(got, c.want) {
			t.Errorf("Anchors(%q) = %q, want %q", c.texts, got, c.want)
		}
	}
}

// A link's #heading is heading text, made a slug before it is looked up
// (qml/NoteSession scrollToHeadingText).
func TestFindHeading(t *testing.T) {
	hs := headings("Getting Started", "Overview", "Overview", "Café")
	cases := map[string]int{
		"Getting Started":  0,
		"getting-started":  0,
		"GETTING STARTED!": 0,
		"Overview":         1,
		"Overview-1":       2,
		"overview 1":       2,
		"café":             3,
		"Nowhere":          -1,
		"!!!":              -1,
	}
	for heading, want := range cases {
		if got := FindHeading(hs, heading); got != want {
			t.Errorf("FindHeading(%q) = %d, want %d", heading, got, want)
		}
	}
	if got := FindAnchor(hs, "overview-1"); got != 2 {
		t.Errorf("FindAnchor(overview-1) = %d", got)
	}
	if got := FindAnchor(hs, "Overview"); got != -1 {
		t.Errorf("an anchor is matched exactly, got %d", got)
	}
}

// What [[note# completion lists (WikiLinkIndex::headingsFor).
func TestHeadingsForCompletion(t *testing.T) {
	body := "# One\n\ntext\n  ## Two **bold**  \n```\n# not\n```\n####### seven\n#nospace\n#\tTab\n~~~\n# not either\n~~~\n###### Six\n#  \n"
	want := []string{"One", "Two **bold**", "Six"}
	if got := Headings(body); !slices.Equal(got, want) {
		t.Errorf("Headings = %q, want %q", got, want)
	}
}
