package editor

// External drops, drawn selections, embed editing and sizing, video hosts,
// image effects, the lightbox, and the dates at a task board card's foot.

import (
	"net/url"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/kanban"
	"github.com/richardwilkes/unison/drag"
)

// fakeDrag is a drag from another application for tests: file paths,
// addresses and text.
type fakeDrag struct {
	files []string
	urls  []string
	text  string
}

func (f fakeDrag) SourceDragOpMask() drag.Op { return drag.Copy }
func (f fakeDrag) DataTypes() []string       { return nil }
func (f fakeDrag) HasString() bool           { return f.text != "" }
func (f fakeDrag) HasFilePaths() bool        { return len(f.files) > 0 }
func (f fakeDrag) HasURLs() bool             { return len(f.urls) > 0 }
func (f fakeDrag) HasDataType(string) bool   { return false }
func (f fakeDrag) Text() string              { return f.text }
func (f fakeDrag) FilePaths() []string       { return f.files }
func (f fakeDrag) URLs() []*url.URL {
	var out []*url.URL
	for _, s := range f.urls {
		if u, err := url.Parse(s); err == nil {
			out = append(out, u)
		}
	}
	return out
}
func (f fakeDrag) Data(string) []byte { return nil }

// External drops (5.4): note files are for the window, images and other
// files for image blocks.
func TestDropFilesSplitNotesImagesOther(t *testing.T) {
	notes, images, other := dropFiles([]string{"a.md", "b.txt", "photo.png", "clip.mp4", "data.csv", "  "})
	if len(notes) != 2 || len(images) != 1 || len(other) != 2 {
		t.Fatalf("split = %q %q %q", notes, images, other)
	}
	if !dropNoteExt("note.markdown") || dropNoteExt("photo.png") {
		t.Fatalf("note extensions misread")
	}
}

// The editor takes images, addresses and text, but not note-only drops.
func TestCanAcceptExternalDeclinesNoteOnly(t *testing.T) {
	s, e := openEditor(t, "first\n")
	var ok, no bool
	s.Do(func() {
		ok = e.canAcceptExternal(fakeDrag{files: []string{"photo.png"}})
		no = e.canAcceptExternal(fakeDrag{files: []string{"a.md"}})
		if e.canAcceptExternal(fakeDrag{text: "  "}) {
			t.Errorf("blank text should not be taken")
		}
		if !e.canAcceptExternal(fakeDrag{text: "hello"}) {
			t.Errorf("text should be taken")
		}
	})
	if !ok || no {
		t.Fatalf("accept images %v, decline notes %v", ok, no)
	}
}

// Dropping an image file inserts an image block at the drop row.
func TestDropExternalInsertsImageAtIndex(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	s.Do(func() {
		e.SaveDroppedImage = func(src string) (string, bool) { return "assets/pic.png", true }
		if !e.dropExternal(fakeDrag{files: []string{"/tmp/photo.png"}}, 1) {
			t.Fatalf("drop should be taken")
		}
	})
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Image:![](assets/pic.png) | Paragraph:second" {
			t.Errorf("after image drop: %s", got)
		}
	})
}

// Dropping text inserts paragraphs, and a fence its blocks.
func TestDropExternalInsertsText(t *testing.T) {
	s, e := openEditor(t, "first\n")
	s.Do(func() {
		if !e.dropExternal(fakeDrag{text: "a\nb"}, 1) {
			t.Fatalf("text drop should be taken")
		}
	})
	s.Do(func() {
		if got := texts(e.Doc); !strings.Contains(got, "Paragraph:a") || !strings.Contains(got, "Paragraph:b") {
			t.Errorf("after text drop: %s", got)
		}
	})
	s.Do(func() {
		e.dropExternal(fakeDrag{text: "```mermaid\ngraph TD\n```"}, 0)
		if e.Doc.Blocks[0].Kind != Code {
			t.Errorf("a fence drop should make its block, got %v", e.Doc.Blocks[0].Kind)
		}
	})
}

// A lone URL drop lands as an embed.
func TestDropExternalLoneURLBecomesEmbed(t *testing.T) {
	s, e := openEditor(t, "first\n")
	s.Do(func() {
		e.dropExternal(fakeDrag{text: "https://example.com/article"}, 1)
		if text := e.Doc.Blocks[1].Text; text != "![](https://example.com/article)" {
			t.Errorf("URL drop: %q", text)
		}
	})
}

