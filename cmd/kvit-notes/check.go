package main

// --check: the editor in a real window. The program opens its window on the
// desktop, drives the editor through the same key and character dispatch
// unison gives platform input (the window's own callback first, then the
// focused panel and its parents), checks what each step did to the note,
// and stays open long enough for a screen capture and a screen reader's
// view of the window to be taken from outside. It sends nothing to the
// desktop, so no keystroke can land in another program.

import (
	"fmt"
	"runtime"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// checkNote is the note the check opens.
const checkNote = `# Kvit Notes in a real window

The quick **brown** fox has *seven* cubs.

- a list item

A last paragraph.
`

// checkTitle is the window's title, which the outside check finds it by.
const checkTitle = "Kvit Notes check"

// windowDriver presses keys and types in a real window.
type windowDriver struct {
	n     *noteWindow
	fails []string
}

// press delivers a key as unison delivers one from the platform: to the
// window's own callback, then to the focused panel and up through its
// parents until one uses it.
func (w *windowDriver) press(key unison.KeyCode, mods mod.Modifiers) {
	uw := w.n.win.Window
	if uw.KeyDownCallback != nil && uw.KeyDownCallback(key, mods, false) {
		return
	}
	for p := uw.CurrentFocus(); p != nil; p = p.Parent() {
		if p.Enabled() && p.KeyDownCallback != nil && p.KeyDownCallback(key, mods, false) {
			return
		}
	}
}

// typ delivers each character of s as a typed character.
func (w *windowDriver) typ(s string) {
	uw := w.n.win.Window
	for _, ch := range s {
		for p := uw.CurrentFocus(); p != nil; p = p.Parent() {
			if p.Enabled() && p.RuneTypedCallback != nil && p.RuneTypedCallback(ch) {
				break
			}
		}
	}
}

func (w *windowDriver) expect(ok bool, format string, args ...any) {
	if !ok {
		w.fails = append(w.fails, fmt.Sprintf(format, args...))
	}
}

// checkSteps are run one at a time, a tenth of a second apart, so the
// window draws between them.
func checkSteps(w *windowDriver) []func() {
	ed := w.n.ed
	d := func() *editor.Doc { return ed.Doc }
	text := func(i int) string { return d().Blocks[i].Text }
	return []func(){
		func() {
			ed.FocusBlock(1, 5)
			w.expect(ed.Focused(), "the editor should hold the keyboard")
		},
		func() {
			w.press(unison.KeyHome, 0)
			for range 14 {
				w.press(unison.KeyRight, 0)
			}
			w.expect(d().Caret.Off == 16, "Home and 14 x Right should put the caret after \"brow\", it is at %d", d().Caret.Off)
		},
		func() {
			w.press(unison.KeyEnd, 0)
			w.typ(" Typed in a real window.")
			w.expect(strings.HasSuffix(text(1), "cubs. Typed in a real window."), "typing: %q", text(1))
		},
		func() {
			w.press(unison.KeyReturn, 0)
			w.typ("## A heading typed here")
			w.expect(d().Blocks[2].Kind == editor.Heading2 && text(2) == "A heading typed here", "\"## \" should make a heading: %v %q", d().Blocks[2].Kind, text(2))
		},
		func() {
			w.press(unison.KeyZ, mod.OSMenuCommand())
			w.expect(d().Blocks[2].Kind == editor.Heading2, "one undo takes back the typing, not the heading")
			w.press(unison.KeyY, mod.OSMenuCommand())
			w.expect(text(2) == "A heading typed here", "redo: %q", text(2))
		},
		func() {
			w.press(unison.KeyReturn, 0)
			w.typ("/")
			names, _ := ed.MenuEntries()
			w.expect(len(names) > 0 && len(w.n.win.Popups()) == 1, "/ should open the menu in the window")
		},
		func() {
			w.typ("todo")
			w.press(unison.KeyReturn, 0)
			w.expect(d().Blocks[3].Kind == editor.Todo && len(w.n.win.Popups()) == 0, "the menu should make a to-do: %v", d().Blocks[3].Kind)
			w.typ("a task from the menu")
		},
		func() {
			w.press(unison.KeyF10, mod.Shift)
			w.expect(len(w.n.win.Popups()) == 1, "Shift+F10 should open the block menu")
		},
		func() {
			w.press(unison.KeyEscape, 0)
			w.expect(len(w.n.win.Popups()) == 0, "Escape should close the block menu")
			// Leave the caret inside "brown", where the outside check reads it.
			ed.FocusBlock(1, 14)
		},
	}
}

// runCheck opens the window, runs the steps, reports, and closes the window
// after stay.
func runCheck(n *noteWindow, started time.Time, stay time.Duration) {
	w := &windowDriver{n: n}
	steps := checkSteps(w)
	var next func(i int)
	next = func(i int) {
		if i < len(steps) {
			steps[i]()
			unison.InvokeTaskAfter(func() { next(i + 1) }, 100*time.Millisecond)
			return
		}
		r := n.win.ContentRect()
		fmt.Printf("window %.0f x %.0f at scale %.2f, %s/%s\n", r.Width, r.Height, n.win.BackingScale().X, runtime.GOOS, runtime.GOARCH)
		for _, f := range w.fails {
			fmt.Println("FAIL", f)
		}
		if len(w.fails) == 0 {
			fmt.Printf("check: all %d steps passed\n", len(steps))
		}
		fmt.Printf("the note now:\n%s", editor.Serialize(n.ed.Doc.Blocks))
		var ms runtime.MemStats
		runtime.GC()
		runtime.ReadMemStats(&ms)
		fmt.Printf("Go heap in use %d MB, Go memory from the system %d MB\n", ms.HeapInuse>>20, ms.Sys>>20)
		unison.InvokeTaskAfter(n.win.Dispose, max(0, stay-time.Since(started)))
	}
	unison.InvokeTaskAfter(func() { next(0) }, 300*time.Millisecond)
}
