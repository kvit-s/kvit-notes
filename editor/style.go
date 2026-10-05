package editor

import (
	"sync"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
)

// The editor's geometry, in design pixels at the default interface size; each
// is scaled with the interface size by px.
const (
	pageMargin   = 20 // around the document, and above its first block
	gutterWidth  = 40 // the block-menu button and handle left of each row
	focusBar     = 3  // the bar beside the block holding the caret
	contentLeft  = 17 // from the end of the focus bar to a paragraph's text
	contentRight = 16 // from a row's text to its right edge
	rowPadTop    = 4  // above a row's text
	rowPadBottom = 4  // below a row's text
	blockGap     = 4  // between rows
	indentStep   = 24 // per list level
	dividerRow   = 28 // a divider's whole row
	caretWidth   = 1.5

	// List markers: the width the bullet, number, check box and quote bar
	// take before the text.
	bulletMarker   = 32
	numberMarker   = 36
	todoMarker     = 36
	quoteMarker    = 21
	quoteBarWidth  = 3
	checkBoxSize   = 16
	checkBoxRadius = 3

	// The code panel: its inset from the row's left, top and bottom, the
	// header holding the language and Copy, the padding above and below the
	// code, the footer strip, and the code's inset from the panel's side.
	codeInset       = 7
	codeRowTop      = 4
	codeRowBottom   = 12
	codeHeader      = 26
	codeTextPad     = 4
	codeFooter      = 18
	codePadSide     = 12
	codeTextInset   = 2
	codePanelRadius = 4

	// The gutter's two controls, side by side, 4 px apart and centred in the
	// gutter: the block-menu button, 18 px square, and the handle, 14 px
	// wide. They are centred on the first line of a row that starts with
	// text, and 7 px below the top of any other row.
	gutterTop       = 7
	gutterColumnGap = 4
	gutterButton    = 18
	gutterNarrow    = 14
	gutterRadius    = 4
	handleDot       = 3 // the handle's four dots, 2 px apart
	handleDotGap    = 2
	handleOpacity   = 0.6
	menuBarWidth    = 8 // the menu button's three bars, 2 px apart
	menuBarHeight   = 1.5
	menuBarGap      = 2

	// The / menu.
	menuWidth      = 300
	menuMaxHeight  = 328
	menuEntry      = 44
	menuGroup      = 24
	menuPad        = 4
	menuIcon       = 28
	menuIconRadius = 5
	menuGap        = 10
	menuBelowCaret = 2
	menuAboveCaret = 4
	menuRadius     = 6
	menuLitRadius  = 4

	// A drag starts once the pointer has moved this far from the press.
	dragThreshold = 5
)

// px is a design value at the current interface size.
func (e *Editor) px(design float32) float32 {
	return design * float32(e.ui.Interface.Px(100)) / 100
}

// tok is the current theme's colours.
func (e *Editor) tok() tokens.Tokens { return e.ui.Theme.Tokens() }

// colour converts a theme colour for the text package.
func colour(c palette.Color) text.Color { return kvitui.TextColor(c) }

// headingRole is the typography role of a block kind's text.
func headingRole(k Kind) tokens.FontRole {
	switch k {
	case Heading1:
		return tokens.Heading1
	case Heading2:
		return tokens.Heading2
	case Heading3:
		return tokens.Heading3
	case Heading4:
		return tokens.Heading4
	case Code, Raw, Table, Math:
		return tokens.Mono
	}
	return tokens.Body
}

// blockStyle is the base text style of a block: its family, size, weight
// and colour before any inline formatting.
func (e *Editor) blockStyle(b *Block) text.Style {
	ty, t := e.ui.Typography, e.tok()
	st := text.Style{
		Family: ty.FontFamily(),
		Size:   float32(ty.SizeForRole(headingRole(b.Kind))),
		Weight: text.Regular,
		Color:  colour(t.TextPrimary),
	}
	switch b.Kind {
	case Heading1:
		st.Weight = text.Bold
	case Heading2:
		st.Weight = text.Semibold
	case Heading3, Heading4:
		st.Weight = text.Medium
	case Quote:
		st.Color = colour(t.TextSecondary)
	case Code, Raw, Table, Math:
		st.Family = e.monoFamily()
	case Todo:
		if b.Checked {
			st.Color = colour(t.TextFaint)
			st.Strike = true
		}
	}
	return st
}

// monoFamily is the family code is set in.
func (e *Editor) monoFamily() string {
	if f := e.ui.Typography.MonoFamily(); f != "" {
		return f
	}
	return text.Monospace
}

// pitch is how far apart the lines of a block's text are: the document's line
// height times the font's own line height, its ascent and descent rounded up
// to a pixel: 22.1 px at 14 px and 1.3, 18 px at 15 px and 1.0.
func (e *Editor) pitch(st text.Style) float32 {
	return e.naturalLine(st) * float32(e.ui.Typography.LineHeight())
}

// lineKey is what a font's own line height depends on.
type lineKey struct {
	fonts        *text.Fonts
	family       string
	size, weight float32
	italic       bool
}

// naturalBox is a font's own line: its height, and its ascent, which is how
// far below the top of a line of a fixed pitch the baseline is.
type naturalBox struct{ height, ascent float32 }

var (
	naturalMu    sync.Mutex
	naturalLines = map[lineKey]naturalBox{}
)

// naturalLine is the font's own line height in style st: one line laid out
// at a line height of 1, which is its ascent and descent rounded up to a
// pixel. It is measured once per font and size.
func (e *Editor) naturalLine(st text.Style) float32 { return e.natural(st).height }

// natural is the font's own line in style st, measured once per font and
// size: one line laid out at a line height of 1 for its height, and again at
// that height as its pitch for its ascent, rounded to a pixel as the lines
// of a block's text place their baselines.
func (e *Editor) natural(st text.Style) naturalBox {
	key := lineKey{e.ui.Fonts, st.Family, st.Size, float32(st.Weight), st.Italic}
	naturalMu.Lock()
	n, ok := naturalLines[key]
	naturalMu.Unlock()
	if ok {
		return n
	}
	plain := []text.Span{{Text: " ", Style: text.Style{Family: st.Family, Size: st.Size, Weight: st.Weight, Italic: st.Italic}}}
	_, n.height = e.ui.Fonts.Layout(plain, text.Options{}).Size()
	n.ascent = e.ui.Fonts.Layout(plain, text.Options{Pitch: n.height}).Baseline()
	naturalMu.Lock()
	naturalLines[key] = n
	naturalMu.Unlock()
	return n
}

// chrome is the style of the editor's own labels in one of the interface's
// type roles.
func (e *Editor) chrome(r kvitui.TypeRole, weight int, c palette.Color) text.Style {
	return e.ui.Chrome(e.ui.Size(r), weight, c)
}
