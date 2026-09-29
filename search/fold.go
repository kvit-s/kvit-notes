package search

// The character rules both searches share: case folding, word characters,
// and how many characters a query has.

import (
	"strings"
	"unicode"
	"unicode/utf8"
)

// foldRune returns the same rune for every member of r's simple case
// folding class and a different rune for every other class. the
// case-insensitive string::indexOf compares characters by their simple case
// folding one character at a time, so a match has the query's length, which
// is what lets a match in folded text keep its offsets. The rune returned is
// the smallest member of the class: for ASCII letters that is the capital.
func foldRune(r rune) rune {
	if r < utf8.RuneSelf {
		if 'a' <= r && r <= 'z' {
			return r - 'a' + 'A'
		}
		return r
	}
	m := r
	for f := unicode.SimpleFold(r); f != r; f = unicode.SimpleFold(f) {
		m = min(m, f)
	}
	return m
}

// fold folds every rune of s. The result has as many runes as s; same
// reports whether each rune also kept its UTF-8 length, in which case byte
// offsets in the result are byte offsets in s too. Only a few characters
// change length (the Kelvin sign folds to K), so same is nearly always true.
func fold(s string) (folded string, same bool) {
	if !needsFold(s) {
		return s, true
	}
	same = true
	var b strings.Builder
	b.Grow(len(s))
	for i := 0; i < len(s); {
		c := s[i]
		if c < utf8.RuneSelf {
			if 'a' <= c && c <= 'z' {
				c -= 'a' - 'A'
			}
			b.WriteByte(c)
			i++
			continue
		}
		r, n := utf8.DecodeRuneInString(s[i:])
		f := foldRune(r)
		if utf8.RuneLen(f) != n {
			same = false
		}
		b.WriteRune(f)
		i += n
	}
	return b.String(), same
}

// needsFold reports whether fold would change s: whether it has a lower-case
// ASCII letter or anything beyond ASCII.
func needsFold(s string) bool {
	for i := 0; i < len(s); i++ {
		if c := s[i]; c >= utf8.RuneSelf || ('a' <= c && c <= 'z') {
			return true
		}
	}
	return false
}

// foldString is fold without the second result.
func foldString(s string) string {
	f, _ := fold(s)
	return f
}

// trim is string::trimmed: the Unicode white space removed from both ends.
// Go's unicode.IsSpace is the same set as QChar::isSpace.
func trim(s string) string { return strings.TrimFunc(s, unicode.IsSpace) }

// isWordChar is the find bar's word character (documentsearch.cpp
// isWordChar): QChar::isLetterOrNumber or the underscore.  asks it of one
// UTF-16 code unit, and half of a surrogate pair is neither, so a character
// beyond the Basic Multilingual Plane is never a word character here.
func isWordChar(r rune) bool {
	return r == '_' || (r <= 0xFFFF && (unicode.IsLetter(r) || unicode.IsNumber(r)))
}

// wordScalars caches isWordScalar for ASCII.
var wordScalars = func() (t [utf8.RuneSelf]bool) {
	for r := range rune(utf8.RuneSelf) {
		t[r] = wordScalarSlow(r)
	}
	return t
}()

// isWordScalar is the search across notes' word character
// (searchindexdb.cpp SearchMatching::isWordScalar): a letter, a number, a
// mark, a modifier symbol, a private-use character or the underscore,
// classified by the whole character. It is the set SQLite's unicode61
// tokenizer keeps inside a word, plus marks and modifier symbols, so it
// includes the ASCII ^ and `.
func isWordScalar(r rune) bool {
	if r < utf8.RuneSelf {
		return wordScalars[r]
	}
	return wordScalarSlow(r)
}

func wordScalarSlow(r rune) bool {
	return r == '_' || unicode.In(r, unicode.L, unicode.N, unicode.M, unicode.Sk, unicode.Co)
}

// hasWordChar reports whether s holds a word character
// (SearchMatching::hasWordChar). A short query without one, such as "::",
// finds nothing.
func hasWordChar(s string) bool {
	for _, r := range s {
		if isWordScalar(r) {
			return true
		}
	}
	return false
}

// scalarCount is how many characters a query has for choosing between whole
// words and substrings (SearchMatching::unicodeScalarCount).  counts the
// Unicode scalar values of the query after NFC normalisation, so a letter
// typed with a separate combining accent counts once. The standard library
// has no normaliser, so this counts a nonspacing mark as part of the
// character before it and composes Hangul jamo as NFC does. The two counts
// differ only for a mark that has no precomposed form with its base, such
// as q with an acute accent, which  counts as two.
func scalarCount(s string) int {
	n := 0
	prev := rune(-1)
	for _, r := range s {
		switch {
		case prev >= 0 && r >= utf8.RuneSelf && unicode.Is(unicode.Mn, r):
			// Part of the character before it.
		case prev >= 0 && composesHangul(prev, r):
			prev = composeHangul(prev, r)
			continue
		default:
			n++
		}
		prev = r
	}
	return n
}

// Hangul composition (Unicode chapter 3.12): a leading consonant and a vowel
// make a syllable, and a syllable without a final consonant takes one.
const (
	hangulSBase  = 0xAC00
	hangulLBase  = 0x1100
	hangulVBase  = 0x1161
	hangulTBase  = 0x11A7
	hangulLCount = 19
	hangulVCount = 21
	hangulTCount = 28
	hangulNCount = hangulVCount * hangulTCount
	hangulSCount = hangulLCount * hangulNCount
)

func composesHangul(a, b rune) bool {
	if a >= hangulLBase && a < hangulLBase+hangulLCount {
		return b >= hangulVBase && b < hangulVBase+hangulVCount
	}
	s := a - hangulSBase
	return s >= 0 && s < hangulSCount && s%hangulTCount == 0 &&
		b > hangulTBase && b < hangulTBase+hangulTCount
}

func composeHangul(a, b rune) rune {
	if a >= hangulLBase && a < hangulLBase+hangulLCount {
		return hangulSBase + ((a-hangulLBase)*hangulVCount+(b-hangulVBase))*hangulTCount
	}
	return a + (b - hangulTBase)
}
