package app

import (
	"bytes"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/export"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/drag"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// features.md 18.1: File, New from template makes a note named after the
// template, with its text filled in and its tags.
func TestANoteFromATemplate(t *testing.T) {
	s := openVault(t, demo)
	s.press("File")
	s.press("New from template")
	s.press("Meeting Notes")
	if s.openTitle() != "Meeting Notes" {
		t.Fatalf("the new note should be open, open is %q", s.openTitle())
	}
	got := s.file("Meeting Notes.md")
	today := time.Now().Format("2006-01-02")
	if !strings.HasPrefix(got, "---\ntags: [meeting]\n---\n# Meeting Notes\n\n**Date:** "+today+"  \n**Time:** ") {
		t.Errorf("the note from the template: %q", got)
	}
	if !s.exists(".kvit/templates/Daily Journal.md") {
		t.Errorf("the built-in templates should be written the first time the menu opens")
	}
	// A second note from the same template takes the next free name.
	s.press("File")
	s.press("New from template")
	s.press("Meeting Notes")
	if s.openTitle() != "Meeting Notes 2" {
		t.Errorf("the second note is %q", s.openTitle())
	}
}

const outlined = "# Plan\n\nIntro\n\n## Goals\n\nText\n\n### Detail\n\n## Risks\n\nMore\n"

// features.md 17.1: the outline lists the headings, follows the caret and
// goes to a heading.
func TestTheOutline(t *testing.T) {
	s := openVault(t, notes{"Plan.md": outlined})
	s.press("View")
	s.press("Outline")
	var rows []string
	s.do(func() { rows = s.w.outline.Rows() })
	if !slices.Equal(rows, []string{"Plan", "  Goals", "    Detail", "  Risks"}) {
		t.Fatalf("outline rows: %q", rows)
	}
	s.do(func() { s.w.Editor.FocusBlock(3, 0) }) // "Text", under Goals
	var current int
	s.do(func() { current = s.w.outline.Current() })
	if current != 1 {
		t.Errorf("the current section should be Goals, is row %d", current)
	}
	s.do(func() { s.w.outline.Go(3) })
	var caret int
	s.do(func() { caret = s.w.Editor.Doc.Index(s.w.Editor.Doc.Caret.Block) })
	if caret != 5 {
		t.Errorf("going to Risks should put the caret in block 5, it is in %d", caret)
	}
	s.shot("features_01_outline.png")
	// Only level 2 and 1 headings, from the H… menu.
	s.do(func() {
		s.w.outline.Levels = 0b0011
		s.w.outline.Refresh()
		rows = s.w.outline.Rows()
	})
	if !slices.Equal(rows, []string{"Plan", "  Goals", "  Risks"}) {
		t.Errorf("levels 1 and 2: %q", rows)
	}
	// The setting is kept.
	s.do(func() { s.w.showPane("view.outline", false) })
	if s.w.prefs.bool("view.outline", true) {
		t.Errorf("hiding the outline should be remembered")
	}
}

// features.md 16.1: focus mode leaves the editor alone, and Escape ends it.
func TestFocusMode(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyF11, mod.None)
	var toolbar, list bool
	s.do(func() { toolbar, list = s.w.toolbar.Window() != nil, s.w.list.Window() != nil })
	if !s.w.FocusMode() || toolbar || list {
		t.Fatalf("focus mode: on %v, toolbar shown %v, list shown %v", s.w.FocusMode(), toolbar, list)
	}
	s.shot("features_02_focus_mode.png")
	s.screen.KeyPress(unison.KeyEscape, mod.None)
	s.do(func() { toolbar, list = s.w.toolbar.Window() != nil, s.w.list.Window() != nil })
	if s.w.FocusMode() || !toolbar || !list {
		t.Errorf("Escape should end focus mode: on %v, toolbar %v, list %v", s.w.FocusMode(), toolbar, list)
	}
}

// View, Sidebar hides the sidebar alone; the note list stays.
func TestPanesHideOneAtATime(t *testing.T) {
	s := openVault(t, demo)
	s.press("View")
	s.press("Sidebar")
	var side, list bool
	s.do(func() { side, list = s.w.scopes.Window() != nil, s.w.list.Window() != nil })
	if side || !list {
		t.Fatalf("after hiding the sidebar: sidebar %v, list %v", side, list)
	}
	s.press("Hide the note list")
	s.do(func() { side, list = s.w.scopes.Window() != nil, s.w.list.Window() != nil })
	if side || list {
		t.Fatalf("after hiding the list too: sidebar %v, list %v", side, list)
	}
	s.press("View")
	s.press("Sidebar")
	s.do(func() { side = s.w.scopes.Window() != nil })
	if !side {
		t.Errorf("the sidebar should be back")
	}
}

