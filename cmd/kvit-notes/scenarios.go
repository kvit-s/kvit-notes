package main

// Scripted scenarios. Each one replays a storyboard from Kvit's
// tests/tst_visual.qml with the same note and the same key presses and
// pointer moves, checks the resulting note, and saves screenshots under the
// same file names as Kvit's reference images, so the two can be compared.
// They run on unison's headless screen: the real event loop, drawing and
// screen-reader tree, with the input injected.

import (
	"fmt"
	"image/png"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// storyWidth and storyHeight are the window size of Kvit's storyboards.
const (
	storyWidth  = 1100
	storyHeight = 720
)

// driver runs one note window on a headless screen.
type driver struct {
	screen *unison.HeadlessScreen
	ui     *kvitui.UI
	n      *noteWindow
	out    string // where screenshots go; "" takes none
	fails  []string
	shots  []string
}

// startDriver opens a note window on a headless screen. theme is a theme
// id, "" for light.
func startDriver(md, theme string, width, height float32) (*driver, error) {
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true})
	if err != nil {
		return nil, err
	}
	if theme != "" {
		ui.Theme.SetThemeID(theme)
	}
	ui.Theme.SetReducedMotion(true)
	doc := editor.NewDoc(editor.ParseMarkdown(md))
	// Typing merges into one undo step while keystrokes are under 500 ms
	// apart; the scenarios' keystrokes arrive on a clock 100 ms apart.
	clock := time.Unix(0, 0)
	doc.Now = func() time.Time { clock = clock.Add(100 * time.Millisecond); return clock }
	dr := &driver{ui: ui}
	var startErr error
	dr.screen, err = unison.StartHeadless(unison.HeadlessConfig{Width: width, Height: height},
		unison.StartupFinishedCallback(func() {
			if dr.n, startErr = newNoteWindow(ui, doc, ""); startErr != nil {
				return
			}
			dr.n.win.SetContentRect(geom.NewRect(0, 0, width, height))
			dr.n.win.ToFront()
		}))
	if err != nil {
		return nil, err
	}
	if startErr != nil {
		dr.screen.Stop()
		return nil, startErr
	}
	dr.screen.EnableAccessibility()
	dr.screen.Sync()
	return dr, nil
}

func (dr *driver) stop() { dr.screen.Stop() }

func (dr *driver) do(f func()) { dr.screen.Do(f); dr.screen.Sync() }

func (dr *driver) ed() *editor.Editor { return dr.n.ed }

func (dr *driver) doc() *editor.Doc { return dr.n.ed.Doc }

func (dr *driver) expect(ok bool, format string, args ...any) {
	if !ok {
		dr.fails = append(dr.fails, fmt.Sprintf(format, args...))
	}
}

// shot saves the window as drawn now under a Kvit storyboard's file name.
func (dr *driver) shot(name string) {
	dr.shots = append(dr.shots, name)
	if dr.out == "" {
		return
	}
	dr.screen.Sync()
	img := dr.screen.CaptureWindow(dr.n.win.Window)
	f, err := os.Create(filepath.Join(dr.out, name))
	if err != nil {
		dr.fails = append(dr.fails, "screenshot: "+err.Error())
		return
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		dr.fails = append(dr.fails, "screenshot: "+err.Error())
	}
}

// focus puts the caret in block i at a source offset, as Kvit's tests do
// with ensureFocus and cursorPosition.
func (dr *driver) focus(i, off int) { dr.do(func() { dr.ed().FocusBlock(i, off) }) }

// clearFocus takes the caret out of the note and moves the pointer off it.
func (dr *driver) clearFocus() {
	dr.do(func() { dr.ed().ClearFocus() })
	dr.screen.MouseMove(geom.NewPoint(900, storyHeight-40), mod.None)
}

func (dr *driver) key(k unison.KeyCode, mods mod.Modifiers) { dr.screen.KeyPress(k, mods) }

func (dr *driver) keys(k unison.KeyCode, n int) {
	for range n {
		dr.key(k, mod.None)
	}
}

func (dr *driver) typ(s string) { dr.screen.Type(s) }

// point converts a point in the editor's coordinates to the screen's.
func (dr *driver) point(p geom.Point) geom.Point {
	var out geom.Point
	dr.do(func() { out = dr.screen.PanelPoint(dr.ed(), p) })
	return out
}

