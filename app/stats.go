package app

// Statistics and writing goals: the word count in the status line opens the
// note's counts, or the selection's, with the words written since the note
// was opened; the goal beside it shows how far the note is towards the word
// count set for it, kept as "goal" in its front matter, and opens the dialog
// that sets it.

import (
	"fmt"
	"math"
	"strconv"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// countFacts are the status line's word count and goal.
func (w *Window) countFacts(stats editor.Stats) []kvitui.StatusFact {
	words := kvitui.StatusFact{Text: plural(stats.Words, "word"), Explanation: "Opens the document statistics"}
	if sel, ok := w.Editor.Doc.SelectionStats(); ok {
		words.Text = plural(sel.Words, "word") + " selected"
	}
	goal := kvitui.StatusFact{Text: "goal", Symbol: "target", Explanation: "Set a writing goal"}
	if w.page != nil {
		if g := w.page.Goal(); g > 0 {
			pct := int(math.Round(min(1, float64(stats.Words)/float64(g)) * 100))
			goal.Text = fmt.Sprintf("%d%%", pct)
			goal.Explanation = fmt.Sprintf("Writing goal: %d of %d words", stats.Words, g)
			if stats.Words >= g {
				goal.Symbol = "check-circle"
			}
		}
	}
	return []kvitui.StatusFact{words, goal}
}

func plural(n int, word string) string {
	if n == 1 {
		return "1 " + word
	}
	return strconv.Itoa(n) + " " + word + "s"
}

// openStatistics shows the counts of the note, or of the selection, above
// the status line's right end.
func (w *Window) openStatistics() {
	ui := w.ui
	d := w.Editor.Doc
	stats, selected := d.SelectionStats()
	title := "Selection"
	if !selected {
		stats, title = d.Stats(), "Document"
	}
	rows := [][2]string{
		{"Words", strconv.Itoa(stats.Words)},
		{"Characters", strconv.Itoa(stats.Chars)},
		{"Characters (no spaces)", strconv.Itoa(stats.CharsNoSpaces)},
		{"Paragraphs", strconv.Itoa(stats.Paragraphs)},
	}
	if !selected {
		rows = append(rows, [2]string{"Blocks", strconv.Itoa(stats.Blocks)})
	}
	reading := "—"
	if stats.ReadingMinutes > 0 {
		reading = fmt.Sprintf("%d min", stats.ReadingMinutes)
	}
	rows = append(rows, [2]string{"Reading time", reading})
	if w.open != nil {
		delta := d.Stats().Words - w.sessionWords
		sign := "+"
		if delta < 0 {
			sign = ""
		}
		rows = append(rows, [2]string{"This session", sign + strconv.Itoa(delta)})
	}
	head := kvitui.NewLabel(ui, title)
	head.Role, head.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	parts := []unison.Paneler{head}
	for _, r := range rows {
		k := kvitui.NewLabel(ui, r[0])
		k.Ink = kvitui.InkTextSecondary
		k.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		v := kvitui.NewLabel(ui, r[1])
		v.Role = kvitui.RoleStrong
		parts = append(parts, kvitui.Row(ui, kvitui.SizeSpace, k, v))
	}
	pop := kvitui.NewPopover(ui, title+" statistics", parts...)
	pop.Width = kvitui.Px(260)
	pop.Open(w.status, func(bounds geom.Rect, size geom.Size) geom.Rect {
		return geom.NewRect(bounds.Right()-size.Width-8, bounds.Bottom()-size.Height-float32(ui.Interface.Px(30)), size.Width, size.Height)
	})
	w.stats = pop
}

// openGoal asks for the open note's writing goal; 0 takes it away.
func (w *Window) openGoal() {
	if w.open == nil || w.Vault.ReadOnly {
		return
	}
	e := w.open
	field := kvitui.NewNumberField(w.ui)
	field.Label = "Words"
	field.Minimum, field.Maximum = 0, 1000000
	field.SetText(strconv.Itoa(w.page.Goal()))
	d := kvitui.NewDialog(w.ui, "Writing goal", field)
	d.Detail = "Target word count for this note (0 to clear):"
	d.ConfirmText = "Set goal"
	d.OnAccept = func() {
		v, ok := field.Value()
		if !ok || e != w.open {
			return
		}
		w.page.SetGoal(int(v))
		w.Editor.Doc.Dirty = true
		w.saveNow()
	}
	d.Open(w.Win)
	unison.InvokeTask(func() { field.Focus(); field.Edit().SelectAll() })
}