// The toolbar's block type list and Insert menu.
func TestTheToolbarChangesAndInsertsBlocks(t *testing.T) {
	s := openVault(t, notes{"A.md": "one\n\ntwo\n"})
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.do(func() { s.w.toolbar.kind.Choose("Heading 2") })
	var kind editor.Kind
	s.do(func() { kind = s.w.Editor.Doc.Blocks[0].Kind })
	if kind != editor.Heading2 {
		t.Errorf("the block should be a heading 2, it is %v", kind)
	}
	var shown string
	s.do(func() { shown = s.w.toolbar.BlockKind() })
	if shown != "Heading 2" {
		t.Errorf("the list should show the caret's kind: %q", shown)
	}
	s.press("+ Insert")
	s.press("Table")
	var blocks []editor.Block
	s.do(func() { blocks = s.w.Editor.Doc.Blocks })
	if len(blocks) != 3 || blocks[1].Kind != editor.Table {
		t.Fatalf("a table should follow the first block: %v", blocks)
	}
	s.press("Align center")
	s.do(func() { blocks = s.w.Editor.Doc.Blocks })
	if a, _ := blocks[1].Attr("align"); a != "center" {
		t.Errorf("align center: %q", blocks[1].Attrs)
	}
}

// The settings dialog changes the shared typography, which the editor
// follows.
func TestSettingsChangeTheEditor(t *testing.T) {
	s := openVault(t, demo)
	s.press("File")
	s.press("Settings…")
	s.press("Typography")
	s.shot("features_03_settings.png")
	var before, after float32
	s.do(func() {
		before = s.w.Editor.RowRect(1).Y
		s.w.ui.Typography.SetParagraphSpacing(20)
	})
	s.do(func() { s.w.Editor.Refresh(); after = s.w.Editor.RowRect(1).Y })
	if after-before < 15 {
		t.Errorf("block spacing 20 should move the second block down by 16: %v to %v", before, after)
	}
}

// features.md 19: the word count opens the statistics, and the goal
// dialog writes the note's goal.
func TestStatisticsAndTheWritingGoal(t *testing.T) {
	s := openVault(t, notes{"A.md": "one two three\n"})
	s.press("3 words")
	var shown bool
	s.do(func() { shown = s.w.stats != nil && s.w.stats.Opened() })
	if !shown {
		t.Fatalf("the statistics should be open")
	}
	s.shot("features_04_statistics.png")
	s.screen.KeyPress(unison.KeyEscape, mod.None)
	s.press("goal")
	s.screen.Type("500")
	s.press("Set goal")
	if got := s.file("A.md"); got != "---\ngoal: 500\n---\none two three\n" {
		t.Errorf("the goal in the front matter: %q", got)
	}
	var facts []string
	s.do(func() {
		s.w.update()
		for _, f := range s.w.status.Groups[0].Facts {
			facts = append(facts, f.Text)
		}
	})
	if !slices.Equal(facts, []string{"3 words", "1%"}) {
		t.Errorf("status facts: %q", facts)
	}
}

// features.md 1.2.14: a web page's card reads the page only when Load
// preview is pressed.
func TestAnEmbedCardLoadsItsPreviewOnRequest(t *testing.T) {
	hits := 0
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		hits++
		fmt.Fprint(w, `<html><head><title>Plain</title><meta property="og:title" content="A &amp; B article"><meta name="description" content="What it says"></head></html>`)
	}))
	defer srv.Close()
	page := srv.URL + "/great-article"
	s := openVault(t, notes{"Reading.md": "# Reading list\n\n![](" + page + ")\n"})
	var p geom.Point
	s.do(func() {
		s.w.Editor.ClearFocus()
		p = s.screen.PanelPoint(s.w.Editor, s.w.Editor.PartRect(1, "load").Center())
	})
	if hits != 0 {
		t.Fatalf("the page must not be read before Load preview")
	}
	s.shot("features_05_embed.png")
	s.screen.Click(p)
	s.waitFor("the preview", func() bool {
		var title string
		s.do(func() { title = s.w.Editor.PreviewTitle(page) })
		return title == "A & B article"
	})
	if hits != 1 {
		t.Errorf("the page should be read once, it was read %d times", hits)
	}
}

