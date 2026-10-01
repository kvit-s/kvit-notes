package query

// Typed field values. A query compares two values as dates when both read as
// dates, as numbers when both read as numbers, and as case-insensitive
// strings otherwise. The strings that read as a date or a number are the ones
// the earlier Qt version of Kvit Notes accepted, because a value that is a
// date in one version and text in the other would sort differently.

import (
	"math"
	"math/big"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"
)

// typedValue is one field's value on one note, with the types it reads as.
type typedValue struct {
	text     string    // what a table cell shows
	date     time.Time // set when isDate
	number   float64   // set when isNumber
	isDate   bool
	isNumber bool
	// exists is false when the note does not have the field at all, which is
	// different from having it with an empty value.
	exists bool
}

// typedFromString is the trimmed text, read as a date if it parses as one,
// else as a number if it parses as one.
func typedFromString(raw string, exists bool) typedValue {
	v := typedValue{text: strings.TrimSpace(raw), exists: exists}
	if v.text == "" {
		return v
	}
	if t, ok := parseDate(v.text); ok {
		v.date = t
		v.isDate = true
		return v
	}
	if n, ok := parseNumber(v.text); ok {
		v.number = n
		v.isNumber = true
	}
	return v
}

// compareTyped compares a and b: -1, 0 or 1.
func compareTyped(a, b typedValue) int {
	if a.isDate && b.isDate {
		switch {
		case a.date.Equal(b.date):
			return 0
		case a.date.Before(b.date):
			return -1
		}
		return 1
	}
	if a.isNumber && b.isNumber {
		// With NaN on either side neither test holds and the result is 1.
		if a.number == b.number {
			return 0
		}
		if a.number < b.number {
			return -1
		}
		return 1
	}
	return compareFold(a.text, b.text)
}

// parseDate reads text as a date: first as a date and time (isoDateTime),
// and when that fails, as a date alone (isoDate) at the start of that day in
// local time. The second step matters: isoDate only looks at the first ten
// characters and rejects a digit after them, so "2026-08-01 is the day" and
// "2026-08-01T24:30" read as the date 2026-08-01.
func parseDate(text string) (time.Time, bool) {
	s := []rune(text)
	if t, ok := isoDateTime(s); ok {
		return t, true
	}
	if y, m, d, ok := isoDate(s); ok {
		return time.Date(y, time.Month(m), d, 0, 0, 0, 0, time.Local), true
	}
	return time.Time{}, false
}

// isoDate reads an ISO 8601 date: four digits, any punctuation character, two
// digits, punctuation, two digits, and then either the end of the string or
// anything that is not a digit. The year must be 1 to 9999 and the day must
// exist. "2026/08/01" and "2026_08_01" are dates; "2026-8-1" is not.
func isoDate(s []rune) (year, month, day int, ok bool) {
	if len(s) < 10 || !isPunctUnit(s[4]) || !isPunctUnit(s[7]) {
		return 0, 0, 0, false
	}
	if len(s) > 10 && isDigitUnit(s[10]) {
		return 0, 0, 0, false
	}
	y, okY := readInt(s[0:4])
	m, okM := readInt(s[5:7])
	d, okD := readInt(s[8:10])
	if !okY || !okM || !okD || y <= 0 || y > 9999 {
		return 0, 0, 0, false
	}
	if m < 1 || m > 12 || d < 1 || d > uint64(daysIn(int(y), int(m))) {
		return 0, 0, 0, false
	}
	return int(y), int(m), int(d), true
}

// isPunctUnit reports whether r is punctuation that fits in one UTF-16 code
// unit. A character above U+FFFF takes two units and does not count.
func isPunctUnit(r rune) bool {
	return r <= 0xFFFF && unicode.IsPunct(r)
}

// isDigitUnit reports whether r is a Unicode decimal digit that fits in one
// UTF-16 code unit.
func isDigitUnit(r rune) bool {
	return r <= 0xFFFF && unicode.IsDigit(r)
}

// readInt reads a non-empty run made only of ASCII digits, and its value. No
// sign and no spaces.
func readInt(s []rune) (uint64, bool) {
	if len(s) == 0 {
		return 0, false
	}
	var b strings.Builder
	for _, r := range s {
		if r < '0' || r > '9' {
			return 0, false
		}
		b.WriteRune(r)
	}
	v, err := strconv.ParseUint(b.String(), 10, 64)
	return v, err == nil
}

func daysIn(year, month int) int {
	return time.Date(year, time.Month(month)+1, 0, 0, 0, 0, 0, time.UTC).Day()
}

