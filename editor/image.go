package editor

// Pictures and media (features.md 1.2.8): a line holding nothing but
// ![alt|width](path "caption") is an image block, or a media block when the
// path names a sound or a video (src/content/imageassets.cpp). The line is
// the block's text and is saved unchanged. The picture is drawn below the
// text while the caret is in the block, so the line can be edited, and on
// its own otherwise, with its caption under it.

import (
	"path"
	"regexp"
	"strconv"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// ImageRef is what an image line says.
type ImageRef struct {
	Alt, Path, Caption string
	// Width is the width the picture is drawn at, 0 for its own.
	Width int
	// Media is a sound or a video rather than a picture.
	Media bool
	// Remote is an http or https address.
	Remote bool
}

var (
	reImageLine = regexp.MustCompile(`^!\[((?:\\.|[^\]\\])*)\]\((.*)\)$`)
	reCaption   = regexp.MustCompile(`^(.*?)\s+"((?:\\.|[^"\\])*)"$`)
	reWidth     = regexp.MustCompile(`^(\d+)(?:x\d+)?$`)
)

var (
	imageExts = map[string]bool{"png": true, "jpg": true, "jpeg": true, "gif": true, "webp": true, "svg": true, "bmp": true}
	mediaExts = map[string]bool{"mp3": true, "wav": true, "ogg": true, "flac": true, "m4a": true,
		"mp4": true, "webm": true, "mkv": true, "mov": true}
)

// extensionOf is a path's or address's extension, lowercased, without a
// query or fragment.
func extensionOf(p string) string {
	if i := strings.IndexAny(p, "?#"); i >= 0 {
		p = p[:i]
	}
	return strings.ToLower(strings.TrimPrefix(path.Ext(p), "."))
}

func unescapeField(s string) string {
	var b strings.Builder
	for i := 0; i < len(s); i++ {
		if s[i] == '\\' && i+1 < len(s) {
			i++
			if s[i] == 'n' {
				b.WriteByte('\n')
				continue
			}
		}
		b.WriteByte(s[i])
	}
	return b.String()
}

// ParseImageLine reads an image line, as Kvit's ImageAssets::parseLine
// does. It fails for anything that is not exactly one image expression
// naming a picture, a sound, a video, or a web address.
func ParseImageLine(line string) (ImageRef, bool) {
	m := reImageLine.FindStringSubmatch(line)
	if m == nil {
		return ImageRef{}, false
	}
	altPart, inner := m[1], m[2]
	ref := ImageRef{Path: inner}
	if cm := reCaption.FindStringSubmatch(inner); cm != nil {
		ref.Path, ref.Caption = cm[1], unescapeField(cm[2])
	}
	if ref.Path == "" {
		return ImageRef{}, false
	}
	alt := altPart
	if bar := lastUnescapedBar(altPart); bar >= 0 {
		if wm := reWidth.FindStringSubmatch(strings.TrimSpace(altPart[bar+1:])); wm != nil {
			ref.Width, _ = strconv.Atoi(wm[1])
			alt = altPart[:bar]
		}
	}
	ref.Alt = unescapeField(alt)
	lower := strings.ToLower(ref.Path)
	ref.Remote = strings.HasPrefix(lower, "http://") || strings.HasPrefix(lower, "https://")
	ext := extensionOf(ref.Path)
	switch {
	case imageExts[ext]:
	case mediaExts[ext]:
		ref.Media = true
	case ref.Remote:
		// A web page: Kvit draws it as a card.
	default:
		return ImageRef{}, false
	}
	return ref, true
}

func lastUnescapedBar(s string) int {
	at := -1
	for i := 0; i < len(s); i++ {
		if s[i] == '\\' {
			i++
			continue
		}
		if s[i] == '|' {
			at = i
		}
	}
	return at
}

// Picture sizes in design pixels: the card drawn for media, a web address
// or a picture that cannot be shown, the gap between the line and the
// picture, and the caption's gap.
const (
	pictureCard   = 64
	pictureGap    = 8
	captionGap    = 4
	pictureRadius = 4
)

// picture is a picture loaded for drawing, or the reason there is none.
type picture struct {
	img    unison.Drawable
	failed string
}

// pictureFor loads the picture of an image block through LoadImage, once.
func (e *Editor) pictureFor(ref ImageRef) picture {
	if p, ok := e.pictures[ref.Path]; ok {
		return p
	}
	var p picture
	switch {
	case ref.Media:
		p.failed = "Media: " + path.Base(ref.Path)
	case ref.Remote && !imageExts[extensionOf(ref.Path)]:
		p.failed = "Web page: " + ref.Path
	case ref.Remote:
		p.failed = "Picture from the web, not loaded: " + ref.Path
	case e.LoadImage == nil:
		p.failed = "Picture: " + ref.Path
	default:
		img, err := e.LoadImage(ref.Path)
		if err != nil {
			p.failed = "Picture not found: " + ref.Path
		} else {
			p.img = img
		}
	}
	e.pictures[ref.Path] = p
	return p
}

// pictureSize is the size an image block's picture, or its card, is drawn
// at in a width.
func (e *Editor) pictureSize(ref ImageRef, width float32) geom.Size {
	if isEmbed(ref) {
		return geom.NewSize(width, e.px(embedHeight))
	}
	p := e.pictureFor(ref)
	if p.img == nil {
		return geom.NewSize(width, e.px(pictureCard))
	}
	natural := p.img.LogicalSize()
	w := natural.Width
	if ref.Width > 0 {
		w = e.px(float32(ref.Width))
	}
	w = min(w, width)
	if natural.Width <= 0 {
		return geom.NewSize(w, e.px(pictureCard))
	}
	return geom.NewSize(w, natural.Height*w/natural.Width)
}

// captionLayout is an image's caption laid out in a width, or nil.
func (e *Editor) captionLayout(ref ImageRef, width float32) *text.Layout {
	if ref.Caption == "" {
		return nil
	}
	st := e.chrome(kvitui.RoleSmall, text.Regular, e.tok().TextSecondary)
	return e.ui.Fonts.Layout([]text.Span{{Text: ref.Caption, Style: st}}, text.Options{MaxWidth: width})
}

// pictureBlock reports the block's image, and whether it shows its line as
// text (the caret is in it).
func (e *Editor) pictureBlock(i int) (ImageRef, bool, bool) {
	b := &e.Doc.Blocks[i]
	if b.Kind != Image && b.Kind != Media {
		return ImageRef{}, false, false
	}
	ref, ok := ParseImageLine(strings.TrimSpace(b.Text))
	if !ok {
		// A line edited out of the image form shows as text.
		return ImageRef{}, false, true
	}
	return ref, true, e.Doc.Focused && e.Doc.Caret.Block == b.ID
}

// pictureHeight is the height an image block adds below its line: the
// picture and its caption, and the gap above the picture when the line
// shows.
func (e *Editor) pictureHeight(i int, showsLine bool) float32 {
	ref, ok, _ := e.pictureBlock(i)
	if !ok {
		return 0
	}
	w := e.textWidth(&e.Doc.Blocks[i])
	h := e.pictureSize(ref, w).Height
	if c := e.captionLayout(ref, w); c != nil {
		_, ch := c.Size()
		h += e.px(captionGap) + ch
	}
	if showsLine {
		h += e.px(pictureGap)
	}
	return h
}

// drawPicture draws an image block's picture, or its card, and caption at a
// point.
func (e *Editor) drawPicture(gc *unison.Canvas, i int, at geom.Point) {
	ref, ok, _ := e.pictureBlock(i)
	if !ok {
		return
	}
	t := e.tok()
	w := e.textWidth(&e.Doc.Blocks[i])
	size := e.pictureSize(ref, w)
	r := geom.NewRect(at.X, at.Y, size.Width, size.Height)
	if card, _, ok := e.embedCard(i); ok {
		e.drawEmbed(gc, ref, card)
		return
	}
	p := e.pictureFor(ref)
	if p.img != nil {
		p.img.DrawInRect(gc, r, nil, nil)
	} else {
		rad := e.px(pictureRadius)
		e.fillRound(gc, r, rad, t.ChipBackground)
		e.stroke(gc, r, rad, e.px(1), t.Border)
		label := p.failed
		if ref.Alt != "" {
			label = ref.Alt + " · " + label
		}
		l := e.ui.Fonts.Layout([]text.Span{{Text: label, Style: e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)}},
			text.Options{MaxWidth: max(1, r.Width-2*e.px(pictureGap)), Elide: true})
		_, lh := l.Size()
		l.Draw(gc, r.X+e.px(pictureGap), r.Y+(r.Height-lh)/2)
	}
	if c := e.captionLayout(ref, w); c != nil {
		c.Draw(gc, at.X, r.Bottom()+e.px(captionGap))
	}
}
