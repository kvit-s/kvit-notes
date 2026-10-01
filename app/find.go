package app

// Finding and replacing in the open note: Ctrl+F opens a bar at the top right
// of the note, Ctrl+H opens it with the replace row. Every match is drawn in
// the note and the current one in its own colour; Enter and Shift+Enter move
// between them, the count says where the current one is, and the toggles are
// match case, whole word and regular expression, kept in the settings. A
// block selection or a cross-block text selection arms the in-selection
// domain, with an "In selection" toggle shown only then, which is not
// persisted. Replace changes the current match and moves to the next; All
// lists every change for a confirmation first. Escape closes the bar and puts
// the caret at the current match. The search itself is done by the search
// package.

import (
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/search"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// finder is the open find bar.
type finder struct {
	w          *Window
	panel      *unison.Panel
	row1       *unison.Panel
	replaceRow *unison.Panel
	query      *kvitui.Field
	repl       *kvitui.Field
	count      *kvitui.Label
	toggles    map[string]*kvitui.Button
	hide       func()

	opts    search.Options
	blocks  []search.Block
	matches []search.Match
	current int
	invalid bool // the query is a regular expression that does not compile

	// The in-selection domain, armed from the selection present when the bar
	// opens. Block ids keep it valid across moves; text ends are Markdown
	// offsets, mapped to display on each recompute. Nil block ids with a nil
	// text range is no domain. inSelOn is the toggle's state: the matches are
	// kept inside the domain only while it is on. The toggle is shown only
	// while a domain is armed, and is not persisted.
	domainBlocks []int64
	domainText   *textDomainEnds
	inSel        *kvitui.Button
	inSelOn      bool
}

// textDomainEnds are one text selection's ends as Markdown offsets.
type textDomainEnds struct {
	startBlock, endBlock int64
	startOff, endOff     int
}

// searchBlocks are the note's blocks as the search sees them: each one's
// source and inline spans, a code block's source taken as it is.
func searchBlocks(d *editor.Doc) []search.Block {
	out := make([]search.Block, len(d.Blocks))
	for i := range d.Blocks {
		b := &d.Blocks[i]
		switch {
		case b.Kind == editor.Divider:
			out[i] = search.Block{Verbatim: true}
		case !b.Kind.HasInline():
			out[i] = search.Block{Markdown: b.Text, Verbatim: true}
		default:
			out[i] = search.Block{Markdown: b.Text, Spans: spansOf(b.Text)}
		}
	}
	return out
}

func spansOf(src string) []search.Span {
	var out []search.Span
	for _, sp := range editor.Spans(src) {
		out = append(out, search.Span{Start: sp.Start, End: sp.End, ContentStart: sp.ContentStart, ContentEnd: sp.ContentEnd})
	}
	return out
}

// openFind shows the find bar, with the replace row when replace is set. A
// block selection or a cross-block text selection arms the in-selection
// domain, with the toggle on; otherwise the domain is cleared and the bar
// is seeded with the selected text when it is on one line.
func (w *Window) openFind(replace bool) {
	if w.finder == nil {
		w.finder = newFinder(w)
	}
	f := w.finder
	d := w.Editor.Doc
	switch {
	case len(w.Editor.SelectedBlocks()) > 0:
		f.domainBlocks = append([]int64(nil), w.Editor.SelectedBlocks()...)
		f.domainText = nil
		f.setInSel(true)
	case d.HasSelection() && d.CrossBlock():
		a, b := d.SelRange()
		f.domainBlocks = nil
		f.domainText = &textDomainEnds{startBlock: a.Block, endBlock: b.Block, startOff: a.Off, endOff: b.Off}
		f.setInSel(true)
	default:
		f.domainBlocks = nil
		f.domainText = nil
		f.setInSel(false)
		if d.HasSelection() && !d.CrossBlock() {
			if sel := editor.PlainText(d.SelectedMarkdown()); sel != "" && !strings.Contains(sel, "\n") {
				f.query.SetText(sel)
			}
		}
	}
	f.showReplace(replace)
	if f.hide == nil {
		f.hide = w.Win.Show(&kvitui.Popup{Panel: f.panel, Anchor: w.region, OnEscape: f.close, Place: f.place})
	}
	f.recompute(true)
	unison.InvokeTask(func() {
		f.query.Focus()
		f.query.Edit().SelectAll()
	})
}

// hasDomain reports whether an in-selection domain is armed, regardless of
// the toggle.
func (f *finder) hasDomain() bool {
	return len(f.domainBlocks) > 0 || f.domainText != nil
}

// setInSel sets the toggle's state and shows the toggle only while a domain
// is armed.
func (f *finder) setInSel(on bool) {
	f.inSelOn = on
	if f.inSel != nil {
		f.inSel.Checked = on
		hidden := !f.hasDomain()
		if f.inSel.Hidden != hidden {
			f.inSel.Hidden = hidden
			f.w.relayout()
		} else {
			f.inSel.MarkForRedraw()
		}
	}
}

// syncDomain resolves the armed domain against the current blocks and puts
// it in the search options while the toggle is on. Ids that vanished prune
// a block domain and clear a text domain.
func (f *finder) syncDomain() {
	d := f.w.Editor.Doc
	if !f.inSelOn || !f.hasDomain() {
		f.opts.Within = nil
		return
	}
	if len(f.domainBlocks) > 0 {
		var idx []int
		for _, id := range f.domainBlocks {
			if i := d.Index(id); i >= 0 {
				idx = append(idx, i)
			}
		}
		if len(idx) == 0 {
			f.domainBlocks = nil
			f.setInSel(false)
			f.opts.Within = nil
			return
		}
		f.opts.Within = search.InBlocks(idx...)
		return
	}
	if t := f.domainText; t != nil {
		ia, ib := d.Index(t.startBlock), d.Index(t.endBlock)
		if ia < 0 || ib < 0 || ia >= len(f.blocks) || ib >= len(f.blocks) {
			f.domainText = nil
			f.setInSel(false)
			f.opts.Within = nil
			return
		}
		f.opts.Within = search.InText(ia, f.blocks[ia].DisplayPos(t.startOff), ib, f.blocks[ib].DisplayPos(t.endOff))
		return
	}
	f.opts.Within = nil
}

func newFinder(w *Window) *finder {
	ui := w.ui
	f := &finder{w: w, current: -1, toggles: map[string]*kvitui.Button{}}
	f.opts = search.Options{
		CaseSensitive: w.prefs.bool("find.caseSensitive", false),
		WholeWord:     w.prefs.bool("find.wholeWord", false),
		Regex:         w.prefs.bool("find.useRegex", false),
		PreserveCase:  w.prefs.bool("find.preserveCase", false),
	}
	f.query = kvitui.NewField(ui)
	f.query.Label = "Find"
	f.query.Placeholder = "Find"
	f.query.OnChange = func(string) { f.recompute(true) }
	f.repl = kvitui.NewField(ui)
	f.repl.Label = "Replace with"
	f.repl.Placeholder = "Replace"
	f.count = kvitui.NewLabel(ui, "")
	f.count.Ink = kvitui.InkTextSecondary
	prev := kvitui.NewIconButton(ui, "caret-up", "Previous match (Shift+Enter)")
	prev.OnClick = func() { f.step(-1) }
	next := kvitui.NewIconButton(ui, "caret-down", "Next match (Enter)")
	next.OnClick = func() { f.step(1) }
	toggle := func(label, name, key string, on *bool) *kvitui.Button {
		b := kvitui.NewButton(ui, label)
		b.Form = kvitui.ButtonFlat
		b.Checkable, b.Checked = true, *on
		b.Explanation = name
		b.OnClick = func() {
			*on = b.Checked
			w.prefs.set(key, *on)
			f.recompute(true)
		}
		f.toggles[key] = b
		return b
	}
	caseB := toggle("Aa", "Match case", "find.caseSensitive", &f.opts.CaseSensitive)
	word := toggle("ab", "Whole word", "find.wholeWord", &f.opts.WholeWord)
	regex := toggle(".*", "Regular expression", "find.useRegex", &f.opts.Regex)
	keep := toggle("AB", "Preserve case", "find.preserveCase", &f.opts.PreserveCase)
	f.inSel = kvitui.NewButton(ui, "In selection")
	f.inSel.Form = kvitui.ButtonFlat
	f.inSel.Checkable = true
	f.inSel.Explanation = "Only in the selection"
	f.inSel.OnClick = func() {
		f.inSelOn = f.inSel.Checked
		f.syncDomain()
		f.recompute(true)
	}
	f.inSel.Hidden = true
	closeB := kvitui.NewIconButton(ui, "close", "Close (Escape)")
	closeB.OnClick = f.close
	replace := kvitui.NewButton(ui, "Replace")
	replace.OnClick = f.replaceOne
	all := kvitui.NewButton(ui, "All")
	all.Explanation = "Replace every match"
	all.OnClick = f.replaceAll

	keys := func(field *kvitui.Field, enter func(shift bool)) {
		edit := field.Edit()
		prevKeys := edit.KeyDownCallback
		edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
			if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
				enter(mods.ShiftDown())
				return true
			}
			return prevKeys != nil && prevKeys(key, mods, repeat)
		}
	}
	keys(f.query, func(shift bool) {
		if shift {
			f.step(-1)
		} else {
			f.step(1)
		}
	})
	keys(f.repl, func(bool) { f.replaceOne() })

	width := func(p unison.Paneler, px int) unison.Paneler { return kvitui.Width(ui, kvitui.Px(px), p) }
	row1 := kvitui.Row(ui, kvitui.SizeSpaceSnug, width(f.query, 190), width(f.count, 70), prev, next, caseB, word, regex, f.inSel, closeB)
	f.row1 = row1
	f.replaceRow = kvitui.Row(ui, kvitui.SizeSpaceSnug, width(f.repl, 190), replace, all, keep)
	for _, p := range []*unison.Panel{row1, f.replaceRow} {
		for _, c := range p.Children() {
			c.SetLayoutData(&unison.FlexLayoutData{VAlign: align.Middle})
		}
	}
	f.panel = kvitui.Column(ui, kvitui.SizeSpaceSnug, row1)
	f.panel.SetBorder(kvitui.Padding(ui, kvitui.Px(6)))
	f.panel.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		t := ui.Theme.Tokens()
		r := f.panel.ContentRect(true)
		rad := float32(ui.Interface.Px(8))
		gc.DrawRoundedRect(r, geom.NewSize(rad, rad), kvitui.Color(t.PopupBackground).Paint(gc, r, paintstyle.Fill))
		edge := kvitui.Color(t.BorderStrong).Paint(gc, r, paintstyle.Stroke)
		edge.SetStrokeWidth(1)
		gc.DrawRoundedRect(r.Inset(geom.NewUniformInsets(0.5)), geom.NewSize(rad, rad), edge)
	}
	f.panel.Accessibility.Name = "Find"
	return f
}