// Menu access keys: Alt+V opens View, and O chooses Outline in it.
func TestMenuAccessKeys(t *testing.T) {
	s := openVault(t, notes{"Plan.md": outlined})
	s.screen.KeyPress(unison.KeyV, mod.Option)
	s.screen.KeyPress(unison.KeyO, mod.None)
	var shown bool
	s.do(func() { shown = s.w.outline.Window() != nil })
	if !shown {
		t.Errorf("Alt+V, O should show the outline")
	}
}

// features.md 12.7: Ctrl+Alt+N opens quick capture, and Ctrl+Enter makes a
// note of what was typed, named from its first line.
func TestQuickCapture(t *testing.T) {
	s := openVault(t, demo)
	s.screen.KeyPress(unison.KeyN, mod.Control|mod.Option)
	var open bool
	s.do(func() { open = s.w.capture != nil && s.w.capture.IsValid() })
	if !open {
		t.Fatalf("quick capture should be open")
	}
	s.screen.Type("Groceries\nmilk")
	s.screen.KeyPress(unison.KeyReturn, mod.Control)
	if got := s.file("Groceries.md"); got != "Groceries\nmilk\n" {
		t.Errorf("the captured note: %q", got)
	}
	s.do(func() { open = s.w.capture.IsValid() })
	if open {
		t.Errorf("the window should close after saving")
	}
	if !slices.Contains(s.listed(), "Groceries") {
		t.Errorf("the note list should show the new note: %q", s.listed())
	}
}

// features.md 9.3: selecting text in a block shows the formatting bar,
// whose buttons act on the selection.
func TestTheFormattingBar(t *testing.T) {
	s := openVault(t, notes{"A.md": "Select a few of these words\n"})
	s.do(func() { s.w.Editor.FocusBlock(0, 7) })
	s.screen.KeyPress(unison.KeyRight, mod.Shift)
	s.screen.KeyPress(unison.KeyRight, mod.Shift|mod.Control)
	var popups int
	s.do(func() { popups = len(s.w.Win.Popups()) })
	if popups != 1 {
		t.Fatalf("a selection should show the formatting bar, popups: %d", popups)
	}
	s.shot("features_06_formatting_bar.png")
	var bold geom.Point
	s.do(func() {
		b := s.w.Editor.FormatBarButtons()[0]
		bold = s.screen.PanelPoint(b, b.ContentRect(false).Center())
	})
	s.screen.Click(bold)
	if got := s.editorText(); got != "Select **a few** of these words" {
		t.Errorf("bold from the bar: %q", got)
	}
	s.screen.KeyPress(unison.KeyEnd, mod.None)
	s.do(func() { popups = len(s.w.Win.Popups()) })
	if popups != 0 {
		t.Errorf("the bar should go with the selection")
	}
}

// features.md 7.1–7.2: Ctrl+F finds in the note as the reader sees it,
// Enter moves between matches, Ctrl+H replaces one or all.
func TestFindAndReplace(t *testing.T) {
	s := openVault(t, notes{"A.md": "The **old** fox and the old hound\n\n- old bullet entry\n\nNothing to replace here\n"})
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.KeyPress(unison.KeyF, mod.Control)
	s.screen.Type("old")
	count := func() string {
		var c string
		s.do(func() { _, c = s.w.FindOpen() })
		return c
	}
	if got := count(); got != "1 of 3" {
		t.Fatalf("count: %q", got)
	}
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	if got := count(); got != "2 of 3" {
		t.Errorf("Enter should move on: %q", got)
	}
	s.screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.shot("features_07_find.png")
	// "old fox" across the bold markers.
	s.do(func() { s.w.finder.query.SetText("old fox") })
	if got := count(); got != "1 of 1" {
		t.Errorf("a match across markers: %q", got)
	}
	s.do(func() { s.w.finder.query.SetText("(") })
	flip := func(key string) {
		s.do(func() {
			b := s.w.finder.toggles[key]
			b.Checked = !b.Checked
			b.OnClick()
		})
	}
	flip("find.useRegex")
	if got := count(); got != "Invalid pattern" {
		t.Errorf("a broken regular expression: %q", got)
	}
	flip("find.useRegex")
	s.screen.KeyPress(unison.KeyEscape, mod.None)
	s.screen.KeyPress(unison.KeyH, mod.Control)
	s.do(func() {
		s.w.finder.query.SetText("old")
		s.w.finder.repl.SetText("new")
	})
	s.do(func() { s.w.finder.replaceOne() })
	// Replacing all of a span's text replaces the span, as selecting it
	// and typing would (Qt's cutRangeResult).
	if got := s.editorText(); got != "The new fox and the old hound\nold bullet entry\nNothing to replace here" {
		t.Errorf("replace one: %q", got)
	}
	s.do(func() { s.w.finder.replaceAll() })
	s.press("Replace All")
	s.screen.KeyPress(unison.KeyS, mod.Control)
	if got := s.file("A.md"); got != "The new fox and the new hound\n\n- new bullet entry\n\nNothing to replace here\n" {
		t.Errorf("replace all: %q", got)
	}
}

