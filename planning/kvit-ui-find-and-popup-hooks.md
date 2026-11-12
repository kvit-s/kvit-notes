# Request to kvit-ui: a find bar's own Ctrl+F, and a removable popup mark

This request comes from Kvit Notes (`github.com/kvit-s/kvit-notes`), the
Markdown block editor built on kvit-ui. Kvit Notes moved to kvit-ui commit
`bb47829` and two of kvit-ui's newer behaviours changed how the app works.
Kvit Notes now has a workaround for each, and each workaround either goes
around kvit-ui or is wider than it should be. This file asks for two small
additions to kvit-ui so the workarounds can be removed. Neither asks for a
change to a decided rule.

## 1. A find bar that takes Ctrl+F without taking Ctrl+E

### Now

Guideline KEY-13 (`docs/ui-guidelines/rules.md`) says, for the Windows form:
Ctrl+F and Ctrl+E both go to the window's search field, and where the window
has a separate find bar, Ctrl+F opens the find bar and Ctrl+E goes to the
search field.

`windowKeys` in `windowkeys.go` implements the first half: any key of
`StdFind` (Ctrl+F and Ctrl+E on the Windows form, Cmd-F on the macOS form)
focuses the window's `SearchField`. It runs inside the window's
`KeyDownCallback`, before `Window.OnKeyDown` and before the focused control
is offered the key.

The second half has no API. The only way for an app to take Find is
`Window.AddCommands` with a `Command{Std: StdFind}`, and that command gets
every key of `StdFind`, so Ctrl+E opens the find bar too and can never reach
the search field. A command with only `Shortcut: Ctrl+F` and no `Std` is
refused under KEY-5, because Ctrl+F is a standard command's key.

### In Kvit Notes

The vault window has both: a `SearchField` in the note list (search across
notes) and a find bar for the open note, opened by Ctrl+F from
`Window.OnKeyDown`. After the update Ctrl+F put the keyboard in the note
list's search field and the find bar could not be opened from the keyboard.

Kvit Notes' editor also uses Ctrl+E for inline code, and the window took that
key before the editor saw it. Whether Ctrl+E stays inline code is a decision
for the owner in Kvit Notes and is not part of this request; it is mentioned
because the workaround below covers both keys.

The workaround is `keepFindKeys` in Kvit Notes' `app/window.go`: it wraps the
unison window's `KeyDownCallback` and answers Ctrl+F, and Ctrl+E while the
editor is focused, before kvit-ui's handler runs. It goes around every earlier
step of kvit-ui's key handling for those two keys.

### Asked for

A way for an app to say that its window has a find bar, so that KEY-13's
second half holds: the first key of Find on each form (Ctrl+F, Cmd-F) runs
the app's function, and the remaining key (Ctrl+E on the Windows form) still
goes to the `SearchField`.

The shape is kvit-ui's to choose. Two that would work for Kvit Notes:

- a field on `Window`, such as `OnFind func()`, that `windowKeys` calls for
  the first Find key instead of focusing the search field; or
- a `Command` with `Std: StdFind` and a flag saying it is a find bar's, which
  takes only the first key of each form and leaves the rest to the search
  field. This form also gives the command's menu line its key.

The same key handling as today should apply: nothing happens while a popup
that is neither passive nor ambient is open, unless kvit-ui decides a find
bar's key should work then too (in Kvit Notes the find bar is itself a
popup, and Ctrl+F pressed while it is open puts the keyboard back in its
field with the text selected).

## 2. Removing the mark that lets a closing press through

### Now

Under PTR-13 (decided, Q-43) the press that closes a popup goes no further,
except on a control marked with `kvitui.OpensPopup(p)`, which sets
`ClientData()["kvitui.opensPopup"]` on the panel (`layer.go`). The key is
unexported and nothing removes the mark.

### In Kvit Notes

The editor is one panel that draws every block. Text edited in place, a table
cell, a task board card's line, a callout's title, a diagram's label, is a
field shown as a `Popup` over the place it edits, with `OnPressOutside`
writing the text back. A press on another cell must close this cell's field
and open that cell's field in the same press, as PTR-13 allows for a control
that opens a popup of its own. Before the update the press reached the
editor; after it, clicking another cell took two presses, and a double press
on a table header no longer sorted the column.

The workaround is `kvitui.OpensPopup(e)` on the whole editor, once, in
`editor.New`. It is wider than intended: the editor's menus (the / menu, the
`[[` list, the math command menu, a card's tag and due-date pickers) are
popups too, and a press in the editor that closes one of them now also
reaches the editor, where Q-43 would have it go no further.

### Asked for

Either of these, whichever fits kvit-ui better:

- a way to remove the mark, such as `kvitui.OpensPopup(p, false)` or a
  second function, so the editor can be marked only while one of its in-place
  fields is open; or
- a field on `Popup`, such as `PressPassesTo unison.Paneler` or a plain
  `PassPress bool`, saying that a press outside this popup closes it and
  still reaches what is under it (or reaches only that panel). The in-place
  fields would set it and the menus would not, and the editor would need no
  mark at all. This is the closer fit, since it is the field that is special
  and not the editor.

## Checking

Kvit Notes has tests that will show both work once it moves to the new
commit and drops the workarounds: `TestFindAndReplace`, `TestFindInSelection`
and `TestCtrlEIsInlineCodeInTheEditor` in `app/features_test.go`, and
`TestTableHeaderDoubleClickSorts` in `editor/tableedit_test.go`. In kvit-ui a
test for each addition in the style of the existing `windowkeys` and `layer`
tests is enough.

## Reply wanted

The names and signatures chosen, and the commit they are in, so Kvit Notes
can replace `keepFindKeys` and the editor-wide `OpensPopup` call.
