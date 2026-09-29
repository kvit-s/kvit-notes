package mathtex

import (
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"math"
	"os"
	"sync"

	cfont "github.com/richardwilkes/canvas/font"
	"github.com/richardwilkes/canvas/textblob"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/strokecap"
	"github.com/richardwilkes/unison/enums/strokejoin"
)

// The operations of the library's drawing commands (KVITMATH_OP_* in
// mathtex/native/kvitmath.h).
const (
	opFont  = 1
	opGlyph = 2
	opLine  = 3
	opRect  = 4
	opText  = 5
)

// command is one drawing command of a formula, its coordinates in the
// formula's own space, with its top-left corner at the origin.
type command struct {
	op     uint8
	color  uint32      // 0xAARRGGBB, or 0 for the foreground colour
	m      geom.Matrix // the transform in force
	font   *mathFont   // opGlyph
	char   rune        // opGlyph
	x, y   float32     // opGlyph, opText: the origin on the baseline; opLine: one end; opRect: the corner
	size   float32     // opGlyph, opText: the font size before the transform
	x2, y2 float32     // opLine: the other end
	w, h   float32     // opRect
	rx, ry float32     // opRect: the corners' radii
	filled bool        // opRect
	stroke float32     // opLine and an opRect not filled: the line width
	cap    uint8       // 0 butt, 1 round, 2 square
	join   uint8       // 0 bevel, 1 miter, 2 round
	style  uint8       // opText: 1 bold, 2 italic
	family string      // opText
	text   string      // opText
}

// reader reads the little-endian command stream.
type reader struct {
	b   []byte
	err error
}

func (r *reader) take(n int) []byte {
	if r.err != nil || len(r.b) < n {
		r.err = errors.New("math library: drawing commands cut short")
		return make([]byte, n)
	}
	v := r.b[:n]
	r.b = r.b[n:]
	return v
}

func (r *reader) u8() uint8   { return r.take(1)[0] }
func (r *reader) u16() uint16 { return binary.LittleEndian.Uint16(r.take(2)) }
func (r *reader) u32() uint32 { return binary.LittleEndian.Uint32(r.take(4)) }
func (r *reader) f32() float32 {
	return math.Float32frombits(r.u32())
}
func (r *reader) str(n int) string { return string(r.take(n)) }

// parseCommands reads the library's drawing commands and loads the font
// files they name.
func parseCommands(data []byte) ([]command, error) {
	r := &reader{b: data}
	fonts := map[uint16]*mathFont{}
	var cmds []command
	for len(r.b) > 0 && r.err == nil {
		op := r.u8()
		if op == opFont {
			id := r.u16()
			path := r.str(int(r.u16()))
			if r.err == nil {
				fonts[id] = loadFont(path)
			}
			continue
		}
		c := command{op: op, color: r.u32()}
		c.m = geom.Matrix{ScaleX: r.f32(), SkewY: r.f32(), SkewX: r.f32(), ScaleY: r.f32(), TransX: r.f32(), TransY: r.f32()}
		switch op {
		case opGlyph:
			c.font = fonts[r.u16()]
			c.char = rune(r.u32())
			c.x, c.y, c.size = r.f32(), r.f32(), r.f32()
		case opLine:
			c.x, c.y, c.x2, c.y2 = r.f32(), r.f32(), r.f32(), r.f32()
			c.stroke, c.cap, c.join = r.f32(), r.u8(), r.u8()
		case opRect:
			c.x, c.y, c.w, c.h, c.rx, c.ry = r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32()
			c.filled = r.u8() != 0
			c.stroke, c.cap, c.join = r.f32(), r.u8(), r.u8()
		case opText:
			c.x, c.y, c.size, c.style = r.f32(), r.f32(), r.f32(), r.u8()
			nf, nt := int(r.u16()), int(r.u32())
			c.family, c.text = r.str(nf), r.str(nt)
		default:
			return nil, fmt.Errorf("math library: unknown drawing command %d", op)
		}
		cmds = append(cmds, c)
	}
	return cmds, r.err
}

// ---- the font files ----

// mathFont is one of MicroTeX's font files, loaded once, with a canvas font
// per size it is drawn at.
type mathFont struct {
	path  string
	tf    *cfont.Typeface // nil when the file could not be read
	sized map[float32]*cfont.Font
}

var (
	fontsMu   sync.Mutex
	fontFiles = map[string]*mathFont{}
)

// loadFont returns the font file at path, reading it the first time.
func loadFont(path string) *mathFont {
	fontsMu.Lock()
	defer fontsMu.Unlock()
	if f, ok := fontFiles[path]; ok {
		return f
	}
	f := &mathFont{path: path, sized: map[float32]*cfont.Font{}}
	if data, err := os.ReadFile(path); err == nil {
		f.tf, _ = cfont.NewTypefaceFromData(data, 0)
	}
	fontFiles[path] = f
	return f
}