// features.md 8.4: the search field finds notes by their text and title,
// lists the lines found, and opens a note at a line.
func TestSearchAcrossNotes(t *testing.T) {
	s := openVault(t, notes{
		"Foxes.md":             "The quick brown fox jumps over the lazy dog\n",
		"Ideas/Field notes.md": "Saw a **fox** near the fence today\n\nThe fox came back at dusk\n",
		"Ideas/Recipes.md":     "Nothing furry in here\n",
	})
	s.waitFor("the index", func() bool {
		var n int
		s.do(func() { n = s.w.index.Len() })
		return n == 3
	})
	s.do(func() { s.w.search.SetText("fox") })
	var rows []string
	s.do(func() { rows = s.w.Results() })
	want := []string{"Foxes", "  The quick brown fox jumps over the lazy dog", "Field notes", "  Saw a fox near the fence today", "  The fox came back at dusk"}
	if !slices.Equal(rows, want) {
		t.Fatalf("results:\n%q\nwant\n%q", rows, want)
	}
	var count string
	s.do(func() { count = s.w.searchCount.Text })
	if count != "3 match(es) in 2 note(s)" {
		t.Errorf("count: %q", count)
	}
	s.shot("features_08_search.png")
	s.do(func() { s.w.results.choose(4) })
	if s.openTitle() != "Field notes" {
		t.Fatalf("the line should open its note, open is %q", s.openTitle())
	}
	var from, to editor.Pos
	s.do(func() { from, to = s.w.Editor.Doc.SelRange() })
	if from.Off != 4 || to.Off != 7 {
		t.Errorf("the caret should select the match: %v to %v", from, to)
	}
	// A saved change is found at once.
	s.do(func() { s.w.Editor.FocusBlock(1, 0) })
	s.screen.Type("A fox again. ")
	s.screen.KeyPress(unison.KeyS, mod.Control)
	s.do(func() { s.w.refreshList(); rows = s.w.Results() })
	if !slices.Contains(rows, "  A fox again. The fox came back at dusk") {
		t.Errorf("the saved text should be found: %q", rows)
	}
	s.do(func() { s.w.search.SetText("") })
	if got := s.listed(); len(got) != 3 {
		t.Errorf("clearing the search should bring the list back: %q", got)
	}
}

