package editor

// The block menu: the gutter's menu button, Shift+F10, the Menu key or a
// right-click opens it. Its commands act on the block selection when the
// block is part of one, otherwise on the block itself. It is kvit-ui's menu;
// "Turn into" and "Copy as" open submenus beside their lines.

import (
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// blockCommand is one line of the block menu, or of a submenu.
type blockCommand struct {
	label string
	sep   bool // a separator above this line
	run   func(e *Editor, ids []int64)
	more  []blockCommand // the submenu this line opens
	// only limits a line to embed or image blocks; "" shows it always.
	only string // "embed", "image", or ""
}

var blockCommands = []blockCommand{
	{label: "&Copy", run: func(e *Editor, ids []int64) { e.copyMarkdown(e.blocksMarkdown(ids)) }},
	{label: "Copy &as…", more: []blockCommand{
		{label: "&Markdown", run: func(e *Editor, ids []int64) { unison.ClipboardSetText(e.blocksMarkdown(ids)) }},
		{label: "&Plain text", run: func(e *Editor, ids []int64) { unison.ClipboardSetText(e.blocksPlain(ids)) }},
	}},
	{label: "&Export…", run: func(e *Editor, ids []int64) {
		if e.OnExport != nil {
			e.OnExport(ids)
		}
	}},
	{label: "&Turn into", sep: true, more: turnInto()},
	{label: "Ali&gn", more: []blockCommand{
		{label: "&Left", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "align", "") }},
		{label: "&Center", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "align", "center") }},
		{label: "&Right", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "align", "right") }},
	}},
	{label: "Dro&p cap", more: []blockCommand{
		{label: "&None", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "dropcap", "") }},
		{label: "&2 lines", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "dropcap", "2") }},
		{label: "&3 lines", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "dropcap", "3") }},
		{label: "&5 lines", run: func(e *Editor, ids []int64) { e.Doc.SetAttr(ids, "dropcap", "5") }},
	}},
	{label: "Remove &line breaks", run: func(e *Editor, ids []int64) { e.Doc.JoinLines(ids) }},
	{label: "Edit &URL…", only: "embed", run: func(e *Editor, ids []int64) {
		for _, id := range ids {
			if b := e.Doc.Block(id); b != nil {
				if ref, ok := ParseImageLine(strings.TrimSpace(b.Text)); ok && isEmbed(ref) {
					if e.OnEditEmbed != nil {
						e.OnEditEmbed(id, ref.Path)
					}
					break
				}
			}
		}
	}},
	{label: "Embe&d size", only: "embed", more: []blockCommand{
		{label: "&Default", run: func(e *Editor, ids []int64) { e.SetEmbedSize(embedIDs(e, ids), 0, 0) }},
		{label: "&320 px", run: func(e *Editor, ids []int64) { e.SetEmbedSize(embedIDs(e, ids), 320, 0) }},
		{label: "&480 px", run: func(e *Editor, ids []int64) { e.SetEmbedSize(embedIDs(e, ids), 480, 0) }},
		{label: "&640 px", run: func(e *Editor, ids []int64) { e.SetEmbedSize(embedIDs(e, ids), 640, 0) }},
	}},
	{label: "Image e&ffects", only: "image", more: []blockCommand{
		{label: "&Rounded", run: func(e *Editor, ids []int64) { e.toggleImageEffect(imageIDs(e, ids), "rounded", "") }},
		{label: "&Shadow", run: func(e *Editor, ids []int64) { e.toggleImageEffect(imageIDs(e, ids), "shadow", "") }},
		{label: "&Border", run: func(e *Editor, ids []int64) { e.toggleImageEffect(imageIDs(e, ids), "border", "") }},
		{label: "&Plain", run: func(e *Editor, ids []int64) { e.SetImageEffects(imageIDs(e, ids), -1, false, "", "", false) }},
	}},
	{label: "Insert block &below", sep: true, run: func(e *Editor, ids []int64) { e.insertBelow(ids[len(ids)-1]) }},
	{label: "D&uplicate", run: func(e *Editor, ids []int64) { e.Doc.Duplicate(ids) }},
	{label: "&Delete", run: func(e *Editor, ids []int64) { e.Doc.DeleteBlocks(ids); e.clearBlockSel() }},
	{label: "&Move up", sep: true, run: func(e *Editor, ids []int64) { e.Doc.Move(ids, -1) }},
	{label: "Move dow&n", run: func(e *Editor, ids []int64) { e.Doc.Move(ids, 1) }},
	{label: "&Indent", run: func(e *Editor, ids []int64) { e.Doc.Indent(ids, 1) }},
	{label: "&Outdent", run: func(e *Editor, ids []int64) { e.Doc.Indent(ids, -1) }},
}

