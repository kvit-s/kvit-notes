package diagram

// Ceilings for what a diagram can ask the process to allocate, a port of the
// app's src/content/diagrams/diagrambudget.h. A note is untrusted input:
// it arrives by import, paste or sync, and its Mermaid source, arrangement
// comment and TeX feed sizes straight into memory. Without a ceiling an
// arrangement comment reading `%% mermaid-flow:pos B=1e12,1e12` makes a scene
// 10^12 pixels across, and a 400,000-character formula a raster of 134 MiB.
//
// The numbers are far above any real diagram. The largest fixture in the
// repository lays out under 4,000 pixels across, and the canvas cites a
// 15,000-pixel flowchart as its long case, so MaxSceneSpan leaves more than
// ten times that. The character-art limits of the same header belong to
// package textdiagram, and the largest pinned node centre is
// mermaid.MaxPinnedCoordinate, because the parser clamps to it.

const (
	// MaxSceneSpan is the largest scene extent, in pixels, along either axis
	// that the editor gives a diagram on screen.
	MaxSceneSpan = 500000.0

	// MaxRasterPixels is the most pixels a picture of one diagram or formula
	// may have: 256 MiB at four bytes a pixel.
	MaxRasterPixels = 64 * 1024 * 1024

	// MaxRasterEdge is the longest side, in pixels, of such a picture.
	MaxRasterEdge = 32768

	// MaxTexChars is the longest TeX source, in characters, a math label may
	// have. Layout draws a longer one as its source without typesetting it.
	MaxTexChars = 8192

	// MaxTextSizePx and MaxDevicePixelRatio bound the two settings that
	// multiply a formula's raster size.
	MaxTextSizePx       = 512
	MaxDevicePixelRatio = 8.0
)
