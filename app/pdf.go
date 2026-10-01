package app

// PDF export: the note drawn onto A4 pages by the editor itself, in the light
// theme whatever the window's, through unison's PDF writer. The text keeps
// its fonts and stays selectable in the PDF.

import (
	"errors"
	"slices"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/canvas/pdf"
	"github.com/richardwilkes/canvas/stream"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// A4 in points, and the margin round the text.
const (
	pdfPageW  = 595
	pdfPageH  = 842
	pdfMargin = 56
)

// pdfPages draws an editor's pages.
type pdfPages struct {
	ed    *editor.Editor
	pages []editor.Page
}

func (p *pdfPages) HasPage(n int) bool  { return n >= 1 && n <= len(p.pages) }
func (p *pdfPages) PageSize() geom.Size { return geom.NewSize(pdfPageW, pdfPageH) }
func (p *pdfPages) DrawPage(gc *unison.Canvas, n int) error {
	gc.Save()
	gc.Translate(geom.NewPoint(pdfMargin, pdfMargin))
	p.ed.DrawPage(gc, p.pages[n-1])
	gc.Restore()
	return nil
}

// writePDF writes blocks to a PDF file at path.
func (w *Window) writePDF(blocks []editor.Block, title, path string) error {
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true})
	if err != nil {
		return err
	}
	ui.Typography.SetBaseSize(w.ui.Typography.BaseSize())
	ui.Typography.SetFontFamily(w.ui.Typography.FontFamily())
	doc := editor.NewDoc(slices.Clone(blocks))
	doc.ReadOnly = true
	ed := editor.New(ui, doc)
	ed.Placeholder = ""
	ed.EquationNumbers = w.Editor.EquationNumbers
	if w.open != nil {
		ed.LoadImage = w.imageLoader(w.open.Path)
	}
	ed.RunQuery = w.runQuery
	pages := ed.Paginate(pdfPageW-2*pdfMargin, pdfPageH-2*pdfMargin)
	s, ok := stream.NewFileWStream(path)
	if !ok {
		return errors.New("the file cannot be written")
	}
	defer s.Close()
	return unison.CreatePDF(s, &pdf.Metadata{Title: title, Creator: "Kvit Notes"}, &pdfPages{ed, pages})
}
