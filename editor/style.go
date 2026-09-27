package editor

import (
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
)

// The editor's geometry, in design pixels at the default interface size; each
// is scaled with the interface size by px. They are Kvit's row metrics
// (qml/EditableBlock.qml, qml/BlockGutter.qml), checked against the
// screenshots of Kvit's own storyboards.
const (
	pageMargin   = 20 // around the document, and above its first block
	gutterWidth  = 40 // the + / handle / × / menu strip left of each row
	focusBar     = 3  // the bar beside the block holding the caret
	contentLeft  = 17 // from the end of the focus bar to a paragraph's text
	contentRight = 16 // from a row's text to its right edge
	rowPadTop    = 10 // above a row's text
	rowPadBottom = 18 // below a row's text
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

	// The code panel (qml/EditableBlock.qml, qml/CodeBlockChrome.qml): its
	// inset from the row's left, top and bottom, the header holding the
	// language and Copy, the padding above and below the code, the footer
	// strip, and the code's inset from the panel's side.
	codeInset       = 7
	codeRowTop      = 4
	codeRowBottom   = 12
	codeHeader      = 26
	codeTextPad     = 4
	codeFooter      = 18
	codePadSide     = 12
	codeTextInset   = 2
	codePanelRadius = 4

	// The quote bar runs from a little below the row's top to a little above
	// its bottom (qml/QuoteDelegate.qml).
	quoteBarTop    = 4
	quoteBarBottom = 8

	// The gutter's four controls (qml/BlockGutter.qml): two columns, 7 px
	// below the row's top and centred in the gutter, 4 px apart. The first
	// holds the add button over the delete button, 18 px square; the second
	// the handle over the block-menu button, 14 px wide. Each column's
	// controls are 2 px apart.
	gutterTop       = 7
	gutterColumnGap = 4
	gutterRowGap    = 2
	gutterButton    = 18
	gutterNarrow    = 14
	gutterAddGlyph  = 14
	gutterDelGlyph  = 16
	gutterRadius    = 4
	handleDot       = 3 // the handle's four dots, 2 px apart
	handleDotGap    = 2
	handleOpacity   = 0.6
	menuBarWidth    = 8 // the menu button's three bars, 2 px apart
	menuBarHeight   = 1.5
	menuBarGap      = 2

	// The / menu (qml/BlockMenu.qml).
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
	case Code, Raw, Table:
		return tokens.Mono
	}
	return tokens.Body
}

// blockStyle is the base text style of a block: its family, size, weight
// and colour before any inline formatting (qml/TextBlockDelegate.qml).
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
	case Code, Raw, Table:
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

// pitch is how far apart the lines of a block's text are: its size times the
// document's line height, as Qt's text document spaces them.
func (e *Editor) pitch(st text.Style) float32 {
	return st.Size * float32(e.ui.Typography.LineHeight())
}

// chrome is the style of the editor's own labels in one of the interface's
// type roles.
func (e *Editor) chrome(r kvitui.TypeRole, weight int, c palette.Color) text.Style {
	return e.ui.Chrome(e.ui.Size(r), weight, c)
}
