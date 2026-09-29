package app

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/drag"
	"github.com/richardwilkes/unison/enums/mod"
)

// session is a vault window on a headless screen.
type session struct {
	t      *testing.T
	screen *unison.HeadlessScreen
	w      *Window
	root   string
}

// notes are written into a new vault for a test, path to text.
type notes map[string]string

func openVault(t *testing.T, n notes) *session {
	t.Helper()
	root := t.TempDir()
	for rel, text := range n {
		p := filepath.Join(root, filepath.FromSlash(rel))
		if strings.HasSuffix(rel, "/") {
			if err := os.MkdirAll(p, 0o755); err != nil {
				t.Fatal(err)
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return openVaultAt(t, root)
}

// openVaultAt opens a vault folder in a window on a headless screen.
func openVaultAt(t *testing.T, root string) *session {
	t.Helper()
	v, err := vault.Open(root)
	if err != nil {
		t.Fatal(err)
	}
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true})
	if err != nil {
		t.Fatal(err)
	}
	ui.Theme.SetReducedMotion(true)
	s := &session{t: t, root: root}
	var openErr error
	s.screen, err = unison.StartHeadless(unison.HeadlessConfig{Width: 1100, Height: 720},
		unison.StartupFinishedCallback(func() {
			if s.w, openErr = Open(ui, v); openErr != nil {
				return
			}
			s.w.Win.SetContentRect(geom.NewRect(0, 0, 1100, 720))
			s.w.Win.ToFront()
		}))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		s.screen.Stop()
		v.Close()
	})
	if openErr != nil {
		t.Fatal(openErr)
	}
	s.screen.EnableAccessibility()
	s.screen.Sync()
	return s
}

func (s *session) do(f func()) { s.screen.Do(f); s.screen.Sync() }

// shot saves the window when KVIT_SHOTS names a directory.
func (s *session) shot(name string) {
	dir := os.Getenv("KVIT_SHOTS")
	if dir == "" {
		return
	}
	_ = os.MkdirAll(dir, 0o755)
	f, err := os.Create(filepath.Join(dir, name))
	if err != nil {
		s.t.Fatal(err)
	}
	defer f.Close()
	_ = png.Encode(f, s.screen.CaptureWindow(s.w.Win.Window))
}

func (s *session) listed() []string {
	var titles []string
	s.do(func() {
		for _, it := range s.w.list.Items {
			titles = append(titles, it.Title)
		}
	})
	return titles
}

func (s *session) openTitle() string {
	var title string
	s.do(func() {
		if s.w.open != nil {
			title = s.w.open.Title
		}
	})
	return title
}

func (s *session) file(rel string) string {
	data, err := os.ReadFile(filepath.Join(s.root, filepath.FromSlash(rel)))
	if err != nil {
		s.t.Fatal(err)
	}
	return string(data)
}

// clickRow presses a row of the note list.
func (s *session) clickRow(i int) {
	var p geom.Point
	s.do(func() {
		h := s.w.list.rowHeight()
		p = s.screen.PanelPoint(s.w.list, geom.NewPoint(40, float32(i)*h+h/2))
	})
	s.screen.Click(p)
}

// clickScope presses the sidebar row with a label.
func (s *session) clickScope(label string) {
	var p geom.Point
	found := false
	s.do(func() {
		tops, heights := s.w.scopes.geometry()
		for i, r := range s.w.scopes.Rows {
			if r.Label == label {
				p = s.screen.PanelPoint(s.w.scopes, geom.NewPoint(60, tops[i]+heights[i]/2))
				found = true
			}
		}
	})
	if !found {
		s.t.Fatalf("no sidebar row %q", label)
	}
	s.screen.Click(p)
}

var demo = notes{
	"Welcome.md":             "---\ntags: [start]\npinned: true\n---\n# Welcome\n\nWelcome to **Kvit**.\n",
	"Reading list.md":        "Books to read this **summer**, in order\n",
	"Ideas/Kvit editor.md":   "",
	"Ideas/Projects/Plan.md": "---\ntags: [work, start]\nfavorite: true\n---\nThe plan.\n",
	"Journal/":               "",
	".kvit/collection.json":  `{"folders":{"Journal":{"color":"#e05c5c","expanded":true}},"tagColors":{"work":"#4a90d9"}}`,
}

