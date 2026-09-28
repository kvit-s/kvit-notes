package editor

// Image effects and the lightbox (features.md 1.2.8, Kvit's ImageBlock.qml
// and Lightbox.qml): per-block presentation attributes (rounded with its
// radius, shadow, border with an optional colour, aspect stretch) drawn as
// Kvit draws them, and a click on a resolved picture opening it full-size.
// A sound or video draws as a card opening externally; playing inline has
// no Go toolkit behind it, so the card opens in the reader's player.

import (
	"strconv"
	"strings"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/pathop"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
)

// imageEffects are an image block's presentation attributes.
type imageEffects struct {
	rounded  bool
	radius   int
	shadow   bool
	border   bool
	color    palette.Color
	hasColor bool
	stretch  bool
}

// effectsOf reads an image block's effects from its attributes.
func (e *Editor) effectsOf(b *Block) imageEffects {
	var fx imageEffects
	t := e.tok()
	fx.color = t.Border
	if v, ok := b.Attr("rounded"); ok {
		fx.rounded = true
		fx.radius = 12
		if v != "" {
			if n, err := strconv.Atoi(v); err == nil && n > 0 {
				fx.radius = n
			}
		}
	}
	if _, ok := b.Attr("shadow"); ok {
		fx.shadow = true
	}
	if v, ok := b.Attr("border"); ok {
		fx.border = true
		if v != "" {
			if c, ok := parseColor(v); ok {
				fx.color, fx.hasColor = palette.RGB8(c.R, c.G, c.B), true
			}
		}
	}
	if v, ok := b.Attr("aspect"); ok && v == "stretch" {
		fx.stretch = true
	}
	_ = t
	return fx
}

// SetImageEffects sets an image block's effects as one undo step (the
// popover ImageEffectsPopover.qml writes): rounded with a radius (0 clears),
// shadow, border with an optional colour ("" clears), and stretch.
func (e *Editor) SetImageEffects(ids []int64, rounded int, shadow bool, border, color string, stretch bool) {
	set := func(id int64, key, value string) {
		b := e.Doc.Block(id)
		if b == nil {
			return
		}
		var keep []string
		for _, tok := range strings.Fields(b.Attrs) {
			if k, _, _ := strings.Cut(tok, "="); k != key {
				keep = append(keep, tok)
			}
		}
		if value != "" {
			keep = append(keep, key+"="+value)
		} else if key == "rounded" && rounded >= 0 || key == "shadow" && shadow || key == "border" && border != "" {
			keep = append(keep, key)
		}
		_ = border
		b.Attrs = canonicalAttrs(strings.Join(keep, " "))
	}
	e.Doc.Edit("attributes", func() {
		for _, id := range ids {
			b := e.Doc.Block(id)
			if b == nil {
				continue
			}
			var keep []string
			for _, tok := range strings.Fields(b.Attrs) {
				if k, _, _ := strings.Cut(tok, "="); k != "rounded" && k != "shadow" && k != "border" && k != "aspect" {
					keep = append(keep, tok)
				}
			}
			if rounded > 0 {
				keep = append(keep, "rounded="+strconv.Itoa(rounded))
			} else if rounded == 0 {
				keep = append(keep, "rounded")
			}
			if shadow {
				keep = append(keep, "shadow")
			}
			if border != "" {
				if color != "" {
					keep = append(keep, "border="+color)
				} else {
					keep = append(keep, "border")
				}
			}
			if stretch {
				keep = append(keep, "aspect=stretch")
			}
			b.Attrs = canonicalAttrs(strings.Join(keep, " "))
			_ = set
		}
	})
	e.touched()
	e.changed()
}

// lightbox is a picture opened full-size over the note.
type lightbox struct {
	path string
	alt  string
}

// OpenLightbox opens a picture full-size, as a click on a resolved image
// does (AppActions.requestLightbox).
func (e *Editor) OpenLightbox(path, alt string) {
	e.light = &lightbox{path: path, alt: alt}
	e.touched()
	e.changed()
}

// CloseLightbox closes the full-size picture.
func (e *Editor) CloseLightbox() {
	if e.light != nil {
		e.light = nil
		e.touched()
		e.changed()
	}
}

// Lightbox reports the open full-size picture, if any.
func (e *Editor) Lightbox() (path, alt string, open bool) {
	if e.light == nil {
		return "", "", false
	}
	return e.light.path, e.light.alt, true
}

// drawLightbox draws the open full-size picture over the note: a dim cover,
// the picture scaled to fit, its alt text, closing on Escape or a press
// outside it.
func (e *Editor) drawLightbox(gc *unison.Canvas) {
	if e.light == nil {
		return
	}
	t := e.tok()
	cover := e.ContentRect(false)
	gc.DrawRect(cover, kvitui.Color(t.TextPrimary).Paint(gc, cover, paintstyle.Fill))
	gc.SaveWithOpacity(0.72)
	gc.DrawRect(cover, kvitui.Color(t.WindowBackground).Paint(gc, cover, paintstyle.Fill))
	gc.Restore()
	ref := ImageRef{Path: e.light.path, Alt: e.light.alt}
	p := e.pictureFor(ref)
	box := cover.Inset(geom.NewUniformInsets(e.px(24)))
	if p.img != nil {
		sz := p.img.LogicalSize()
		w, h := box.Width, sz.Height*box.Width/sz.Width
		if h > box.Height {
			h, w = box.Height, sz.Width*box.Height/sz.Height
		}
		r := geom.NewRect(box.X+(box.Width-w)/2, box.Y+(box.Height-h)/2, w, h)
		p.img.DrawInRect(gc, r, nil, nil)
		if e.light.alt != "" {
			e.label(e.light.alt, e.chrome(kvitui.RoleSmall, 0, t.TextSecondary)).Draw(gc, r.X, r.Bottom()+e.px(8))
		}
	} else {
		e.label(p.failed, e.chrome(kvitui.RoleBody, 0, t.TextPrimary)).Draw(gc, box.X, box.Y)
	}
}

// lightboxPress closes the lightbox on Escape or a press outside the
// picture, reporting whether it handled the input.
func (e *Editor) lightboxPress() bool {
	if e.light != nil {
		e.CloseLightbox()
		return true
	}
	return false
}

// clipRounded clips drawing to a rounded rectangle.
func clipRounded(gc *unison.Canvas, r geom.Rect, radius float32) int {
	p := unison.NewPath()
	p.RoundedRect(r, geom.NewSize(radius, radius))
	n := gc.Save()
	gc.ClipPath(p, pathop.Intersect, true)
	return n
}