// features.md 2.4 and 8.5: following wiki links and Markdown links, making
// the note a link names, and the link dialog.
func TestFollowingLinksAndTheLinkDialog(t *testing.T) {
	s := openVault(t, notes{
		"Home.md":           "See [[Plan#Risks|the risks]] and [[New idea]] and [intro](#intro)\n\n## Intro\n\nText\n",
		"Ideas/Plan.md":     "# Plan\n\n## Goals\n\nG\n\n## Risks\n\nR\n",
		"Ideas/Old/Plan.md": "x\n",
	})
	open := func(title string) {
		s.t.Helper()
		s.clickRow(slices.Index(s.listed(), title))
		if s.openTitle() != title {
			s.t.Fatalf("could not open %s", title)
		}
	}
	open("Home")
	press := func(block int, text string) {
		s.t.Helper()
		var p geom.Point
		s.do(func() {
			src := s.w.Editor.Doc.Blocks[block].Text
			off := len([]rune(src[:strings.Index(src, text)])) + 1
			s.w.Editor.ClearFocus()
			p = s.screen.PanelPoint(s.w.Editor, s.w.Editor.TextPoint(block, off))
		})
		s.screen.Click(p)
	}
	// "Plan" names two notes: the choice is offered and nothing opens.
	press(0, "the risks")
	if s.openTitle() != "Home" {
		t.Fatalf("an ambiguous link must not open a note, %q is open", s.openTitle())
	}
	s.press("Ideas/Plan.md")
	if s.openTitle() != "Plan" {
		t.Fatalf("choosing a note should open it, %q is open", s.openTitle())
	}
	var caret string
	s.do(func() { caret = s.w.Editor.Doc.CaretBlock().Text })
	if caret != "Risks" {
		t.Errorf("the link should open at its heading, the caret is in %q", caret)
	}
	s.screen.KeyPress(unison.KeyLeft, mod.Option)
	// A link to a note that does not exist makes it beside the note.
	press(0, "New idea")
	if s.openTitle() != "New idea" || !s.exists("New idea.md") {
		t.Fatalf("following [[New idea]] should make it, %q is open", s.openTitle())
	}
	s.screen.KeyPress(unison.KeyLeft, mod.Option)
	press(0, "intro")
	s.do(func() { caret = s.w.Editor.Doc.CaretBlock().Text })
	if caret != "Intro" {
		t.Errorf("[intro](#intro) should go to the heading, the caret is in %q", caret)
	}
	// Ctrl+K on a selection makes a link; on a link, edits it.
	s.do(func() {
		d := s.w.Editor.Doc
		d.Anchor, d.Caret = editor.Pos{Block: d.Blocks[2].ID, Off: 0}, editor.Pos{Block: d.Blocks[2].ID, Off: 4}
		d.Focused = true
	})
	s.screen.KeyPress(unison.KeyK, mod.Control)
	s.screen.Type("https://kvit.app")
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	var text string
	s.do(func() { text = s.w.Editor.Doc.Blocks[2].Text })
	if text != "[Text](https://kvit.app)" {
		t.Fatalf("Ctrl+K on a selection: %q", text)
	}
	s.do(func() { s.w.Editor.FocusBlock(2, 3) })
	s.screen.KeyPress(unison.KeyK, mod.Control)
	s.shot("features_09_link_dialog.png")
	s.press("Remove link")
	s.do(func() { text = s.w.Editor.Doc.Blocks[2].Text })
	if text != "Text" {
		t.Errorf("Remove link should leave the text: %q", text)
	}
}

// Typing "[[" offers the notes; after "#", a note's headings.
func TestWikiLinkCompletion(t *testing.T) {
	s := openVault(t, notes{
		"Home.md":           "Start\n",
		"Ideas/Plan.md":     "# Plan\n\n## Risks\n\nR\n",
		"Ideas/Old/Plan.md": "x\n",
		"Planet facts.md":   "y\n",
		"Airplane.md":       "z\n",
	})
	s.clickRow(slices.Index(s.listed(), "Home"))
	s.do(func() { s.w.Editor.FocusBlock(0, 5) })
	s.screen.Type(" [[plan")
	var entries []string
	s.do(func() { entries, _ = s.w.Editor.WikiMenuEntries() })
	if len(entries) != 4 || entries[3] != "Airplane" {
		t.Fatalf("completion for plan: %q", entries)
	}
	s.shot("features_10_wiki_completion.png")
	// The first Plan: two notes share the name, so its path goes in.
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	text := s.editorText()
	if !strings.HasPrefix(text, "Start [[Ideas/") || !strings.HasSuffix(text, "Plan]]") {
		t.Fatalf("after choosing: %q", text)
	}
	s.screen.Type(" [[Planet facts#")
	s.do(func() { entries, _ = s.w.Editor.WikiMenuEntries() })
	if len(entries) != 0 {
		t.Errorf("a note without headings offers none: %q", entries)
	}
	s.screen.KeyPress(unison.KeyEscape, mod.None)
	s.do(func() { entries, _ = s.w.Editor.WikiMenuEntries() })
	if entries != nil {
		t.Errorf("Escape should close the list")
	}
}

// Ctrl+Shift+B shows the notes linking to the open one.
func TestTheBacklinksPane(t *testing.T) {
	s := openVault(t, notes{
		"Plan.md":        "# Plan\n",
		"Meeting.md":     "We agreed on [[Plan]].\n\nSee [[plan|the plan]] again.\n",
		"Journal/Day.md": "Nothing here\n",
		"Other.md":       "Links to [[Journal/Day]]\n",
	})
	s.clickRow(slices.Index(s.listed(), "Plan"))
	s.screen.KeyPress(unison.KeyB, mod.Control|mod.Shift)
	var rows []string
	s.do(func() {
		for _, r := range s.w.backlinks.list.rows {
			rows = append(rows, fmt.Sprintf("%s|%d|%s", r.title, r.count, r.snippet))
		}
	})
	want := []string{"Meeting|2|", "|0|We agreed on [[Plan]].", "|0|See [[plan|the plan]] again."}
	if !slices.Equal(rows, want) {
		t.Fatalf("backlinks:\n%q\nwant\n%q", rows, want)
	}
	s.shot("features_11_backlinks.png")
	s.do(func() { s.w.backlinks.list.choose(0) })
	if s.openTitle() != "Meeting" {
		t.Errorf("a backlink should open its note, %q is open", s.openTitle())
	}
}

