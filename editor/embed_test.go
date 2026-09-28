package editor

// Web embeds from the / menu (PARITY 4.2, Kvit's Web Embed row): the catalog
// entry, the address a typed query names (ImageAssets::normalizeEmbedUrl and
// isEmbedUrl), and choosing it inserting an embed line.

import (
	"slices"
	"strings"
	"testing"
)

func TestNormalizeEmbedURL(t *testing.T) {
	cases := []struct{ in, want string }{
		{"https://example.com/article", "https://example.com/article"},
		{"http://example.com/article", "http://example.com/article"},
		{"HTTPS://example.com/article", "HTTPS://example.com/article"},
		{"example.com/article", "https://example.com/article"},
		{"//example.com/article", "https://example.com/article"},
		{"localhost:8080/wiki", "https://localhost:8080/wiki"},
		{"", ""},
		{"   ", ""},
		{"two words", ""},
		{"mailto:someone@example.com", ""},
		{"file:///etc/hosts", ""},
	}
	for _, c := range cases {
		if got := normalizeEmbedURL(c.in); got != c.want {
			t.Errorf("normalizeEmbedURL(%q) = %q, want %q", c.in, got, c.want)
		}
	}
	for _, u := range []string{"https://example.com/article", "https://example.com", "https://youtu.be/x"} {
		if !isEmbedURL(u) {
			t.Errorf("%q should be an embed address", u)
		}
	}
	for _, u := range []string{"https://example.com/pic.png", "https://example.com/song.mp3", "https://example.com/clip.mp4", "notes/pic.png", ""} {
		if isEmbedURL(u) {
			t.Errorf("%q should not be an embed address", u)
		}
	}
}

func TestEmbedQueryURL(t *testing.T) {
	cases := []struct{ q, want string }{
		{"https://example.com/article", "https://example.com/article"},
		{"example.com/article", "https://example.com/article"},
		{"embed example.com/article", "https://example.com/article"},
		{"embed https://example.com/article", "https://example.com/article"},
		{"embed", ""},
		{"embed ", ""},
		{"https://example.com/pic.png", ""},
		{"two words", ""},
		{"mailto:x@y", ""},
		{"h1", ""},
	}
	for _, c := range cases {
		if got := embedQueryURL(c.q); got != c.want {
			t.Errorf("embedQueryURL(%q) = %q, want %q", c.q, got, c.want)
		}
	}
}

// The / menu offers a Web Embed that inserts an embed line from the typed
// address, and the plain entry inserts a template without asking for a file.
func TestSlashMenuEmbedFromTypedAddress(t *testing.T) {
	s, e := openEditor(t, "")
	var names []string
	var sel int
	s.Do(func() {
		e.FocusBlock(0, 0)
		e.Doc.Blocks[0].Text = "/embed"
		e.Doc.SetCaret(e.Doc.Blocks[0].ID, len([]rune("/embed")))
		e.openSlashMenu(e.Doc.Blocks[0].ID, true)
		names, sel = e.MenuEntries()
	})
	if !slices.Contains(names, "Web Embed") {
		t.Fatalf("entries for /embed: %q", names)
	}
	s.Do(func() {
		e.Doc.Blocks[0].Text = "/https://example.com/article"
		e.Doc.SetCaret(e.Doc.Blocks[0].ID, len([]rune("/https://example.com/article")))
		e.syncMenu()
		names, sel = e.MenuEntries()
	})
	if len(names) == 0 || names[0] != "Web Embed: https://example.com/article" {
		t.Fatalf("entries for a typed address: %q sel %d", names, sel)
	}
	var picked bool
	s.Do(func() {
		e.PickImage = func(int64) { picked = true }
		e.chooseMenu(e.menu.items[0])
	})
	var kind Kind
	var text string
	s.Do(func() {
		kind, text = e.Doc.Blocks[0].Kind, e.Doc.Blocks[0].Text
	})
	if kind != Image || text != "![](https://example.com/article)" {
		t.Fatalf("chose embed: %v %q", kind, text)
	}
	if picked {
		t.Error("choosing a web embed should not ask for a picture file")
	}
	if ref, ok := ParseImageLine(text); !ok || !isEmbed(ref) {
		t.Errorf("the inserted line should be an embed: %+v %v", ref, ok)
	}
	// The plain Web Embed entry inserts a template, also without a picker.
	s.Do(func() {
		e.Doc.Blocks[0].Text = "/embed"
		e.Doc.SetCaret(e.Doc.Blocks[0].ID, len([]rune("/embed")))
		e.openSlashMenu(e.Doc.Blocks[0].ID, true)
		for _, it := range e.menu.items {
			if it.name == "Web Embed" {
				picked = false
				e.chooseMenu(it)
				return
			}
		}
		t.Error("no plain Web Embed entry")
	})
	s.Do(func() {
		text = e.Doc.Blocks[0].Text
	})
	if text != "![]()" {
		t.Errorf("plain web embed template: %q", text)
	}
	if picked {
		t.Error("the plain web embed should not ask for a picture file")
	}
	// Choosing it is remembered as Web Embed, not as its address.
	s.Do(func() {
		if !slices.Contains(e.recent, "Web Embed") {
			t.Errorf("recent: %q", e.recent)
		}
		for _, r := range e.recent {
			if strings.HasPrefix(r, "Web Embed: ") {
				t.Errorf("an address should not be remembered: %q", e.recent)
			}
		}
	})
	s.CheckNamed()
}
