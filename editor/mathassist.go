package editor

// Typing mathematics (features.md 1.2.15, Kvit's MathEntryAssist and the
// source editor of MathBlock): the `$…$` pair, the backslash command menu
// (mathmenu.go), and Tab walking the empty slots a template leaves.
//
// They work on a math surface: a block of the note (blockSurface) or a field
// of a card on a task board (cardmath.go). Where they apply is a math unit:
// the text an editor of the app would hold. In a block of inline Markdown
// it is the block; in a table showing its Markdown it is the cell holding the
// caret, between its pipes; in a display equation (a Math block) it is the
// block's TeX; in a card's field it is the field. Code and every other block
// type dollars and backslashes as they are.
//
//   - Typing `$` in a block or a cell puts in "$$" with the caret between,
// unless a dollar before the caret is still open, the dollar would be
// escaped (\$), the caret is in inline code, or a letter, a digit or a
// dollar follows (a price). While that pair is empty, Backspace takes out
// both dollars and Delete only the closing one, which leaves a literal
// dollar; a `$` typed just before the closing dollar steps over it. With
// a selection, `$` puts the selection between two dollars.
//   - Typing `\` inside $…$ (or inside a fresh pair), or anywhere in a display
// block's TeX, opens the command menu, which follows the letters typed
// after the backslash. Choosing an entry puts its template in place of
// the backslash and the letters, with the caret in the template's first
// empty slot.
//   - After a template with empty {} or [] slots, Tab and Shift+Tab move
// between the empty slots of the same formula.
//   - Ctrl+Space opens the menu again for the backslash word at the caret.
//
// What is remembered between keys (the open pair, the template whose slots
// Tab walks) is dropped as soon as the caret leaves what it describes.

