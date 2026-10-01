package query

// Case-insensitive string handling for queries. Comparing, searching and
// matching prefixes case-folds each character (Unicode simple case folding,
// as Unicode 16.0 defines it) and compares the folded strings as UTF-16 code
// units. That is how the earlier Qt version of Kvit Notes compared them, so a
// query sorts and matches the same notes it did there.

import (
	"unicode"
	"unicode/utf8"
)

// foldOverrides pins the characters where Go's Unicode tables and Unicode
// 16.0 disagree about simple case folding. Go 1.25 has Unicode 15.0 and
// lacks the folds added since; Go 1.27 has Unicode 17.0 and has folds 16.0
// does not have yet. Neither Go version links U+1FD3, U+1FE3 or U+FB05 to
// their partners through unicode.SimpleFold. The list was made by comparing
// foldRune with Unicode 16.0's folding for every code point.
var foldOverrides = func() map[rune]rune {
	m := map[rune]rune{
		0x1FD3: 0x0390, 0x1FE3: 0x03B0, 0xFB05: 0xFB06,
		0x1C89: 0x1C8A, 0xA7CB: 0x0264, 0xA7CC: 0xA7CD,
		0xA7DA: 0xA7DB, 0xA7DC: 0x019B,
		// Unicode 17 pairs, which Unicode 16.0 does not fold.
		0xA7CE: 0xA7CE, 0xA7D2: 0xA7D2, 0xA7D4: 0xA7D4,
	}
	// Garay capitals fold to the small letters 0x20 above them (Unicode 16).
	for r := rune(0x10D50); r <= 0x10D65; r++ {
		m[r] = r + 0x20
	}
	// Beria Erfe capitals (Unicode 17), which Unicode 16.0 leaves alone.
	for r := rune(0x16EA0); r <= 0x16EB8; r++ {
		m[r] = r
	}
	return m
}()

// foldRune is the one character that r and every character equal to it
// ignoring case fold to. For almost every letter that is its lowercase form;
// Cherokee letters fold to their uppercase form.
func foldRune(r rune) rune {
	if r < utf8.RuneSelf {
		if 'A' <= r && r <= 'Z' {
			return r + 'a' - 'A'
		}
		return r
	}
	if f, ok := foldOverrides[r]; ok {
		return f
	}
	if unicode.SimpleFold(r) == r {
		return r
	}
	if (r >= 0x13A0 && r <= 0x13FD) || (r >= 0xAB70 && r <= 0xABBF) {
		return unicode.ToUpper(r)
	}
	return unicode.ToLower(unicode.ToUpper(r))
}

// utf16Lead is the first UTF-16 code unit of r: r itself inside the Basic
// Multilingual Plane, its high surrogate above it.
func utf16Lead(r rune) rune {
	if r >= 0x10000 {
		return 0xD800 + (r-0x10000)>>10
	}
	return r
}

// compareUTF16Runes orders two characters the way comparing their UTF-16
// encodings does. That differs from comparing code points only for
// U+E000..U+FFFF against characters above U+FFFF, whose surrogates sort
// lower.
func compareUTF16Runes(a, b rune) int {
	la, lb := utf16Lead(a), utf16Lead(b)
	switch {
	case la < lb:
		return -1
	case la > lb:
		return 1
	case a < b:
		return -1
	case a > b:
		return 1
	}
	return 0
}

// compareFold compares a and b ignoring case, giving -1, 0 or 1. The first
// character that differs after case folding decides; when one string is a
// prefix of the other, the shorter one orders first.
func compareFold(a, b string) int {
	for a != "" && b != "" {
		ra, na := utf8.DecodeRuneInString(a)
		rb, nb := utf8.DecodeRuneInString(b)
		a, b = a[na:], b[nb:]
		if ra == rb {
			continue
		}
		if c := compareUTF16Runes(foldRune(ra), foldRune(rb)); c != 0 {
			return c
		}
	}
	switch {
	case a == "" && b == "":
		return 0
	case a == "":
		return -1
	}
	return 1
}

