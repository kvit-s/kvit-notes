package editor

import "testing"

// An embedding can set its lines closer than the reader's line height, as a
// message box does, without the reader's setting or another editor moving.
func TestAnEmbeddingSpacesItsLinesAtItsOwnLineHeight(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	s.Do(func() { c.SetMarkdown("one\ntwo") })
	s.Sync()
	s.Do(func() {
		e := c.Editor
		st := e.blockStyle(&e.Doc.Blocks[0])
		natural := e.naturalLine(st)
		reader := float32(e.ui.Typography.LineHeight())
		before := c.ContentHeight()
		if got := e.layout(0).pitch; got != natural*reader {
			t.Fatalf("with no line height of its own, lines %v apart, want %v", got, natural*reader)
		}

		emb, _ := e.Embedded()
		emb.LineHeight = 1.15
		e.Embed(emb)
		if got, want := e.layout(0).pitch, natural*1.15; got != want {
			t.Errorf("lines %v apart, want %v", got, want)
		}
		if got := c.ContentHeight(); got >= before {
			t.Errorf("the box is %v tall with closer lines, and was %v", got, before)
		}
		if got := e.ui.Typography.LineHeight(); float32(got) != reader {
			t.Errorf("the reader's line height became %v", got)
		}

		// The reader's setting moves an editor with none of its own only.
		e.ui.Typography.SetLineHeight(1.6)
		if got, want := e.pitch(st), natural*1.15; got != want {
			t.Errorf("with the reader's setting changed, lines %v apart, want %v", got, want)
		}
		emb.LineHeight = 0
		e.Embed(emb)
		if got, want := e.pitch(st), natural*1.6; got != want {
			t.Errorf("with none of its own again, lines %v apart, want %v", got, want)
		}
	})
}