// rowRect is block i's row on the screen.
func (dr *driver) rowRect(i int) geom.Rect {
	var r geom.Rect
	dr.do(func() { r = dr.ed().RowRect(i) })
	p := dr.point(r.Point)
	return geom.NewRect(p.X, p.Y, r.Width, r.Height)
}

// partCentre is the middle of one of block i's controls on the screen.
func (dr *driver) partCentre(i int, part string) geom.Point {
	var r geom.Rect
	dr.do(func() { r = dr.ed().PartRect(i, part) })
	return dr.point(r.Center())
}

// textPoint is where source offset off of block i is drawn, on the screen.
func (dr *driver) textPoint(i, off int) geom.Point {
	var p geom.Point
	dr.do(func() { p = dr.ed().TextPoint(i, off) })
	return dr.point(p)
}

// hoverRow rests the pointer on block i's row, which shows its gutter.
func (dr *driver) hoverRow(i int) {
	r := dr.rowRect(i)
	dr.screen.MouseMove(geom.NewPoint(r.X+30, r.Y+r.Height/2), mod.None)
}

func (dr *driver) click(p geom.Point, mods mod.Modifiers) {
	dr.screen.ClickWith(p, unison.ButtonLeft, mods)
}

func (dr *driver) text(i int) string {
	var s string
	dr.do(func() { s = dr.doc().Blocks[i].Text })
	return s
}

func (dr *driver) kind(i int) editor.Kind {
	var k editor.Kind
	dr.do(func() { k = dr.doc().Blocks[i].Kind })
	return k
}

func (dr *driver) blocks() string {
	var parts []string
	dr.do(func() {
		for _, b := range dr.doc().Blocks {
			parts = append(parts, b.Kind.String()+":"+b.Text)
		}
	})
	return strings.Join(parts, " | ")
}

func (dr *driver) count() int {
	var n int
	dr.do(func() { n = len(dr.doc().Blocks) })
	return n
}

func (dr *driver) caret() editor.Pos {
	var p editor.Pos
	dr.do(func() { p = dr.doc().Caret })
	return p
}

func (dr *driver) blockID(i int) int64 {
	var id int64
	dr.do(func() { id = dr.doc().Blocks[i].ID })
	return id
}

func (dr *driver) selected() []int64 {
	var ids []int64
	dr.do(func() { ids = dr.ed().SelectedBlocks() })
	return ids
}

func (dr *driver) menu() ([]string, int) {
	var names []string
	sel := -1
	dr.do(func() { names, sel = dr.ed().MenuEntries() })
	return names, sel
}

// popups is how many popups the window shows: the / menu, the block menu.
func (dr *driver) popups() int {
	var n int
	dr.do(func() { n = len(dr.n.win.Popups()) })
	return n
}

func (dr *driver) clipboard() string {
	var s string
	dr.do(func() { s = unison.ClipboardGetText() })
	return s
}

const storyDoc = `# Welcome to Kvit Notes

The quick **brown** fox has *seven* cubs.

## Getting Started

Each block is independent. You can edit them separately.

More blocks can be added later with the Enter key.
`

func withLine(line string) string {
	return strings.Replace(storyDoc, "The quick **brown** fox has *seven* cubs.", line, 1)
}

// runeIndex is the rune offset of the first occurrence of sub in s.
func runeIndex(s, sub string) int { return len([]rune(s[:strings.Index(s, sub)])) }

type scenario struct {
	name string
	md   string
	run  func(dr *driver)
}

