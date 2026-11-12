# Request to kvit-ui: let a focused control keep Ctrl+E

This request comes from Kvit Notes (`github.com/kvit-s/kvit-notes`), the
Markdown block editor built on kvit-ui, which is on kvit-ui commit `c6f6e90`.
It asks for one small addition so that Kvit Notes can remove the last place
where it handles a key ahead of kvit-ui's own key handling.

## Now

Guideline KEY-13 (`docs/ui-guidelines/rules.md`) gives Find two keys on the
Windows form, Ctrl+F and Ctrl+E. `windowKeys` in `windowkeys.go` sends any
Find key that no command has to the window's `SearchField`. It runs inside
the window's `KeyDownCallback`, before `Window.OnKeyDown` and before the
focused control is offered the key.

A window with a find bar registers `Command{Std: StdFind, FindBar: true}`,
which takes Ctrl+F only; Ctrl+E still goes to the search field. Kvit Notes
uses this.

## In Kvit Notes

The vault window has a `SearchField` (search across notes) and the block
editor. In the editor Ctrl+E turns the selection into inline code, as Ctrl+B
makes it bold and Ctrl+I italic; the key is in the app's shortcuts list and
the owner has decided it stays (12 November 2026). Because the window takes
Ctrl+E before the focused control sees it, the editor never receives the key.

The workaround is in `keepFindKeys` in Kvit Notes' `app/window.go`: it wraps
the unison window's `KeyDownCallback` and, for Ctrl+E while the editor is
focused, returns false before kvit-ui's handler runs, so unison offers the
key to the editor. For that one key this skips every earlier step of
kvit-ui's handling (the drag keys, the popup keys, the menu bar, the access
keys, the commands).

The editor is the app's own panel, not a `unison.Field`, so kvit-ui's
existing exception for a text field's typing keys (`isTextField` and
`typingKey` in `commandkeys.go`) does not apply to it.

## Asked for

A way for a focused control to keep a key that `windowKeys` would otherwise
send to the search field, so that Ctrl+E pressed in Kvit Notes' editor
reaches the editor, and Ctrl+E pressed anywhere else in the window still
goes to the search field as KEY-13 says.

The shape is kvit-ui's to choose. Shapes that would work for Kvit Notes:

- `windowKeys` offers the second Find key (Ctrl+E) to the focused control
  first, and focuses the search field only when the control does not use it;
  or
- a mark on a panel, in the manner of `OpensPopup`, saying the control uses
  Ctrl+E itself (or, more generally, naming the keys it keeps), which
  `windowKeys` checks on the focused control and its ancestors; or
- a field on `Window` asked before the search field is focused, such as
  `KeepsFindKey func(focus *unison.Panel) bool`.

Only the search-field fallback in `windowKeys` needs to give way. A
registered command's key should go on working as it does now.

If kvit-ui's view is that KEY-13 should not have this exception, say so in
the reply and Kvit Notes will keep its workaround; the owner has decided the
key's meaning in Kvit Notes, and the wording of KEY-13 is kvit-ui's.

## Checking

Kvit Notes has `TestCtrlEIsInlineCodeInTheEditor` in `app/features_test.go`,
which presses Ctrl+E in the vault window with a selection in the editor and
expects inline code. In kvit-ui, a test beside
`TestAFindBarTakesOnlyFindsFirstKey` in `keyboard_test.go` would do: with a
`SearchField` in the window, Ctrl+E reaches a focused control that keeps the
key and focuses the search field from any other control.

## Reply wanted

The name and signature chosen, so Kvit Notes can replace the wrapper in
`keepFindKeys`.
