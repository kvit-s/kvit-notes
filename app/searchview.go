package app

// Searching across notes (features.md 8.4, Kvit's SearchResultsView.qml over
// src/search): what is typed in the sidebar's search field is looked for in
// every note of the scope shown, and the note list becomes the results:
// each note found, with how often, and under it the lines the text was found
// on, the text in bold. A date menu keeps the notes changed today, in the
// last 7, 30 or 365 days. Clicking a note opens it; clicking a line opens it
// with the caret at that place.
//
// The index is the search package's. It is built when the window opens, in
// the background, and kept up to date as notes are saved, made, renamed,
// trashed or changed by other programs, by comparing each note's time of
// change with the one it was indexed at.

import (
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/search"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// indexedNote is what the index needs of a note: its blocks' text as the
// reader sees it.
func indexedNote(e *vault.Entry, body string, modified time.Time) search.Note {
	doc := editor.NewDoc(editor.ParseMarkdown(body))
	var blocks []string
	for _, b := range searchBlocks(doc) {
		blocks = append(blocks, b.Text())
	}
	title, folder := search.TitleFolder(e.Path)
	return search.Note{Path: e.Path, Title: title, Folder: folder, Tags: e.Tags, Modified: modified, Blocks: blocks}
}

// buildIndex indexes every note, in the background.
func (w *Window) buildIndex() {
	w.index = &search.Index{}
	w.indexed = map[string]time.Time{}
	type job struct {
		e        *vault.Entry
		path     string
		modified time.Time
	}
	var jobs []job
	for _, e := range w.Vault.Entries {
		jobs = append(jobs, job{e, w.Vault.Path(e.Path), e.Modified})
		w.indexed[e.Path] = e.Modified
	}
	ix := w.index
	go func() {
		for _, j := range jobs {
			data, err := os.ReadFile(j.path)
			if err != nil {
				continue
			}
			ix.Add(indexedNote(j.e, vault.ParseText(string(data)).Body, j.modified))
		}
		unison.InvokeTask(func() {
			if w.search.Text() != "" {
				w.refreshList()
			}
		})
	}()
}

// syncIndex brings the index up to the vault: notes gone are taken out, and
// notes new or changed since they were indexed are read again.
func (w *Window) syncIndex() {
	if w.index == nil {
		return
	}
	seen := map[string]bool{}
	for _, e := range w.Vault.Entries {
		seen[e.Path] = true
		if at, ok := w.indexed[e.Path]; ok && at.Equal(e.Modified) {
			continue
		}
		data, err := os.ReadFile(w.Vault.Path(e.Path))
		if err != nil {
			continue
		}
		w.index.Add(indexedNote(e, vault.ParseText(string(data)).Body, e.Modified))
		w.indexed[e.Path] = e.Modified
	}
	for p := range w.indexed {
		if !seen[p] {
			w.index.Remove(p)
			delete(w.indexed, p)
		}
	}
}

// dateChoices are the results' date menu.
var dateChoices = []struct {
	label string
	dates search.Dates
}{
	{"Any time", search.AnyTime}, {"Today", search.Today}, {"Last 7 days", search.Last7Days},
	{"Last 30 days", search.Last30Days}, {"Last year", search.LastYear},
}

// runSearch shows the results of what is typed in the search field.
func (w *Window) runSearch() {
	w.syncIndex()
	q := search.Query{Text: w.search.Text()}
	for _, c := range dateChoices {
		if c.label == w.searchDates.Current {
			q.Dates = c.dates
		}
	}
	switch w.scope.Kind {
	case ScopeFolder:
		q.Folder = w.scope.Path
	case ScopeTag:
		q.Tag = w.scope.Path
	}
	res := w.index.Query(q)
	if w.scope.Kind == ScopeFavorites {
		var kept []search.Result
		matches := 0
		for _, r := range res.Notes {
			if e := w.Vault.Find(r.Path); e != nil && e.Favorite {
				kept = append(kept, r)
				matches += r.MatchCount
			}
		}
		res.Notes, res.MatchCount = kept, matches
	}
	w.searchCount.Text = fmt.Sprintf("%d match(es) in %d note(s)", res.MatchCount, len(res.Notes))
	w.searchCount.MarkForRedraw()
	w.results.set(res)
	w.showSearch(true)
}

// showSearch puts the results in the note list's place, and the match
// count and date menu in the sort row's, or the list back.
func (w *Window) showSearch(on bool) {
	if on == (w.resultsRegion.Parent() != nil) {
		return
	}
	swap := func(out, in unison.Paneler) {
		parent := out.AsPanel().Parent()
		i := parent.IndexOfChild(out)
		out.AsPanel().RemoveFromParent()
		parent.AddChildAtIndex(in, i)
	}
	if on {
		swap(w.listRegion, w.resultsRegion)
		swap(w.sortRow, w.searchRow)
	} else {
		swap(w.resultsRegion, w.listRegion)
		swap(w.searchRow, w.sortRow)
	}
	w.relayout()
}

// openResult opens a note found, at a hit when one is given.
func (w *Window) openResult(path string, hit *search.Hit) {
	e := w.Vault.Find(path)
	if e == nil {
		return
	}
	w.openNote(e)
	if w.open != e {
		return
	}
	d := w.Editor.Doc
	if hit == nil || hit.Block >= len(d.Blocks) {
		w.Editor.FocusBlock(0, 0)
		return
	}
	b := searchBlocks(d)[hit.Block]
	start, end := b.MarkdownPos(hit.Start), b.MarkdownPos(hit.Start+hit.Length)
	id := d.Blocks[hit.Block].ID
	w.Editor.FocusBlock(hit.Block, end)
	d.Anchor = editor.Pos{Block: id, Off: start}
	w.Editor.Refresh()
	w.Editor.RevealBlockRange(id, start)
}

// resultRow is one line of the results: a note, or a hit under it.
type resultRow struct {
	path    string
	title   string
	count   int
	hit     *search.Hit
	snippet string
	at, n   int  // the match in the snippet, in runes
	more    int  // on a note's last hit: how many hits are not listed
	line    bool // a line under a note: a hit, or a backlink's context
}

// ResultsList draws the results.
type ResultsList struct {
	unison.Panel
	ui      *kvitui.UI
	rows    []resultRow
	current int
	hover   int
	// OnOpen opens a note, at a hit when it is not nil.
	OnOpen func(path string, hit *search.Hit)
	// Counting is what a note's count counts, for a screen reader.
	Counting string
}

// Result row heights in design pixels (SearchResultsView.qml).
const (
	resultNoteRow = 28
	resultHitRow  = 24
	resultIndent  = 14
)

func newResultsList(ui *kvitui.UI) *ResultsList {
	l := &ResultsList{ui: ui, current: -1, hover: -1, Counting: "matches"}
	l.Self = l
	l.SetFocusable(true)
	l.SetSizer(func(hint geom.Size) (geom.Size, geom.Size, geom.Size) {
		h := l.top(len(l.rows))
		return geom.NewSize(0, h), geom.NewSize(hint.Width, h), geom.NewSize(unison.DefaultMaxSize, h)
	})
	l.DrawCallback = l.draw
	l.MouseDownCallback = func(where geom.Point, button, _ int, _ mod.Modifiers) bool {
		if i := l.rowAt(where.Y); i >= 0 && button == unison.ButtonLeft {
			l.RequestFocus()
			l.choose(i)
		}
		return true
	}
	l.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool {
		if i := l.rowAt(where.Y); i != l.hover {
			l.hover = i
			l.MarkForRedraw()
		}
		return true
	}
	l.MouseEnterCallback = l.MouseMoveCallback
	l.MouseExitCallback = func() bool { l.hover = -1; l.MarkForRedraw(); return true }
	l.KeyDownCallback = func(key unison.KeyCode, _ mod.Modifiers, _ bool) bool {
		n := len(l.rows)
		if n == 0 {
			return false
		}
		switch key {
		case unison.KeyUp:
			l.move(max(0, l.current-1))
		case unison.KeyDown:
			l.move(min(n-1, l.current+1))
		case unison.KeyReturn, unison.KeyNumPadEnter:
			if l.current >= 0 {
				l.choose(l.current)
			}
		default:
			return false
		}
		return true
	}
	l.GainedFocusCallback = l.MarkForRedraw
	l.LostFocusCallback = l.MarkForRedraw
	l.Accessibility.Role = role.List
	l.Accessibility.Name = "Search results"
	return l
}

func (l *ResultsList) px(design float32) float32 {
	return design * float32(l.ui.Interface.Px(100)) / 100
}

func (l *ResultsList) height(i int) float32 {
	if !l.rows[i].line {
		return l.px(resultNoteRow)
	}
	return l.px(resultHitRow)
}

// top is where row i starts.
func (l *ResultsList) top(i int) float32 {
	var y float32
	for k := 0; k < i && k < len(l.rows); k++ {
		y += l.height(k)
	}
	return y
}

func (l *ResultsList) rowAt(y float32) int {
	var top float32
	for i := range l.rows {
		h := l.height(i)
		if y >= top && y < top+h {
			return i
		}
		top += h
	}
	return -1
}

// set shows a query's results.
func (l *ResultsList) set(res search.Results) {
	l.rows = l.rows[:0]
	for _, r := range res.Notes {
		l.rows = append(l.rows, resultRow{path: r.Path, title: r.Title, count: r.MatchCount})
		for k := range r.Hits {
			h := r.Hits[k]
			snippet, at := h.Snippet()
			row := resultRow{path: r.Path, hit: &h, snippet: snippet, at: at, n: h.Length, line: true}
			if k == len(r.Hits)-1 {
				row.more = r.MoreMatches
			}
			l.rows = append(l.rows, row)
		}
	}
	l.current = -1
	for p := l.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	l.MarkForRedraw()
}

func (l *ResultsList) move(i int) {
	l.current = i
	top := l.top(i)
	l.ScrollRectIntoView(geom.NewRect(0, top, 1, l.height(i)))
	l.MarkForRedraw()
}

func (l *ResultsList) choose(i int) {
	l.move(i)
	if l.OnOpen != nil {
		r := l.rows[i]
		l.OnOpen(r.path, r.hit)
	}
}

func (l *ResultsList) draw(gc *unison.Canvas, dirty geom.Rect) {
	ui := l.ui
	t := ui.Theme.Tokens()
	w := l.ContentRect(false).Width
	fill := func(r geom.Rect, c unison.Color) { gc.DrawRect(r, c.Paint(gc, r, paintstyle.Fill)) }
	y := float32(0)
	for i, r := range l.rows {
		h := l.height(i)
		if y > dirty.Bottom() {
			break
		}
		if y+h < dirty.Y {
			y += h
			continue
		}
		row := geom.NewRect(0, y, w, h)
		switch {
		case i == l.current && l.Focused():
			fill(row, kvitui.Color(t.SelectionActiveTint))
		case i == l.current:
			fill(row, kvitui.Color(t.SelectionTint))
		case i == l.hover:
			fill(row, kvitui.Color(t.HoverTint))
		}
		pad := l.px(rowPadSide)
		if !r.line {
			title := ui.Chrome(ui.Size(kvitui.RoleStrong), text.Bold, t.TextPrimary)
			count := ui.Chrome(ui.Size(kvitui.RoleSmall), text.Regular, t.TextFaint)
			cl := ui.Fonts.Layout([]text.Span{{Text: fmt.Sprint(r.count), Style: count}}, text.Options{})
			cw, ch := cl.Size()
			cl.Draw(gc, w-pad-cw, y+(h-ch)/2)
			tl := ui.Fonts.Layout([]text.Span{{Text: r.title, Style: title}}, text.Options{MaxWidth: max(1, w-3*pad-cw), Elide: true})
			_, th := tl.Size()
			tl.Draw(gc, pad, y+(h-th)/2)
		} else {
			base := ui.Chrome(ui.Size(kvitui.RoleSmall), text.Regular, t.TextSecondary)
			bold := base
			bold.Weight, bold.Color = text.Bold, kvitui.TextColor(t.TextPrimary)
			rs := []rune(r.snippet)
			a, b := max(0, min(r.at, len(rs))), max(0, min(r.at+r.n, len(rs)))
			if r.at < 0 {
				// A backlink's line has no match to set in bold.
				a, b = len(rs), len(rs)
			}
			spans := []text.Span{{Text: string(rs[:a]), Style: base}, {Text: string(rs[a:b]), Style: bold}, {Text: string(rs[b:]), Style: base}}
			if r.more > 0 {
				faint := base
				faint.Color = kvitui.TextColor(t.TextFaint)
				spans = append(spans, text.Span{Text: fmt.Sprintf("  · %d more", r.more), Style: faint})
			}
			x := pad + l.px(resultIndent)
			tl := ui.Fonts.Layout(spans, text.Options{MaxWidth: max(1, w-x-pad), Elide: true})
			_, th := tl.Size()
			tl.Draw(gc, x, y+(h-th)/2)
		}
		y += h
	}
}

// ProvideAccessibility describes each row: a note with its count, or a
// line found in it.
func (l *ResultsList) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	var focus accessibility.NodeID
	y := float32(0)
	for i, r := range l.rows {
		h := l.height(i)
		name := r.snippet
		if !r.line {
			name = fmt.Sprintf("%s, %d %s", r.title, r.count, l.Counting)
		}
		top := y
		id := b.AddVirtualChild(i, func(n *accessibility.Node) {
			n.Role = role.ListItem
			n.Name = strings.TrimSpace(name)
			n.Selected = i == l.current
			n.Bounds = geom.NewRect(0, top, l.ContentRect(false).Width, h)
			n.Actions = n.Actions.With(accessibility.Select)
		})
		if i == l.current {
			focus = id
		}
		y += h
	}
	if focus != 0 && l.Focused() {
		b.FocusChild(focus)
	}
}

// PerformAccessibilityAction opens a row chosen by a screen reader.
func (l *ResultsList) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	i, ok := req.Key.(int)
	if !ok || i < 0 || i >= len(l.rows) || req.Action != accessibility.Select {
		return false
	}
	l.choose(i)
	return true
}

// Results are the rows the results list shows, for tests: a note as its
// title, a hit as its line indented two spaces.
func (w *Window) Results() []string {
	var out []string
	for _, r := range w.results.rows {
		if !r.line {
			out = append(out, r.title)
		} else {
			out = append(out, "  "+r.snippet)
		}
	}
	return out
}
