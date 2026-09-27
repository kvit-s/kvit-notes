// Package export writes notes out as HTML, plain text and Markdown, imports
// Markdown and text files into a vault, and converts HTML to Markdown, the
// way the Qt app does (features.md 12.5 Export Options and 12.6 Import
// Options). It is the Qt app's DocumentExporter, DocumentImporter and
// HtmlToMarkdown (the converter behind pasting HTML) without their dialogs
// and without writing anything: every function returns the text or the list
// of files to write, and the caller writes them.
//
// PDF is not here. The Qt app prints the HTML page through QTextDocument; the
// Go app will draw it through the UI toolkit.
package export

import (
	"errors"
	"slices"
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
)

// Format is an export format, as the Qt export dialog names them.
type Format string

// The export formats.
const (
	FormatMarkdown Format = "markdown"
	FormatHTML     Format = "html"
	FormatPDF      Format = "pdf"
	FormatText     Format = "text"
)

// Extension is the file extension a format is written with, without the
// dot: "md", "html", "pdf" or "txt" (DocumentExporter::extensionFor). An
// unknown format is Markdown's.
func (f Format) Extension() string {
	switch f {
	case FormatHTML:
		return "html"
	case FormatPDF:
		return "pdf"
	case FormatText:
		return "txt"
	}
	return "md"
}

// ErrPDF is returned for a PDF export, which this package does not write.
var ErrPDF = errors.New("export: PDF is drawn through the UI toolkit, not written by this package")

// ErrFormat is returned for a format that is none of the four.
var ErrFormat = errors.New("export: unknown format")

// HTMLFromMarkdown is a note's body (its Markdown without front matter) as
// an HTML page titled title; an empty title is "Kvit Export"
// (DocumentExporter::htmlForMarkdown).
func HTMLFromMarkdown(body, title string, opt Options) string {
	return page(fromEditor(parseBody(body), false), title, opt)
}

// HTMLFromBlocks is the note the editor holds as an HTML page
// (DocumentExporter::htmlForModel). A leading block of front matter is left
// out, as the Qt app never has it in the editor.
func HTMLFromBlocks(blocks []editor.Block, title string, opt Options) string {
	return page(fromEditor(blocks, true), title, opt)
}

func page(doc []block, title string, opt Options) string {
	r := newRenderer(doc, opt)
	body := r.body(doc, r.docSlugs)
	return wrapPage(body, title, opt.Colors, r.sawMath, r.sawMermaid)
}

// HTMLFromSelection is some of the editor's blocks as an HTML page: only the
// blocks at indexes are written, in the order given, but a table of contents
// among them still lists every heading of the note, and a heading keeps the
// anchor it has in the whole note (DocumentExporter::htmlForModelBlocks).
// Indexes out of range, and repeats, are ignored.
func HTMLFromSelection(blocks []editor.Block, indexes []int, title string, opt Options) string {
	doc := fromEditor(blocks, true)
	r := newRenderer(doc, opt)
	sel, slugs := selectBlocks(doc, r.docSlugs, validIndexes(indexes, len(blocks)))
	body := r.body(sel, slugs)
	return wrapPage(body, title, opt.Colors, r.sawMath, r.sawMermaid)
}

// TextFromMarkdown is a note's body as plain text
// (DocumentExporter::plainTextForMarkdown).
func TextFromMarkdown(body string, opt Options) string {
	doc := fromEditor(parseBody(body), false)
	return newRenderer(doc, opt).plainText(doc)
}

// TextFromBlocks is the note the editor holds as plain text
// (DocumentExporter::plainTextForModel).
func TextFromBlocks(blocks []editor.Block, opt Options) string {
	doc := fromEditor(blocks, true)
	return newRenderer(doc, opt).plainText(doc)
}

// TextFromSelection is some of the editor's blocks as plain text, with the
// whole note behind a table of contents among them
// (DocumentExporter::plainTextForModelBlocks).
func TextFromSelection(blocks []editor.Block, indexes []int, opt Options) string {
	doc := fromEditor(blocks, true)
	sel, _ := selectBlocks(doc, nil, validIndexes(indexes, len(blocks)))
	return newRenderer(doc, opt).plainText(sel)
}

// MarkdownFromSelection is some of the editor's blocks as Markdown, in note
// order whatever order the indexes are given in, with no newline at the end
// (DocumentSerializer::serializeBlocks). A numbered item keeps the number it
// has in the whole note.
func MarkdownFromSelection(blocks []editor.Block, indexes []int) string {
	sorted := validIndexes(indexes, len(blocks))
	slices.Sort(sorted)
	var sb strings.Builder
	for n, i := range sorted {
		if n > 0 {
			if blocks[sorted[n-1]].Kind.IsList() && blocks[i].Kind.IsList() {
				sb.WriteString("\n")
			} else {
				sb.WriteString("\n\n")
			}
		}
		number := 1
		if blocks[i].Kind == editor.Numbered {
			number = editor.ListNumber(blocks, i)
		}
		sb.WriteString(editor.BlockMarkdown(blocks[i], number))
	}
	return sb.String()
}

// Note is the note the editor holds, written in a format: what the Qt export
// dialog writes for its "This note" scope (DocumentExporter::writeModel).
// Markdown is the note's body as the editor saves it, without front matter;
// HTML and text are as HTMLFromBlocks and TextFromBlocks. PDF returns ErrPDF.
func Note(blocks []editor.Block, title string, format Format, opt Options) ([]byte, error) {
	switch format {
	case FormatMarkdown:
		if len(blocks) == 0 {
			return nil, nil
		}
		return []byte(editor.Serialize(blocks)), nil
	case FormatHTML:
		return []byte(HTMLFromBlocks(blocks, title, opt)), nil
	case FormatText:
		return []byte(TextFromBlocks(blocks, opt)), nil
	case FormatPDF:
		return nil, ErrPDF
	}
	return nil, ErrFormat
}

// Selection is some of the editor's blocks written in a format: what the Qt
// export dialog writes for its "Selected blocks" scope
// (DocumentExporter::writeModelBlocks).
func Selection(blocks []editor.Block, indexes []int, title string, format Format, opt Options) ([]byte, error) {
	switch format {
	case FormatMarkdown:
		return []byte(MarkdownFromSelection(blocks, indexes)), nil
	case FormatHTML:
		return []byte(HTMLFromSelection(blocks, indexes, title, opt)), nil
	case FormatText:
		return []byte(TextFromSelection(blocks, indexes, opt)), nil
	case FormatPDF:
		return nil, ErrPDF
	}
	return nil, ErrFormat
}

// parseBody reads a note's body with the editor's parser.
func parseBody(body string) []editor.Block {
	return editor.ParseMarkdown(body)
}

// validIndexes keeps the indexes in range, first occurrence only, in the
// order given (DocumentExporter::validIndexes).
func validIndexes(indexes []int, count int) []int {
	var out []int
	for _, i := range indexes {
		if i >= 0 && i < count && !slices.Contains(out, i) {
			out = append(out, i)
		}
	}
	return out
}

// selectBlocks is the export blocks that came from the chosen editor blocks,
// in the order the editor blocks were chosen, with their anchors.
func selectBlocks(doc []block, slugs []string, chosen []int) ([]block, []string) {
	var sel []block
	var selSlugs []string
	for _, i := range chosen {
		for j, b := range doc {
			if b.origin != i {
				continue
			}
			sel = append(sel, b)
			if slugs != nil {
				selSlugs = append(selSlugs, slugs[j])
			}
		}
	}
	return sel, selSlugs
}
