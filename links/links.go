// Package links is everything about links between notes that draws
// nothing: finding [[wiki links]] in a note's Markdown, deciding which note a
// link names, the anchor a #heading matches, which notes link to a note, the
// rewrite of links when a note is renamed or moved, and the table of renamed
// notes in .kvit/redirects.json. It follows the rules of the earlier Qt
// version of Kvit Notes exactly, so vaults written with that version keep
// their links.
//
// Positions are rune offsets into the text.
//
// Paths are vault-relative, with forward slashes, and end in ".md":
// "Ideas/Reading list.md".
package links

import (
	"strings"
	"unicode"
)

// isSpace reports white space: the Unicode space separators, the line and
// paragraph separators, tab to carriage return, and U+0085, the set
// unicode.IsSpace reports.
func isSpace(r rune) bool { return unicode.IsSpace(r) }

// isDigit reports a decimal digit of the Basic Multilingual Plane. A
// character beyond that plane is never a digit.
func isDigit(r rune) bool { return r <= 0xFFFF && unicode.IsDigit(r) }

// isLetterOrNumber reports a letter or a number of the Basic Multilingual
// Plane. A character beyond that plane is neither.
func isLetterOrNumber(r rune) bool {
	return r <= 0xFFFF && (unicode.IsLetter(r) || unicode.IsNumber(r))
}

// trim is s without white space at either end.
func trim(s string) string { return strings.TrimFunc(s, isSpace) }

// lower is s in lower case. It differs from strings.ToLower only for U+0130
// (capital I with a dot), which it lowers to "i" and a combining dot, as
// Unicode's special casing says.
func lower(s string) string {
	return strings.Map(unicode.ToLower, strings.ReplaceAll(s, "\u0130", "i\u0307"))
}

// trimMD removes one ".md" from the end of s, in any case.
func trimMD(s string) string {
	if len(s) >= 3 && strings.EqualFold(s[len(s)-3:], ".md") {
		return s[:len(s)-3]
	}
	return s
}

// baseName is the last segment of a path.
func baseName(path string) string { return path[strings.LastIndexByte(path, '/')+1:] }

// unitKey orders runes as their first UTF-16 code unit does: a character
// beyond the Basic Multilingual Plane sorts before U+E000 to U+FFFF.
func unitKey(r rune) rune {
	if r > 0xFFFF {
		return 0xD800 + (r-0x10000)>>10
	}
	return r
}

// compareUTF16 compares two strings by their UTF-16 code units, one by one.
func compareUTF16(a, b string) int {
	ra, rb := []rune(a), []rune(b)
	for i := 0; i < len(ra) && i < len(rb); i++ {
		if ra[i] == rb[i] {
			continue
		}
		ka, kb := unitKey(ra[i]), unitKey(rb[i])
		if ka == kb {
			ka, kb = ra[i], rb[i]
		}
		if ka < kb {
			return -1
		}
		return 1
	}
	return len(ra) - len(rb)
}

// fold is the case folding compareFold uses, for the characters notes are
// named with.
func fold(r rune) rune { return unicode.ToLower(unicode.ToUpper(r)) }

// compareFold compares two strings without regard to case, by UTF-16 code
// units, falling back to the exact order when they differ only in case so
// that the result never depends on the order they came in.
func compareFold(a, b string) int {
	if c := compareUTF16(strings.Map(fold, a), strings.Map(fold, b)); c != 0 {
		return c
	}
	return compareUTF16(a, b)
}