func TestTheWindowShowsTheVault(t *testing.T) {
	s := openVault(t, demo)
	s.shot("vault_01_opened.png")
	var labels []string
	var counts []int
	s.do(func() {
		for _, r := range s.w.scopes.Rows {
			labels = append(labels, r.Label)
			counts = append(counts, r.Count)
		}
	})
	want := []string{"All Notes", "★ Favorites", "Folders", "Ideas", "Projects", "Journal", "Tags", "#start", "#work", "Trash"}
	if !slices.Equal(labels, want) {
		t.Errorf("sidebar: %q\nwant %q", labels, want)
	}
	if counts[0] != 4 || counts[1] != 1 {
		t.Errorf("counts: %v", counts)
	}
	s.do(func() {
		for _, r := range s.w.scopes.Rows {
			if (r.Label == "Journal" || r.Label == "#work") != r.HasMark {
				t.Errorf("%s: colour mark %v", r.Label, r.HasMark)
			}
		}
	})
	if got := s.listed(); len(got) != 4 || got[0] != "Welcome" {
		t.Errorf("the pinned note should come first: %q", got)
	}
	if s.openTitle() == "" {
		t.Errorf("a note should be open")
	}
	s.clickScope("Ideas")
	if got := s.listed(); !slices.Equal(got, []string{"Kvit editor"}) {
		t.Errorf("the Ideas folder shows %q", got)
	}
	s.clickScope("#start")
	if got := s.listed(); len(got) != 2 {
		t.Errorf("the start tag shows %q", got)
	}
	s.clickScope("All Notes")
	s.waitFor("the index", func() bool { return s.w.index.Len() == 4 })
	s.do(func() { s.w.search.SetText("summer") })
	var found []string
	s.do(func() { found = s.w.Results() })
	if !slices.Equal(found, []string{"Reading list", "  Books to read this summer, in order"}) {
		t.Errorf("searching summer shows %q", found)
	}
	s.shot("vault_02_searched.png")
}

func TestEditingSavesTheNote(t *testing.T) {
	s := openVault(t, demo)
	idx := slices.Index(s.listed(), "Reading list")
	s.clickRow(idx)
	if s.openTitle() != "Reading list" {
		t.Fatalf("clicking the row should open it, open is %q", s.openTitle())
	}
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.KeyPress(unison.KeyEnd, mod.None)
	s.screen.Type(" and then some")
	s.screen.KeyPress(unison.KeyS, mod.Control)
	if got := s.file("Reading list.md"); got != "Books to read this **summer**, in order and then some\n" {
		t.Errorf("Ctrl+S should save: %q", got)
	}
	s.screen.Type(" more")
	// The pending save is written when another note opens.
	s.clickRow(slices.Index(s.listed(), "Welcome"))
	if got := s.file("Reading list.md"); !strings.HasSuffix(got, "then some more\n") {
		t.Errorf("switching notes should save the last edit: %q", got)
	}
	if got := s.file("Welcome.md"); got != demo["Welcome.md"] {
		t.Errorf("opening a note must not change it: %q", got)
	}
}

func TestAutoSaveAfterTypingStops(t *testing.T) {
	s := openVault(t, demo)
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.Type("Now ")
	deadline := time.Now().Add(saveDelay + 2*time.Second)
	for time.Now().Before(deadline) && !strings.HasPrefix(s.file("Reading list.md"), "Now ") {
		time.Sleep(100 * time.Millisecond)
		s.screen.Sync()
	}
	if got := s.file("Reading list.md"); !strings.HasPrefix(got, "Now Books") {
		t.Errorf("the note should save itself after the typing stops: %q", got)
	}
}

func TestNewNoteAndTags(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyN, mod.Control)
	if s.openTitle() != "Untitled" {
		t.Fatalf("Ctrl+N should open a new Untitled note, open is %q", s.openTitle())
	}
	if _, err := os.Stat(filepath.Join(s.root, "Untitled.md")); err != nil {
		t.Errorf("the new note should be a file: %v", err)
	}
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.do(func() { s.w.tags.OnAdd("books") })
	if got := s.file("Reading list.md"); got != "---\ntags: [books]\n---\nBooks to read this **summer**, in order\n" {
		t.Errorf("adding a tag: %q", got)
	}
	s.do(func() { s.w.tags.OnRemove("books") })
	if got := s.file("Reading list.md"); got != "Books to read this **summer**, in order\n" {
		t.Errorf("removing the last tag should remove the front matter: %q", got)
	}
}

