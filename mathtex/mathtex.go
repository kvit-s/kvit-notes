// Package mathtex typesets LaTeX math for Kvit Notes: the formulas of display
// math blocks ($$…$$) and of inline spans ($…$). It is the port of the Qt
// app's src/content/mathrenderer.{h,cpp}.
//
// The typesetting is done by MicroTeX, the C++ engine the Qt app vendored,
// which stays C++: it is built as a shared library (kvitmath.dll,
// libkvitmath.dylib, libkvitmath.so, from third_party/microtex and
// mathtex/native by tools/build-mathlib.sh) that this package loads at run
// time and calls without cgo, through purego on Linux and macOS and through
// the system's DLL loader on Windows. The library lays a formula out and
// records what drawing it would do: which character of which font file goes
// where, and the lines and rectangles. This package draws those on a unison
// canvas with the same font files. When the library or its resources are not
// found, math is off: Available is false, LoadError says why, and the editor
// shows the TeX source.
//
// MicroTeX is not thread-safe; every call into it holds one mutex. Rendered
// formulas are cached by TeX, size and style.
package mathtex

import (
	"errors"
	"fmt"
	"math"
	"strings"
	"sync"
	"unicode/utf16"
)

// Ceilings on what a formula can ask for, from the Qt app's
// src/content/diagrams/diagrambudget.h. A note is untrusted input, and its TeX
// feeds sizes straight into layout and into rasters.
const (
	// MaxTeXChars is the longest TeX source laid out, in UTF-16 code units
	// as Qt counts them. Real formulas are a line or two.
	MaxTeXChars = 8192
	// MaxTextSize is the largest size a formula is set at, in pixels per em.
	MaxTextSize = 512
	// MaxDevicePixelRatio, MaxRasterPixels and MaxRasterEdge bound the
	// images Image makes.
	MaxDevicePixelRatio = 8.0
	MaxRasterPixels     = 64 * 1024 * 1024
	MaxRasterEdge       = 32768
)

// Metrics is a formula's layout, in logical pixels.
type Metrics struct {
	// Width and Height are the formula's box, rounded up to whole pixels so
	// an image of that size holds all of it.
	Width, Height float64
	// Baseline is the distance from the top of the box down to the line the
	// formula stands on, unrounded: drawing puts the baseline exactly there.
	Baseline float64
	// Ascent is Baseline, and Descent the rest of Height below it.
	Ascent, Descent float64
	// Depth is how far the formula reaches below its baseline.
	Depth float64
	// Valid is false when the TeX does not parse; Error then says why.
	Valid bool
	Error string
}

// Formula is a laid-out formula, ready to draw.
type Formula struct {
	Metrics
	// TeX, Size and Display are what it was rendered from.
	TeX     string
	Size    int
	Display bool
	cmds    []command
}

// normalizedTeX turns the spaces ChatGPT puts around operators (U+202F,
// U+00A0, U+2009, U+200A), which MicroTeX rejects, into ASCII spaces, and
// trims the ends. The Markdown importer rewrites them too; this covers math
// typed or pasted into the editor.
func normalizedTeX(tex string) string {
	tex = strings.Map(func(r rune) rune {
		switch r {
		case 0x202F, 0x00A0, 0x2009, 0x200A:
			return ' '
		}
		return r
	}, tex)
	return strings.TrimSpace(tex)
}

// utf16Len is the length Qt gives a string: its UTF-16 code units.
func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		n += utf16.RuneLen(r)
	}
	return n
}

// errTooLong is the error for a formula over MaxTeXChars.
func errTooLong(n int) error {
	return fmt.Errorf("Formula is too long (%d characters; the limit is %d)", n, MaxTeXChars)
}

// boundedSize is the size a formula is set at: 20 when none is given, and
// never above MaxTextSize.
func boundedSize(sizePx int) int {
	if sizePx <= 0 {
		return 20
	}
	return min(sizePx, MaxTextSize)
}

type cacheKey struct {
	tex     string
	size    int
	display bool
}

type cached struct {
	f   *Formula
	err error
}

// maxCached is how many rendered formulas the cache keeps. A table renders
// every formula in it again on each keystroke in any of its cells; the cache
// makes that a lookup.
const maxCached = 512

var (
	cacheMu    sync.Mutex
	cache      = map[cacheKey]cached{}
	cacheOrder []cacheKey
	cacheGen   int // counts clearCache calls
)

// Render lays tex out at sizePx pixels per em, in display style (a $$…$$
// block: limits over and under large operators, full-size fractions) or in
// text style (a $…$ span: limits beside the operator, a formula that fits a
// line of text). The result is cached. Blank TeX renders as an empty formula
// with no error; TeX that does not parse, or is longer than MaxTeXChars,
// returns the error MicroTeX gives, and so does every call while math is off.
func Render(tex string, sizePx int, display bool) (*Formula, error) {
	key := cacheKey{tex, sizePx, display}
	cacheMu.Lock()
	if c, ok := cache[key]; ok {
		cacheMu.Unlock()
		return c.f, c.err
	}
	gen := cacheGen
	cacheMu.Unlock()

	f, err := render(tex, sizePx, display)
	// An engine that is off is not a property of the formula, and the
	// error is not kept; nor is a formula laid out while the fonts changed.
	if f != nil || !errors.Is(err, errOff) {
		cacheMu.Lock()
		if _, ok := cache[key]; !ok && gen == cacheGen {
			cache[key] = cached{f, err}
			cacheOrder = append(cacheOrder, key)
			for len(cacheOrder) > maxCached {
				delete(cache, cacheOrder[0])
				cacheOrder = cacheOrder[1:]
			}
		}
		cacheMu.Unlock()
	}
	return f, err
}