var scenarios = []scenario{
	{"01_reveal", storyDoc, func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_01_reveal_01_cursor_outside.png")
		dr.focus(1, 5)
		dr.key(unison.KeyHome, mod.None)
		dr.expect(dr.caret().Off == 0, "Home: caret at %d", dr.caret().Off)
		dr.shot("visual_01_reveal_02_cursor_at_line_start.png")
		dr.keys(unison.KeyRight, 14)
		dr.expect(dr.caret().Off == 16, "14 x Right: caret at %d, Kvit's is after \"brow\" (16)", dr.caret().Off)
		dr.shot("visual_01_reveal_03_cursor_in_bold.png")
		dr.keys(unison.KeyRight, 17)
		dr.expect(dr.caret().Off == 34, "17 more x Right: caret at %d, Kvit's is after \"seven\" (34)", dr.caret().Off)
		dr.shot("visual_01_reveal_04_cursor_in_italic.png")
		dr.clearFocus()
		dr.shot("visual_01_reveal_05_cursor_elsewhere.png")
	}},
	{"03_types", withLine("Strike ~~gone~~ code `x = 1` mark ==note== under ++line++."), func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_03_types_01_all_rendered.png")
		for _, st := range []struct{ word, shot string }{
			{"gone", "visual_03_types_02_strike_revealed.png"},
			{"x = 1", "visual_03_types_03_code_revealed.png"},
			{"note", "visual_03_types_04_highlight_revealed.png"},
			{"line", "visual_03_types_05_underline_revealed.png"},
		} {
			dr.focus(1, runeIndex(dr.text(1), st.word)+1)
			dr.shot(st.shot)
		}
		dr.clearFocus()
		dr.shot("visual_03_types_06_all_rendered_again.png")
	}},
	{"04_nested", withLine("Nested: **bold *and italic* inside** here."), func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_04_nested_01_rendered.png")
		dr.focus(1, runeIndex(dr.text(1), "and")+1)
		dr.shot("visual_04_nested_02_revealed.png")
		dr.clearFocus()
		dr.shot("visual_04_nested_03_rendered_again.png")
	}},
	{"05_links", withLine("Docs at [Qt site](https://qt.io) and bare https://kde.org too."), func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_05_links_01_rendered.png")
		dr.focus(1, runeIndex(dr.text(1), "Qt site")+1)
		dr.shot("visual_05_links_02_revealed.png")
		dr.clearFocus()
		dr.shot("visual_05_links_03_rendered_again.png")
	}},
	{"07_block_types", "# Block types\n\n- bullet at level zero\n  - level one goes deeper\n    - level two deeper still\n1. first numbered item\n2. second with **bold** text\n   1. nested child restarts\n- [ ] an open task\n- [x] a completed task\n\n---\n\n> a quotation\n> with a second line\n\n```\ndef area(r):\n    return 3.14 * r * r\n```\n\n#### A level-four heading (Phase 5)\n", func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_07_types_01_all_block_types.png")
		var n int
		dr.do(func() { n = editor.ListNumber(dr.doc().Blocks, 6) })
		dr.expect(n == 1, "the nested numbered item should restart at 1, shows %d", n)
		dr.click(dr.partCentre(7, "check"), mod.None)
		var checked bool
		dr.do(func() { checked = dr.doc().Blocks[7].Checked })
		dr.expect(checked, "clicking the check box did not tick the task")
		dr.shot("visual_07_types_02_todo_toggled.png")
		dr.focus(5, runeIndex(dr.text(5), "bold")+1)
		dr.shot("visual_07_types_03_list_item_bold_revealed.png")
		dr.clearFocus()
		dr.shot("visual_07_types_04_all_rendered_again.png")
	}},
	{"08_listflow", "- the first item\n", func(dr *driver) {
		dr.focus(0, 0)
		dr.key(unison.KeyEnd, mod.None)
		dr.key(unison.KeyReturn, mod.None)
		dr.expect(dr.count() == 2 && dr.kind(1) == editor.Bullet, "Enter should continue the list: %s", dr.blocks())
		dr.shot("visual_08_listflow_01_enter_continues.png")
		dr.key(unison.KeyTab, mod.None)
		var indent int
		dr.do(func() { indent = dr.doc().Blocks[1].Indent })
		dr.expect(indent == 1, "Tab should nest the item")
		dr.shot("visual_08_listflow_02_tab_nests.png")
		dr.typ("a nested child")
		dr.expect(dr.text(1) == "a nested child", "typed text: %q", dr.text(1))
		dr.shot("visual_08_listflow_03_typed_in_child.png")
		dr.key(unison.KeyReturn, mod.None)
		dr.key(unison.KeyReturn, mod.None)
		dr.expect(dr.count() == 3 && dr.kind(2) == editor.Paragraph, "the empty item should leave the list: %s", dr.blocks())
		dr.shot("visual_08_listflow_04_empty_item_exited.png")
	}},
	{"09_prefix", "", func(dr *driver) {
		dr.focus(0, 0)
		dr.typ("-")
		dr.expect(dr.kind(0) == editor.Paragraph, "a lone dash should stay a paragraph")
		dr.shot("visual_09_prefix_01_dash_still_paragraph.png")
		dr.typ(" ")
		dr.expect(dr.kind(0) == editor.Bullet, "\"- \" should make a bullet, got %v", dr.kind(0))
		dr.shot("visual_09_prefix_02_bullet_converted.png")
		dr.typ("[ ] ")
		dr.expect(dr.kind(0) == editor.Todo, "\"[ ] \" should make a to-do, got %v", dr.kind(0))
		dr.shot("visual_09_prefix_03_todo_converted.png")
		dr.typ("write the report")
		dr.expect(dr.text(0) == "write the report", "content: %q", dr.text(0))
		dr.shot("visual_09_prefix_04_task_typed.png")
		dr.key(unison.KeyZ, mod.Control)
		dr.key(unison.KeyZ, mod.Control)
		dr.expect(dr.kind(0) == editor.Bullet && dr.text(0) == "[ ] ", "two undos should bring back the literal \"[ ] \" in the bullet: %s", dr.blocks())
	}},
	{"10_menu", "", func(dr *driver) {
		dr.focus(0, 0)
		dr.typ("/")
		names, _ := dr.menu()
		dr.expect(len(names) > 0 && dr.popups() == 1, "/ should open the menu")
		dr.shot("visual_10_menu_01_open_grouped.png")
		dr.typ("h1")
		names, _ = dr.menu()
		dr.expect(len(names) > 0 && names[0] == "Heading 1", "\"h1\" should put Heading 1 first: %v", names)
		dr.shot("visual_10_menu_02_filtered_h1.png")
		dr.key(unison.KeyBackspace, mod.None)
		dr.key(unison.KeyBackspace, mod.None)
		dr.key(unison.KeyDown, mod.None)
		dr.key(unison.KeyDown, mod.None)
		_, sel := dr.menu()
		dr.expect(sel == 2, "the arrows should move the highlight to the third entry, it is on %d", sel)
		dr.shot("visual_10_menu_03_arrow_highlight.png")
		dr.typ("quote")
		dr.key(unison.KeyReturn, mod.None)
		dr.expect(dr.kind(0) == editor.Quote && dr.text(0) == "", "Enter should convert to a quote and clear the query: %s", dr.blocks())
		dr.expect(dr.popups() == 0, "the menu should close")
		dr.shot("visual_10_menu_04_converted_quote.png")
	}},
	{"11_menu_flip", "filler line 1\n\nfiller line 2\n\nfiller line 3\n\nfiller line 4\n\nfiller line 5\n\nfiller line 6\n\nfiller line 7\n\nfiller line 8\n\nfiller line 9\n\nfiller line 10\n\nfiller line 11\n\nfiller line 12\n\nfiller line 13\n", func(dr *driver) {
		dr.do(func() {
			d := dr.doc()
			d.Blocks = append(d.Blocks, editor.NewBlock(editor.Paragraph, ""))
			dr.ed().Refresh()
		})
		dr.focus(dr.count()-1, 0)
		dr.typ("/")
		dr.shot("visual_11_menu_05_flipped_at_bottom.png")
		var menuBottom, caretTop float32
		dr.do(func() {
			for _, p := range dr.n.win.Popups() {
				menuBottom = p.Panel.AsPanel().FrameRect().Bottom()
			}
			c, _ := dr.ed().CaretRect()
			caretTop = dr.n.win.Content().PointFromRoot(dr.ed().PointToRoot(c.Point)).Y
		})
		dr.expect(dr.popups() == 1 && menuBottom <= caretTop, "the menu should open above the caret near the bottom (menu ends at %v, caret at %v)", menuBottom, caretTop)
		dr.key(unison.KeyEscape, mod.None)
		dr.expect(dr.popups() == 0, "Escape should close the menu")
	}},
	{"12_plus", "An existing block of text\n", func(dr *driver) {
		dr.clearFocus()
		dr.hoverRow(0)
		dr.shot("visual_12_plus_01_hover_shows_button.png")
		dr.click(dr.partCentre(0, "add"), mod.None)
		dr.expect(dr.count() == 2 && dr.popups() == 1, "+ should add a block and open the menu: %s", dr.blocks())
		dr.shot("visual_12_plus_02_menu_for_new_block.png")
		dr.typ("tod")
		dr.key(unison.KeyReturn, mod.None)
		dr.expect(dr.count() == 2 && dr.kind(1) == editor.Todo, "the new block should be a to-do: %s", dr.blocks())
		dr.shot("visual_12_plus_03_new_todo_created.png")
	}},
	{"13_select", "# Project notes\n\nFirst paragraph with **bold** text\n\n- bullet one\n- bullet two\n\n---\n\nClosing paragraph\n", func(dr *driver) {
		dr.clearFocus()
		dr.hoverRow(1)
		dr.click(dr.partCentre(1, "handle"), mod.None)
		dr.hoverRow(3)
		dr.click(dr.partCentre(3, "handle"), mod.Shift)
		dr.expect(len(dr.selected()) == 3, "Shift+click should select blocks 2 to 4, selected %d", len(dr.selected()))
		dr.screen.MouseMove(geom.NewPoint(900, storyHeight-40), mod.None)
		dr.shot("visual_13_select_01_contiguous_range.png")
		dr.key(unison.KeyEscape, mod.None)
		for _, i := range []int{0, 2, 4} {
			dr.hoverRow(i)
			dr.click(dr.partCentre(i, "handle"), mod.Control)
		}
		sel := dr.selected()
		dr.expect(len(sel) == 3 && slices.Contains(sel, dr.blockID(4)), "Ctrl+click should toggle blocks 1, 3 and 5")
		dr.screen.MouseMove(geom.NewPoint(900, storyHeight-40), mod.None)
		dr.shot("visual_13_select_02_non_contiguous_with_divider.png")
		dr.focus(1, 3)
		dr.key(unison.KeyA, mod.Control)
		dr.key(unison.KeyA, mod.Control)
		dr.expect(len(dr.selected()) == dr.count(), "Ctrl+A twice should select every block")
		dr.shot("visual_13_select_03_select_all.png")
		dr.key(unison.KeyEscape, mod.None)
	}},
	{"14_ops", "# Meeting agenda\n\n- [ ] prepare slides\n- [ ] book the room\n\nNotes go here afterwards\n", func(dr *driver) {
		dr.clearFocus()
		dr.hoverRow(1)
		dr.click(dr.partCentre(1, "handle"), mod.None)
		dr.hoverRow(2)
		dr.click(dr.partCentre(2, "handle"), mod.Shift)
		dr.screen.MouseMove(geom.NewPoint(900, storyHeight-40), mod.None)
		dr.shot("visual_14_ops_01_selection_before_duplicate.png")
		dr.key(unison.KeyD, mod.Control)
		dr.expect(dr.count() == 6, "Ctrl+D should duplicate the two to-dos: %s", dr.blocks())
		dr.shot("visual_14_ops_02_clones_selected_below.png")
		dr.key(unison.KeyDown, mod.Option)
		dr.expect(dr.text(3) == "Notes go here afterwards", "Alt+Down should move the selection below the notes: %s", dr.blocks())
		dr.shot("visual_14_ops_03_selection_moved_down.png")
		dr.key(unison.KeyZ, mod.Control)
		dr.expect(dr.text(5) == "Notes go here afterwards", "one undo should undo the move: %s", dr.blocks())
	}},
	{"15_xsel", "The first paragraph has **bold** text inside\n\n- a bulleted item in between\n\nand the closing paragraph ends here\n", func(dr *driver) {
		dr.clearFocus()
		drag := func(fi, fo, ti, to int) {
			from := dr.textPoint(fi, fo)
			dr.screen.MouseDown(from, unison.ButtonLeft, mod.None)
			dr.screen.MouseMove(geom.NewPoint(from.X+8, from.Y), mod.None)
			dr.screen.MouseMove(dr.textPoint(ti, to), mod.None)
			dr.screen.MouseUp(dr.textPoint(ti, to), unison.ButtonLeft, mod.None)
		}
		drag(0, 4, 2, 15)
		var a, b editor.Pos
		var ia, ib int
		dr.do(func() {
			a, b = dr.doc().SelRange()
			ia, ib = dr.doc().Index(a.Block), dr.doc().Index(b.Block)
		})
		dr.expect(ia == 0 && a.Off == 4 && ib == 2 && b.Off == 15, "the forward drag selected %d:%d to %d:%d", ia, a.Off, ib, b.Off)
		dr.shot("visual_15_xsel_01_forward_range.png")
		dr.key(unison.KeyEscape, mod.None)
		drag(2, 15, 0, 4)
		dr.shot("visual_15_xsel_02_backward_range.png")
		dr.key(unison.KeyEscape, mod.None)
		drag(0, 4, 2, 15)
		dr.key(unison.KeyX, mod.Control)
		dr.expect(dr.count() == 1 && dr.text(0) == "The  paragraph ends here", "cut: %s", dr.blocks())
		clip := dr.clipboard()
		dr.expect(strings.HasPrefix(clip, "first paragraph has **bold** text inside\n\n- a bulleted item in between\n\nand the closing"),
			"clipboard: %q", clip)
		dr.shot("visual_15_xsel_03_after_cut.png")
	}},
	{"16_drag", "# Shopping list\n\n- apples\n- bread\n- cheese\n\nEverything else goes below\n", func(dr *driver) {
		dr.clearFocus()
		dr.hoverRow(1)
		h := dr.partCentre(1, "handle")
		dr.screen.MouseDown(h, unison.ButtonLeft, mod.None)
		dr.screen.MouseMove(geom.NewPoint(h.X+2, h.Y+26), mod.None)
		cheese := dr.rowRect(3)
		target := cheese.Y + cheese.Height*0.8
		for k := 1; k <= 6; k++ {
			dr.screen.MouseMove(geom.NewPoint(h.X+70, h.Y+(target-h.Y)*float32(k)/6), mod.None)
		}
		dr.expect(dr.text(3) == "apples", "apples should be moved past cheese while dragging: %s", dr.blocks())
		dr.shot("visual_16_drag_01_live_make_room.png")
		dr.screen.MouseUp(geom.NewPoint(h.X+70, target), unison.ButtonLeft, mod.None)
		dr.screen.MouseMove(geom.NewPoint(900, storyHeight-40), mod.None)
		dr.shot("visual_16_drag_02_after_drop.png")
		var steps int
		dr.do(func() { steps = dr.doc().UndoSteps() })
		dr.expect(dr.text(3) == "apples" && steps == 1, "the drop should be one undo step (%d)", steps)
		dr.key(unison.KeyZ, mod.Control)
		dr.expect(dr.text(1) == "apples", "undo should put apples back: %s", dr.blocks())
	}},
	{"29_block_menu", "Right-click text with a [link](https://example.com) inside\n\nA second block for the handle\n", func(dr *driver) {
		dr.clearFocus()
		dr.hoverRow(1)
		dr.click(dr.partCentre(1, "menu"), mod.None)
		dr.expect(dr.popups() == 1, "the menu button should open the block menu")
		dr.shot("visual_29_menus_03_block_menu.png")
		lines, more := editor.BlockMenuCommands()
		// The block menu opens with no line lit; the first Down lights the
		// first line. Right opens "Turn into"'s submenu with its first line
		// lit.
		dr.keys(unison.KeyDown, slices.Index(lines, "Turn into")+1)
		dr.key(unison.KeyRight, mod.None)
		dr.keys(unison.KeyDown, slices.Index(more["Turn into"], "Heading 2"))
		dr.key(unison.KeyReturn, mod.None)
		dr.expect(dr.popups() == 0 && dr.kind(1) == editor.Heading2, "Turn into Heading 2 from the keyboard: %s", dr.blocks())
		dr.focus(0, 3)
		dr.key(unison.KeyF10, mod.Shift)
		dr.expect(dr.popups() == 1, "Shift+F10 should open the block menu")
		dr.keys(unison.KeyDown, 1)
		dr.key(unison.KeyReturn, mod.None)
		clip := dr.clipboard()
		dr.expect(clip == "Right-click text with a [link](https://example.com) inside", "Copy from Shift+F10's menu should copy the caret's block: %q", clip)
		dr.key(unison.KeyF10, mod.Shift)
		dr.key(unison.KeyEscape, mod.None)
		dr.expect(dr.popups() == 0, "Escape should close it")
	}},
	{"30_input_method_text", "Before after\n", func(dr *driver) {
		// Kvit's storyboard 30 draws an input method's composition at the
		// caret. Composition is not a requirement of the Go apps (the owner's
		// decision of 2026-09-26), so what is kept is the text an input method
		// commits, which arrives as whole characters with no key press.
		dr.focus(0, 7)
		dr.typ("日本語")
		dr.expect(dr.text(0) == "Before 日本語after", "committed text: %q", dr.text(0))
		dr.expect(dr.caret().Off == 10, "the caret should follow the committed text: %d", dr.caret().Off)
		dr.shot("scenario_30_input_method_text.png")
	}},
	{"20_caret_nav", "First paragraph, long enough that it wraps onto a second visual line: it keeps going with more words, and then some more words after those, and still more, until the line is certainly wider than the text column of the window and has to wrap.\n\n## A heading\n\n- item\n\n```\n  indented code\n```\n", func(dr *driver) {
		dr.focus(0, 3)
		dr.key(unison.KeyDown, mod.None)
		dr.expect(dr.caret().Block == dr.blockID(0), "Down on the first of two lines stays in the block")
		dr.key(unison.KeyDown, mod.None)
		dr.expect(dr.caret().Block == dr.blockID(1) && dr.caret().Off == 0, "Down on the last line goes to the start of the next block")
		dr.key(unison.KeyUp, mod.None)
		dr.expect(dr.caret().Block == dr.blockID(0) && dr.caret().Off == len([]rune(dr.text(0))), "Up on the first line goes to the end of the previous block")
		dr.focus(3, len([]rune(dr.text(3))))
		dr.key(unison.KeyReturn, mod.None)
		dr.typ("x")
		dr.expect(dr.text(3) == "  indented code\n  x", "Enter in code should keep the indentation: %q", dr.text(3))
		dr.key(unison.KeyReturn, mod.Control)
		dr.expect(dr.count() == 5 && dr.kind(4) == editor.Paragraph && dr.caret().Block == dr.blockID(4), "Ctrl+Enter should leave the code block")
		dr.key(unison.KeyUp, mod.Shift)
		var cross bool
		dr.do(func() { cross = dr.doc().CrossBlock() })
		dr.expect(cross, "Shift+Up at a block's edge should select across blocks")
		dr.shot("scenario_20_caret_nav.png")
	}},
}