// compareUTF16 orders two strings by their UTF-16 code units, case
// sensitively, which is how SortNotes orders note paths.
func compareUTF16(a, b string) int {
	for a != "" && b != "" {
		ra, na := utf8.DecodeRuneInString(a)
		rb, nb := utf8.DecodeRuneInString(b)
		a, b = a[na:], b[nb:]
		if c := compareUTF16Runes(ra, rb); c != 0 {
			return c
		}
	}
	switch {
	case a == "" && b == "":
		return 0
	case a == "":
		return -1
	}
	return 1
}

// foldedRunes is s as case-folded characters.
func foldedRunes(s string) []rune {
	out := make([]rune, 0, len(s))
	for _, r := range s {
		out = append(out, foldRune(r))
	}
	return out
}

// containsFold reports whether s contains needle, ignoring case.
func containsFold(s, needle string) bool {
	if needle == "" {
		return true
	}
	h, n := foldedRunes(s), foldedRunes(needle)
	for i := 0; i+len(n) <= len(h); i++ {
		match := true
		for j := range n {
			if h[i+j] != n[j] {
				match = false
				break
			}
		}
		if match {
			return true
		}
	}
	return false
}

// hasPrefixFold reports whether s starts with prefix, ignoring case.
func hasPrefixFold(s, prefix string) bool {
	for prefix != "" {
		if s == "" {
			return false
		}
		rs, ns := utf8.DecodeRuneInString(s)
		rp, np := utf8.DecodeRuneInString(prefix)
		if rs != rp && foldRune(rs) != foldRune(rp) {
			return false
		}
		s, prefix = s[ns:], prefix[np:]
	}
	return true
}

// suffixFold reports whether s ends with suffix, ignoring case, by returning
// the byte offset in s where the matching suffix starts, or -1 when s does
// not end with suffix.
func suffixFold(s, suffix string) int {
	end := len(s)
	for suffix != "" {
		if end == 0 {
			return -1
		}
		rs, ns := utf8.DecodeLastRuneInString(s[:end])
		rp, np := utf8.DecodeLastRuneInString(suffix)
		if rs != rp && foldRune(rs) != foldRune(rp) {
			return -1
		}
		end -= ns
		suffix = suffix[:len(suffix)-np]
	}
	return end
}

// indexASCIIFold finds an ASCII token in s, as a byte offset or -1,
// matching letters ignoring ASCII case only: "ſ" (long s) does not match "s"
// here, although it does in containsFold and suffixFold.
func indexASCIIFold(s, token string) int {
	for i := 0; i+len(token) <= len(s); i++ {
		match := true
		for j := 0; j < len(token); j++ {
			if asciiLower(s[i+j]) != asciiLower(token[j]) {
				match = false
				break
			}
		}
		if match {
			return i
		}
	}
	return -1
}

func asciiLower(c byte) byte {
	if 'A' <= c && c <= 'Z' {
		return c + 'a' - 'A'
	}
	return c
}

// asciiEqualFold reports whether s equals word ignoring ASCII case only.
func asciiEqualFold(s, word string) bool {
	if len(s) != len(word) {
		return false
	}
	for i := 0; i < len(s); i++ {
		if asciiLower(s[i]) != asciiLower(word[i]) {
			return false
		}
	}
	return true
}

// qtToLower lowercases s. It differs from strings.ToLower in one character:
// it lowercases U+0130 (capital I with dot above) to "i" followed by U+0307
// (combining dot above), where Go gives a plain "i".
func qtToLower(s string) string {
	for i := 0; i < len(s); i++ {
		if s[i] >= utf8.RuneSelf {
			return qtToLowerUnicode(s)
		}
	}
	lower := []byte(s)
	for i, c := range lower {
		lower[i] = asciiLower(c)
	}
	return string(lower)
}

func qtToLowerUnicode(s string) string {
	out := make([]rune, 0, len(s))
	for _, r := range s {
		if r == 0x0130 {
			out = append(out, 'i', 0x0307)
			continue
		}
		out = append(out, unicode.ToLower(r))
	}
	return string(out)
}
