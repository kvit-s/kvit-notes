package editor

import (
	"runtime"
	"testing"
	"weak"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/unison"
)

// An editor no longer used is not kept by the hook that tells it the theme
// or the size changed, while one in use is still told. Kvit Works makes a
// read-only document for each turn of a conversation it draws and throws
// them away as it draws another, and a hook holding each editor kept every
// one ever made, with its note, its laid-out lines and its pictures, for the
// life of the program.
func TestAnEditorNoLongerUsedIsNotKeptByTheUI(t *testing.T) {
	var ui *kvitui.UI
	var host *unison.Panel
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(u *kvitui.UI) unison.Paneler {
		ui = u
		host = unison.NewPanel()
		host.SetLayout(&unison.FlexLayout{Columns: 1})
		return host
	})
	var kept *Document
	var dropped weak.Pointer[Editor]
	s.Do(func() {
		kept = NewDocument(ui, DocumentOptions{})
		kept.SetMarkdown("A turn still on the screen.")
		host.AddChild(kept)
		d := NewDocument(ui, DocumentOptions{})
		d.SetMarkdown("A turn drawn and then thrown away.")
		host.AddChild(d)
		dropped = weak.Make(d.Editor)
	})
	s.Sync()
	s.Do(func() { host.RemoveChild(host.Children()[1]) })
	s.Sync()
	for i := 0; dropped.Value() != nil; i++ {
		if i == 10 {
			t.Fatal("an editor no longer used is still held")
		}
		runtime.GC()
		s.Sync()
	}
	var before, after int
	s.Do(func() {
		before = kept.Editor.generation
		ui.Interface.SetFontSize(ui.Interface.FontSize() + 1)
		after = kept.Editor.generation
	})
	if after == before {
		t.Error("an editor in use is not told the interface size changed")
	}
}
