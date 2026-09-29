package diagram

// Mathematics in a diagram label, a port of the app's
// src/content/diagrams/diagramtext.cpp and the archived notes sources/diagram-math.md.
//
// Mermaid settled the syntax in v10.9.0: a label may be a LaTeX expression
// between `$$` delimiters, in flowchart node and edge labels and in a
// sequence diagram's participants, messages and notes. Only a whole label
// counts: a label with a `$$…$$` among other words stays text, because a
// Text has one font, and a single `$` is always literal, which keeps
// currency amounts and shell variables in existing diagrams as they were.
// Layout sizes a math label's box from LayoutOptions.Math, and the editor
// typesets the same TeX into it.

import (
	"regexp"
	"strings"
	"unicode/utf8"
)

// brTag is an HTML line break in any spelling Mermaid accepts: `<br>`,
// `<br/>`, `<BR />`.
var brTag = regexp.MustCompile(`(?i)<br\s*/?>`)

// normalizeBreaks turns `<br>` tags and the two characters `\n` into line
// breaks, which is how every family writes a line break in a label.
func normalizeBreaks(label string) string {
	s := brTag.ReplaceAllString(label, "\n")
	return strings.ReplaceAll(s, `\n`, "\n")
}

// LabelLines splits a label into its lines at `<br>` tags and `\n` escapes.
func LabelLines(label string) []string {
	return strings.Split(normalizeBreaks(label), "\n")
}

// MathLabel is the TeX inside a label that is one whole `$$…$$` expression,
// or "" for every other label. Line breaks are normalized and the ends
// trimmed first, so `$$x^2$$<br>` still counts.
func MathLabel(label string) string {
	s := strings.TrimSpace(normalizeBreaks(label))
	// "$$x$$" is the shortest expression with any content.
	if utf8.RuneCountInString(s) < 5 || !strings.HasPrefix(s, "$$") || !strings.HasSuffix(s, "$$") {
		return ""
	}
	inner := s[2 : len(s)-2]
	// Two expressions side by side are not one label's worth of mathematics,
	// and the delimiters would pair the wrong way round.
	if strings.Contains(inner, "$$") || strings.TrimSpace(inner) == "" {
		return ""
	}
	return inner
}

// mathSize is the typeset size of tex, and false when there is nothing to
// typeset, no MathMeasurer, a TeX source over MaxTexChars, or TeX that does
// not typeset. The caller then measures and draws the label's source.
func (o *LayoutOptions) mathSize(tex string) (Size, bool) {
	if tex == "" || o.Math == nil || utf8.RuneCountInString(tex) > MaxTexChars {
		return Size{}, false
	}
	sz, ok := o.Math.Size(tex)
	if !ok || sz.W <= 0 || sz.H <= 0 {
		return Size{}, false
	}
	return sz, true
}

// mathTex is the label's TeX when it is one expression that typesets, and ""
// otherwise; it is what a Text's Tex is set to.
func (o *LayoutOptions) mathTex(label string) string {
	tex := MathLabel(label)
	if _, ok := o.mathSize(tex); ok {
		return tex
	}
	return ""
}
