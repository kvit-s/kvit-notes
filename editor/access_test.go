package editor

import (
	"slices"
	"strings"

	"testing"

	"github.com/kvit-s/kvit-notes/mathtex"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/check"
	"github.com/richardwilkes/unison/enums/role"
)

const accessNote = `# Welcome to Kvit Notes

The quick **brown** fox has *seven* cubs.

- [ ] an open task

---

A last paragraph that is long enough to wrap onto a second line and then a third one in any window this test opens: it keeps going with more words, and then some more words after those, and still more, until the line is certainly wider than the text column of the window, and it goes on for a while after that as well, so that nothing about the width of the screen can keep it on one line.
`

// openEditor shows an editor on a headless screen with a screen reader's
// view on.
func openEditor(t *testing.T, md string) (*uitest.Session, *Editor) {
	t.Helper()
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown(md)))
		return e
	})
	return s, e
}

// blockNode is the node a screen reader is given for block i: the one named
// by its kind whose value is its drawn text.
func blockNode(s *uitest.Session, e *Editor, i int) *accessibility.Node {
	name, drawn := blockName(s, e, i), blockDrawn(s, e, i)
	for _, n := range s.Tree().Nodes {
		if n.Name == name && n.Text != nil && n.Value == drawn {
			return n
		}
	}
	return nil
}

func blockName(s *uitest.Session, e *Editor, i int) string {
	var name string
	s.Do(func() { name = e.Doc.Blocks[i].Kind.String() + " block" })
	return name
}

func blockDrawn(s *uitest.Session, e *Editor, i int) string {
	var text string
	s.Do(func() { text = string(e.layout(i).proj.Disp) })
	return text
}

// Kvit's accessibility.md: every text block is announced as editable text
// named by its kind, a to-do carries its state, and a divider is a
// separator.
func TestBlocksAreEditableTextsNamedByKind(t *testing.T) {
	s, e := openEditor(t, accessNote)
	var names []string
	for _, n := range s.Nodes(role.TextArea) {
		names = append(names, n.Name)
	}
	for _, want := range []string{"Heading 1 block", "Paragraph block", "To-do block"} {
		if !slices.Contains(names, want) {
			t.Errorf("no editable text named %q among %v", want, names)
		}
	}
	if len(s.Nodes(role.Separator)) != 1 {
		t.Errorf("the divider should be one separator")
	}
	todo := blockNode(s, e, 2)
	if todo == nil || todo.Checked != check.Off || !todo.Actions.Has(accessibility.Toggle) {
		t.Fatalf("the to-do should be unticked and offer Toggle: %+v", todo)
	}
	s.CheckNamed()
}

// A block that does not hold the caret is read as drawn, markers hidden;
// the block with the caret has the keyboard focus, with its markers, caret
// and styled runs.
func TestTheCaretsBlockReportsTextCaretAndRuns(t *testing.T) {
	s, e := openEditor(t, accessNote)
	if n := blockNode(s, e, 1); n == nil || n.Text.Text != "The quick brown fox has seven cubs." {
		t.Fatalf("an unfocused paragraph should read without markers: %+v", n)
	}
	s.Do(func() { e.FocusBlock(1, 14) }) // inside "brown"
	s.Sync()
	f := s.Focused()
	if f == nil || f.Name != "Paragraph block" {
		t.Fatalf("the focus should be reported on the caret's block, is on %+v", f)
	}
	info := f.Text
	if info.Text != "The quick **brown** fox has seven cubs." {
		t.Errorf("the focused block should read with the markers around the caret: %q", info.Text)
	}
	if info.Caret != 14 || info.SelStart != 14 || info.SelEnd != 14 {
		t.Errorf("caret %d, selection %d-%d; want 14", info.Caret, info.SelStart, info.SelEnd)
	}
	if len(info.Lines) != 1 || len(info.Lines[0].Advances) != len([]rune(info.Text))+1 {
		t.Errorf("one line with an advance per rune boundary: %+v", info.Lines)
	}
	var bold bool
	for _, r := range info.Runs {
		if string([]rune(info.Text)[r.Start:r.End]) == "brown" && r.Weight >= 700 {
			bold = true
		}
	}
	if !bold {
		t.Errorf("no bold run over \"brown\" in %+v", info.Runs)
	}
	last := blockNode(s, e, 4)
	if last == nil || !last.Text.Multiline || len(last.Text.Lines) < 2 {
		t.Errorf("the long paragraph should report its wrapped lines: %+v", last)
	} else {
		lines := last.Text.Lines
		for k := 1; k < len(lines); k++ {
			if lines[k].Start != lines[k-1].End || lines[k].Bounds.Y <= lines[k-1].Bounds.Y {
				t.Errorf("lines should tile the text top to bottom: %+v", lines)
			}
		}
		if lines[len(lines)-1].End != len([]rune(last.Text.Text)) {
			t.Errorf("the last line should end at the end of the text")
		}
	}
}

