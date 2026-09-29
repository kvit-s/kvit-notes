package links

import "testing"

// Paths sort as string compares them, by UTF-16 code unit, so a character
// beyond the Basic Multilingual Plane comes before U+E000 to U+FFFF.
func TestCompareUTF16(t *testing.T) {
	cases := []struct {
		a, b string
		less bool
	}{
		{"B.md", "a.md", true},
		{"a.md", "a.md/x", true},
		{"\U0001F600.md", "！.md", true},
		{"！.md", "\U0001F600.md", false},
		{"é.md", "\U0001F600.md", true},
		{"\U0001F600.md", "\U0001F601.md", true},
	}
	for _, c := range cases {
		if got := compareUTF16(c.a, c.b) < 0; got != c.less {
			t.Errorf("compareUTF16(%q, %q) < 0 = %v", c.a, c.b, got)
		}
	}
}