// isoDateTime reads an ISO 8601 date and time: a date as isoDate reads it
// from exactly the first ten characters, then either nothing (the start of
// that day, local time) or "T", "t" or a space, a time, and optionally "Z"
// for UTC or a UTC offset such as "+02:00", "+0200" or "+02". Without an
// offset the time is local time.
func isoDateTime(s []rune) (time.Time, bool) {
	if len(s) < 10 {
		return time.Time{}, false
	}
	y, m, d, ok := isoDate(s[:10])
	if !ok {
		return time.Time{}, false
	}
	if len(s) == 10 {
		return time.Date(y, time.Month(m), d, 0, 0, 0, 0, time.Local), true
	}
	rest := s[10:]
	if len(rest) < 2 || (rest[0] != 'T' && rest[0] != 't' && rest[0] != ' ') {
		return time.Time{}, false
	}
	rest = rest[1:]

	loc := time.Local
	if last := rest[len(rest)-1]; last == 'Z' || last == 'z' {
		loc = time.UTC
		rest = rest[:len(rest)-1]
	} else {
		// The offset starts at the last "+" or "-"; a time has neither.
		sign := -1
		for i := len(rest) - 1; i >= 0; i-- {
			if rest[i] == '+' || rest[i] == '-' {
				sign = i
				break
			}
		}
		if sign >= 0 {
			offset, ok := isoOffset(rest[sign:])
			// An offset may be up to 16 hours either way; beyond that the
			// date-time is invalid.
			if !ok || offset < -16*3600 || offset > 16*3600 {
				return time.Time{}, false
			}
			loc = time.FixedZone("", offset)
			rest = rest[:sign]
		}
	}

	hour, minute, second, msec, midnight24, ok := isoTime(rest)
	if !ok {
		return time.Time{}, false
	}
	if midnight24 {
		// "24:00" is the start of the next day.
		d++
	}
	return time.Date(y, time.Month(m), d, hour, minute, second, msec*int(time.Millisecond), loc), true
}

// isoOffset reads a UTC offset: a sign, hours, and optionally minutes after a
// colon or directly after two hour digits, as seconds east of UTC. Hours
// above 23 or minutes above 59 are rejected.
func isoOffset(s []rune) (int, bool) {
	if len(s) < 2 || len(s) > 6 {
		return 0, false
	}
	sign := 1
	switch s[0] {
	case '+':
	case '-':
		sign = -1
	default:
		return 0, false
	}
	t := s[1:]
	hhLen, mmIndex := -1, 0
	for i, r := range t {
		if r == ':' {
			hhLen = i
			break
		}
	}
	if hhLen == -1 {
		hhLen, mmIndex = 2, 2
	} else {
		mmIndex = hhLen + 1
	}
	hour, ok := qtToInt(string(t[:min(hhLen, len(t))]))
	if !ok || hour > 23 {
		return 0, false
	}
	minute := 0
	if mm := t[min(mmIndex, len(t)):]; len(mm) > 0 {
		minute, ok = qtToInt(string(mm))
	}
	if !ok || minute < 0 || minute > 59 {
		return 0, false
	}
	return sign * (hour*60 + minute) * 60, true
}

// qtToInt reads a decimal integer with an optional sign, surrounding
// whitespace allowed.
func qtToInt(s string) (int, bool) {
	v, err := strconv.ParseInt(strings.TrimSpace(s), 10, 32)
	return int(v), err == nil
}