// A screen reader can move the caret, select, replace text and tick a
// to-do, each as the editor's own operations, so undo takes them back.
func TestScreenReaderActions(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() { e.FocusBlock(1, 0) })
	s.Sync()
	act := func(i int, a accessibility.Action, start, end int, value string) {
		t.Helper()
		n := blockNode(s, e, i)
		if n == nil {
			t.Fatalf("block %d has no node", i)
		}
		if !s.Screen.PerformAccessibilityAction(accessibility.ActionRequest{Node: n.ID, Action: a, Start: start, End: end, Value: value}) {
			t.Fatalf("action %v on block %d was refused", a, i)
		}
		s.Sync()
	}
	act(1, accessibility.SetTextSelection, 4, 9, "")
	s.Do(func() {
		if a, c := e.Doc.Anchor, e.Doc.Caret; a.Off != 4 || c.Off != 9 || c.Block != e.Doc.Blocks[1].ID {
			t.Errorf("SetTextSelection should select \"quick\": %v to %v", a, c)
		}
	})
	act(1, accessibility.ReplaceText, 4, 9, "slow")
	var got string
	s.Do(func() { got = e.Doc.Blocks[1].Text })
	if got != "The slow **brown** fox has *seven* cubs." {
		t.Errorf("ReplaceText: %q", got)
	}
	act(2, accessibility.Toggle, 0, 0, "")
	var checked bool
	s.Do(func() { checked = e.Doc.Blocks[2].Checked })
	if !checked {
		t.Errorf("Toggle should tick the to-do")
	}
	if n := blockNode(s, e, 2); n == nil || n.Checked != check.On {
		t.Errorf("the to-do should now report ticked")
	}
	s.Do(func() {
		e.Doc.Undo()
		e.Doc.Undo()
		if e.Doc.Blocks[1].Text != "The quick **brown** fox has *seven* cubs." || e.Doc.Blocks[2].Checked {
			t.Errorf("two undos should take both changes back: %q", e.Doc.Blocks[1].Text)
		}
	})
}

// The gutter's controls are buttons while the pointer is on a row, and a
// screen reader can press them.
func TestGutterControlsAreButtons(t *testing.T) {
	s, e := openEditor(t, accessNote)
	var p unison.Paneler = e
	s.Screen.MouseMove(s.Screen.PanelPoint(p, e.RowRect(1).Center()), 0)
	add := s.Named("Insert block below")
	if add == nil || add.Role != role.Button || !add.Actions.Has(accessibility.Press) {
		t.Fatalf("no Insert block below button: %+v", add)
	}
	for _, name := range []string{"Delete block", "Block menu", "Drag to move, click to select"} {
		if s.Named(name) == nil {
			t.Errorf("no %q button", name)
		}
	}
	s.Screen.PerformAccessibilityAction(accessibility.ActionRequest{Node: add.ID, Action: accessibility.Press})
	s.Sync()
	var n int
	s.Do(func() { n = len(e.Doc.Blocks) })
	if n != 6 || !e.MenuOpen() {
		t.Errorf("pressing Insert block below should add a block and open the / menu: %d blocks", n)
	}
	s.CheckNamed()
}

// A typeset inline formula is heard as its TeX, not as the placeholder it
// is drawn as; its lines, runs and selection map onto that text, and a
// screen reader's offsets reach the formula.
func TestInlineMathIsHeardAsItsTeX(t *testing.T) {
	if !mathtex.Available() {
		t.Skip(mathtex.LoadError())
	}
	const src = "The relation $E = mc^2$ ties mass to energy."
	s, e := openEditor(t, src)
	var acc string
	var lines int
	s.Do(func() {
		if d := placeholderIn(e, 0); d < 0 {
			t.Fatalf("drawn %q has no placeholder", string(e.layout(0).proj.Disp))
		}
		info := e.textInfo(0)
		acc = info.Text
		lines = len(info.Lines)
		if strings.ContainsRune(acc, mathPlaceholder) {
			t.Fatalf("accessible text should not hold U+FFFC: %q", acc)
		}
		if !strings.Contains(acc, "$E = mc^2$") {
			t.Fatalf("accessible text should hold the formula's source: %q", acc)
		}
		if got := len([]rune(acc)); info.Lines[0].End != got || len(info.Lines[0].Advances) != got+1 {
			t.Fatalf("one line should tile the accessible text: %+v", info.Lines[0])
		}
		end := 0
		for _, r := range info.Runs {
			if r.Start != end {
				t.Fatalf("runs should tile the text without gaps: %+v", info.Runs)
			}
			end = r.End
		}
		if end != len([]rune(acc)) {
			t.Fatalf("runs should reach the end of the text: %+v", info.Runs)
		}
	})
	// The node a screen reader is given holds the same text as its value.
	var found bool
	for _, n := range s.Tree().Nodes {
		if n.Name == "Paragraph block" && n.Text != nil && n.Text.Text == acc {
			found = true
			if n.Value != acc {
				t.Errorf("the node's value should be its accessible text: %q", n.Value)
			}
		}
	}
	if !found {
		t.Fatalf("no paragraph node holds %q", acc)
	}
	// Moving the caret to the formula through its accessible offsets lands
	// at the span, which reveals its source for editing.
	idx := strings.Index(acc, "$E = mc^2$")
	if idx < 0 {
		t.Fatalf("no formula in %q", acc)
	}
	ar := []rune(acc)
	aStart := len([]rune(acc[:idx]))
	aEnd := aStart + len([]rune("$E = mc^2$"))
	var n *accessibility.Node
	for _, c := range s.Tree().Nodes {
		if c.Name == "Paragraph block" && c.Text != nil && c.Text.Text == acc {
			n = c
		}
	}
	if n == nil {
		t.Fatalf("no node for the paragraph")
	}
	if !s.Screen.PerformAccessibilityAction(accessibility.ActionRequest{Node: n.ID, Action: accessibility.SetTextSelection, Start: aStart, End: aEnd}) {
		t.Fatalf("SetTextSelection over the formula was refused")
	}
	s.Sync()
	s.Do(func() {
		l := e.layout(0)
		if placeholderIn(e, 0) >= 0 {
			t.Errorf("the caret at the formula should reveal its source: %q", string(l.proj.Disp))
		}
	})
	_ = lines
	_ = ar
	s.CheckNamed()
}
