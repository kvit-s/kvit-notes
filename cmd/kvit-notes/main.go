// Command kvit-notes edits one Markdown note in Kvit's block editor. It is
// the start of the Go version of Kvit Notes: for now one window, the editor
// and a status line; the rest of the app (vaults, search, the sidebar, export)
// comes in the plan's step 6.
//
//	kvit-notes [note.md]                   open a note, or the sample note
//	kvit-notes --scenario all --out DIR    run the scripted scenarios headlessly
//	kvit-notes --check 12s                 drive the editor in a real window, then close
//	kvit-notes --help                      every option
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

func main() {
	scenario := flag.String("scenario", "", "run a scripted scenario headlessly, by name or `all`, and exit")
	out := flag.String("out", "", "with --scenario: write the scenarios' screenshots into this directory")
	compare := flag.String("compare", "", "with --scenario and --out: stack each screenshot under Kvit's of the same name from this directory, into OUT/compare")
	theme := flag.String("theme", "", "light, dark, sepia or highContrast; the desktop's choice unless set")
	check := flag.Duration("check", 0, "open a window, drive the editor through a scripted check, print the result, and close after this long")
	bench := flag.String("bench", "", "time opening, scrolling and typing in 1,237 blocks of Kvit's documentation, read from this `directory` (Kvit's Qt repository), and exit")
	flag.Parse()

	if *bench != "" {
		if err := runBench(*bench); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		return
	}

	if *scenario != "" {
		err := runScenarios(*scenario, *out, *theme)
		if *compare != "" && *out != "" {
			n, cerr := writeComparisons(*out, *compare, filepath.Join(*out, "compare"))
			if cerr != nil {
				fmt.Fprintln(os.Stderr, cerr)
				os.Exit(1)
			}
			fmt.Printf("%d comparison images in %s\n", n, filepath.Join(*out, "compare"))
		}
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		return
	}

	path := flag.Arg(0)
	doc, err := loadDoc(path)
	if *check > 0 {
		path, doc = "", editor.NewDoc(editor.ParseMarkdown(checkNote))
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	opt := kvitui.Options{SettingsPath: kvitui.DefaultSettingsPath("kvit-notes")}
	if *check > 0 {
		// The check leaves the reader's settings alone and looks the same on
		// every machine.
		opt = kvitui.Options{IgnoreDesktop: true}
	}
	ui, err := kvitui.New(opt)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if *theme != "" {
		ui.Theme.SetThemeID(*theme)
	}
	started := time.Now()
	unison.Start(
		unison.ThemeChangedCallback(ui.Appearance.Refresh),
		unison.StartupFinishedCallback(func() {
			n, err := newNoteWindow(ui, doc, path)
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			if *check > 0 {
				n.win.SetTitle(checkTitle)
				n.win.SetContentRect(geom.NewRect(80, 80, storyWidth, storyHeight))
				draw := n.ed.DrawCallback
				first := true
				n.ed.DrawCallback = func(gc *unison.Canvas, r geom.Rect) {
					draw(gc, r)
					if first {
						first = false
						fmt.Printf("first frame after %d ms\n", time.Since(started).Milliseconds())
						runCheck(n, started, *check)
					}
				}
			}
			n.win.ToFront()
			if *check == 0 {
				n.ed.FocusBlock(0, 0)
			}
		}))
}
