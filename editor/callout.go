package editor

// Callouts (features.md 1.2.10): a quote whose first line is an Obsidian
// callout header, "> [!type] Title", optionally with "-" after the type for a
// folded one (Kvit's DocumentSerializer::parse and containerkinds.cpp). The
// type is kept in the block's Lang, the fold in Checked, the title in Title,
// and the lines after the header are the block's text. It is drawn as a
// tinted panel with the type's symbol and the title over the body; a folded
// callout shows its header alone until the caret enters it.

import (
	"regexp"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

var reCallout = regexp.MustCompile(`^\[!([A-Za-z][A-Za-z0-9_-]*)\]([+-]?)\s*(.*)$`)

// calloutMarkdown writes a callout as Kvit does: the header, then each body
// line after "> ", an empty one as ">" so no line ends in a space.
func calloutMarkdown(b Block) string {
	header := "> [!" + b.Lang + "]"
	if b.Checked {
		header += "-"
	}
	if b.Title != "" {
		header += " " + b.Title
	}
	out := []string{header}
	if b.Text != "" {
		for _, l := range strings.Split(b.Text, "\n") {
			if l == "" {
				out = append(out, ">")
			} else {
				out = append(out, "> "+l)
			}
		}
	}
	return strings.Join(out, "\n")
}

// calloutKind is how a callout type is drawn: the glyph before its title,
// its colour, and the name its header shows until it is given a title.
type calloutKind struct {
	icon, label string
	color       func(t tokens.Tokens) palette.Color
}

// calloutKinds are Kvit's callout types (EditableBlock.qml, calloutSpec).
// "toggle" is the foldable type with no chrome of its own.
var calloutKinds = map[string]calloutKind{
	"info":    {"i", "Info", func(t tokens.Tokens) palette.Color { return t.Accent }},
	"warning": {"!", "Warning", func(t tokens.Tokens) palette.Color { return t.Warning }},
	"success": {"✓", "Success", func(t tokens.Tokens) palette.Color { return t.Success }},
	"error":   {"✕", "Error", func(t tokens.Tokens) palette.Color { return t.Danger }},
	"tip":     {"★", "Tip", func(t tokens.Tokens) palette.Color { return t.CalloutTip }},
	"note":    {"✎", "Note", func(t tokens.Tokens) palette.Color { return t.TextMuted }},
	"toggle":  {"", "", func(t tokens.Tokens) palette.Color { return t.TextMuted }},
}

// calloutOrder is the order the type menu offers the types in.
var calloutOrder = []string{"info", "note", "tip", "success", "warning", "error", "toggle"}

// calloutKindOf is how a type is drawn; a type Kvit does not know keeps
// its own name, with a question mark.
func calloutKindOf(typ string) calloutKind {
	if typ == "" {
		typ = "note"
	}
	if k, ok := calloutKinds[strings.ToLower(typ)]; ok {
		return k
	}
	return calloutKind{"?", typ, func(t tokens.Tokens) palette.Color { return t.TextMuted }}
}

// calloutColor is a callout's colour: its "color" attribute, or its type's.
func (e *Editor) calloutColor(b *Block) palette.Color {
	if v, ok := b.Attr("color"); ok {
		if c, ok := parseColor(v); ok {
			return palette.RGB8(c.R, c.G, c.B)
		}
	}
	return calloutKindOf(b.Lang).color(e.tok())
}

// The callout panel in design pixels (CalloutBlockChrome.qml): the header's
// height, the body text's inset from the panel's left and the space under
// it, the bar down the left, the header's parts, and the colour dot.
const (
	calloutHeader   = 28
	calloutText     = 24
	calloutBottom   = 10
	calloutBar      = 4
	calloutRadius   = 5
	calloutChevron  = 10 // the fold arrow, from the panel's left
	calloutIconX    = 25 // the type's glyph, from the panel's left
	calloutTitleGap = 16 // from the glyph to the title
	calloutDot      = 14
	calloutDotRight = 6
	calloutTint     = 0.10
	calloutEdge     = 0.35
)

// calloutOpen reports whether a callout's body shows: unfolded, or holding
// the caret.
func (e *Editor) calloutOpen(b *Block) bool {
	return !b.Checked || (e.Doc.Focused && e.Doc.Caret.Block == b.ID)
}

// calloutPanel is a callout's panel.
func (e *Editor) calloutPanel(i int) geom.Rect {
	body := e.bodyRect(i)
	return geom.NewRect(body.X+e.px(codeInset), e.tops[i]+e.px(codeRowTop), body.Width-e.px(codeInset+contentRight),
		e.heights[i]-e.px(codeRowTop+codeRowBottom))
}

// calloutParts are where a callout's header controls are: the fold arrow,
// the type's glyph, the title and the colour dot.
func (e *Editor) calloutParts(i int) (chevron, icon, title, dot geom.Rect) {
	b := &e.Doc.Blocks[i]
	p := e.calloutPanel(i)
	h := e.px(calloutHeader)
	chevron = geom.NewRect(p.X+e.px(calloutChevron-4), p.Y, e.px(18), h)
	k := calloutKindOf(b.Lang)
	iconW := float32(0)
	if k.icon != "" {
		iconW, _ = e.label(k.icon, e.calloutHeaderStyle(b)).Size()
		iconW += e.px(6)
	}
	icon = geom.NewRect(p.X+e.px(calloutIconX-4), p.Y, iconW+e.px(12), h)
	x := p.X + e.px(calloutIconX) + iconW + e.px(calloutTitleGap)
	dot = geom.NewRect(p.Right()-e.px(calloutDotRight+calloutDot), p.Y+(h-e.px(calloutDot))/2, e.px(calloutDot), e.px(calloutDot))
	title = geom.NewRect(x, p.Y, max(1, dot.X-e.px(6)-x), h)
	return
}

// calloutHeaderStyle is the style of the header's glyph and title.
func (e *Editor) calloutHeaderStyle(b *Block) text.Style {
	st := e.chrome(kvitui.RoleStrong, text.Bold, e.calloutColor(b))
	return st
}

func (e *Editor) drawCallout(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	t := e.tok()
	c := e.calloutColor(b)
	p := e.calloutPanel(i)
	r := e.px(calloutRadius)
	gc.DrawRoundedRect(p, geom.NewSize(r, r), kvitui.Color(c).SetAlphaIntensity(calloutTint).Paint(gc, p, paintstyle.Fill))
	edge := kvitui.Color(c).SetAlphaIntensity(calloutEdge).Paint(gc, p, paintstyle.Stroke)
	edge.SetStrokeWidth(e.px(1))
	gc.DrawRoundedRect(p.Inset(geom.NewUniformInsets(e.px(0.5))), geom.NewSize(r, r), edge)
	e.fillRound(gc, geom.NewRect(p.X, p.Y, e.px(calloutBar), p.Height), e.px(2), c)
	chevron, icon, title, dot := e.calloutParts(i)
	h := e.px(calloutHeader)
	arrow := "▾"
	if b.Checked {
		arrow = "▸"
	}
	a := e.label(arrow, e.chrome(kvitui.RoleBody, text.Regular, c))
	_, ah := a.Size()
	a.Draw(gc, chevron.X+e.px(4), p.Y+(h-ah)/2)
	k := calloutKindOf(b.Lang)
	if k.icon != "" {
		l := e.label(k.icon, e.calloutHeaderStyle(b))
		_, lh := l.Size()
		l.Draw(gc, icon.X+e.px(4), p.Y+(h-lh)/2)
	}
	words, st := b.Title, e.calloutHeaderStyle(b)
	if words == "" {
		// An untitled callout shows its type's name, faint, where the title
		// goes, as the Qt app's empty title field shows it.
		words = k.label
		st.Color = colour(t.TextFaint)
		st.Color.A = 178
	}
	if words != "" {
		l := e.ui.Fonts.Layout([]text.Span{{Text: words, Style: st}}, text.Options{MaxWidth: title.Width, Elide: true})
		_, lh := l.Size()
		l.Draw(gc, title.X, p.Y+(h-lh)/2)
	}
	alpha := float32(0.35)
	if e.hover == b.ID {
		alpha = 1
	}
	gc.DrawOval(dot, kvitui.Color(c).SetAlphaIntensity(alpha).Paint(gc, dot, paintstyle.Fill))
}

// calloutTypeItems are the type menu: each of Kvit's types, the current
// one ticked.
func (e *Editor) calloutTypeItems(id int64) []kvitui.MenuItem {
	b := e.Doc.Block(id)
	if b == nil {
		return nil
	}
	var items []kvitui.MenuItem
	for _, typ := range calloutOrder {
		k := calloutKinds[typ]
		label := k.label
		if label == "" {
			label = "Toggle"
		}
		items = append(items, kvitui.MenuItem{Text: label, Checked: strings.EqualFold(b.Lang, typ), OnSelect: func() {
			e.Doc.Edit("callout type", func() { b.Lang = typ })
			e.changed()
		}})
	}
	return items
}

// calloutColorItems are the colour menu: the theme's palette, and the
// type's own colour back.
func (e *Editor) calloutColorItems(id int64) []kvitui.MenuItem {
	current, _ := e.Doc.Block(id).Attr("color")
	names := tokens.ColorPaletteNames()
	var items []kvitui.MenuItem
	for n, c := range tokens.ColorPalette() {
		hex := c.Hex()
		items = append(items, kvitui.MenuItem{Text: names[n], Checked: strings.EqualFold(current, hex), OnSelect: func() {
			e.Doc.SetAttr([]int64{id}, "color", hex)
			e.changed()
		}})
	}
	return append(items, kvitui.MenuItem{Separator: true}, kvitui.MenuItem{Text: "Type's own color", Disabled: current == "",
		OnSelect: func() {
			e.Doc.SetAttr([]int64{id}, "color", "")
			e.changed()
		}})
}

// editCalloutTitle opens a field over a callout's title; Return keeps what
// is typed, as one undo step, and Escape leaves the title as it was.
func (e *Editor) editCalloutTitle(i int) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly {
		return
	}
	b := &e.Doc.Blocks[i]
	id := b.ID
	_, _, title, _ := e.calloutParts(i)
	field := kvitui.NewField(e.ui)
	field.Label = "Callout title"
	field.Placeholder = calloutKindOf(b.Lang).label
	field.SetText(b.Title)
	var hide func()
	done := func(keep bool) {
		if hide == nil {
			return
		}
		hide()
		hide = nil
		if blk := e.Doc.Block(id); keep && blk != nil && blk.Title != strings.TrimSpace(field.Text()) {
			e.Doc.Edit("callout title", func() { blk.Title = strings.TrimSpace(field.Text()) })
		}
		e.RequestFocus()
		e.changed()
	}
	edit := field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
			done(true)
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	at := e.RectToRoot(title)
	hide = w.Show(&kvitui.Popup{Panel: field, Modal: false, Anchor: e,
		OnEscape:       func() { done(false) },
		OnPressOutside: func() { done(true) },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			return geom.NewRect(at.X-e.px(4), at.Y+(at.Height-size.Height)/2, max(at.Width, e.px(160)), size.Height)
		}})
	unison.InvokeTask(func() { field.Focus(); field.Edit().SelectAll() })
}