// showReplace shows or hides the replace row.
func (f *finder) showReplace(on bool) {
	if on == (f.replaceRow.Parent() != nil) {
		return
	}
	if on {
		f.panel.AddChild(f.replaceRow)
	} else {
		f.replaceRow.RemoveFromParent()
	}
	f.w.relayout()
}

// place puts the bar at the top right of the note.
func (f *finder) place(bounds geom.Rect, size geom.Size) geom.Rect {
	w := f.w
	if w.Win.Window == nil {
		return geom.NewRect(bounds.X, bounds.Y, size.Width, size.Height)
	}
	r := w.Win.Content().RectFromRoot(w.region.RectToRoot(w.region.ContentRect(false)))
	x := max(r.X, r.Right()-size.Width-8)
	return geom.NewRect(x, r.Y+8, size.Width, size.Height)
}

// recompute searches the note again; fresh picks the match at or after
// the caret as the current one, as a new query or option does. The armed
// in-selection domain is resolved first, so edits that move blocks keep it.
func (f *finder) recompute(fresh bool) {
	d := f.w.Editor.Doc
	f.blocks = searchBlocks(d)
	f.syncDomain()
	texts := make([]string, len(f.blocks))
	for i, b := range f.blocks {
		texts[i] = b.Text()
	}
	p, err := search.Compile(f.query.Text(), f.opts)
	f.invalid = err != nil
	if err != nil {
		f.matches = nil
	} else {
		f.matches = p.Find(texts)
	}
	switch {
	case len(f.matches) == 0:
		f.current = -1
	case fresh || f.current < 0 || f.current >= len(f.matches):
		block, pos := -1, 0
		if b := d.CaretBlock(); b != nil {
			block = d.Index(b.ID)
			pos = f.blocks[block].DisplayPos(d.Caret.Off)
		}
		f.current = search.Nearest(f.matches, block, pos)
	}
	f.show()
}