// press presses the button a screen reader knows by name.
func (s *session) press(name string) {
	s.t.Helper()
	tree := s.screen.AccessibilityTree(s.w.Win.Window)
	for _, n := range tree.Nodes {
		if n.Name == name && n.Actions.Has(accessibility.Press) {
			s.screen.PerformAccessibilityAction(accessibility.ActionRequest{Node: n.ID, Action: accessibility.Press})
			s.screen.Sync()
			return
		}
	}
	s.t.Fatalf("no button %q", name)
}

func (s *session) exists(rel string) bool {
	_, err := os.Stat(filepath.Join(s.root, filepath.FromSlash(rel)))
	return err == nil
}

// features.md 8.3: an Untitled note takes its name from its first block
// once that block is finished.
func TestAnUntitledNoteIsNamedAfterItsFirstBlock(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyN, mod.Control)
	s.screen.Type("Grocery list")
	if s.openTitle() != "Untitled" {
		t.Errorf("the note must keep its name while its first block is being typed, it is %q", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	s.screen.Type("milk")
	if s.openTitle() != "Grocery list" || !s.exists("Grocery list.md") || s.exists("Untitled.md") {
		t.Errorf("leaving the first block should name the note: %q", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyS, mod.Control)
	if got := s.file("Grocery list.md"); got != "Grocery list\n\nmilk\n" {
		t.Errorf("the renamed note: %q", got)
	}
	// Only once: editing the first block afterwards leaves the name alone.
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.Type("My ")
	s.do(func() { s.w.Editor.FocusBlock(1, 0) })
	if s.openTitle() != "Grocery list" {
		t.Errorf("a named note keeps its name: %q", s.openTitle())
	}
}

// A note that opens with a display equation keeps its automatic name, as the
// app's titleBearing (qml/NoteAutoTitle) leaves a note that opens with
// code, a table or a picture.
func TestAnUntitledNoteOpeningWithAnEquationKeepsItsName(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyN, mod.Control)
	s.screen.Type("/math")
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	s.screen.Type("x^2")
	if k := s.w.Editor.Doc.Blocks[0].Kind; k != editor.Math {
		t.Fatalf("the / menu's Math should make the first block an equation, it is %v", k)
	}
	s.screen.KeyPress(unison.KeyReturn, mod.Control)
	s.screen.Type("milk")
	if s.openTitle() != "Untitled" {
		t.Errorf("a note opening with an equation should keep its name, it is %q", s.openTitle())
	}
}

func TestPinFromTheNotesMenu(t *testing.T) {
	s := openVault(t, demo)
	i := slices.Index(s.listed(), "Reading list")
	var p geom.Point
	s.do(func() {
		h := s.w.list.rowHeight()
		p = s.screen.PanelPoint(s.w.list, geom.NewPoint(40, float32(i)*h+h/2))
	})
	s.screen.ClickWith(p, unison.ButtonRight, mod.None)
	s.press("Pin to top")
	if got := s.file("Reading list.md"); got != "---\npinned: true\n---\nBooks to read this **summer**, in order\n" {
		t.Errorf("pinning: %q", got)
	}
	if got := s.listed(); got[0] != "Reading list" && got[1] != "Reading list" {
		t.Errorf("a pinned note goes to the top: %q", got)
	}
}

func TestRenameAndTrashFromTheKeyboard(t *testing.T) {
	s := openVault(t, demo)
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.screen.KeyPress(unison.KeyF2, mod.None)
	s.screen.Type("Books\n")
	if !s.exists("Books.md") || s.exists("Reading list.md") {
		t.Fatalf("F2 and a new name should rename the note: %q", s.listed())
	}
	s.do(func() { s.w.list.RequestFocus() })
	s.screen.KeyPress(unison.KeyDelete, mod.None)
	s.press("Move to trash")
	if s.exists("Books.md") {
		t.Errorf("the note should have gone to the trash")
	}
	trashed, _ := filepath.Glob(filepath.Join(s.root, ".kvit", "trash", "*-Books.md"))
	if len(trashed) != 1 {
		t.Errorf("trash: %q", trashed)
	}
	if s.openTitle() == "Books" {
		t.Errorf("another note should be open")
	}
}

func TestCtrlBackslashHidesTheSidePanes(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyBackslash, mod.Control)
	var hidden, listShown bool
	s.do(func() { hidden, listShown = s.w.SidesHidden(), s.w.list.Window() != nil })
	if !hidden || listShown {
		t.Errorf("Ctrl+\\ should hide the note list")
	}
	s.shot("vault_03_sides_hidden.png")
	s.screen.KeyPress(unison.KeyBackslash, mod.Control)
	s.do(func() { hidden, listShown = s.w.SidesHidden(), s.w.list.Window() != nil })
	if hidden || !listShown {
		t.Errorf("Ctrl+\\ again should bring the note list back")
	}
}

