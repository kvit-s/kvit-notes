package editor

// Code fence languages a program adds to the editor (the Qt core's
// src/domain/blockkindregistry.h, registerFenceLanguage). Kvit Works adds
// `diff`, so a ```diff block in a transcript or a document draws its added
// lines in the success colour and its removed lines in the danger colour
// (its qml/agent/DiffBlock.qml). A language is drawn by its line tones;
// the block stays a code block in every other respect: it is saved as it
// was, copied as it was, and edited as code in an editor that edits.
//
// Kvit Notes registers nothing, and a fence nobody registered is drawn as it
// always was.

import (
	"slices"
	"strings"
	"sync"
)

// FenceTone is how a line of a registered fence language is drawn.
type FenceTone int

// The tones a line takes.
const (
	// FencePlain draws the line as the rest of the code.
	FencePlain FenceTone = iota
	// FenceAdded draws it in the theme's success colour.
	FenceAdded
	// FenceRemoved draws it in the theme's danger colour.
	FenceRemoved
)

// FenceLanguage is what a program says about a fence language it adds.
type FenceLanguage struct {
	// Tone is how each line of a block in the language is drawn.
	Tone func(line string) FenceTone
}

// DiffFenceLanguage is the language Kvit Works registers for diffs
// (src/agent/agentmodule.h, DiffFenceLanguage).
const DiffFenceLanguage = "diff"

// DiffFence is the diff drawing Kvit Works registers under
// DiffFenceLanguage: a line starting with "+" is added, one starting with
// "-" removed, and every other line plain (DiffBlock.qml).
func DiffFence() FenceLanguage { return FenceLanguage{Tone: DiffTone} }

// DiffTone is DiffFence's tone for one line.
func DiffTone(line string) FenceTone {
	switch {
	case strings.HasPrefix(line, "+"):
		return FenceAdded
	case strings.HasPrefix(line, "-"):
		return FenceRemoved
	}
	return FencePlain
}

// builtinFences are the fence languages the editor draws in its own way,
// which a program cannot take over.
var builtinFences = []string{"mermaid", "kanban", "toc", "query", "diagram", "plain"}

var (
	fenceMu    sync.RWMutex
	fenceLangs = map[string]FenceLanguage{}
)

// RegisterFenceLanguage adds a fence language, for every editor in the
// program. Programs register their languages as they start, before any
// block is drawn. It reports false, and changes nothing, for a language the
// editor draws itself or one already registered, so one part of a program
// cannot take over another's fence.
func RegisterFenceLanguage(lang string, def FenceLanguage) bool {
	key := strings.ToLower(strings.TrimSpace(lang))
	if key == "" || def.Tone == nil || slices.Contains(builtinFences, key) {
		return false
	}
	fenceMu.Lock()
	defer fenceMu.Unlock()
	if _, taken := fenceLangs[key]; taken {
		return false
	}
	fenceLangs[key] = def
	return true
}

// FenceLanguages are the registered fence languages, sorted.
func FenceLanguages() []string {
	fenceMu.RLock()
	defer fenceMu.RUnlock()
	out := make([]string, 0, len(fenceLangs))
	for k := range fenceLangs {
		out = append(out, k)
	}
	slices.Sort(out)
	return out
}

// ResetFenceLanguages forgets every registered language. Tests use it to
// start from a known state; a program never calls it.
func ResetFenceLanguages() {
	fenceMu.Lock()
	defer fenceMu.Unlock()
	clear(fenceLangs)
}

func fenceLanguage(lang string) (FenceLanguage, bool) {
	if lang == "" {
		return FenceLanguage{}, false
	}
	fenceMu.RLock()
	defer fenceMu.RUnlock()
	def, ok := fenceLangs[strings.ToLower(lang)]
	return def, ok
}

// fenceColours are the colours a code block in a registered language draws
// its characters in, one for each character of its text: a line's tone's
// colour, or "" to leave the character as the code draws it. colors, the
// colours the projection already had, are returned unchanged for a block in
// no registered language.
func (e *Editor) fenceColours(b *Block, colors []string) []string {
	if b.Kind != Code {
		return colors
	}
	def, ok := fenceLanguage(b.Lang)
	if !ok {
		return colors
	}
	t := e.tok()
	added, removed := t.Success.Hex(), t.Danger.Hex()
	out := make([]string, 0, len([]rune(b.Text)))
	for k, line := range strings.Split(b.Text, "\n") {
		c := ""
		switch def.Tone(line) {
		case FenceAdded:
			c = added
		case FenceRemoved:
			c = removed
		}
		if k > 0 {
			out = append(out, "") // the line break before this line
		}
		for range []rune(line) {
			out = append(out, c)
		}
	}
	return out
}