// isoTime reads an ISO 8601 time: "HH", "HH:mm" or "HH:mm:ss", optionally
// followed by "." or "," and a fraction of the last field given. A fraction
// of an hour or a minute is converted to minutes and seconds, a fraction of a
// second to milliseconds, rounded. "24:00" (with nothing but zeros after it)
// reports midnight24 and hour 0.
func isoTime(s []rune) (hour, minute, second, msec int, midnight24, ok bool) {
	var tail []rune
	hasSeparator := false
	dot, comma := indexOfRune(s, '.'), indexOfRune(s, ',')
	if dot != -1 {
		tail = s[dot+1:]
		if indexOfRune(tail, '.') != -1 {
			return
		}
		s = s[:dot]
		hasSeparator = true
	} else if comma != -1 {
		tail = s[comma+1:]
		s = s[:comma]
		hasSeparator = true
	}
	if indexOfRune(tail, ',') != -1 {
		return
	}
	frac, fracOK := readInt(tail)
	if len(tail) == 0 {
		if hasSeparator {
			return
		}
	} else if !fracOK {
		return
	}
	fraction := 0.0
	if fracOK {
		fraction = float64(frac) * powTenth(len(tail))
	}

	size := len(s)
	if size < 2 || size > 8 {
		return
	}
	h, hOK := readInt(s[:2])
	if !hOK || h > 24 {
		return
	}
	hour = int(h)

	if size > 2 {
		var mOK bool
		var mm uint64
		if s[2] == ':' && size > 4 {
			mm, mOK = readInt(s[3:5])
		}
		if !mOK || mm >= 60 {
			return
		}
		minute = int(mm)
	} else if fracOK {
		fraction *= 60
		minute = int(fraction)
		fraction -= float64(minute)
	}

	if size > 5 {
		var sOK bool
		var ss uint64
		if s[5] == ':' && size == 8 {
			ss, sOK = readInt(s[6:8])
		}
		if !sOK || ss >= 60 {
			return
		}
		second = int(ss)
	} else if fracOK && size > 2 {
		fraction *= 60
		second = int(fraction)
		fraction -= float64(second)
	}

	if fracOK {
		// qRound on x86-64: add one half and truncate.
		msec = int(1000*fraction + 0.5)
	}
	if msec == 1000 {
		second++
		if second == 60 {
			second = 0
			minute++
			if minute == 60 {
				minute = 0
				hour++
			}
		}
		msec = 0
	}
	if hour == 24 && minute == 0 && second == 0 && msec == 0 {
		midnight24 = true
		hour = 0
	}
	if hour >= 24 || minute >= 60 || second >= 60 || msec >= 1000 {
		return 0, 0, 0, 0, false, false
	}
	return hour, minute, second, msec, midnight24, true
}

func indexOfRune(s []rune, r rune) int {
	for i, c := range s {
		if c == r {
			return i
		}
	}
	return -1
}

// powTenthTable holds pow(0.1, n), correctly rounded, for small n. A
// fraction's value is its digits times pow(0.1, digits); Go's math.Pow is off
// by a unit in the last place for four digits and more, which can move a
// millisecond that sits on a rounding boundary.
var powTenthTable = sync.OnceValue(func() []float64 {
	table := make([]float64, 41)
	for n := range table {
		table[n] = exactPowTenth(n)
	}
	return table
})

func powTenth(n int) float64 {
	if table := powTenthTable(); n < len(table) {
		return table[n]
	}
	return exactPowTenth(n)
}

// exactPowTenth is the float64 nearest to the exact n-th power of the
// float64 0.1, computed with enough bits that the product is exact.
func exactPowTenth(n int) float64 {
	prec := uint(53*n + 64)
	tenth := new(big.Float).SetPrec(prec).SetFloat64(0.1)
	p := new(big.Float).SetPrec(prec).SetFloat64(1)
	for i := 0; i < n; i++ {
		p.Mul(p, tenth)
	}
	f, _ := p.Float64()
	return f
}

// parseNumber reads already trimmed text as a number: a decimal number with
// an optional sign, fraction and exponent (".5", "5.", "1e5", "-.5e2"), or
// "nan", "inf", "+inf", "-inf" in any letter case. No hexadecimal, no digit
// grouping, no "infinity". A value too large for a double fails, and so does
// a nonzero value too small for one.
func parseNumber(s string) (float64, bool) {
	switch {
	case asciiEqualFold(s, "nan"):
		return math.NaN(), true
	case asciiEqualFold(s, "inf"), asciiEqualFold(s, "+inf"):
		return math.Inf(1), true
	case asciiEqualFold(s, "-inf"):
		return math.Inf(-1), true
	}
	nonzero, ok := scanDecimal(s)
	if !ok {
		return 0, false
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return 0, false
	}
	if v == 0 && nonzero {
		return 0, false
	}
	return v, true
}

// scanDecimal checks s against [+-]?(digits[.digits?] | .digits)([eE][+-]?digits)?
// and reports whether the part before the exponent has a nonzero digit.
func scanDecimal(s string) (nonzero, ok bool) {
	i := 0
	if i < len(s) && (s[i] == '+' || s[i] == '-') {
		i++
	}
	digits := 0
	for ; i < len(s) && s[i] >= '0' && s[i] <= '9'; i++ {
		digits++
		nonzero = nonzero || s[i] != '0'
	}
	if i < len(s) && s[i] == '.' {
		i++
		for ; i < len(s) && s[i] >= '0' && s[i] <= '9'; i++ {
			digits++
			nonzero = nonzero || s[i] != '0'
		}
	}
	if digits == 0 {
		return false, false
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
			return false, false
		}
	}
	return nonzero, i == len(s)
}