import (
	"slices"
	"strings"
	"unicode"

	"github.com/kvit-s/kvit-notes/mathcmd"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// mathEntry is what the math typing aids remember between keys.
type mathEntry struct {
	// track is what is remembered about the note's blocks; a card's field
	// keeps its own.
	track mathTrack
	// menu is the open command menu, nil when closed.
	menu *mathMenu
	// model is the menu's list of commands.
	model *mathcmd.Model
}

// mathTrack is what is remembered about one kind of surface between keys.
type mathTrack struct {
	// pairOn is the surface where `$` put in a pair, nil for none, and
	// pairOpen the offset of the pair's opening dollar.
	pairOn   any
	pairOpen int
	// chainOn is the surface whose template slots Tab walks, nil for none.
	chainOn any
}

// mathSurface is text the math typing aids act on.
type mathSurface interface {
	// id tells surfaces apart: a block's id, or a card's field.
	id() any
	text() []rune
	// selection is the selected range, start first; start == end when
	// nothing is selected, and caret is the caret then.
	selection() (start, end int)
	caret() int
	// unitAt is the math unit around an offset.
	unitAt(off int) mathUnit
	// replace puts s in place of text[start:end] and the caret at caret, as
	// one undo step; typing lets the step join the typing around it.
	replace(start, end int, s string, caret int, typing bool)
	setCaret(off int)
	// caretRect is the caret in the window's content coordinates, which is
	// where the menu is placed.
	caretRect() (geom.Rect, bool)
}

// The kinds of math unit.
const (
	unitNone    = iota
	unitProse   // a block of inline Markdown
	unitCell    // a table cell, while the table shows its Markdown
	unitDisplay // the TeX of a display block
)

// mathUnit is the math unit around a source offset: its kind and its range
// in the block's source.
type mathUnit struct {
	kind   int
	lo, hi int
}

// mathUnitAt is the math unit of block b around source offset off.
func mathUnitAt(b *Block, off int) mathUnit {
	r := runes(b.Text)
	off = max(0, min(off, len(r)))
	switch {
	case b.Kind.HasInline():
		return mathUnit{unitProse, 0, len(r)}
	case b.Kind == Table:
		lineStart, lineEnd := off, off
		for lineStart > 0 && r[lineStart-1] != '\n' {
			lineStart--
		}
		for lineEnd < len(r) && r[lineEnd] != '\n' {
			lineEnd++
		}
		lo, hi := lineStart, lineEnd
		for i := lineStart; i < lineEnd; i++ {
			if r[i] != '|' || i > 0 && r[i-1] == '\\' {
				continue
			}
			if i < off {
				lo = i + 1
			} else {
				hi = i
				break
			}
		}
		return mathUnit{unitCell, lo, hi}
	case b.Kind == Math:
		return mathUnit{unitDisplay, 0, len(r)}
	}
	return mathUnit{}
}

// mathSpanAt is the TeX around source offset off: in a display block the
// whole unit, elsewhere the content of the $…$ span holding off, both of its
// edges counting as inside. end is the offset of the closing dollar.
func mathSpanAt(r []rune, u mathUnit, off int) (start, end int, ok bool) {
	switch u.kind {
	case unitDisplay:
		return u.lo, u.hi, true
	case unitProse, unitCell:
		rel := off - u.lo
		for _, sp := range parseInline(r[u.lo:u.hi]) {
			if sp.Kind == sMath && rel >= sp.CStart && rel <= sp.CEnd {
				return u.lo + sp.CStart, u.lo + sp.CEnd, true
			}
		}
	}
	return 0, 0, false
}

// escapedAt reports whether the character at pos follows an odd run of
// backslashes.
func escapedAt(r []rune, pos int) bool {
	n := 0
	for i := pos - 1; i >= 0 && r[i] == '\\'; i-- {
		n++
	}
	return n%2 == 1
}

// shouldPairDollar reports whether a `$` typed at pos, an offset into the
// unit's text r, puts in its closing dollar too (InlineMarkdown's
// shouldAutoPairDollarIn). ignoreFollowing skips the rule about what
// follows, for wrapping a selection, which is what follows then.
func shouldPairDollar(r []rune, pos int, ignoreFollowing bool) bool {
	pos = max(0, min(pos, len(r)))
	if escapedAt(r, pos) {
		return false
	}
	// A dollar before the caret still open: this one closes it.
	dollars := 0
	for i := 0; i < pos; i++ {
		if r[i] == '$' && !escapedAt(r, i) {
			dollars++
		}
	}
	if dollars%2 == 1 {
		return false
	}
	for _, sp := range parseInline(r) {
		if sp.Kind == sCode && pos >= sp.CStart && pos <= sp.CEnd {
			return false
		}
	}
	if !ignoreFollowing && pos < len(r) {
		if next := r[pos]; unicode.IsLetter(next) || unicode.IsNumber(next) || next == '$' {
			return false // a dollar in front of text is a price
		}
	}
	return true
}

func isASCIILetter(c rune) bool { return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' }

// mathWord is the backslash word ending at pos in r, not reaching back
// before lo: where its backslash is and what follows the backslash. A TeX
// control symbol (\| \, \; \: \!) is one character, and a backslash just
// after the backslash word's own backslash is the query `\`, for \\.
func mathWord(r []rune, lo, pos int) (trigger int, query string, ok bool) {
	if pos-2 >= lo && r[pos-2] == '\\' && strings.ContainsRune("|,;:!", r[pos-1]) {
		return pos - 2, string(r[pos-1]), true
	}
	s := pos
	for s > lo && isASCIILetter(r[s-1]) {
		s--
	}
	if s == lo || r[s-1] != '\\' {
		return 0, "", false
	}
	if s == pos && s-2 >= lo && r[s-2] == '\\' {
		return s - 2, `\`, true
	}
	return s - 1, string(r[s:pos]), true
}

// blockSurface is a block of the note, holding the caret.
type blockSurface struct {
	e   *Editor
	bid int64
}

func (s blockSurface) block() *Block { return s.e.Doc.Block(s.bid) }
func (s blockSurface) id() any       { return s.bid }
func (s blockSurface) text() []rune  { return runes(s.block().Text) }
func (s blockSurface) caret() int    { return s.e.Doc.Caret.Off }

func (s blockSurface) selection() (int, int) {
	d := s.e.Doc
	return min(d.Anchor.Off, d.Caret.Off), max(d.Anchor.Off, d.Caret.Off)
}

func (s blockSurface) unitAt(off int) mathUnit { return mathUnitAt(s.block(), off) }

func (s blockSurface) replace(start, end int, text string, caret int, typing bool) {
	d := s.e.Doc
	kind := "insert"
	if typing {
		kind = "typing"
	}
	d.Edit(kind, func() {
		b := s.block()
		r := runes(b.Text)
		b.Text = string(r[:start]) + text + string(r[end:])
		d.SetCaret(b.ID, caret)
	})
}

func (s blockSurface) setCaret(off int) { s.e.Doc.SetCaret(s.bid, off) }

func (s blockSurface) caretRect() (geom.Rect, bool) {
	e := s.e
	caret, ok := e.caretRect()
	w := e.Window()
	if !ok || w == nil {
		return geom.Rect{}, false
	}
	return w.Content().RectFromRoot(e.RectToRoot(caret)), true
}

// blockSurface is the block holding the caret, when the note can be
// changed there and nothing is selected across blocks.
func (e *Editor) blockSurface() (mathSurface, bool) {
	d := e.Doc
	b := d.CaretBlock()
	if b == nil || !d.Focused || d.CrossBlock() || d.ReadOnly {
		return nil, false
	}
	return blockSurface{e, b.ID}, true
}

// pairClose is the offset of the closing dollar of the pair `$` put in on
// s, while the pair still stands with the caret inside it, else -1.
func pairClose(s mathSurface, t *mathTrack) int {
	if s == nil || t.pairOn != s.id() {
		return -1
	}
	r := s.text()
	open, caret := t.pairOpen, s.caret()
	if open < 0 || open >= len(r) || r[open] != '$' || caret <= open {
		return -1
	}
	for i := open + 1; i < len(r); i++ {
		if r[i] == '$' {
			if i < caret {
				return -1
			}
			return i
		}
	}
	return -1
}

// SetMathCommands gives the editor the command menu's list: the application
// makes it once, with the math engine's commands (mathcmd.New(mathtex.Commands)),
// shares it between editors, and saves its recently used commands.
func (e *Editor) SetMathCommands(m *mathcmd.Model) { e.math.model = m }

// mathModel is the command menu's list, made without the engine's commands
// when the application gave none.
func (e *Editor) mathModel() *mathcmd.Model {
	if e.math.model == nil {
		e.math.model = mathcmd.New(nil)
	}
	return e.math.model
}

// mathTyped handles a `$` or `\` typed at the caret in the note, and
// reports whether it did; otherwise the character is typed as it is.
func (e *Editor) mathTyped(ch string) bool {
	s, ok := e.blockSurface()
	if !ok || !e.mathType(s, &e.math.track, ch) {
		return false
	}
	e.selectAllN = 0
	e.hasGoal = false
	e.touched()
	return true
}

// mathType handles a `$` or `\` typed on surface s.
func (e *Editor) mathType(s mathSurface, t *mathTrack, ch string) bool {
	if ch != "$" && ch != `\` {
		return false
	}
	selA, selB := s.selection()
	u := s.unitAt(selA)
	if ch == "$" {
		return e.dollarTyped(s, t, u, selA, selB)
	}
	if !e.backslashTyped(s, t, u, selA, selB) {
		return false
	}
	if e.math.menu == nil {
		e.openMathMenu(s, t, u.kind == unitDisplay)
	}
	return true
}

func (e *Editor) dollarTyped(s mathSurface, t *mathTrack, u mathUnit, selA, selB int) bool {
	if u.kind != unitProse && u.kind != unitCell {
		return false
	}
	unit := s.text()[u.lo:u.hi]
	if selA == selB {
		if c := pairClose(s, t); c >= 0 && c == selA {
			// Step over the closing dollar rather than add a third.
			t.pairOn = nil
			s.setCaret(c + 1)
			return true
		}
		if !shouldPairDollar(unit, selA-u.lo, false) {
			return false
		}
		s.replace(selA, selA, "$$", selA+1, true)
		t.pairOn, t.pairOpen = s.id(), selA
		return true
	}
	if selB > u.hi || !shouldPairDollar(unit, selA-u.lo, true) {
		return false
	}
	wrapped := "$" + string(s.text()[selA:selB]) + "$"
	s.replace(selA, selB, wrapped, selA+len(runes(wrapped)), false)
	return true
}

func (e *Editor) backslashTyped(s mathSurface, t *mathTrack, u mathUnit, selA, selB int) bool {
	if u.kind == unitNone {
		return false
	}
	_, _, inSpan := mathSpanAt(s.text(), u, selA)
	if e.math.menu == nil && !inSpan && pairClose(s, t) < 0 {
		return false // prose: a backslash like any other character
	}
	s.replace(selA, selB, `\`, selA+1, true)
	return true
}

// mathKey handles the keys of the math typing aids in the note other than
// the menu's, and reports whether it used the key.
func (e *Editor) mathKey(key unison.KeyCode, ctrl, shift bool) bool {
	s, ok := e.blockSurface()
	return ok && e.mathKeyOn(s, &e.math.track, key, ctrl, shift)
}

// mathKeyOn handles Backspace and Delete in a fresh pair, Tab and Shift+Tab
// along template slots, and Ctrl+Space, on surface s.
func (e *Editor) mathKeyOn(s mathSurface, t *mathTrack, key unison.KeyCode, ctrl, shift bool) bool {
	r := s.text()
	selA, selB := s.selection()
	caret := s.caret()
	switch {
	case key == unison.KeySpace && ctrl:
		u := s.unitAt(caret)
		if _, _, in := mathSpanAt(r, u, caret); !in {
			return false
		}
		w := caret
		for w > u.lo && isASCIILetter(r[w-1]) {
			w--
		}
		if w > u.lo && r[w-1] == '\\' && e.math.menu == nil {
			e.openMathMenu(s, t, u.kind == unitDisplay)
		}
		return true
	case key == unison.KeyBackspace && !ctrl && selA == selB && t.pairOn == s.id() &&
		caret == t.pairOpen+1 && pairClose(s, t) == t.pairOpen+1:
		// The pair is still empty: both dollars go, as if the `$` had not
		// been typed.
		open := t.pairOpen
		t.pairOn = nil
		s.replace(open, open+2, "", open, false)
		return true
	case key == unison.KeyDelete && !ctrl && selA == selB && t.pairOn == s.id() && pairClose(s, t) == caret:
		// Only the closing dollar goes, which leaves a literal dollar.
		t.pairOn = nil
		s.replace(caret, caret+1, "", caret, false)
		return true
	case key == unison.KeyTab && !ctrl && t.chainOn == s.id():
		return jumpSlot(s, t, shift)
	}
	return false
}

// jumpSlot moves the caret to the next (or previous) empty {} or [] pair of
// the formula holding it. It reports false, and ends the chain when going
// forward, when there is none that way or the caret left the formula.
func jumpSlot(s mathSurface, t *mathTrack, backward bool) bool {
	r := s.text()
	caret := s.caret()
	from, to, ok := mathSpanAt(r, s.unitAt(caret), caret)
	if !ok {
		t.chainOn = nil
		return false
	}
	var slots []int
	for i := from; i+1 < min(to, len(r)); i++ {
		if two := string(r[i : i+2]); two == "{}" || two == "[]" {
			slots = append(slots, i+1)
		}
	}
	if len(slots) == 0 {
		t.chainOn = nil
		return false
	}
	if backward {
		for k := len(slots) - 1; k >= 0; k-- {
			if slots[k] < caret {
				s.setCaret(slots[k])
				return true
			}
		}
		return false
	}
	for _, slot := range slots {
		if slot > caret {
			s.setCaret(slot)
			return true
		}
	}
	t.chainOn = nil
	return false
}

// applyMathCommand puts a menu entry's template in place of the backslash
// word at the caret of s, with the caret in its first empty slot, and starts
// Tab's walk of its slots. A display equation takes the entry's form on
// several lines.
func (e *Editor) applyMathCommand(row mathcmd.Entry, s mathSurface, t *mathTrack, display bool) {
	insert, off := row.Insert, row.CursorOffset
	if display && row.InsertDisplay != "" {
		insert, off = row.InsertDisplay, row.CursorOffsetDisplay
	}
	r := s.text()
	end := min(s.caret(), len(r))
	start := end
	if trigger, _, ok := mathWord(r, s.unitAt(end).lo, end); ok {
		start = trigger
	}
	// A command without slots runs into a letter after it (\alphax): a
	// space keeps them apart.
	ins := runes(insert)
	if off < 0 && end < len(r) && isASCIILetter(r[end]) && len(ins) > 0 && isASCIILetter(ins[len(ins)-1]) {
		insert += " "
		ins = runes(insert)
	}
	caret := start + len(ins)
	if off >= 0 {
		caret = start + off
	}
	s.replace(start, end, insert, caret, false)
	t.chainOn = nil
	if strings.Contains(insert, "{}") || strings.Contains(insert, "[]") {
		t.chainOn = s.id()
	}
	e.touched()
}

// syncMath brings what the math typing aids remember about the note back in
// step with it after a change.
func (e *Editor) syncMath() {
	s, _ := e.blockSurface()
	e.syncMathOn(s, &e.math.track)
}

// syncMathOn brings what is remembered about a kind of surface back in step
// after a change: s is the surface with the caret, nil for none. A pair the
// caret left, or whose dollars are gone, is forgotten, as is a slot walk on
// a surface the caret left, and the menu, when it was opened for this kind
// of surface, follows the backslash word at the caret, closing when there
// is none.
func (e *Editor) syncMathOn(s mathSurface, t *mathTrack) {
	if t.pairOn != nil && pairClose(s, t) < 0 {
		t.pairOn = nil
	}
	if t.chainOn != nil && (s == nil || t.chainOn != s.id()) {
		t.chainOn = nil
	}
	m := e.math.menu
	if m == nil || m.track != t {
		return
	}
	if s == nil || s.id() != m.surface.id() {
		e.closeMathMenu()
		return
	}
	selA, selB := s.selection()
	u := s.unitAt(selA)
	_, q, ok := mathWord(s.text(), u.lo, s.caret())
	if selA != selB || !ok || u.kind == unitNone {
		e.closeMathMenu()
		return
	}
	m.setQuery(q)
}

// MathMenuState is the command menu's state for tests: whether it is open,
// its query, the names it lists (the completion rows, or in browse mode the
// highlighted category's entries), and the highlighted one's index.
func (e *Editor) MathMenuState() (open bool, query string, names []string, sel int) {
	m := e.math.menu
	if m == nil {
		return false, "", nil, -1
	}
	rows, sel := m.rows, m.sel
	if m.query == "" {
		rows, sel = m.grid, m.cell
	}
	for _, row := range rows {
		names = append(names, row.Name)
	}
	return true, m.query, slices.Clip(names), sel
}