// clearCache forgets every rendered formula, for when UseFonts changes the
// fonts that text without a math font is measured with.
func clearCache() {
	cacheMu.Lock()
	cache = map[cacheKey]cached{}
	cacheOrder = nil
	cacheGen++
	cacheMu.Unlock()
}

// errOff wraps LoadError in the errors of Render while math is off.
var errOff = errors.New("math is off")

func render(tex string, sizePx int, display bool) (*Formula, error) {
	trimmed := normalizedTeX(tex)
	size := boundedSize(sizePx)
	f := &Formula{TeX: tex, Size: size, Display: display}
	if trimmed == "" {
		f.Valid = true
		return f, nil
	}
	// Layout cost grows with the source, and a note can hold a formula of
	// any length; past the limit it is an error the block shows.
	if n := utf16Len(trimmed); n > MaxTeXChars {
		return nil, errTooLong(n)
	}
	e, err := load()
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errOff, err)
	}
	r, data, err := e.layout(trimmed, size, display, true)
	if err != nil {
		return nil, err
	}
	if f.cmds, err = parseCommands(data); err != nil {
		return nil, err
	}
	// MicroTeX's whole-pixel getters truncate, but drawing places the formula
	// from the unrounded box, so the baseline is exactly the unrounded value
	// below the top. The extents round up, so a raster that size holds the
	// last row and column of ink.
	f.Width = max(1, math.Ceil(float64(r.width)))
	f.Height = max(1, math.Ceil(float64(r.height)))
	f.Baseline = min(max(0, float64(r.baseline)), f.Height)
	f.Ascent = f.Baseline
	f.Descent = max(0, f.Height-f.Baseline)
	f.Depth = max(0, float64(r.depth))
	f.Valid = true
	return f, nil
}

// Measure is a formula's layout at sizePx in display or text style, as Render
// makes it. Blank TeX is valid with zero size; TeX that does not parse is not
// valid, with the error.
func Measure(tex string, sizePx int, display bool) Metrics {
	f, err := Render(tex, sizePx, display)
	if err != nil {
		return Metrics{Error: err.Error()}
	}
	return f.Metrics
}

// ErrorFor is "" when tex renders, else why not: what the block shows beside
// the source instead of the formula. Blank TeX is not an error; it renders
// nothing.
func ErrorFor(tex string) string {
	m := Measure(tex, 20, true)
	if m.Valid || strings.TrimSpace(tex) == "" {
		return ""
	}
	if m.Error == "" {
		return "Unrenderable expression"
	}
	return m.Error
}

var (
	commandsOnce sync.Once
	commandList  []string
)

// Commands is every command name a user can type, from MicroTeX's tables of
// symbols and macros (the \newcommand definitions and the NewTX additions
// included), without the backslash, sorted: what the math-command menu
// completes from. Internal names, those holding '@', are left out. It is nil
// while math is off.
func Commands() []string {
	commandsOnce.Do(func() {
		e, err := load()
		if err != nil {
			return
		}
		commandList, _ = e.commandNames()
	})
	return commandList
}

// SideBearingPadding is the transparent margin, in logical pixels, a formula
// drawn at textSizePx needs left and right so no glyph is cut off at its own
// edges.
//
// MicroTeX sizes a formula to its typographic advance width, which is not the
// area its glyphs cover: the italic f carries its tail left of the origin and
// its hook right of the advance, and other letters overhang by less. Measured
// across the NewTX/XCharter reference formulas at 15, 17, 20 and 32 px, the
// ink reaches at most 0.067 of the text size left of the box (x^2) and 0.200
// right of it (the script-size f in A_f). The margin is a little above the
// worst case on both sides and scales with the size.
func SideBearingPadding(textSizePx int) int {
	const maxOverhangPerEm = 0.22
	return max(2, int(math.Ceil(float64(max(1, textSizePx))*maxOverhangPerEm)))
}

var (
	mathXOnce     sync.Once
	mathXHeightEm float64
)

// OpticalMathSize is the size, in pixels, to set math at beside text of
// textSizePx whose font has an x-height of textXHeightPx, so both look the
// same size: the math font's x-height (Charter math 0.442 em, Computer Modern
// 0.4305 em, well below an interface sans-serif's 0.5 em) is matched to the
// text's, as LaTeX's mathscale does when math is paired with an unrelated
// text face. It never shrinks the math below the text size and enlarges it at
// most 1.5 times. TextXHeight measures textXHeightPx.
func OpticalMathSize(textSizePx int, textXHeightPx float64) int {
	size := 15.0
	if textSizePx > 0 {
		size = float64(textSizePx)
	}
	// The height of a lowercase x in the math font is its x-height; which
	// font that is is fixed once the engine starts, so the value per em is
	// too.
	mathXOnce.Do(func() {
		mathXHeightEm = 0.4305
		if m := Measure("x", 256, true); m.Valid && m.Height > 0 {
			mathXHeightEm = m.Height / 256
		}
	})
	if mathXHeightEm <= 0 || textXHeightPx <= 0 {
		return int(math.Round(size))
	}
	scale := min(max(textXHeightPx/size/mathXHeightEm, 1), 1.5)
	return int(math.Round(size * scale))
}
