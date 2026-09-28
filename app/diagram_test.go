package app

import "testing"

// A diagram's messages, such as what a gesture did, show in the status bar,
// and its PNG control is given the window's save dialog.
func TestDiagramHooksReachTheWindow(t *testing.T) {
	s := openVault(t, notes{"Flow.md": "# Flow\n\n```mermaid\nflowchart LR\n  A --> B\n```\n"})
	s.do(func() {
		if s.w.Editor.SaveDiagramPNG == nil {
			t.Error("the PNG control has no save dialog")
		}
		s.w.Editor.OnStatus("Arranged A")
		if got := s.w.status.Activity; got != "Arranged A" {
			t.Errorf("the status bar says %q", got)
		}
	})
}