// Renaming a note that links name asks first, and Update links rewrites
// them; Rename only leaves them.
func TestRenamingUpdatesLinks(t *testing.T) {
	s := openVault(t, notes{
		"Target.md":    "The target\n",
		"Referrer.md":  "Go to [[Target]] and [[target#Head|there]]\n",
		"Unrelated.md": "Nothing\n",
	})
	s.clickRow(slices.Index(s.listed(), "Target"))
	s.screen.KeyPress(unison.KeyF2, mod.None)
	s.screen.Type("Renamed\n")
	s.shot("features_12_update_links.png")
	s.press("Update links")
	if !s.exists("Renamed.md") || s.exists("Target.md") {
		t.Fatalf("the note should be renamed")
	}
	if got := s.file("Referrer.md"); got != "Go to [[Renamed]] and [[Renamed#Head|there]]\n" {
		t.Errorf("the links should follow the rename: %q", got)
	}
	if s.exists(".kvit/redirects.json") {
		t.Errorf("no link needs the redirect any more, so the table should be gone")
	}
	s.clickRow(slices.Index(s.listed(), "Renamed"))
	s.screen.KeyPress(unison.KeyF2, mod.None)
	s.screen.Type("Again\n")
	s.press("Rename only")
	if got := s.file("Referrer.md"); got != "Go to [[Renamed]] and [[Renamed#Head|there]]\n" || !s.exists("Again.md") {
		t.Errorf("Rename only should leave the links: %q", got)
	}
}

// features.md 12.5–12.6: exporting the notes of a vault and importing a
// folder of files.
func TestExportAndImport(t *testing.T) {
	s := openVault(t, notes{"Report.md": "# Quarterly Report\n\nRevenue rose **12%** this quarter.\n", "Existing.md": "x\n"})
	dest := t.TempDir()
	var files []string
	s.do(func() {
		notes := []export.VaultNote{}
		for _, e := range s.w.Vault.Entries {
			data, _ := os.ReadFile(s.w.Vault.Path(e.Path))
			notes = append(notes, export.VaultNote{RelPath: e.Path, Text: string(data), Title: e.Title})
		}
		out, err := export.Vault(export.VaultExport{Root: s.root, Notes: notes, Dest: dest, Format: export.FormatHTML})
		if err != nil {
			t.Fatal(err)
		}
		for _, f := range out {
			files = append(files, filepath.Base(f.Path))
		}
	})
	if !slices.Contains(files, "Report.html") {
		t.Errorf("export files: %q", files)
	}
	src := t.TempDir()
	os.WriteFile(filepath.Join(src, "Existing.md"), []byte("imported\n"), 0o644)
	os.MkdirAll(filepath.Join(src, "sub"), 0o755)
	os.WriteFile(filepath.Join(src, "sub", "Deep.txt"), []byte("deep\n"), 0o644)
	os.WriteFile(filepath.Join(src, "picture.png"), []byte("not a note"), 0o644)
	s.do(func() {
		sum, err := export.DryRunFolder(s.root, src, "")
		if err != nil || sum.Files != 2 || sum.Collisions != 1 || sum.Folders != 1 {
			t.Errorf("summary %+v: %v", sum, err)
		}
		s.w.runImport(func() (export.ImportPlan, error) { return export.ImportFolder(s.root, src, "", export.ImportOptions{}) })
	})
	if got := s.file("Existing.md"); got != "x\n" {
		t.Errorf("an import must never write over a note: %q", got)
	}
	names := s.listed()
	if len(names) != 4 {
		t.Errorf("after the import the list is %q", names)
	}
}