// at is the canvas font at size pixels. Glyphs are placed where MicroTeX put
// them, as the app drew them as outlines: no hinting, and positions and
// the baseline kept at fractions of a pixel.
func (f *mathFont) at(size float32) *cfont.Font {
	fontsMu.Lock()
	defer fontsMu.Unlock()
	if cf, ok := f.sized[size]; ok {
		return cf
	}
	if len(f.sized) > 256 {
		clear(f.sized)
	}
	cf := cfont.NewFont(f.tf, size, 1, 0)
	cf.SetSubpixel(true)
	cf.SetHinting(cfont.HintingNone)
	cf.SetBaselineSnap(false)
	f.sized[size] = cf
	return cf
}

// glyph is the glyph for a character through the font's character map. The
// NewTX fonts hold the TeX slots below 33 at U+E000 + slot, where the library
// records them; a font with the slot itself is looked up there too.
func (f *mathFont) glyph(c rune) uint16 {
	g := f.tf.UnicharToGlyph(c)
	if g == 0 && c >= 0xE000 && c < 0xE000+33 {
		g = f.tf.UnicharToGlyph(c - 0xE000)
	}
	return g
}

// ---- drawing ----

// colour is the colour a command draws in.
func (c *command) colour(fg unison.Color) unison.Color {
	if c.color == 0 {
		return fg
	}
	return unison.Color(c.color)
}

// uniformScale is how much a transform enlarges when it only enlarges and
// moves, or 0 when it also rotates, skews or stretches one way more.
func uniformScale(m geom.Matrix) float32 {
	const eps = 1e-4
	if abs32(m.SkewX) > eps*abs32(m.ScaleY) || abs32(m.SkewY) > eps*abs32(m.ScaleY) ||
		abs32(m.ScaleX-m.ScaleY) > eps*abs32(m.ScaleY) || m.ScaleY <= 0 {
		return 0
	}
	return m.ScaleY
}

func abs32(v float32) float32 { return float32(math.Abs(float64(v))) }

// Draw draws the formula with the top-left corner of its box at (x, y); its
// baseline is Baseline below y. Everything drawn in the default colour takes
// fg, and colours the TeX sets with \color are kept. Text the math fonts do
// not cover is drawn with the fonts given to UseFonts, and left out without
// them.
func (f *Formula) Draw(gc *unison.Canvas, x, y float32, fg unison.Color) {
	if f == nil || len(f.cmds) == 0 {
		return
	}
	gc.Save()
	defer gc.Restore()
	gc.Translate(geom.NewPoint(x, y))
	for i := 0; i < len(f.cmds); {
		c := &f.cmds[i]
		switch c.op {
		case opGlyph:
			i += drawGlyphs(gc, f.cmds[i:], fg)
			continue
		case opLine:
			paint := strokePaint(c, fg)
			gc.Save()
			gc.Concat(c.m)
			gc.DrawLine(geom.NewPoint(c.x, c.y), geom.NewPoint(c.x2, c.y2), paint)
			gc.Restore()
		case opRect:
			var paint *unison.Paint
			if c.filled {
				paint = c.colour(fg).Paint(gc, geom.Rect{}, paintstyle.Fill)
			} else {
				paint = strokePaint(c, fg)
			}
			rect := geom.NewRect(c.x, c.y, c.w, c.h)
			gc.Save()
			gc.Concat(c.m)
			if c.rx > 0 || c.ry > 0 {
				gc.DrawRoundedRect(rect, geom.NewSize(c.rx, c.ry), paint)
			} else {
				gc.DrawRect(rect, paint)
			}
			gc.Restore()
		case opText:
			drawText(gc, c, fg)
		}
		i++
	}
}

func strokePaint(c *command, fg unison.Color) *unison.Paint {
	paint := c.colour(fg).Paint(nil, geom.Rect{}, paintstyle.Stroke)
	paint.SetStrokeWidth(c.stroke)
	switch c.cap {
	case 1:
		paint.SetStrokeCap(strokecap.Round)
	case 2:
		paint.SetStrokeCap(strokecap.Square)
	default:
		paint.SetStrokeCap(strokecap.Butt)
	}
	switch c.join {
	case 0:
		paint.SetStrokeJoin(strokejoin.Bevel)
	case 1:
		paint.SetStrokeJoin(strokejoin.Miter)
	default:
		paint.SetStrokeJoin(strokejoin.Round)
	}
	return paint
}

