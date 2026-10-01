package app

// The tag strip above a note: the note's tags as removable tags, and a small
// field that offers the vault's tags as the reader types and adds the one
// chosen, or a new one.

import (
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
)

// tagFieldWidth is the add field's width in design pixels.
const tagFieldWidth = 110

// TagStrip shows a note's tags and adds and removes them.
type TagStrip struct {
	*unison.Panel
	ui    *kvitui.UI
	field *kvitui.TypeAhead
	tags  []string
	// OnAdd and OnRemove run when the reader adds or removes a tag.
	OnAdd    func(tag string)
	OnRemove func(tag string)
}

// NewTagStrip returns an empty strip.
func NewTagStrip(ui *kvitui.UI) *TagStrip {
	s := &TagStrip{ui: ui}
	s.field = kvitui.NewTypeAhead(ui, "Add a tag", true)
	s.field.Placeholder = "+ Tag"
	s.field.OnChoose = func(value string) {
		tag := strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(value), "#"))
		s.field.SetText("")
		if tag != "" && s.OnAdd != nil {
			s.OnAdd(tag)
		}
	}
	s.Panel = unison.NewPanel()
	s.SetLayout(&unison.FlexLayout{Columns: 1})
	s.rebuild()
	return s
}

// SetSuggestions is every tag the field offers.
func (s *TagStrip) SetSuggestions(tags []string) {
	s.field.Source = s.field.Source[:0]
	for _, t := range tags {
		s.field.Source = append(s.field.Source, kvitui.Suggestion{Value: t})
	}
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
