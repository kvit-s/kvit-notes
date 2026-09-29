// Command kvit-notes is the Go version of Kvit Notes, the Markdown block
// editor. It opens a vault, a folder of notes, in a window with the sidebar,
// the note list and the editor, or edits one note file on its own.
//
//	kvit-notes [folder]                    open a vault: the folder, else the one
//
// the app had open last, else Documents/Kvit
//
//	kvit-notes note.md                     edit one note file on its own
//	kvit-notes --scenario all --out DIR    run the scripted scenarios headlessly
//	kvit-notes --check 12s                 drive the editor in a real window, then close
//	kvit-notes --tray-check 20s [folder]   show the tray icon and a notification, print
//
// what the tray reports, then quit
//
//	kvit-notes --math-selftest             find the math library, draw one formula, say how it went
//	kvit-notes --version                   print the version
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
	"github.com/kvit-s/kvit-notes/mathtex"
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
	trayCheck := flag.Duration("tray-check", 0, "open the vault with the tray icon, post a notification, print what the tray reports, and quit after this long")
	bench := flag.String("bench", "", "time opening, scrolling and typing in 1,237 blocks of Kvit's documentation, read from this `directory`, and exit")
	mathSelftest := flag.Bool("math-selftest", false, "find the math library and its resources, draw one formula, print where they were found and whether it drew, and exit")
	showVersion := flag.Bool("version", false, "print the version and exit")
	usage := flag.Usage
	flag.Usage = func() {
		useParentConsole()
		usage()
	}
	flag.Parse()

	// These two print and exit before any window starts; a Windows release
	// build prints into the terminal it was started from (console_windows.go).
	if *showVersion {
		useParentConsole()
		fmt.Println("kvit-notes", appVersion())
		return
	}
	if *mathSelftest {
		useParentConsole()
		os.Exit(mathtex.SelfTest(os.Stdout))
	}

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
	if *check == 0 && *closeAfter == 0 && *trayCheck == 0 {
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
			runVault(root, *theme, *closeAfter, *trayCheck)
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
		unison.AllowQuitCallback(app.AllowQuit),
		unison.QuittingCallback(app.Quitting),
		unison.OpenFilesCallback(func(paths []string) { openPaths(ui, paths) }),
		unison.StartupFinishedCallback(func() {
			n, err := newNoteWindow(ui, doc, path)
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			n.page = page
			if *check == 0 && *closeAfter == 0 {
				serveLaterCopies(ui)
				startTray()
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
// and vaults, under the app's keys. It starts as a copy of the app's.
func settingsPath() string {
	p := kvitui.DefaultSettingsPath("kvit-notes")
	vault.SeedSettings(p)
	return p
}

// vaultRoot is the vault to open for the command-line argument: the folder
// named, or with none named the vault the app had open last, else
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
// closes, or for closeAfter when that is set; trayCheck runs the tray
// check for that long instead.
func runVault(root, theme string, closeAfter, trayCheck time.Duration) {
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
		unison.AllowQuitCallback(app.AllowQuit),
		unison.QuittingCallback(app.Quitting),
		unison.OpenFilesCallback(func(paths []string) { openPaths(ui, paths) }),
		unison.StartupFinishedCallback(func() {
			w, err := app.OpenVault(ui, root)
			if err != nil {
				fmt.Fprintf(os.Stderr, "kvit-notes: cannot open %s: %v\n", root, err)
				os.Exit(1)
			}
			w.Win.ToFront()
			if closeAfter == 0 && trayCheck == 0 {
				serveLaterCopies(ui)
			}
			if closeAfter == 0 {
				startTray()
			}
			if trayCheck > 0 {
				runTrayCheck(trayCheck)
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
// what each hands over, as openPath does.
func serveLaterCopies(ui *kvitui.UI) {
	_, err := listen(socketPath(), func(req instanceRequest) {
		done := make(chan struct{})
		unison.InvokeTask(func() {
			defer close(done)
			openPath(ui, req.Open)
		})
		<-done
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, "kvit-notes: later copies will open their own windows:", err)
	}
}

// openPath opens what the running app is asked to open, by a copy started
// later with a path or by the desktop: a folder as a vault, or the window
// it is already open in; a file on its own; and with nothing, or with a
// path that is not there, the app brought forward.
func openPath(ui *kvitui.UI, path string) {
	switch info, err := os.Stat(path); {
	case path == "" || err != nil:
		if ws := unison.Windows(); !app.RaiseAny() && len(ws) > 0 {
			ws[0].ToFront()
		}
	case info.IsDir():
		if err := app.OpenOrRaise(ui, path); err != nil {
			fmt.Fprintf(os.Stderr, "kvit-notes: cannot open %s: %v\n", path, err)
		}
	default:
		openFileWindow(ui, path)
	}
}

// openPaths is unison's OpenFilesCallback, which macOS calls with the files
// and folders Finder or the Dock asks the app to open: a double-click on a
// Markdown file, which the bundle's Info.plist declares the app opens, or
// something dropped on the Dock icon. macOS starts no second copy for them;
// when the app is not running, it starts it without a path and asks
// afterwards, so the vault opened last opens as well. Each path goes where
// one handed over by a later copy goes. Only macOS makes this call; it is
// compiled here but has not run on a Mac. TestPathsOpenAsOnTheCommandLine
// runs the routing.
func openPaths(ui *kvitui.UI, paths []string) {
	for _, p := range paths {
		openPath(ui, p)
	}
}
