package mermaid

import (
	"math"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"
)

// The string handling the parsers share. Positions and lengths count runes;
// white space is what unicode.IsSpace reports, and trimming is
// strings.TrimSpace.

func trimSpace(s string) string { return strings.TrimSpace(s) }

// isLetterOrNumber reports a letter or a number of any script.
func isLetterOrNumber(r rune) bool { return unicode.IsLetter(r) || unicode.IsNumber(r) }

// runeLen is the number of runes in s.
func runeLen(s string) int { return utf8.RuneCountInString(s) }

// leftRunes is the first n runes of s, or all of s.
func leftRunes(s string, n int) string {
	i := 0
	for count := 0; i < len(s); count++ {
		if count == n {
			return s[:i]
		}
		_, size := utf8.DecodeRuneInString(s[i:])
		i += size
	}
	return s
}

// runeOffset is the byte offset of the n-th rune of s, or -1 when s has fewer
// than n runes.
func runeOffset(s string, n int) int {
	i := 0
	for ; n > 0; n-- {
		if i >= len(s) {
			return -1
		}
		_, size := utf8.DecodeRuneInString(s[i:])
		i += size
	}
	return i
}

// leadingSpaces is the number of runes of white space s starts with.
func leadingSpaces(s string) int {
	n := 0
	for _, r := range s {
		if !unicode.IsSpace(r) {
			break
		}
		n++
	}
	return n
}

// cutPair takes the first and last byte off s when both are q, which must be
// ASCII, and s has two runes or more.
func cutPair(s string, q byte) (string, bool) {
	if len(s) >= 2 && s[0] == q && s[len(s)-1] == q {
		return s[1 : len(s)-1], true
	}
	return s, false
}

// stripQuotePair is s without the q at each end, when it has one at each.
func stripQuotePair(s string, q byte) string {
	s, _ = cutPair(s, q)
	return s
}

// stripQuotes trims s and takes off a pair of double quotes around it.
func stripQuotes(s string) string { return stripQuotePair(trimSpace(s), '"') }

// cutPrefixFold reports whether s starts with prefix, comparing
// case-folded characters, and returns what follows the prefix.
func cutPrefixFold(s, prefix string) (string, bool) {
	i := runeOffset(s, runeLen(prefix))
	if i < 0 || !strings.EqualFold(s[:i], prefix) {
		return s, false
	}
	return s[i:], true
}

// hasPrefixFold reports whether s starts with prefix, comparing case-folded
// characters.
func hasPrefixFold(s, prefix string) bool {
	_, ok := cutPrefixFold(s, prefix)
	return ok
}

// cutSuffixFold reports whether s ends with suffix, comparing case-folded
// characters, and returns what comes before the suffix.
func cutSuffixFold(s, suffix string) (string, bool) {
	n := runeLen(s) - runeLen(suffix)
	if n < 0 {
		return s, false
	}
	i := runeOffset(s, n)
	if !strings.EqualFold(s[i:], suffix) {
		return s, false
	}
	return s[:i], true
}

// keyword reports whether line starts with the keyword kw followed by white
// space or nothing, and returns the rest of the line trimmed. With fold the
// keyword's case does not matter.
func keyword(line, kw string, fold bool) (string, bool) {
	var tail string
	if fold {
		var ok bool
		if tail, ok = cutPrefixFold(line, kw); !ok {
			return "", false
		}
	} else {
		if !strings.HasPrefix(line, kw) {
			return "", false
		}
		tail = line[len(kw):]
	}
	if r, _ := utf8.DecodeRuneInString(tail); tail != "" && !unicode.IsSpace(r) {
		return "", false
	}
	return trimSpace(tail), true
}

// splitSkipEmpty splits s at each sep and leaves out the empty parts.
func splitSkipEmpty(s, sep string) []string {
	var parts []string
	for _, p := range strings.Split(s, sep) {
		if p != "" {
			parts = append(parts, p)
		}
	}
	return parts
}

// toLower is s in lower case. It differs from strings.ToLower only for İ,
// whose lower case it writes in full as i and a combining dot.
func toLower(s string) string {
	return strings.ToLower(strings.ReplaceAll(s, "İ", "i̇"))
}

// indexRuneFrom is the index of the first r in rs at or after from, or -1.
func indexRuneFrom(rs []rune, r rune, from int) int {
	for i := max(from, 0); i < len(rs); i++ {
		if rs[i] == r {
			return i
		}
	}
	return -1
}

// midRunes is the n runes of rs from pos. A start before the slice counts
// the missing runes against n, and a negative n means the rest of rs.
func midRunes(rs []rune, pos, n int) []rune {
	size := len(rs)
	if pos > size {
		return nil
	}
	if pos < 0 {
		if n < 0 || n+pos >= size {
			return rs
		}
		if n+pos <= 0 {
			return nil
		}
		n += pos
		pos = 0
	} else if n < 0 || n > size-pos {
		n = size - pos
	}
	return rs[pos : pos+n]
}

// qtToInt reads a decimal int32 with an optional sign, white space allowed
// around it.
func qtToInt(s string) (int, bool) {
	s = trimSpace(s)
	digits := s
	if digits != "" && (digits[0] == '+' || digits[0] == '-') {
		digits = digits[1:]
	}
	if digits == "" {
		return 0, false
	}
	for i := 0; i < len(digits); i++ {
		if digits[i] < '0' || digits[i] > '9' {
			return 0, false
		}
	}
	v, err := strconv.ParseInt(s, 10, 32)
	if err != nil {
		return 0, false
	}
	return int(v), true
}

// qtToDouble reads a decimal number with a point, an optional sign and
// exponent, or nan, inf, +inf or -inf in any case, white space allowed
// around it. A value too large, or too small to be anything but zero, is
// refused.
func qtToDouble(s string) (float64, bool) {
	s = trimSpace(s)
	switch {
	case strings.EqualFold(s, "nan"):
		return math.NaN(), true
	case strings.EqualFold(s, "inf") || strings.EqualFold(s, "+inf"):
		return math.Inf(1), true
	case strings.EqualFold(s, "-inf"):
		return math.Inf(-1), true
	}
	i := 0
	if i < len(s) && (s[i] == '+' || s[i] == '-') {
		i++
	}
	intDigits, fracDigits := 0, 0
	nonZero := false
	for ; i < len(s) && s[i] >= '0' && s[i] <= '9'; i++ {
		intDigits++
		nonZero = nonZero || s[i] != '0'
	}
	if i < len(s) && s[i] == '.' {
		i++
		for ; i < len(s) && s[i] >= '0' && s[i] <= '9'; i++ {
			fracDigits++
			nonZero = nonZero || s[i] != '0'
		}
	}
	if intDigits+fracDigits == 0 {
		return 0, false
	}
	if i < len(s) && (s[i] == 'e' || s[i] == 'E') {
		i++
		if i < len(s) && (s[i] == '+' || s[i] == '-') {
			i++
		}
		expDigits := 0
		for ; i < len(s) && s[i] >= '0' && s[i] <= '9'; i++ {
			expDigits++
		}
		if expDigits == 0 {
			return 0, false
		}
	}
	if i != len(s) {
		return 0, false
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil || (v == 0 && nonZero) {
		return 0, false
	}
	return v, true
}
