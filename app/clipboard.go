package app

// HTML on the clipboard (features.md 5.1–5.3): copying from a note puts the
// Markdown on the clipboard as text and its HTML beside it, so a mail or a
// word processor keeps the formatting; pasting HTML, as a web page copies
// it, turns it into Markdown by the Qt app's rules (the export package's
// HtmlToMarkdown port), unless it is only a wrapper round plain text, when
// the text is pasted. Each system names HTML on the clipboard its own way:
// "HTML Format" with a header of offsets on Windows, public.html on macOS
// and text/html on Linux.

import (
	"fmt"
	"runtime"
	"strings"

	"github.com/kvit-s/kvit-notes/export"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/drag"
)

// htmlType is this system's clipboard type for HTML.
var htmlType = func() *uti.DataType {
	name := "text/html"
	switch runtime.GOOS {
	case "windows":
		name = "HTML Format"
	case "darwin":
		name = "public.html"
	}
	if dt := uti.ByUTI(name); dt != nil {
		return dt
	}
	return uti.Register(&uti.DataType{UTI: name, MimeTypes: []string{"text/html"}})
}()

// internalType is the clipboard type copying from a note adds beside the
// text and the HTML: Kvit's application/x-kvit-markdown. Pasting it back
// uses the text as it is, so a copy never round-trips through the HTML
// converter, which would mistake its fence for content.
var internalType = func() *uti.DataType {
	const name = "application/x-kvit-markdown"
	if dt := uti.ByUTI(name); dt != nil {
		return dt
	}
	return uti.Register(&uti.DataType{UTI: name})
}()

// withWindowsHeader wraps HTML in the offsets Windows' "HTML Format" needs
// before it.
func withWindowsHeader(html string) string {
	const header = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n"
	pre, post := "<html><body>\r\n<!--StartFragment-->", "<!--EndFragment-->\r\n</body></html>"
	headLen := len(fmt.Sprintf(header, 0, 0, 0, 0))
	start := headLen + len(pre)
	end := start + len(html)
	return fmt.Sprintf(header, headLen, end+len(post), start, end) + pre + html + post
}

// fragmentOf is the HTML a clipboard's data holds: on Windows, the part
// between the fragment markers.
func fragmentOf(data string) string {
	if a := strings.Index(data, "<!--StartFragment-->"); a >= 0 {
		rest := data[a+len("<!--StartFragment-->"):]
		if b := strings.Index(rest, "<!--EndFragment-->"); b >= 0 {
			return rest[:b]
		}
		return rest
	}
	return data
}

// copyRich puts Markdown on the clipboard as text, with its HTML.
func (w *Window) copyRich(md string) {
	html := export.HTMLFromMarkdown(md, "", w.exportOptions())
	if runtime.GOOS == "windows" {
		html = withWindowsHeader(html)
	}
	unison.ClipboardSetData(drag.Data{Type: uti.UTF8PlainText, Data: []byte(md)}, drag.Data{Type: htmlType, Data: []byte(html)},
		drag.Data{Type: internalType, Data: []byte(md)})
}

// pasteRich reads HTML off the clipboard as Markdown. A copy this app made
// carries the internal type, and pastes as its text, never through the
// converter.
func pasteRich() (string, bool) {
	if unison.ClipboardHasDataType(internalType) {
		return "", false
	}
	if !unison.ClipboardHasDataType(htmlType) {
		return "", false
	}
	html := fragmentOf(string(unison.ClipboardGetData(htmlType)))
	if strings.TrimSpace(html) == "" || !export.HasStructure(html) {
		return "", false
	}
	md := export.HTMLToMarkdown(html)
	return md, md != ""
}
