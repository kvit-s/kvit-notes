package mermaid

import (
	"unicode/utf16"
)

// ParseColor reads a colour as colour's string constructor does, which is
// what the app reads classDef, style and box colours with. It accepts
// #rgb, #rrggbb, #aarrggbb (alpha first), #rrrgggbbb and #rrrrggggbbbb in
// either case, and the SVG colour names and "transparent" in any case, with
// spaces and tabs inside the name ignored. Anything else gives a Color that
// is not set.
func ParseColor(name string) Color {
	if name == "" {
		return Color{}
	}
	if name[0] == '#' {
		return hexColor(name)
	}
	return namedColor(name)
}

// ColorRGB is colour(r, g, b): an opaque colour, or a Color that is not set
// when a component is outside 0 to 255.
func ColorRGB(r, g, b int) Color {
	if r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 {
		return Color{}
	}
	return Color{R: uint8(r), G: uint8(g), B: uint8(b), A: 255, Set: true}
}

// utf16Len is the length of s in UTF-16 units, which is how colour measures
// the names it is given.
func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		n += utf16.RuneLen(r)
	}
	return n
}

// hexValue reads n hex digits, or gives -1 when one of them is not a hex
// digit.
func hexValue(digits []rune, n int) int {
	v := 0
	for _, r := range digits[:n] {
		v <<= 4
		switch {
		case r >= '0' && r <= '9':
			v |= int(r - '0')
		case r >= 'a' && r <= 'f':
			v |= int(r-'a') + 10
		case r >= 'A' && r <= 'F':
			v |= int(r-'A') + 10
		default:
			return -1
		}
	}
	return v
}

// hexColor reads `#` and 3, 6, 8, 9 or 12 hex digits. colour keeps 16 bits a
// channel, so each length is first widened to 16 bits and then brought down
// to 8 the way colour::red and the others do.
func hexColor(name string) Color {
	if utf16Len(name) > 13 {
		return Color{}
	}
	d := []rune(name)[1:]
	a, r, g, b := 0xffff, -1, -1, -1
	switch len(d) {
	case 12:
		r, g, b = hexValue(d[0:], 4), hexValue(d[4:], 4), hexValue(d[8:], 4)
	case 9:
		r, g, b = hexValue(d[0:], 3), hexValue(d[3:], 3), hexValue(d[6:], 3)
		if r < 0 || g < 0 || b < 0 {
			return Color{}
		}
		r, g, b = r<<4|r>>8, g<<4|g>>8, b<<4|b>>8
	case 8:
		a, r, g, b = hexValue(d[0:], 2)*0x101, hexValue(d[2:], 2)*0x101,
			hexValue(d[4:], 2)*0x101, hexValue(d[6:], 2)*0x101
	case 6:
		r, g, b = hexValue(d[0:], 2)*0x101, hexValue(d[2:], 2)*0x101, hexValue(d[4:], 2)*0x101
	case 3:
		r, g, b = hexValue(d[0:], 1)*0x1111, hexValue(d[1:], 1)*0x1111, hexValue(d[2:], 1)*0x1111
	}
	for _, v := range []int{a, r, g, b} {
		if v < 0 || v > 0xffff {
			return Color{}
		}
	}
	return Color{R: div257(r), G: div257(g), B: div257(b), A: div257(a), Set: true}
}

// div257 brings a 16-bit channel down to 8 bits, rounding x/257 to the
// nearest as the qt_div_257 does.
func div257(x int) uint8 { return uint8((x + 128 - (x+128)>>8) >> 8) }

// namedColor looks a name up in the SVG colour names.  drops spaces and
// tabs, lowers the case, and turns each character to Latin-1, where one
// outside Latin-1 becomes a NUL that ends the name: "red" followed by any
// such character is still red.
func namedColor(name string) Color {
	if utf16Len(name) > 255 {
		return Color{}
	}
	key := make([]byte, 0, len(name))
	for _, r := range name {
		if r == ' ' || r == '\t' {
			continue
		}
		if r == 0 || r > 0xff {
			break
		}
		if r >= 'A' && r <= 'Z' {
			r += 'a' - 'A'
		}
		key = append(key, byte(r))
	}
	return colorNames[string(key)]
}

