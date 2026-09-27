package export

import "testing"

// Front matter is recognised by the Qt app's rule (testSplitRecognition in
// tests/test_notefrontmatter.cpp).
func TestSplitRecognition(t *testing.T) {
	cases := []struct {
		name, text string
		present    bool
	}{
		{"plain text", "Just a paragraph\n", false},
		{"empty file", "", false},
		{"simple block", "---\ntags: [a]\n---\nBody\n", true},
		{"all known keys", "---\ntags: [a, b]\ncreated: 2026-07-06T10:00:00\npinned: true\nfavorite: false\n---\nBody\n", true},
		{"foreign keys only", "---\nlayout: post\ntitle: Hello\n---\nBody\n", true},
		{"unterminated fence", "---\ntags: [a]\nno closing fence\n", false},
		{"fence not at byte 0", "\n---\ntags: [a]\n---\nBody\n", false},
		{"leading spaces before fence", " ---\ntags: [a]\n---\nBody\n", false},
		{"four dashes is not a fence", "----\ntags: [a]\n----\nBody\n", false},
		{"divider-led document with prose between dividers", "---\n\nSome text\n\n---\nMore\n", false},
		{"two leading dividers, nothing between", "---\n\n---\nBody\n", false},
		{"divider then heading then divider", "---\n\n# Title\n\n---\n", false},
		{"key-shaped prose between dividers reads as front-matter", "---\nnote: buy milk\n---\nBody\n", true},
		{"comment-only block is not front-matter", "---\n# just a comment\n---\nBody\n", false},
		{"blank-only block is not front-matter", "---\n\n\n---\nBody\n", false},
		{"key without space after colon is not a mapping", "---\nkey:value\n---\nBody\n", false},
		{"block list under a key is mapping-shaped", "---\ntags:\n- a\n- b\n---\nBody\n", true},
		{"close fence at end of file", "---\ntags: [a]\n---", true},
		{"crlf", "---\r\ntags: [a]\r\npinned: true\r\n---\r\nBody\r\n", true},
	}
	for _, c := range cases {
		fm, body := SplitNote(c.text)
		if (fm != "") != c.present {
			t.Errorf("%s: front matter %q", c.name, fm)
		}
		if fm+body != c.text {
			t.Errorf("%s: split is not byte for byte", c.name)
		}
	}
}

// A note's front matter is written in the Qt app's canonical form
// (testSerializeCanonicalOrder, testSerializeTagQuoting,
// testSerializeUnknownLinesAfterKnown, testParseTagsForms).
func TestCanonicalFrontMatter(t *testing.T) {
	cases := []struct{ in, want string }{
		{"---\nfavorite: true\npinned: true\ncreated: 2026-07-06T10:30:00\ntags: [b, a]\n---\n",
			"---\ntags: [b, a]\ncreated: 2026-07-06T10:30:00\npinned: true\nfavorite: true\n---\n"},
		{"---\nfavorite: true\n---\n", "---\nfavorite: true\n---\n"},
		{"---\npinned: false\nfavorite: false\n---\n", ""},
		{"---\nlayout: post\naliases:\n  - n\npinned: true\n---\n", "---\npinned: true\nlayout: post\naliases:\n  - n\n---\n"},
		{"---\ntags:\n- work\n- 'a,b'\n- \"x\\\"y\"\n---\n", "---\ntags: [work, \"a,b\", \"x\\\"y\"]\n---\n"},
		{"---\ntags: [\"#tag\", \" pad\", project x, a\\b]\n---\n", "---\ntags: [\"#tag\", \" pad\", project x, \"a\\\\b\"]\n---\n"},
		{"---\ncreated: 2026-07-06\n---\n", "---\ncreated: 2026-07-06T00:00:00\n---\n"},
		{"---\ncreated: 2026-07-06T10:30:00.250Z\n---\n", "---\ncreated: 2026-07-06T10:30:00Z\n---\n"},
		{"---\ncreated: 2026-07-06T10:30:00+05:30\n---\n", "---\ncreated: 2026-07-06T10:30:00+05:30\n---\n"},
		{"---\ncreated: not a date\npinned: maybe\ngoal: 0\n---\n", "---\ncreated: not a date\npinned: maybe\ngoal: 0\n---\n"},
		{"---\ncreated: 2026-07-06T24:00:00\n---\n", "---\ncreated: 2026-07-07T00:00:00\n---\n"},
		{"---\ncreated: 2026-07-06T10:30:00-0800\n---\n", "---\ncreated: 2026-07-06T10:30:00-08:00\n---\n"},
		{"---\ncreated: 2026-07-06T10:30:00+00:00\n---\n", "---\ncreated: 2026-07-06T10:30:00Z\n---\n"},
		{"---\ncreated: 2026-07-06 10:30\n---\n", "---\ncreated: 2026-07-06T10:30:00\n---\n"},
		{"---\ncreated: 2026-02-30\n---\n", "---\ncreated: 2026-02-30\n---\n"},
		{"---\ngoal: 500\n# comment\n\n---\n", "---\ngoal: 500\n# comment\n\n---\n"},
	}
	for _, c := range cases {
		if got := CanonicalFrontMatter(c.in); got != c.want {
			t.Errorf("%q:\n got %q\nwant %q", c.in, got, c.want)
		}
	}
}