// turnInto is the submenu of "Turn into": every kind a block can be
// turned into, as the / menu names them.
func turnInto() []blockCommand {
	var out []blockCommand
	for _, it := range menuItems {
		kind := it.kind
		// A divider has no text to keep, and the entries that make a new
		// kind of block from scratch (a picture, a table) have none to
		// start from.
		if kind == Divider || it.init != nil {
			continue
		}
		out = append(out, blockCommand{label: it.name, run: func(e *Editor, ids []int64) {
			for _, id := range ids {
				e.Doc.Convert(id, kind)
			}
		}})
	}
	return out
}

// BlockMenuCommands are the block menu's lines, in order, and for a line
// that opens a submenu, the submenu's lines under its label.
func BlockMenuCommands() (lines []string, more map[string][]string) {
	more = map[string][]string{}
	for _, c := range blockCommands {
		label, _, _ := kvitui.AccessText(c.label)
		lines = append(lines, label)
		for _, m := range c.more {
			sub, _, _ := kvitui.AccessText(m.label)
			more[label] = append(more[label], sub)
		}
	}
	return lines, more
}

// blocksPlain is blocks' text without Markdown, a blank line between them.
func (e *Editor) blocksPlain(ids []int64) string {
	d := e.Doc
	var parts []string
	for _, id := range ids {
		b := d.Block(id)
		src := []rune(b.Text)
		var spans []span
		if b.Kind.HasInline() {
			spans = parseInline(src)
		}
		parts = append(parts, string(project(src, spans, nil).Disp))
	}
	return strings.Join(parts, "\n\n")
}

// embedIDs are the ids that are embed cards.
func embedIDs(e *Editor, ids []int64) []int64 {
	var out []int64
	for _, id := range ids {
		if b := e.Doc.Block(id); b != nil {
			if ref, ok := ParseImageLine(strings.TrimSpace(b.Text)); ok && isEmbed(ref) {
				out = append(out, id)
			}
		}
	}
	if len(out) == 0 {
		return ids
	}
	return out
}

// imageIDs are the ids that are pictures or media.
func imageIDs(e *Editor, ids []int64) []int64 {
	var out []int64
	for _, id := range ids {
		if b := e.Doc.Block(id); b != nil && (b.Kind == Image || b.Kind == Media) {
			out = append(out, id)
		}
	}
	if len(out) == 0 {
		return ids
	}
	return out
}

// toggleImageEffect toggles one image effect flag: rounded (default 12),
// shadow or border.
func (e *Editor) toggleImageEffect(ids []int64, key, value string) {
	e.Doc.Edit("attributes", func() {
		for _, id := range ids {
			b := e.Doc.Block(id)
			if b == nil {
				continue
			}
			has := false
			var keep []string
			for _, tok := range strings.Fields(b.Attrs) {
				if k, _, _ := strings.Cut(tok, "="); k == key {
					has = true
					continue
				}
				keep = append(keep, tok)
			}
			if !has {
				if value != "" {
					keep = append(keep, key+"="+value)
				} else if key == "rounded" {
					keep = append(keep, key+"=12")
				} else {
					keep = append(keep, key)
				}
			}
			b.Attrs = canonicalAttrs(strings.Join(keep, " "))
		}
	})
	e.touched()
	e.changed()
}

