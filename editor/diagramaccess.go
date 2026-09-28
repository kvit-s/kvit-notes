package editor

// What a screen reader is told of a Mermaid diagram block. Read, the block
// is a picture named by the diagram's summary ("Mermaid flowchart with 5
// nodes and 4 connections"), or by the selected element, with the
// diagram's accessible title and description, as Kvit's diagram block
// reports them; while the pointer is over it, its controls are buttons.
// With the caret in it, the block is its source, an editable text, and the
// preview under it is a picture of its own.

import (
	"strings"

	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/role"
)

// describeDiagram describes diagram block i shown as its drawing, and
// reports false for any other block.
func (e *Editor) describeDiagram(i int, n *accessibility.Node) bool {
	if !e.diagramReads(i) {
		return false
	}
	c := e.diagramFor(&e.Doc.Blocks[i]).read
	n.Role = role.Image
	switch {
	case c.hasSelection():
		n.Name = c.selectionLabel()
	case c.hasScene:
		n.Name = c.scene.Summary
	default:
		n.Name, _ = e.diagramNotice(i, e.diagramFor(&e.Doc.Blocks[i]))
	}
	if c.hasScene {
		var parts []string
		for _, s := range []string{c.scene.AccTitle, c.scene.AccDescr} {
			if s = strings.TrimSpace(s); s != "" {
				parts = append(parts, s)
			}
		}
		n.Description = strings.Join(parts, ". ")
	}
	return true
}

// describeDiagramParts adds the controls of diagram block i under the
// pointer, or the preview under its source, as children of its node.
func (e *Editor) describeDiagramParts(b *unison.AccessibilityBuilder, id accessibility.NodeID, i int) {
	blk := &e.Doc.Blocks[i]
	if id == 0 || !isMermaid(blk) {
		return
	}
	if e.diagramReads(i) {
		if e.hover != blk.ID {
			return
		}
		chips, _ := e.diagramChips(i)
		for _, ch := range chips {
			b.AddVirtualChildOf(id, partKey{blk.ID, ch.part}, func(n *accessibility.Node) {
				n.Role = role.Button
				n.Name = ch.name
				n.Bounds = ch.rect
				n.Actions = n.Actions.With(accessibility.Press)
			})
		}
		return
	}
	c := e.diagramFor(blk).preview
	p := e.diagramPreviewParts(i)
	b.AddVirtualChildOf(id, partKey{blk.ID, partDiagramPreview}, func(n *accessibility.Node) {
		n.Role = role.Image
		n.Name = "Diagram preview"
		n.Description = "Click a shape to move the caret to the line that draws it"
		if c.rendering {
			n.Description = "Rendering"
		}
		n.Bounds = p.panel
	})
}