// features.md 8.1: notes move between folders by dragging them onto one.
func TestDragANoteOntoAFolder(t *testing.T) {
	s := openVault(t, demo)
	i := slices.Index(s.listed(), "Reading list")
	var from, to geom.Point
	s.do(func() {
		h := s.w.list.rowHeight()
		from = s.screen.PanelPoint(s.w.list, geom.NewPoint(40, float32(i)*h+h/2))
		tops, heights := s.w.scopes.geometry()
		for k, r := range s.w.scopes.Rows {
			if r.Label == "Journal" {
				to = s.screen.PanelPoint(s.w.scopes, geom.NewPoint(60, tops[k]+heights[k]/2))
			}
		}
	})
	s.screen.MouseDown(from, unison.ButtonLeft, mod.None)
	for k := 1; k <= 5; k++ {
		s.screen.MouseMove(from.Add(to.Sub(from).Mul(float32(k)/5)), mod.None)
	}
	var target int
	s.do(func() { target = s.w.scopes.Target })
	if target < 0 {
		t.Errorf("the folder under a dragged note should be marked")
	}
	s.shot("vault_04_dragging.png")
	s.screen.MouseUp(to, unison.ButtonLeft, mod.None)
	if !s.exists("Journal/Reading list.md") || s.exists("Reading list.md") {
		t.Errorf("dropping the note on Journal should move it there")
	}
}

// features.md 8.1: folders move by dragging them onto another folder.
func TestDragAFolderOntoAFolder(t *testing.T) {
	s := openVault(t, demo)
	var from, to geom.Point
	s.do(func() {
		tops, heights := s.w.scopes.geometry()
		for k, r := range s.w.scopes.Rows {
			if r.Label == "Projects" {
				from = s.screen.PanelPoint(s.w.scopes, geom.NewPoint(60, tops[k]+heights[k]/2))
			}
			if r.Label == "Journal" {
				to = s.screen.PanelPoint(s.w.scopes, geom.NewPoint(60, tops[k]+heights[k]/2))
			}
		}
	})
	if from == (geom.Point{}) || to == (geom.Point{}) {
		t.Fatal("the Projects and Journal rows should be shown")
	}
	s.screen.MouseDown(from, unison.ButtonLeft, mod.None)
	for k := 1; k <= 5; k++ {
		s.screen.MouseMove(from.Add(to.Sub(from).Mul(float32(k)/5)), mod.None)
	}
	var target int
	s.do(func() { target = s.w.scopes.Target })
	if target < 0 {
		t.Errorf("the folder under a dragged folder should be marked")
	}
	s.shot("vault_04b_dragging_folder.png")
	s.screen.MouseUp(to, unison.ButtonLeft, mod.None)
	if !s.exists("Journal/Projects/Plan.md") || s.exists("Ideas/Projects/Plan.md") {
		t.Errorf("dropping Projects on Journal should move it there")
	}
}

// features.md 5.3: a picture on the clipboard pastes as an image block,
// saved into the vault's picture folder.
func TestPastedPictureIsSavedAsAnImageBlock(t *testing.T) {
	s := openVault(t, demo)
	var buf bytes.Buffer
	img := image.NewRGBA(image.Rect(0, 0, 40, 20))
	for x := range 40 {
		for y := range 20 {
			img.Set(x, y, color.RGBA{R: uint8(x * 6), G: 90, B: 160, A: 255})
		}
	}
	if err := png.Encode(&buf, img); err != nil {
		t.Fatal(err)
	}
	s.do(func() {
		unison.ClipboardSetData(drag.Data{Type: uti.PNG, Data: buf.Bytes()})
		s.w.Editor.FocusBlock(0, 0)
	})
	s.screen.KeyPress(unison.KeyV, mod.Control)
	var kind editor.Kind
	s.do(func() {
		for _, b := range s.w.Editor.Doc.Blocks {
			if b.Kind == editor.Image {
				kind = b.Kind
			}
		}
	})
	if kind != editor.Image {
		t.Fatalf("a pasted picture should become an image block")
	}
	matches, _ := filepath.Glob(filepath.Join(s.root, "assets", "*.png"))
	if len(matches) != 1 {
		t.Errorf("the picture should be saved into assets/: %v", matches)
	}
}