// openBlockMenu opens the block menu for a block under a part of the
// editor, given in the editor's coordinates.
func (e *Editor) openBlockMenu(id int64, at geom.Rect) {
	ids := []int64{id}
	if e.blockSel[id] {
		ids = e.SelectedBlocks()
	}
	e.closeMenu()
	e.ui.ShowMenuAt(e, at, "Block", e.menuItems(blockCommands, ids))
}

// hasOnly reports whether ids hold a block of the menu's limited kind.
func (e *Editor) hasOnly(ids []int64, only string) bool {
	for _, id := range ids {
		b := e.Doc.Block(id)
		if b == nil {
			continue
		}
		switch only {
		case "embed":
			if ref, ok := ParseImageLine(strings.TrimSpace(b.Text)); ok && isEmbed(ref) {
				return true
			}
		case "image":
			if b.Kind == Image || b.Kind == Media {
				return true
			}
		}
	}
	return false
}

// menuItems turns commands for blocks into menu lines.
func (e *Editor) menuItems(cmds []blockCommand, ids []int64) []kvitui.MenuItem {
	var items []kvitui.MenuItem
	for _, c := range cmds {
		if c.only != "" && !e.hasOnly(ids, c.only) {
			continue
		}
		if c.sep && len(items) > 0 {
			items = append(items, kvitui.MenuItem{Separator: true})
		}
		if c.more != nil {
			sub := e.menuItems(c.more, ids)
			if c.label == "Copy &as…" && e.BlocksHTML != nil {
				sub = append(sub, kvitui.MenuItem{Text: "&HTML", OnSelect: func() {
					unison.ClipboardSetText(e.BlocksHTML(e.Doc.Blocks, e.IndexesOf(ids)))
				}})
			}
			items = append(items, kvitui.MenuItem{Text: c.label, Items: sub})
			continue
		}
		c := c
		items = append(items, kvitui.MenuItem{Text: c.label, OnSelect: func() {
			c.run(e, ids)
			e.touched()
			e.changed()
		}})
	}
	return items
}

// openBlockMenuForCaret opens the block menu for the block holding the
// caret, or the first selected block, under that block's row: what
// Shift+F10 and the Menu key do.
func (e *Editor) openBlockMenuForCaret() bool {
	d := e.Doc
	id := d.Caret.Block
	if ids := e.SelectedBlocks(); len(ids) > 0 {
		id = ids[0]
	} else if !d.Focused {
		return false
	}
	i := d.Index(id)
	if i < 0 || i >= len(e.tops) {
		return false
	}
	r := e.rowRect(i)
	e.openBlockMenu(id, geom.NewRect(r.X+e.px(gutterWidth), r.Y, 0, r.Height))
	return true
}

// inSelection reports whether a position is inside the text selection.
func (e *Editor) inSelection(p Pos) bool {
	d := e.Doc
	if !d.HasSelection() {
		return false
	}
	a, b := d.SelRange()
	ia, ib, ip := d.Index(a.Block), d.Index(b.Block), d.Index(p.Block)
	after := ip > ia || (ip == ia && p.Off >= a.Off)
	before := ip < ib || (ip == ib && p.Off <= b.Off)
	return after && before
}