// Pasting HTML makes Markdown of it; copying puts HTML beside the text.
func TestHTMLOnTheClipboard(t *testing.T) {
	s := openVault(t, notes{"A.md": "Some **bold** words\n"})
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.KeyPress(unison.KeyEnd, mod.Shift)
	s.screen.KeyPress(unison.KeyC, mod.Control)
	var html, text string
	s.do(func() {
		html = string(unison.ClipboardGetData(htmlType))
		text = unison.ClipboardGetText()
	})
	if text != "Some **bold** words" || !strings.Contains(html, "<strong>bold</strong>") {
		t.Errorf("copied text %q and HTML %q", text, html)
	}
	s.do(func() {
		unison.ClipboardSetData(drag.Data{Type: uti.UTF8PlainText, Data: []byte("one two")},
			drag.Data{Type: htmlType, Data: []byte("<ul><li>one</li><li><b>two</b></li></ul>")})
		s.w.Editor.FocusBlock(0, len([]rune("Some **bold** words")))
	})
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	s.screen.KeyPress(unison.KeyV, mod.Control)
	if got := s.editorText(); got != "Some **bold** words\none\n**two**" {
		t.Errorf("pasted HTML: %q", got)
	}
}

// features.md 1.2.18: a query block lists the notes its spec selects, and a
// row opens its note.
func TestAQueryBlock(t *testing.T) {
	s := openVault(t, notes{
		"Dashboard.md":       "# Dashboard\n\n```query\nfrom: projects/\nwhere: status = active\ncolumns: title, status, due\nsort: due asc\n```\n",
		"projects/Alpha.md":  "---\nstatus: active\ndue: 2026-10-01\n---\nA\n",
		"projects/Beta.md":   "---\nstatus: done\ndue: 2026-09-01\n---\nB\n",
		"projects/Gamma.md":  "---\nstatus: active\ndue: 2026-09-15\n---\nC\n",
		"elsewhere/Delta.md": "---\nstatus: active\n---\nD\n",
	})
	s.clickRow(slices.Index(s.listed(), "Dashboard"))
	var got string
	s.do(func() { s.w.Editor.ClearFocus(); got = s.w.Editor.QueryText(1) })
	if got != "[title status due]\n[Gamma active 2026-09-15]\n[Alpha active 2026-10-01]\n" {
		t.Fatalf("query rows:\n%s", got)
	}
	s.shot("features_13_query.png")
	var p geom.Point
	s.do(func() {
		r := s.w.Editor.RowRect(1)
		p = s.screen.PanelPoint(s.w.Editor, geom.NewPoint(r.X+120, r.Y+4+8+20+26+12))
	})
	s.screen.Click(p)
	if s.openTitle() != "Gamma" {
		t.Errorf("the first row should open Gamma, %q is open", s.openTitle())
	}
}

// Tags renamed, merged and given a colour from the sidebar.
func TestTagManagementFromTheSidebar(t *testing.T) {
	s := openVault(t, notes{"A.md": "---\ntags: [books]\n---\nA\n", "B.md": "---\ntags: [reading]\n---\nB\n"})
	var p geom.Point
	row := func(label string) {
		s.do(func() {
			tops, heights := s.w.scopes.geometry()
			for i, r := range s.w.scopes.Rows {
				if r.Label == label {
					p = s.screen.PanelPoint(s.w.scopes, geom.NewPoint(60, tops[i]+heights[i]/2))
				}
			}
		})
	}
	row("#books")
	s.screen.ClickWith(p, unison.ButtonRight, mod.None)
	s.press("Color")
	s.press("Red")
	var color string
	s.do(func() { color = s.w.Vault.State.TagColors["books"] })
	if color == "" {
		t.Fatalf("the tag should have a colour")
	}
	row("#books")
	s.screen.ClickWith(p, unison.ButtonRight, mod.None)
	s.press("Rename…")
	s.screen.KeyPress(unison.KeyA, mod.Control)
	s.screen.Type("reading\n")
	s.press("Merge")
	if got := s.file("A.md"); got != "---\ntags: [reading]\n---\nA\n" {
		t.Errorf("merged: %q", got)
	}
	var labels []string
	s.do(func() {
		for _, r := range s.w.scopes.Rows {
			labels = append(labels, r.Label)
		}
	})
	if slices.Contains(labels, "#books") || !slices.Contains(labels, "#reading") {
		t.Errorf("sidebar after the merge: %q", labels)
	}
}

