package app

// The tag strip above a note (Kvit's TagStrip.qml): the note's tags as
// removable tags, and a small field that adds one on Return.

import (
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// tagFieldWidth is the add field's width in design pixels (TagStrip.qml).
const tagFieldWidth = 110

// TagStrip shows a note's tags and adds and removes them.
type TagStrip struct {
	*unison.Panel
	ui    *kvitui.UI
	field *kvitui.Field
	tags  []string
	// OnAdd and OnRemove run when the reader adds or removes a tag.
	OnAdd    func(tag string)
	OnRemove func(tag string)
}

// NewTagStrip returns an empty strip.
func NewTagStrip(ui *kvitui.UI) *TagStrip {
	s := &TagStrip{ui: ui}
	s.field = kvitui.NewField(ui)
	s.field.Label = "Add a tag"
	s.field.Placeholder = "+ Tag"
	edit := s.field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
			tag := strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(s.field.Text()), "#"))
			s.field.SetText("")
			if tag != "" && s.OnAdd != nil {
				s.OnAdd(tag)
			}
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	s.Panel = unison.NewPanel()
	s.SetLayout(&unison.FlexLayout{Columns: 1})
	s.rebuild()
	return s
}

// SetTags shows a note's tags.
func (s *TagStrip) SetTags(tags []string) {
	s.tags = append([]string(nil), tags...)
	s.rebuild()
}

func (s *TagStrip) rebuild() {
	s.RemoveAllChildren()
	var parts []unison.Paneler
	for _, tag := range s.tags {
		t := kvitui.NewTag(s.ui, tag)
		t.Removable = true
		t.OnRemove = func() {
			if s.OnRemove != nil {
				s.OnRemove(tag)
			}
		}
		parts = append(parts, t)
	}
	parts = append(parts, kvitui.Width(s.ui, kvitui.Px(tagFieldWidth), s.field))
	s.AddChild(kvitui.Row(s.ui, kvitui.SizeSpaceNear, parts...))
	for p := s.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	s.MarkForRedraw()
}
