package diagram

// parseCssColor of the app's src/content/diagrams/sequencelayout.cpp: the
// colour a sequence diagram's `rect` block names.

import (
	"math"
	"strconv"
	"strings"

	"github.com/kvit-s/kvit-notes/mermaid"
)

// parseCSSColor reads `rgb(r, g, b)`, `rgba(r, g, b, a)`, or anything
// mermaid.ParseColor reads. Empty text and "transparent" give a colour that
// is not set, so the block keeps the theme's tint.
func parseCSSColor(raw string) mermaid.Color {
	s := strings.TrimSpace(raw)
	if s == "" || strings.EqualFold(s, "transparent") {
		return mermaid.Color{}
	}
	if len(s) >= 3 && strings.EqualFold(s[:3], "rgb") {
		open, end := strings.IndexByte(s, '('), strings.IndexByte(s, ')')
		if open < 0 || end <= open {
			return mermaid.Color{}
		}
		var parts []string
		for _, p := range strings.Split(s[open+1:end], ",") {
			if p != "" {
				parts = append(parts, p)
			}
		}
		if len(parts) < 3 {
			return mermaid.Color{}
		}
		var rgb [3]int
		for k := range rgb {
			v, err := strconv.Atoi(strings.TrimSpace(parts[k]))
			if err != nil {
				return mermaid.Color{}
			}
			rgb[k] = v
		}
		c := mermaid.ColorRGB(rgb[0], rgb[1], rgb[2])
		if c.Set && len(parts) >= 4 {
			// the toDouble reads no hexadecimal float, which ParseFloat
			// does, and the clamp turns NaN into 0.
			alpha := strings.TrimSpace(parts[3])
			if a, err := strconv.ParseFloat(alpha, 64); err == nil && !strings.ContainsAny(alpha, "xX_") {
				if math.IsNaN(a) {
					a = 0
				}
				c.A = uint8(math.Round(max(0, min(1, a)) * 255))
			}
		}
		return c
	}
	return mermaid.ParseColor(s)
}