// colorNames are the names colour::colorNames lists, with their values, as
//
//	6.10 gives them.
var colorNames = map[string]Color{
	"aliceblue":            {240, 248, 255, 255, true},
	"antiquewhite":         {250, 235, 215, 255, true},
	"aqua":                 {0, 255, 255, 255, true},
	"aquamarine":           {127, 255, 212, 255, true},
	"azure":                {240, 255, 255, 255, true},
	"beige":                {245, 245, 220, 255, true},
	"bisque":               {255, 228, 196, 255, true},
	"black":                {0, 0, 0, 255, true},
	"blanchedalmond":       {255, 235, 205, 255, true},
	"blue":                 {0, 0, 255, 255, true},
	"blueviolet":           {138, 43, 226, 255, true},
	"brown":                {165, 42, 42, 255, true},
	"burlywood":            {222, 184, 135, 255, true},
	"cadetblue":            {95, 158, 160, 255, true},
	"chartreuse":           {127, 255, 0, 255, true},
	"chocolate":            {210, 105, 30, 255, true},
	"coral":                {255, 127, 80, 255, true},
	"cornflowerblue":       {100, 149, 237, 255, true},
	"cornsilk":             {255, 248, 220, 255, true},
	"crimson":              {220, 20, 60, 255, true},
	"cyan":                 {0, 255, 255, 255, true},
	"darkblue":             {0, 0, 139, 255, true},
	"darkcyan":             {0, 139, 139, 255, true},
	"darkgoldenrod":        {184, 134, 11, 255, true},
	"darkgray":             {169, 169, 169, 255, true},
	"darkgreen":            {0, 100, 0, 255, true},
	"darkgrey":             {169, 169, 169, 255, true},
	"darkkhaki":            {189, 183, 107, 255, true},
	"darkmagenta":          {139, 0, 139, 255, true},
	"darkolivegreen":       {85, 107, 47, 255, true},
	"darkorange":           {255, 140, 0, 255, true},
	"darkorchid":           {153, 50, 204, 255, true},
	"darkred":              {139, 0, 0, 255, true},
	"darksalmon":           {233, 150, 122, 255, true},
	"darkseagreen":         {143, 188, 143, 255, true},
	"darkslateblue":        {72, 61, 139, 255, true},
	"darkslategray":        {47, 79, 79, 255, true},
	"darkslategrey":        {47, 79, 79, 255, true},
	"darkturquoise":        {0, 206, 209, 255, true},
	"darkviolet":           {148, 0, 211, 255, true},
	"deeppink":             {255, 20, 147, 255, true},
	"deepskyblue":          {0, 191, 255, 255, true},
	"dimgray":              {105, 105, 105, 255, true},
	"dimgrey":              {105, 105, 105, 255, true},
	"dodgerblue":           {30, 144, 255, 255, true},
	"firebrick":            {178, 34, 34, 255, true},
	"floralwhite":          {255, 250, 240, 255, true},
	"forestgreen":          {34, 139, 34, 255, true},
	"fuchsia":              {255, 0, 255, 255, true},
	"gainsboro":            {220, 220, 220, 255, true},
	"ghostwhite":           {248, 248, 255, 255, true},
	"gold":                 {255, 215, 0, 255, true},
	"goldenrod":            {218, 165, 32, 255, true},
	"gray":                 {128, 128, 128, 255, true},
	"green":                {0, 128, 0, 255, true},
	"greenyellow":          {173, 255, 47, 255, true},
	"grey":                 {128, 128, 128, 255, true},
	"honeydew":             {240, 255, 240, 255, true},
	"hotpink":              {255, 105, 180, 255, true},
	"indianred":            {205, 92, 92, 255, true},
	"indigo":               {75, 0, 130, 255, true},
	"ivory":                {255, 255, 240, 255, true},
	"khaki":                {240, 230, 140, 255, true},
	"lavender":             {230, 230, 250, 255, true},
	"lavenderblush":        {255, 240, 245, 255, true},
	"lawngreen":            {124, 252, 0, 255, true},
	"lemonchiffon":         {255, 250, 205, 255, true},
	"lightblue":            {173, 216, 230, 255, true},
	"lightcoral":           {240, 128, 128, 255, true},
	"lightcyan":            {224, 255, 255, 255, true},
	"lightgoldenrodyellow": {250, 250, 210, 255, true},
	"lightgray":            {211, 211, 211, 255, true},
	"lightgreen":           {144, 238, 144, 255, true},
	"lightgrey":            {211, 211, 211, 255, true},
	"lightpink":            {255, 182, 193, 255, true},
	"lightsalmon":          {255, 160, 122, 255, true},
	"lightseagreen":        {32, 178, 170, 255, true},
	"lightskyblue":         {135, 206, 250, 255, true},
	"lightslategray":       {119, 136, 153, 255, true},
	"lightslategrey":       {119, 136, 153, 255, true},
	"lightsteelblue":       {176, 196, 222, 255, true},
	"lightyellow":          {255, 255, 224, 255, true},
	"lime":                 {0, 255, 0, 255, true},
	"limegreen":            {50, 205, 50, 255, true},
	"linen":                {250, 240, 230, 255, true},
	"magenta":              {255, 0, 255, 255, true},
	"maroon":               {128, 0, 0, 255, true},
	"mediumaquamarine":     {102, 205, 170, 255, true},
	"mediumblue":           {0, 0, 205, 255, true},
	"mediumorchid":         {186, 85, 211, 255, true},
	"mediumpurple":         {147, 112, 219, 255, true},
	"mediumseagreen":       {60, 179, 113, 255, true},
	"mediumslateblue":      {123, 104, 238, 255, true},
	"mediumspringgreen":    {0, 250, 154, 255, true},
	"mediumturquoise":      {72, 209, 204, 255, true},
	"mediumvioletred":      {199, 21, 133, 255, true},
	"midnightblue":         {25, 25, 112, 255, true},
	"mintcream":            {245, 255, 250, 255, true},
	"mistyrose":            {255, 228, 225, 255, true},
	"moccasin":             {255, 228, 181, 255, true},
	"navajowhite":          {255, 222, 173, 255, true},
	"navy":                 {0, 0, 128, 255, true},
	"oldlace":              {253, 245, 230, 255, true},
	"olive":                {128, 128, 0, 255, true},
	"olivedrab":            {107, 142, 35, 255, true},
	"orange":               {255, 165, 0, 255, true},
	"orangered":            {255, 69, 0, 255, true},
	"orchid":               {218, 112, 214, 255, true},
	"palegoldenrod":        {238, 232, 170, 255, true},
	"palegreen":            {152, 251, 152, 255, true},
	"paleturquoise":        {175, 238, 238, 255, true},
	"palevioletred":        {219, 112, 147, 255, true},
	"papayawhip":           {255, 239, 213, 255, true},
	"peachpuff":            {255, 218, 185, 255, true},
	"peru":                 {205, 133, 63, 255, true},
	"pink":                 {255, 192, 203, 255, true},
	"plum":                 {221, 160, 221, 255, true},
	"powderblue":           {176, 224, 230, 255, true},
	"purple":               {128, 0, 128, 255, true},
	"red":                  {255, 0, 0, 255, true},
	"rosybrown":            {188, 143, 143, 255, true},
	"royalblue":            {65, 105, 225, 255, true},
	"saddlebrown":          {139, 69, 19, 255, true},
	"salmon":               {250, 128, 114, 255, true},
	"sandybrown":           {244, 164, 96, 255, true},
	"seagreen":             {46, 139, 87, 255, true},
	"seashell":             {255, 245, 238, 255, true},
	"sienna":               {160, 82, 45, 255, true},
	"silver":               {192, 192, 192, 255, true},
	"skyblue":              {135, 206, 235, 255, true},
	"slateblue":            {106, 90, 205, 255, true},
	"slategray":            {112, 128, 144, 255, true},
	"slategrey":            {112, 128, 144, 255, true},
	"snow":                 {255, 250, 250, 255, true},
	"springgreen":          {0, 255, 127, 255, true},
	"steelblue":            {70, 130, 180, 255, true},
	"tan":                  {210, 180, 140, 255, true},
	"teal":                 {0, 128, 128, 255, true},
	"thistle":              {216, 191, 216, 255, true},
	"tomato":               {255, 99, 71, 255, true},
	"transparent":          {0, 0, 0, 0, true},
	"turquoise":            {64, 224, 208, 255, true},
	"violet":               {238, 130, 238, 255, true},
	"wheat":                {245, 222, 179, 255, true},
	"white":                {255, 255, 255, 255, true},
	"whitesmoke":           {245, 245, 245, 255, true},
	"yellow":               {255, 255, 0, 255, true},
	"yellowgreen":          {154, 205, 50, 255, true},
}
