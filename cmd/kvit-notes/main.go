// Command kvit-notes is the Go version of Kvit Notes, the Markdown block
// editor. It opens a vault, a folder of notes, in a window with the sidebar,
// the note list and the editor, or edits one note file on its own.
//
//	kvit-notes [folder]                    open a vault: the folder, else the one
//	                                       the Qt app had open last, else Documents/Kvit
//	kvit-notes note.md                     edit one note file on its own
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

	"github.com/kvit-s/kvit-notes/app"
	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
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
	closeAfter := flag.Duration("close-after", 0, "close the window after this long, printing when it first drew")
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
	if *check == 0 && *closeAfter == 0 {
		// A copy already running opens what this one was asked to.
		target := ""
		if path != "" {
			target, _ = filepath.Abs(path)
		}
		if handOff(socketPath(), target) {
			return
		}
	}
	if *check == 0 {
		if root, ok := vaultRoot(path); ok {
			runVault(root, *theme, *closeAfter)
			return
		}
	}
	doc, err := loadDoc(path)
	var page *vault.Page
	if path != "" && *check == 0 {
		doc, page, err = loadFile(path)
	}
	if *check > 0 {
		path, doc = "", editor.NewDoc(editor.ParseMarkdown(checkNote))
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	opt := kvitui.Options{SettingsPath: settingsPath()}
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
	app.OpenFile = openFileWindow
	unison.Start(
		unison.ThemeChangedCallback(ui.Appearance.Refresh),
		unison.StartupFinishedCallback(func() {
			n, err := newNoteWindow(ui, doc, path)
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			n.page = page
			if *check == 0 && *closeAfter == 0 {
				serveLaterCopies(ui)
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

// settingsPath is the Go app's settings file: the theme, typography, panes
// and vaults, under the Qt app's keys. It starts as a copy of the Qt app's.
func settingsPath() string {
	p := kvitui.DefaultSettingsPath("kvit-notes")
	vault.SeedSettings(p)
	return p
}

// vaultRoot is the vault to open for the command-line argument: the folder
// named, or with none named the vault the Qt app had open last, else
// Documents/Kvit. A file argument opens that file on its own instead.
func vaultRoot(arg string) (string, bool) {
	if arg != "" {
		info, err := os.Stat(arg)
		return arg, err == nil && info.IsDir()
	}
	if open := vault.OpenVaultsIn(settingsPath()); len(open) > 0 {
		return open[0], true
	}
	if open := vault.OpenVaults(); len(open) > 0 {
		return open[0], true
	}
	return vault.DefaultRoot(), true
}

// runVault opens a vault in a window and runs until the last window
// closes, or for closeAfter when that is set.
func runVault(root, theme string, closeAfter time.Duration) {
	started := time.Now()
	ui, err := kvitui.New(kvitui.Options{SettingsPath: settingsPath()})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if theme != "" {
		ui.Theme.SetThemeID(theme)
	}
	app.OpenFile = openFileWindow
	unison.Start(
		unison.ThemeChangedCallback(ui.Appearance.Refresh),
		unison.StartupFinishedCallback(func() {
			w, err := app.OpenVault(ui, root)
			if err != nil {
				fmt.Fprintf(os.Stderr, "kvit-notes: cannot open %s: %v\n", root, err)
				os.Exit(1)
			}
			w.Win.ToFront()
			if closeAfter == 0 {
				serveLaterCopies(ui)
			}
			if closeAfter > 0 {
				draw := w.Editor.DrawCallback
				first := true
				w.Editor.DrawCallback = func(gc *unison.Canvas, r geom.Rect) {
					draw(gc, r)
					if first {
						first = false
						fmt.Printf("first frame after %d ms, %d notes\n", time.Since(started).Milliseconds(), len(w.Vault.Entries))
					}
				}
				unison.InvokeTaskAfter(w.Win.Dispose, closeAfter)
			}
		}))
}

// serveLaterCopies listens for copies started after this one and opens
// what each hands over: a folder as a vault, a file on its own, and with
// nothing, the app brought forward.
func serveLaterCopies(ui *kvitui.UI) {
	_, err := listen(socketPath(), func(req instanceRequest) {
		done := make(chan struct{})
		unison.InvokeTask(func() {
			defer close(done)
			switch info, err := os.Stat(req.Open); {
			case req.Open == "" || err != nil:
				if ws := unison.Windows(); !app.RaiseAny() && len(ws) > 0 {
					ws[0].ToFront()
				}
			case info.IsDir():
				if err := app.OpenOrRaise(ui, req.Open); err != nil {
					fmt.Fprintf(os.Stderr, "kvit-notes: cannot open %s: %v\n", req.Open, err)
				}
			default:
				openFileWindow(ui, req.Open)
			}
		})
		<-done
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, "kvit-notes: later copies will open their own windows:", err)
	}
}