// Drawn selections (2.5): a table of contents carries its entries' text.
func TestDrawnTextOfTocQueryEmbed(t *testing.T) {
	s, e := openEditor(t, "# Plan\n\n```toc\n```\n")
	var toc string
	var ok bool
	s.Do(func() {
		id := e.Doc.Blocks[1].ID
		toc, ok = e.drawnText(id)
	})
	if !ok || toc != "Plan" {
		t.Fatalf("toc drawn text %q %v", toc, ok)
	}
	s.Do(func() {
		id := e.Doc.Blocks[1].ID
		e.setDrawn(id, 0, 4)
		if s, ok := e.drawnCopy(); !ok || s != "Plan" {
			t.Errorf("toc copy %q %v", s, ok)
		}
		e.clearDrawn()
		if e.drawn != nil {
			t.Errorf("Escape should drop the span")
		}
	})
}

// Words and lines expand as double- and triple-clicks do.
func TestDrawnWordAndLine(t *testing.T) {
	r := []rune("alpha beta\ngamma")
	if a, b := drawnWordAt(r, 7); string(r[a:b]) != "beta" {
		t.Errorf("word at 7: %q", string(r[a:b]))
	}
	if a, b := drawnLineAt(r, 12); string(r[a:b]) != "gamma" {
		t.Errorf("line at 12: %q", string(r[a:b]))
	}
}

// Embed editing (1.2.14): retargeting keeps the alt text, sizing lands in
// attributes, and video hosts are recognised.
func TestSetEmbedURLKeepsAlt(t *testing.T) {
	s, e := openEditor(t, "![Read](https://example.com/a)\n")
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() {
		if !e.SetEmbedURL(id, "https://example.com/b") {
			t.Fatalf("retarget should succeed")
		}
	})
	s.Do(func() {
		if got := e.Doc.Block(id).Text; got != "![Read](https://example.com/b)" {
			t.Errorf("after Edit URL: %q", got)
		}
	})
}

func TestSetEmbedSizeAndVideoHosts(t *testing.T) {
	s, e := openEditor(t, "![](https://example.com/article)\n")
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() { e.SetEmbedSize([]int64{id}, 480, 0) })
	s.Do(func() {
		v, ok := e.Doc.Block(id).Attr("width")
		if !ok || v != "480" {
			t.Errorf("width attribute %q %v", v, ok)
		}
	})
	if !isVideoHost("https://www.youtube.com/watch?v=x") || !isVideoHost("https://youtu.be/x") {
		t.Errorf("video hosts should be recognised")
	}
	if isVideoHost("https://example.com/article") {
		t.Errorf("an article is not a video")
	}
}

// Image effects (1.2.8): toggles land in attributes and parse back.
func TestImageEffectsRoundTrip(t *testing.T) {
	s, e := openEditor(t, "![](assets/pic.png)\n")
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() { e.SetImageEffects([]int64{id}, 12, true, "border", "", false) })
	s.Do(func() {
		b := e.Doc.Block(id)
		fx := e.effectsOf(b)
		if !fx.rounded || fx.radius != 12 || !fx.shadow || !fx.border {
			t.Errorf("effects %+v", fx)
		}
	})
	s.Do(func() { e.toggleImageEffect([]int64{id}, "shadow", "") })
	s.Do(func() {
		if fx := e.effectsOf(e.Doc.Block(id)); fx.shadow {
			t.Errorf("toggling shadow again should clear it")
		}
	})
}

// The lightbox opens a resolved picture and closes on Escape.
func TestLightboxOpensAndCloses(t *testing.T) {
	s, e := openEditor(t, "![](assets/pic.png)\n")
	s.Do(func() {
		e.OpenLightbox("assets/pic.png", "alt")
		if _, _, open := e.Lightbox(); !open {
			t.Fatalf("lightbox should be open")
		}
		e.CloseLightbox()
		if _, _, open := e.Lightbox(); open {
			t.Errorf("lightbox should be shut")
		}
	})
}

// kanbanCard is a card with the given added and changed days, for foot
// tests.
func kanbanCard(created, modified string) kanban.Card {
	return kanban.Card{Created: created, Modified: modified}
}

// Task-board feet (1.2.12): the added and changed days at a card's foot.
func TestCardFootDates(t *testing.T) {
	if got := cardFoot(kanbanCard("2026-09-01", "2026-09-02")); got != "added 2026-09-01 · changed 2026-09-02" {
		t.Errorf("both dates: %q", got)
	}
	if got := cardFoot(kanbanCard("2026-09-01", "2026-09-01")); got != "added 2026-09-01" {
		t.Errorf("one date: %q", got)
	}
	if got := cardFoot(kanbanCard("", "")); got != "" {
		t.Errorf("no dates: %q", got)
	}
}
