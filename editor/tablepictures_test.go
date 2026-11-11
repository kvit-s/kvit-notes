package editor

// Pictures of the tables of tablefit_test.go in a read-only document,
// drawn when KVIT_TABLE_SHOTS names a folder: with the table options off and
// on, in an editor 760 and 1,220 pixels wide, and the note editor's own
// drawing of them for comparing with a build without the options.

import (
	"image"
	"image/png"
	"os"
	"path/filepath"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// tablePictureNote is the document the pictures show: a reply with the six
// tables, each under a line saying what it is, and a code block with a long
// line.
var tablePictureNote = "The five tables an agent's reply often holds, each under a line naming it. " +
	"This paragraph is long enough to show where the text wraps: at the reading column, " +
	"while a table or a code block may reach past it to the right.\n\n" +
	"**1. A short report**\n\n" + fitReport + "\n\n" +
	"**2. One column of prose**\n\n" + fitProse + "\n\n" +
	"**3. Eight numeric columns**\n\n" + fitNumbers + "\n\n" +
	"**4. Twelve columns, wider than the room**\n\n" + fitWide + "\n\n" +
	"**5. A long path**\n\n" + fitPath + "\n\n" +
	"**6. Short cells beside a sentence**\n\n" + fitTimes + "\n\n" +
	"```go\nfunc describe() string { return \"a code line long enough to run past the reading column, to show how far a code panel reaches\" }\n```\n"

// tablePicture draws tablePictureNote in a read-only document width design
// pixels wide, embedded as Kvit Works embeds a reply, with more changed
// about the embedding, and saves it as name in dir.
func tablePicture(t *testing.T, dir, name string, width int, change func(*Embedding), after func(*Editor)) {
	t.Run(name, func(t *testing.T) {
		var d *Document
		s := uitest.Open(t, uitest.Options{Width: float32(width) + 80, Height: 2600}, func(ui *kvitui.UI) unison.Paneler {
			d = NewDocument(ui, DocumentOptions{TightRows: true, BlockSpacing: 10, ListSpacing: 2})
			if change != nil {
				emb, _ := d.Editor.Embedded()
				change(&emb)
				d.Editor.Embed(emb)
			}
			d.SetMarkdown(tablePictureNote)
			return kvitui.Width(ui, kvitui.Px(width), d)
		})
		if after != nil {
			s.Do(func() { after(d.Editor) })
		}
		s.Sync()
		savePicture(t, s, dir, name, d.AsPanel())
	})
}

// notePicture draws tablePictureNote in the note editor, with no embedding.
func notePicture(t *testing.T, dir, name string, width int) {
	t.Run(name, func(t *testing.T) {
		var e *Editor
		s := uitest.Open(t, uitest.Options{Width: float32(width) + 80, Height: 2600}, func(ui *kvitui.UI) unison.Paneler {
			e = New(ui, NewDoc(ParseMarkdown(tablePictureNote)))
			return kvitui.Width(ui, kvitui.Px(width), e)
		})
		s.Sync()
		savePicture(t, s, dir, name, e.AsPanel())
	})
}

// savePicture writes the window's picture down to the bottom of p and a
// margin, as a PNG. A Kvit window opens 960 pixels tall, so the window is
// first made as tall as the screen.
func savePicture(t *testing.T, s *uitest.Session, dir, name string, p *unison.Panel) {
	t.Helper()
	s.Do(func() {
		w := s.Window.Window
		r := w.ContentRect()
		w.SetContentRect(geom.NewRect(r.X, r.Y, r.Width, 2500))
	})
	s.Sync()
	var right, bottom int
	s.Do(func() {
		r := p.RectToRoot(p.ContentRect(true))
		right, bottom = int(r.Right())+24, int(r.Bottom())+24
	})
	img := s.Capture()
	if sub, ok := img.(interface {
		SubImage(image.Rectangle) image.Image
	}); ok {
		b := img.Bounds()
		img = sub.SubImage(image.Rect(b.Min.X, b.Min.Y, min(b.Max.X, b.Min.X+right), min(b.Max.Y, b.Min.Y+bottom)))
	}
	f, err := os.Create(filepath.Join(dir, name+".png"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		t.Fatal(err)
	}
}

func TestTablePictures(t *testing.T) {
	dir := os.Getenv("KVIT_TABLE_SHOTS")
	if dir == "" {
		t.Skip("KVIT_TABLE_SHOTS names no folder")
	}
	on := func(textWidth float32) func(*Embedding) {
		return func(emb *Embedding) {
			emb.FitTables, emb.ScrollTables, emb.TextWidth = true, true, textWidth
		}
	}
	tablePicture(t, dir, "tables-off-760", 760, nil, nil)
	tablePicture(t, dir, "tables-off-1220", 1220, nil, nil)
	tablePicture(t, dir, "tables-on-760", 760, on(0), nil)
	tablePicture(t, dir, "tables-on-1220-text-760", 1220, on(760), nil)
	tablePicture(t, dir, "tables-on-1220-text-760-scrolled", 1220, on(760), func(e *Editor) {
		for i, b := range e.Doc.Blocks {
			if _, ok := e.tableScrolls(i); ok {
				e.setCodeScroll(b.ID, e.codeMaxScroll(i))
			}
		}
	})
	notePicture(t, dir, "note-1000", 1000)
}