// runScenario runs one scenario and returns what went wrong. after, when
// set, checks the window the scenario ended with.
func runScenario(scn scenario, out, theme string, after func(dr *driver)) (fails, shots []string) {
	dr, err := startDriver(scn.md, theme, storyWidth, storyHeight)
	if err != nil {
		return []string{err.Error()}, nil
	}
	defer dr.stop()
	dr.out = out
	func() {
		defer func() {
			if r := recover(); r != nil {
				dr.fails = append(dr.fails, fmt.Sprintf("panic: %v (blocks: %s)", r, dr.blocks()))
			}
		}()
		scn.run(dr)
	}()
	for _, e := range dr.screen.Errors() {
		dr.fails = append(dr.fails, "unison: "+e.Error())
	}
	if after != nil {
		after(dr)
	}
	return dr.fails, dr.shots
}

// runScenarios runs the named scenario, or every one for "all", printing
// each result, and returns an error naming the ones that failed.
func runScenarios(name, out, theme string) error {
	if out != "" {
		if err := os.MkdirAll(out, 0o755); err != nil {
			return err
		}
	}
	var failed []string
	for _, scn := range scenarios {
		if name != "all" && name != scn.name {
			continue
		}
		fails, _ := runScenario(scn, out, theme, nil)
		status := "PASS"
		if len(fails) > 0 {
			status = "FAIL"
			failed = append(failed, scn.name)
		}
		fmt.Printf("%s %s\n", status, scn.name)
		for _, f := range fails {
			fmt.Printf("    %s\n", f)
		}
	}
	if len(failed) > 0 {
		return fmt.Errorf("%d scenario(s) failed: %s", len(failed), strings.Join(failed, ", "))
	}
	return nil
}
