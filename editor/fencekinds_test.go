package editor

import (
	"slices"
	"testing"
)

// A fence language a program adds (the core's
// tests/test_blockkindregistry.cpp for the registry's rules, and Kvit
// Works' diff fence).

func TestARegisteredFenceLanguageIsRefusedTheSecondTime(t *testing.T) {
	ResetFenceLanguages()
	t.Cleanup(ResetFenceLanguages)
	if !RegisterFenceLanguage(DiffFenceLanguage, DiffFence()) {
		t.Fatal("diff was not registered")
	}
	if RegisterFenceLanguage("DIFF", DiffFence()) {
		t.Error("a language registered twice was taken")
	}
	for _, builtin := range builtinFences {
		if RegisterFenceLanguage(builtin, DiffFence()) {
			t.Errorf("the editor's own %q fence was taken over", builtin)
		}
	}
	if RegisterFenceLanguage("", DiffFence()) || RegisterFenceLanguage("patch", FenceLanguage{}) {
		t.Error("a language with no name or no tone was registered")
	}
	if got := FenceLanguages(); !slices.Equal(got, []string{"diff"}) {
		t.Errorf("registered %v", got)
	}
}

func TestADiffFenceDrawsItsAddedAndRemovedLines(t *testing.T) {
	ResetFenceLanguages()
	t.Cleanup(ResetFenceLanguages)
	RegisterFenceLanguage(DiffFenceLanguage, DiffFence())
	s, e := openEditor(t, "```diff\n+added\n-removed\n same\n```\n\n```patch\n+added\n```\n")
	s.Do(func() {
		added, removed := e.tok().Success.Hex(), e.tok().Danger.Hex()
		colors := e.layout(0).proj.colors
		text := []rune(e.Doc.Blocks[0].Text)
		if len(colors) != len(text) {
			t.Fatalf("%d colours for %d characters", len(colors), len(text))
		}
		for k, r := range text {
			want := ""
			switch line := lineAt(text, k); {
			case r == '\n':
			case line == 0:
				want = added
			case line == 1:
				want = removed
			}
			if colors[k] != want {
				t.Errorf("character %d %q is %q, want %q", k, r, colors[k], want)
			}
		}
		if e.layout(1).proj.colors != nil {
			t.Error("a fence nobody registered was coloured")
		}
		if got := Serialize(e.Doc.Blocks); got != "```diff\n+added\n-removed\n same\n```\n\n```patch\n+added\n```\n" {
			t.Errorf("the note changed:\n%s", got)
		}
	})
}

// lineAt is the line of text character k is on.
func lineAt(text []rune, k int) int {
	n := 0
	for _, r := range text[:k] {
		if r == '\n' {
			n++
		}
	}
	return n
}

func TestDiffToneReadsTheFirstCharacter(t *testing.T) {
	for line, want := range map[string]FenceTone{"+x": FenceAdded, "-x": FenceRemoved, " x": FencePlain, "": FencePlain, "@@ -1 +1 @@": FencePlain} {
		if got := DiffTone(line); got != want {
			t.Errorf("%q is %v, want %v", line, got, want)
		}
	}
}