// glyphPlace is where a glyph command draws in the formula's space: the
// transform from the glyph's em square, and its pixel size and origin when
// that transform only enlarges and moves.
func glyphPlace(c *command) (m geom.Matrix, size float32, at geom.Point) {
	m = c.m.Multiply(geom.NewTranslationMatrix(c.x, c.y)).Multiply(geom.NewScaleMatrix(c.size, c.size))
	if s := uniformScale(m); s > 0 {
		return m, s, geom.NewPoint(m.TransX, m.TransY)
	}
	return m, 0, geom.Point{}
}

// drawGlyphs draws the glyph command at the start of cmds, and with it every
// following one in the same font, size and colour as one run, and returns how
// many it drew.
func drawGlyphs(gc *unison.Canvas, cmds []command, fg unison.Color) int {
	first := &cmds[0]
	m, size, at := glyphPlace(first)
	if first.font == nil || first.font.tf == nil {
		return 1
	}
	if size == 0 {
		// Rotated, skewed or stretched: drawn under its own transform, at
		// the size the transform enlarges by on average.
		k := float32(math.Sqrt(math.Abs(float64(m.ScaleX*m.ScaleY - m.SkewX*m.SkewY))))
		if k <= 0 {
			return 1
		}
		gc.Save()
		gc.Concat(m.Multiply(geom.NewScaleMatrix(1/k, 1/k)))
		b := textblob.NewBuilder()
		run := b.AllocRunPos(first.font.at(k), 1, nil)
		run.Glyphs[0] = first.font.glyph(first.char)
		if blob := b.Make(); blob != nil {
			gc.DrawTextBlob(blob, geom.Point{}, first.colour(fg).Paint(gc, geom.Rect{}, paintstyle.Fill))
		}
		gc.Restore()
		return 1
	}
	n := 1
	points := []geom.Point{at}
	for n < len(cmds) {
		c := &cmds[n]
		if c.op != opGlyph || c.font != first.font || c.color != first.color {
			break
		}
		_, s, p := glyphPlace(c)
		if s != size {
			break
		}
		points = append(points, p)
		n++
	}
	b := textblob.NewBuilder()
	run := b.AllocRunPos(first.font.at(size), n, nil)
	for i := range n {
		run.Glyphs[i] = first.font.glyph(cmds[i].char)
		run.Pos[2*i] = points[i].X
		run.Pos[2*i+1] = points[i].Y
	}
	if blob := b.Make(); blob != nil {
		gc.DrawTextBlob(blob, geom.Point{}, first.colour(fg).Paint(gc, geom.Rect{}, paintstyle.Fill))
	}
	return n
}

// Image draws tex into a new image, as the app's MathRenderer::render
// rasterised a formula: sizePx pixels per em, in fg, dpr device pixels per
// logical pixel, with vpad transparent logical pixels above and below and
// hpad left and right (SideBearingPadding says how much keeps overhanging
// glyphs inside). The formula is placed hpad from the left edge, so a caller
// that lines it up with a box shifts it left by hpad.
//
// Blank TeX gives no image and no error. The ratio is capped at
// MaxDevicePixelRatio and lowered further until the image fits in
// MaxRasterEdge pixels a side and MaxRasterPixels in all, because the size,
// the ratio and the formula's length all multiply into the image and a note
// sets them.
func Image(tex string, sizePx int, fg unison.Color, dpr float64, vpad, hpad int, display bool) (*image.NRGBA, error) {
	if normalizedTeX(tex) == "" {
		return nil, nil
	}
	f, err := Render(tex, sizePx, display)
	if err != nil {
		return nil, err
	}
	vpad, hpad = max(0, vpad), max(0, hpad)
	logicalW := f.Width + float64(2*hpad)
	logicalH := f.Height + float64(2*vpad)
	ratio := 1.0
	if dpr > 0 {
		ratio = min(dpr, MaxDevicePixelRatio)
	}
	byEdge := min(MaxRasterEdge/max(logicalW, 1), MaxRasterEdge/max(logicalH, 1))
	byArea := math.Sqrt(MaxRasterPixels / max(logicalW*logicalH, 1))
	ratio = max(0.01, min(ratio, byEdge, byArea))
	w := max(1, int(math.Round(logicalW*ratio)))
	h := max(1, int(math.Round(logicalH*ratio)))
	img, err := unison.NewImageFromDrawing(w, h, 72, func(gc *unison.Canvas) {
		gc.Scale(geom.NewPoint(float32(ratio), float32(ratio)))
		f.Draw(gc, float32(hpad), float32(vpad), fg)
	})
	if err != nil {
		return nil, err
	}
	return img.ToNRGBA()
}
