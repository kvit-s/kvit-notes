package highlight

import (
	"fmt"
	"os"
	"slices"
	"strings"
	"testing"
	"unicode/utf8"
)

// sample is one text of testdata/corpus.txt.
type sample struct{ name, text string }

// corpus reads testdata/corpus.txt: each sample starts after a line
// "==== name" and runs to the next such line, its lines joined with
// newlines.
func corpus(t testing.TB) []sample {
	t.Helper()
	data, err := os.ReadFile("testdata/corpus.txt")
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(string(data), "\n")
	if lines[len(lines)-1] == "" {
		lines = lines[:len(lines)-1]
	}
	var out []sample
	var body []string
	for _, l := range lines {
		if name, ok := strings.CutPrefix(l, "==== "); ok {
			if len(out) > 0 {
				out[len(out)-1].text = strings.Join(body, "\n")
			}
			out = append(out, sample{name: name})
			body = nil
		} else if len(out) > 0 {
			body = append(body, l)
		}
	}
	if len(out) > 0 {
		out[len(out)-1].text = strings.Join(body, "\n")
	}
	return out
}

// ids are the IDs of every language Kvit colours: the menu's, and Mermaid.
func ids() []string {
	var out []string
	for _, l := range Languages() {
		out = append(out, l.ID)
	}
	return append(out, "mermaid")
}

// format writes spans as testdata/qt-spans.txt has them: start-end:class,
// with K, T, S, C and N for the classes.
func format(spans []Span) string {
	parts := make([]string, len(spans))
	for i, s := range spans {
		parts[i] = fmt.Sprintf("%d-%d:%c", s.Start, s.End, "PKTSCN"[s.Class])
	}
	return strings.Join(parts, " ")
}

// Every sample of the corpus, in every language, is coloured exactly as the
// Qt app's highlighter colours it. testdata/qt-spans.txt is the Qt
// highlighter's output, made by testdata/qtspans/main.cpp.
func TestQtSpans(t *testing.T) {
	samples := corpus(t)
	data, err := os.ReadFile("testdata/qt-spans.txt")
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]string{}
	for _, l := range strings.Split(strings.TrimSuffix(string(data), "\n"), "\n") {
		f := strings.SplitN(l, "\t", 3)
		if len(f) != 3 {
			t.Fatalf("qt-spans.txt: bad line %q", l)
		}
		want[f[0]+"\t"+f[1]] = f[2]
	}
	if n := len(samples) * len(ids()); len(want) != n {
		t.Fatalf("qt-spans.txt has %d lines, the corpus makes %d; make it again with testdata/qtspans", len(want), n)
	}
	for _, s := range samples {
		for _, id := range ids() {
			w, ok := want[s.name+"\t"+id]
			if !ok {
				t.Errorf("qt-spans.txt has no line for %s in %s", s.name, id)
				continue
			}
			if got := format(Highlight(id, s.text)); got != w {
				t.Errorf("%s in %s:\n got %s\nwant %s", s.name, id, got, w)
			}
		}
	}
}

// Colouring a whole text is colouring its lines one after another, each
// starting in the state the line before ended in, and moving each line's
// spans by where the line starts (wholeTextEqualsThreadedLines). That is the
// contract that lets an editor colour only the lines an edit changed.
func TestWholeTextEqualsThreadedLines(t *testing.T) {
	for _, s := range corpus(t) {
		for _, id := range ids() {
			var threaded []Span
			st, base := normal, 0
			for _, line := range strings.Split(s.text, "\n") {
				var spans []Span
				spans, st = lineSpans(id, line, st)
				for _, sp := range spans {
					threaded = append(threaded, Span{sp.Start + base, sp.End + base, sp.Class})
				}
				base += utf8.RuneCountInString(line) + 1
			}
			if got := Highlight(id, s.text); !slices.Equal(got, threaded) {
				t.Errorf("%s in %s: whole text %v, line by line %v", s.name, id, got, threaded)
			}
		}
	}
}

// checkSpans says what is wrong with spans for text, if anything: every span
// must be coloured, not empty, inside the text, after the one before it, and
// on one line.
func checkSpans(text string, spans []Span) error {
	rs := []rune(text)
	end := 0
	for _, s := range spans {
		switch {
		case s.Class <= Plain || s.Class > Number:
			return fmt.Errorf("span %v has no colour", s)
		case s.End <= s.Start:
			return fmt.Errorf("span %v is empty", s)
		case s.Start < end:
			return fmt.Errorf("span %v starts before %d, where the span before it ends", s, end)
		case s.End > len(rs):
			return fmt.Errorf("span %v ends after the text's %d runes", s, len(rs))
		case slices.Contains(rs[s.Start:s.End], '\n'):
			return fmt.Errorf("span %v covers a newline", s)
		}
		end = s.End
	}
	return nil
}

// Whatever the text, the spans of every language are in order, do not
// overlap, and stay inside the text and inside one line. go test runs the
// corpus as the seeds; go test -fuzz=FuzzHighlight looks for more.
func FuzzHighlight(f *testing.F) {
	for _, s := range corpus(f) {
		f.Add(s.text)
	}
	f.Fuzz(func(t *testing.T, text string) {
		for _, id := range ids() {
			if err := checkSpans(text, Highlight(id, text)); err != nil {
				t.Errorf("%s %q: %v", id, text, err)
			}
		}
	})
}