// Picking notes with Ctrl and Shift, and pinning them together; the Manual
// order, changed by dragging a note in the list.
func TestBulkActionsAndManualOrder(t *testing.T) {
	s := openVault(t, notes{
		"F/Apricot.md":   "---\ncreated: 2026-01-01\n---\na\n",
		"F/Blueberry.md": "---\ncreated: 2026-01-02\n---\nb\n",
		"F/Citrus.md":    "---\ncreated: 2026-01-03\n---\nc\n",
	})
	s.clickScope("F")
	s.do(func() {
		s.w.ascend = true
		s.w.sortBy = "manual"
		s.w.refreshList()
	})
	if got := s.listed(); !slices.Equal(got, []string{"Apricot", "Blueberry", "Citrus"}) {
		t.Fatalf("manual order, oldest first: %q", got)
	}
	row := func(i int) geom.Point {
		var p geom.Point
		s.do(func() {
			h := s.w.list.rowHeight()
			p = s.screen.PanelPoint(s.w.list, geom.NewPoint(40, float32(i)*h+h/2))
		})
		return p
	}
	// Drag Citrus above Apricot.
	from, to := row(2), row(0)
	to.Y -= 20
	s.screen.MouseDown(from, unison.ButtonLeft, mod.None)
	s.screen.MouseMove(geom.NewPoint(from.X, from.Y-20), mod.None)
	s.screen.MouseMove(to, mod.None)
	s.screen.MouseUp(to, unison.ButtonLeft, mod.None)
	if got := s.listed(); !slices.Equal(got, []string{"Citrus", "Apricot", "Blueberry"}) {
		t.Errorf("after dragging Citrus to the top: %q", got)
	}
	// Pick Apricot and Blueberry and pin both.
	s.clickRow(1)
	s.screen.ClickWith(row(2), unison.ButtonLeft, mod.Shift)
	var count string
	s.do(func() { count = s.w.bulkCount.Text })
	if count != "2 selected" {
		t.Fatalf("the bar says %q", count)
	}
	s.shot("features_14_bulk.png")
	s.press("Pin or unpin the notes picked")
	if !strings.HasPrefix(s.file("F/Apricot.md"), "---\ncreated: 2026-01-01\npinned: true\n") ||
		!strings.Contains(s.file("F/Blueberry.md"), "pinned: true") || strings.Contains(s.file("F/Citrus.md"), "pinned") {
		t.Errorf("pinning the pick: %q %q", s.file("F/Apricot.md"), s.file("F/Blueberry.md"))
	}
}

// PDF export draws the note onto A4 pages.
func TestPDFExport(t *testing.T) {
	long := "# Report\n\nRevenue rose **12%** this quarter.\n\n"
	for i := 0; i < 80; i++ {
		long += fmt.Sprintf("Paragraph %d of the report, long enough to wrap onto a second line at the width of a page.\n\n", i)
	}
	s := openVault(t, notes{"Report.md": long})
	target := filepath.Join(t.TempDir(), "Report.pdf")
	if dir := os.Getenv("KVIT_SHOTS"); dir != "" {
		target = filepath.Join(dir, "features_15_export.pdf")
	}
	var err error
	s.do(func() { err = s.w.writePDF(s.w.Editor.Doc.Blocks, "Report", target) })
	if err != nil {
		t.Fatal(err)
	}
	data, _ := os.ReadFile(target)
	if !bytes.HasPrefix(data, []byte("%PDF")) || bytes.Count(data, []byte("/Type /Page\n"))+bytes.Count(data, []byte("/Type /Page ")) < 1 {
		t.Fatalf("not a PDF: %d bytes", len(data))
	}
	pages := bytes.Count(data, []byte("/Type /Page"))
	if pages < 3 {
		t.Errorf("82 blocks should take several pages, the PDF has %d page objects", pages)
	}
	// The first page drawn to a picture as it is drawn into the PDF, to look
	// at when KVIT_SHOTS is set.
	if dir := os.Getenv("KVIT_SHOTS"); dir != "" {
		s.do(func() {
			ui, _ := kvitui.New(kvitui.Options{IgnoreDesktop: true})
			ed := editor.New(ui, editor.NewDoc(slices.Clone(s.w.Editor.Doc.Blocks)))
			pp := &pdfPages{ed, ed.Paginate(pdfPageW-2*pdfMargin, pdfPageH-2*pdfMargin)}
			img, err := unison.NewImageFromDrawing(pdfPageW, pdfPageH, 72, func(gc *unison.Canvas) {
				gc.DrawRect(geom.NewRect(0, 0, pdfPageW, pdfPageH), unison.White.Paint(gc, geom.Rect{}, paintstyle.Fill))
				_ = pp.DrawPage(gc, 1)
			})
			if err == nil {
				if png, err := img.ToPNG(6); err == nil {
					_ = os.WriteFile(filepath.Join(dir, "features_15_pdf_page1.png"), png, 0o644)
				}
			}
		})
	}
}