// show draws the matches and says how many there are.
func (f *finder) show() {
	d := f.w.Editor.Doc
	var marks []editor.Mark
	for k, m := range f.matches {
		b := f.blocks[m.Block]
		marks = append(marks, editor.Mark{Block: d.Blocks[m.Block].ID, Start: b.MarkdownPos(m.Start),
			End: b.MarkdownPos(m.Start + m.Length), Current: k == f.current})
	}
	f.w.Editor.SetMarks(marks)
	switch {
	case f.invalid:
		f.count.Text = "Invalid pattern"
		f.count.Ink = kvitui.InkDanger
	case f.query.Text() == "":
		f.count.Text, f.count.Ink = "", kvitui.InkTextSecondary
	case len(f.matches) == 0:
		f.count.Text, f.count.Ink = "No results", kvitui.InkTextSecondary
	default:
		f.count.Text, f.count.Ink = fmt.Sprintf("%d of %d", f.current+1, len(f.matches)), kvitui.InkTextSecondary
	}
	f.count.MarkForLayoutAndRedraw()
	if f.current >= 0 {
		m := f.matches[f.current]
		f.w.Editor.RevealBlockRange(d.Blocks[m.Block].ID, f.blocks[m.Block].MarkdownPos(m.Start))
	}
}

// step moves to the next match, or the previous one.
func (f *finder) step(by int) {
	if by > 0 {
		f.current = search.Next(f.current, len(f.matches))
	} else {
		f.current = search.Previous(f.current, len(f.matches))
	}
	f.show()
}

