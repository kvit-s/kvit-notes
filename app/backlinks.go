package app

// The backlinks pane (features.md 8.5, Kvit's BacklinksPanel): the
// notes whose wiki links name the open note, each with how many links and
// the lines they are on, at the right of the editor, left of the outline.
// Ctrl+Shift+B or View shows and hides it. Clicking a note opens it. The
// links are resolved by the links package, redirects included, as the
// app's index resolves them.

import (
	"fmt"
	"os"
	"time"

	"github.com/kvit-s/kvit-notes/search"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// backlinksWidth is the pane's width in design pixels until the reader
// changes it.
const backlinksWidth = 240

// Backlinks is the pane.
type Backlinks struct {
	*unison.Panel
	list  *ResultsList
	count *kvitui.Label
	empty *unison.Panel
	rows  *kvitui.Region
}

func newBacklinks(ui *kvitui.UI, open func(path string), hide func()) *Backlinks {
	b := &Backlinks{}
	title := kvitui.NewLabel(ui, "Backlinks")
	title.Role, title.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	b.count = kvitui.NewLabel(ui, "")
	b.count.Ink = kvitui.InkTextFaint
	closeB := kvitui.NewIconButton(ui, "close", "Hide backlinks")
	closeB.OnClick = hide
	head := headerRow(ui, title, b.count, closeB)
	head.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	head.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	b.list = newResultsList(ui)
	b.list.Accessibility.Name = "Backlinks"
	b.list.Counting = "backlinks"
	b.list.OnOpen = func(p string, _ *search.Hit) { open(p) }
	b.rows = kvitui.NewRegion(ui, b.list)
	b.rows.Padding = kvitui.Px(0)
	b.rows.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	none := kvitui.NewLabel(ui, "No backlinks")
	none.Ink = kvitui.InkTextSecondary
	b.empty = kvitui.Column(ui, kvitui.Px(0), none)
	b.empty.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	b.empty.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	b.Panel = kvitui.Column(ui, kvitui.Px(0), head, b.empty)
	b.Panel.DrawCallback = ground(ui, b.Panel, func() kvitui.Ink { return kvitui.InkPanelBackground })
	return b
}

// bodies are every note's body, read again only for notes changed since.
func (w *Window) bodies() map[string]string {
	if w.bodyCache == nil {
		w.bodyCache = map[string]cachedBody{}
	}
	out := make(map[string]string, len(w.Vault.Entries))
	for _, e := range w.Vault.Entries {
		c, ok := w.bodyCache[e.Path]
		if !ok || !c.modified.Equal(e.Modified) {
			data, err := os.ReadFile(w.Vault.Path(e.Path))
			if err != nil {
				continue
			}
			page := vault.ParseText(string(data))
			c = cachedBody{e.Modified, page.Body, page.Fields()}
			w.bodyCache[e.Path] = c
		}
		out[e.Path] = c.body
	}
	return out
}

// cachedBody is a note's body as read at a time of change.
type cachedBody struct {
	modified time.Time
	body     string
	fields   map[string]string // its front matter's keys
}

// refreshBacklinks lists the open note's backlinks, when the pane shows.
func (w *Window) refreshBacklinks() {
	b := w.backlinks
	if b.Window() == nil {
		return
	}
	var rows []resultRow
	total := 0
	if w.open != nil {
		for _, bl := range w.linkIndex().Backlinks(w.open.Path, w.bodies()) {
			title := bl.Path
			if e := w.Vault.Find(bl.Path); e != nil {
				title = e.Title
			}
			rows = append(rows, resultRow{path: bl.Path, title: title, count: bl.Count})
			for _, c := range bl.Contexts {
				rows = append(rows, resultRow{path: bl.Path, snippet: c, at: -1, line: true})
			}
			total += bl.Count
		}
	}
	b.list.rows, b.list.current = rows, -1
	b.count.Text = ""
	if total > 0 {
		b.count.Text = fmt.Sprint(total)
	}
	shown, hide := b.rows.AsPanel(), b.empty
	if len(rows) == 0 {
		shown, hide = hide, shown
	}
	if shown.Parent() == nil {
		hide.RemoveFromParent()
		b.AddChild(shown)
	}
	for p := b.list.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	b.MarkForLayoutRecursively()
	b.MarkForRedraw()
}