// features.md 1.2.8: a lone image line shows its picture, found beside the
// note or from the top of the vault.
func TestPicturesAreDrawn(t *testing.T) {
	var buf bytes.Buffer
	img := image.NewRGBA(image.Rect(0, 0, 200, 100))
	for x := range 200 {
		for y := range 100 {
			img.Set(x, y, color.RGBA{R: uint8(x), G: 90, B: 160, A: 255})
		}
	}
	if err := png.Encode(&buf, img); err != nil {
		t.Fatal(err)
	}
	s := openVault(t, notes{
		"assets/pic.png": buf.String(),
		"Pictures.md":    "Before\n\n![A test picture|150](assets/pic.png \"Its caption\")\n\n![Missing](assets/none.png)\n",
	})
	s.clickRow(slices.Index(s.listed(), "Pictures"))
	var kinds []editor.Kind
	var rows []float32
	s.do(func() {
		for i, b := range s.w.Editor.Doc.Blocks {
			kinds = append(kinds, b.Kind)
			rows = append(rows, s.w.Editor.RowRect(i).Height)
		}
	})
	if !slices.Equal(kinds, []editor.Kind{editor.Paragraph, editor.Image, editor.Image}) {
		t.Fatalf("kinds: %v", kinds)
	}
	// 150 wide keeps the picture's shape: 75 high, plus the caption.
	if rows[1] < 75+28 || rows[1] > 75+28+30 {
		t.Errorf("the picture's row is %v high", rows[1])
	}
	s.shot("vault_08_pictures.png")
	if got := s.file("Pictures.md"); !strings.Contains(got, "![A test picture|150](assets/pic.png \"Its caption\")") {
		t.Errorf("the line must stay as written: %q", got)
	}
}

func TestAPictureFromOutsideIsCopiedIntoAssets(t *testing.T) {
	s := openVault(t, demo)
	outside := filepath.Join(t.TempDir(), "My Photo (1).png")
	if err := os.WriteFile(outside, []byte("png bytes"), 0o644); err != nil {
		t.Fatal(err)
	}
	var stored string
	var err error
	s.do(func() { stored, err = s.w.ingestFile(outside, "Ideas/Reading List (2).md") })
	if err != nil || !strings.HasPrefix(stored, "assets/reading-list-2-") || !strings.HasSuffix(stored, ".png") {
		t.Fatalf("stored as %q: %v", stored, err)
	}
	if !s.exists(stored) {
		t.Errorf("the copy should be in the vault")
	}
	inside := filepath.Join(s.root, "Ideas", "inside.png")
	_ = os.WriteFile(inside, []byte("x"), 0o644)
	s.do(func() { stored, err = s.w.ingestFile(inside, "Ideas/Reading List (2).md") })
	if stored != "Ideas/inside.png" {
		t.Errorf("a picture already in the vault is linked where it is: %q", stored)
	}
}

func TestBackForwardAndTheQuickSwitcher(t *testing.T) {
	s := openVault(t, demo)
	first := s.openTitle()
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.clickRow(slices.Index(s.listed(), "Plan"))
	s.screen.KeyPress(unison.KeyLeft, mod.Option)
	if s.openTitle() != "Reading list" {
		t.Errorf("Alt+Left should go back to Reading list, it is %q", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyLeft, mod.Option)
	if s.openTitle() != first {
		t.Errorf("and again to %q, it is %q", first, s.openTitle())
	}
	s.screen.KeyPress(unison.KeyRight, mod.Option)
	if s.openTitle() != "Reading list" {
		t.Errorf("Alt+Right should go forward, it is %q", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyP, mod.Control)
	s.screen.Type("kvit ed")
	s.shot("vault_09_switcher.png")
	s.screen.KeyPress(unison.KeyDown, mod.None)
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	if s.openTitle() != "Kvit editor" {
		t.Errorf("the switcher should open Kvit editor, open is %q", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyP, mod.Control)
	s.screen.Type("Brand new idea\n")
	if s.openTitle() != "Brand new idea" || !s.exists("Brand new idea.md") {
		t.Errorf("unmatched words should make a note: %q", s.openTitle())
	}
}