// openTextMenu opens the menu for text: cut, copy and paste, the inline
// formats, the link dialog, and the block's own commands.
func (e *Editor) openTextMenu(at geom.Rect) {
	d := e.Doc
	e.closeMenu()
	has := d.HasSelection()
	act := func(f func()) func() {
		return func() {
			f()
			e.touched()
			e.changed()
		}
	}
	format := func(label, marker string) kvitui.MenuItem {
		return kvitui.MenuItem{Text: label, Disabled: d.ReadOnly, OnSelect: act(func() { d.ToggleFormat(marker) })}
	}
	items := []kvitui.MenuItem{
		{Text: "Cut", Disabled: !has || d.ReadOnly, OnSelect: act(func() {
			e.copyMarkdown(d.SelectedMarkdown())
			d.DeleteSelection()
		})},
		{Text: "Copy", Disabled: !has, OnSelect: func() { e.copyMarkdown(d.SelectedMarkdown()) }},
		{Text: "Paste", Disabled: d.ReadOnly || !unison.ClipboardHasText(), OnSelect: act(e.pasteClipboard)},
		{Text: "Paste as plain text", Disabled: d.ReadOnly || !unison.ClipboardHasText(), OnSelect: act(func() {
			d.Paste(strings.ReplaceAll(unison.ClipboardGetText(), "\r\n", "\n"), true)
		})},
		{Separator: true},
		{Text: "Formatting", Items: []kvitui.MenuItem{
			format("Bold", "**"), format("Italic", "*"), format("Underline", "++"),
			format("Strikethrough", "~~"), format("Inline code", "`"), format("Highlight", "=="),
		}},
	}
	if e.OnLink != nil {
		link := e.OnLink
		items = append(items, kvitui.MenuItem{Text: "Link…", Disabled: d.ReadOnly, OnSelect: func() { link() }})
	}
	if b := d.CaretBlock(); b != nil {
		items = append(items, kvitui.MenuItem{Separator: true},
			kvitui.MenuItem{Text: "Block", Items: e.menuItems(blockCommands, []int64{b.ID})})
	}
	e.ui.ShowMenuAt(e, at, "Text", items)
}

// openLinkMenu opens the menu for a link under the pointer: open it, edit
// it, or remove its formatting while keeping its text.
func (e *Editor) openLinkMenu(at geom.Rect, pos Pos) {
	if items := e.linkMenuItems(pos); items != nil {
		e.closeMenu()
		e.ui.ShowMenuAt(e, at, "Link", items)
		return
	}
	e.openTextMenu(at)
}

// linkMenuItems are the link menu's lines for the link at pos, or nil when
// no menu of its own opens there and the text menu serves instead: a wiki
// link or a bare address only opens, a Markdown link also edits and removes.
func (e *Editor) linkMenuItems(pos Pos) []kvitui.MenuItem {
	b := e.Doc.Block(pos.Block)
	if b == nil {
		return nil
	}
	ref, sp, ok := linkAt(b.Text, pos.Off)
	if !ok {
		return nil
	}
	var items []kvitui.MenuItem
	if e.FollowLink != nil {
		follow := e.FollowLink
		items = append(items, kvitui.MenuItem{Text: "Open link", OnSelect: func() { follow(ref) }})
	}
	if e.OnLink != nil && !ref.Wiki && sp.Start != sp.CStart {
		link := e.OnLink
		id, off := pos.Block, sp.CStart
		items = append(items, kvitui.MenuItem{Text: "Edit link…", Disabled: e.Doc.ReadOnly, OnSelect: func() {
			e.clearBlockSel()
			e.Doc.SetCaret(id, off)
			e.RequestFocus()
			e.touched()
			e.changed()
			link()
		}})
	}
	if !ref.Wiki && sp.Start != sp.CStart {
		items = append(items, kvitui.MenuItem{Text: "Remove link", Disabled: e.Doc.ReadOnly, OnSelect: func() {
			e.RemoveLinkAt(pos)
		}})
	}
	if len(items) == 0 {
		return nil
	}
	return items
}

// IndexesOf are blocks' places in the note, in order.
func (e *Editor) IndexesOf(ids []int64) []int {
	var out []int
	for _, id := range ids {
		if i := e.Doc.Index(id); i >= 0 {
			out = append(out, i)
		}
	}
	return out
}
