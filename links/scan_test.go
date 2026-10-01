package links

import (
	"slices"
	"testing"
)

// Where a link is and what its parts are.
func TestMatchAtFindsTheParts(t *testing.T) {
	cases := []struct {
		text                  string
		pos                   int
		start, length         int
		target, note          string
		heading, alias        string
		targetLen             int
		headingStart, aliasAt int
	}{
		{"[[My Note]]", 0, 0, 11, "My Note", "My Note", "", "", 7, -1, -1},
		{"see [[notes/plan]] soon", 4, 4, 14, "notes/plan", "notes/plan", "", "", 10, -1, -1},
		// With an alias the editor hides "[[Plan|", two brackets and the
		// target and the bar.
		{"[[Plan|the plan]]", 0, 0, 17, "Plan", "Plan", "", "the plan", 4, -1, 7},
		{"[[Plan#Goals]]", 0, 0, 14, "Plan#Goals", "Plan", "Goals", "", 10, 7, -1},
		{"[[Plan#Goals|goals]]", 0, 0, 20, "Plan#Goals", "Plan", "Goals", "goals", 10, 7, 13},
		{"[[#Goals]]", 0, 0, 10, "#Goals", "", "Goals", "", 6, 3, -1},
		// Spaces inside the brackets are kept out of each part.
		{"[[ a b # H 1 | al ]]", 0, 0, 20, "a b # H 1", "a b", "H 1", "al", 11, 9, 15},
		// "[[" wins over "[": never a [text](url) link.
		{"[[a]](b)", 0, 0, 5, "a", "a", "", "", 1, -1, -1},
		// Rune offsets: the emoji is one position.
		{"🙂 [[old]]", 2, 2, 7, "old", "old", "", "", 3, -1, -1},
	}
	for _, c := range cases {
		l, ok := MatchAt([]rune(c.text), c.pos)
		if !ok {
			t.Errorf("%q: no link at %d", c.text, c.pos)
			continue
		}
		if l.Start != c.start || l.Length != c.length || l.Target != c.target || l.Note != c.note ||
			l.Heading != c.heading || l.Alias != c.alias || l.TargetLength != c.targetLen ||
			l.HeadingStart != c.headingStart || l.AliasStart != c.aliasAt {
			t.Errorf("%q: got %+v", c.text, l)
		}
		rs := []rune(c.text)
		if got := string(rs[l.NoteStart : l.NoteStart+l.NoteLength]); got != c.note {
			t.Errorf("%q: note offsets cover %q", c.text, got)
		}
		if l.AliasStart >= 0 {
			if got := string(rs[l.AliasStart : l.AliasStart+l.AliasLength]); got != c.alias {
				t.Errorf("%q: alias offsets cover %q", c.text, got)
			}
		}
	}
}

// Unclosed, empty and malformed forms, and an escaped opener, are text.
func TestMalformedLinksAreText(t *testing.T) {
	for _, text := range []string{
		`\[[a]]`, "[[a", "[[]]", "[[  ]]", "[[a|]]", "[[a#]]", "[[a|b|c]]", "[[a\nb]]",
		"[[a#b#c]]", "[[a]b]]", "[[ |x]]", "[[a| ]]",
	} {
		if l, ok := MatchAt([]rune(text), 0); ok {
			t.Errorf("%q: matched %+v", text, l)
		}
		if links := Scan(text); len(links) != 0 {
			t.Errorf("%q: scanned %+v", text, links)
		}
	}
}

// Math owns what is inside it, and "![[note]]" is a "!" and an ordinary
// link.
func TestScanSkipsMathAndKeepsEmbedsAsLinks(t *testing.T) {
	if links := Scan("$[[x]]$"); len(links) != 0 {
		t.Errorf("inline math: %+v", links)
	}
	links := Scan("![[note]]")
	if len(links) != 1 || links[0].Start != 1 || links[0].Note != "note" {
		t.Errorf("![[note]]: %+v", links)
	}
}

// Targets keep their heading, lose their alias, repeat, and are not found
// in code, math or after a backslash.
func TestTargetsKeepTheirHeadingAndDropTheirAlias(t *testing.T) {
	cases := []struct {
		body string
		want []string
	}{
		{"See [[A]] then [[b/C|alias]] and [[A]].\n[[D#H]]", []string{"A", "b/C", "A", "D#H"}},
		{"[[before]]\n```\n[[inside]]\n```\n[[after]]", []string{"before", "after"}},
		{"[[]] [[a|]] [[a#]] [[#local]] \\[[escaped]]", nil},
		{"[[yes]] `[[code]]` ``x ` [[code2]] x`` $x + [[math]]$ $$\n[[display]]\n$$ \\$ [[after]]",
			[]string{"yes", "after"}},
		// An escaped dollar opens no math, and a longer fence is closed only
		// by a run of its own character at least as long.
		{"\\$[[literal]]$\n````lang\n[[fenced]]\n```\nstill [[fenced2]]\n````\n[[outside]]",
			[]string{"literal", "outside"}},
		// A ~~~ fence is not closed by ```, and an indented fence counts.
		{"~~~\n```\n[[in]]\n~~~\n  ```\n[[in2]]\n  ```\n[[out]]", []string{"out"}},
		// A code span that never closes hides only its backticks.
		{"`[[open]] and [[two]]", []string{"open", "two"}},
		// A code span may run over lines, and hides a fence inside it.
		{"`a\n```\n[[in]]` [[out]]", []string{"out"}},
		// "$5 and $6" is not math: the second dollar has a space before it.
		{"costs $5 [[x]] and $6 more", []string{"x"}},
	}
	for _, c := range cases {
		if got := Targets(c.body); !slices.Equal(got, c.want) {
			t.Errorf("%q: got %q, want %q", c.body, got, c.want)
		}
	}
}
