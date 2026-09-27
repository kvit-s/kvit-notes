package links

import (
	"strconv"
	"strings"
	"unicode"
)

// Heading anchors, from the Qt app's src/domain/documentoutline.cpp. A
// [[note#Heading]] link is followed by opening the note and turning the
// heading text into a slug, which is looked up among the slugs of the
// note's headings; [text](#slug) links look the slug up directly.

// Slug is the anchor text of a heading: lowercased; letters and digits of
// any script kept; each run of spaces, underscores and hyphens made one
// hyphen, and none at either end; every other character dropped.
// "Section 2: Details (v1)" becomes "section-2-details-v1". A character
// outside the Basic Multilingual Plane is dropped even when it is a letter,
// as in the Qt app, which sees it as two surrogates.
func Slug(text string) string {
	var b strings.Builder
	pending := false
	for _, r := range text {
		switch {
		case isLetterOrNumber(r):
			if pending && b.Len() > 0 {
				b.WriteByte('-')
			}
			pending = false
			b.WriteRune(unicode.ToLower(r))
		case isSpace(r) || r == '_' || r == '-':
			pending = true
		}
	}
	return b.String()
}

// Heading is one heading of a note, as the outline sees it.
type Heading struct {
	// Block is the heading's block index in the note.
	Block int
	// Text is the heading as displayed, with the Markdown markers of bold,
	// links and the like removed: "The bold title" for
	// "# The **bold** title".
	Text string
}

// Anchors gives each heading of a note, in document order, the slug links
// reach it by. The first heading with a slug keeps it and later ones get
// "-1", "-2" and so on, skipping any another heading already has, so
// "Foo", "Foo", "Foo-1" become "foo", "foo-1", "foo-1-1". A heading with no
// letters or digits is "section-" and its block index.
func Anchors(headings []Heading) []string {
	out := make([]string, len(headings))
	taken := map[string]bool{}
	counts := map[string]int{}
	for i, h := range headings {
		base := Slug(h.Text)
		if base == "" {
			base = "section-" + strconv.Itoa(h.Block)
		}
		seen := counts[base]
		slug := base
		if seen > 0 {
			slug = base + "-" + strconv.Itoa(seen)
		}
		for taken[slug] {
			seen++
			slug = base + "-" + strconv.Itoa(seen)
		}
		counts[base] = seen + 1
		taken[slug] = true
		out[i] = slug
	}
	return out
}

// FindAnchor is the index in headings of the heading whose anchor is slug,
// or -1. This is how [text](#slug) is followed.
func FindAnchor(headings []Heading, slug string) int {
	for i, a := range Anchors(headings) {
		if a == slug {
			return i
		}
	}
	return -1
}

// FindHeading is the index in headings of the heading a link's #heading
// part names, or -1: the heading text is made a slug and looked up among
// the anchors, so [[note#Getting started]], [[note#getting-started]] and
// [[note#GETTING STARTED!]] all reach "Getting Started", and
// [[note#Overview-1]] reaches the second "Overview".
func FindHeading(headings []Heading, heading string) int {
	return FindAnchor(headings, Slug(heading))
}

// Headings is the heading texts of a note body, in order, as [[note#
// completion offers them (WikiLinkIndex::headingsFor in
// src/repository/wikilinkindex.cpp): lines of one to six "#" and a space,
// outside ``` and ~~~ fences, with the text as written, markers included.
func Headings(body string) []string {
	var out []string
	inFence := false
	for _, line := range strings.Split(body, "\n") {
		t := trim(line)
		if strings.HasPrefix(t, "```") || strings.HasPrefix(t, "~~~") {
			inFence = !inFence
			continue
		}
		if inFence || !strings.HasPrefix(t, "#") {
			continue
		}
		level := 0
		for level < len(t) && t[level] == '#' {
			level++
		}
		if level > 6 || level >= len(t) || t[level] != ' ' {
			continue
		}
		if text := trim(t[level+1:]); text != "" {
			out = append(out, text)
		}
	}
	return out
}