// replaceOne replaces the current match and moves to the one after it.
func (f *finder) replaceOne() {
	d := f.w.Editor.Doc
	if f.current < 0 || d.ReadOnly {
		return
	}
	m := f.matches[f.current]
	md, after := search.ReplaceOne(f.blocks[m.Block], m, f.repl.Text(), f.opts)
	b := &d.Blocks[m.Block]
	d.Edit("replace", func() {
		b.Text = md
		d.SetCaret(b.ID, after)
	})
	f.w.Editor.Refresh()
	f.w.edited()
	f.recompute(true)
}

// replaceAll lists every change and makes them, as one undo step, once
// the reader agrees.
func (f *finder) replaceAll() {
	d := f.w.Editor.Doc
	if len(f.matches) == 0 || d.ReadOnly {
		return
	}
	ui := f.w.ui
	texts := make([]string, len(f.blocks))
	blocksTouched := map[int]bool{}
	for i, b := range f.blocks {
		texts[i] = b.Text()
	}
	list := kvitui.Column(ui, kvitui.Px(2))
	t := ui.Theme.Tokens()
	for _, r := range search.Preview(texts, f.matches, f.repl.Text(), f.opts) {
		blocksTouched[r.Block] = true
		base := ui.Chrome(ui.Size(kvitui.RoleBody), text.Regular, t.TextSecondary)
		old, repl := base, base
		old.Color, old.Strike = kvitui.TextColor(t.Danger), true
		repl.Color, repl.Weight = kvitui.TextColor(t.Success), text.Bold
		num := base
		num.Color = kvitui.TextColor(t.TextFaint)
		line := richLine(ui, []text.Span{{Text: fmt.Sprintf("%d:  ", r.Block+1), Style: num},
			{Text: r.Prefix, Style: base}, {Text: r.Matched, Style: old}, {Text: r.Replacement, Style: repl},
			{Text: r.Suffix, Style: base}})
		list.AddChild(line)
	}
	region := kvitui.NewRegion(ui, list)
	dlg := kvitui.NewDialog(ui, fmt.Sprintf("Replace %d match(es) in %d block(s)?", len(f.matches), len(blocksTouched)),
		kvitui.Height(ui, kvitui.Px(160), region))
	dlg.ConfirmText = "Replace All"
	dlg.OnAccept = func() {
		edits, _ := search.ReplaceAll(f.blocks, f.matches, f.repl.Text(), f.opts, spansOf)
		d.Edit("replace all", func() {
			for _, e := range edits {
				d.Blocks[e.Block].Text = e.Markdown
			}
		})
		f.w.Editor.Refresh()
		f.w.edited()
		f.recompute(true)
	}
	dlg.Open(f.w.Win)
}

// close takes the bar and the matches away and puts the caret at the
// current match, and clears the armed domain.
func (f *finder) close() {
	w := f.w
	if f.hide != nil {
		f.hide()
		f.hide = nil
	}
	f.domainBlocks = nil
	f.domainText = nil
	f.setInSel(false)
	f.opts.Within = nil
	w.Editor.SetMarks(nil)
	if f.current >= 0 && f.current < len(f.matches) {
		m := f.matches[f.current]
		d := w.Editor.Doc
		if m.Block < len(d.Blocks) {
			id := d.Blocks[m.Block].ID
			b := f.blocks[m.Block]
			d.Anchor = editor.Pos{Block: id, Off: b.MarkdownPos(m.Start)}
			d.Caret = editor.Pos{Block: id, Off: b.MarkdownPos(m.Start + m.Length)}
			d.Focused = true
		}
	}
	w.Editor.RequestFocus()
	w.Editor.Refresh()
}

// FindOpen reports whether the find bar is shown, and its count.
func (w *Window) FindOpen() (bool, string) {
	if w.finder == nil || w.finder.hide == nil {
		return false, ""
	}
	return true, w.finder.count.Text
}

// richLine is one line of text in several styles, cut short with "…" when
// it does not fit, as the replace preview shows each change.
func richLine(ui *kvitui.UI, spans []text.Span) *unison.Panel {
	p := unison.NewPanel()
	var all strings.Builder
	for _, sp := range spans {
		all.WriteString(sp.Text)
	}
	p.SetSizer(func(geom.Size) (geom.Size, geom.Size, geom.Size) {
		w, h := ui.Fonts.Layout(spans, text.Options{}).Size()
		return geom.NewSize(10, h), geom.NewSize(w, h), geom.NewSize(unison.DefaultMaxSize, h)
	})
	p.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		ui.Fonts.Layout(spans, text.Options{MaxWidth: max(1, p.ContentRect(false).Width), Elide: true}).Draw(gc, 0, 0)
	}
	p.Accessibility.Role = role.Label
	p.Accessibility.Name = all.String()
	return p
}
