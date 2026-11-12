# Result of kvit-ui-find-and-popup-hooks.md

Project kvit-ui, track kvit-ui-find-and-popup-hooks. On its main branch as commit c6f6e90d411e0cc0597de2215bb64bd9a912cd62, 2026-11-12T06:41:32.002Z.

## Find bar (`Command.FindBar`) and closing press (`Popup.PressPasses`)

### What was done
- **`Command.FindBar bool`** in `command.go`, for use as `&kvitui.Command{Std: kvitui.StdFind, FindBar: true, Run: openFindBar}` with `w.AddCommands(find)`. `Shortcuts(form)` returns only the first Find key: Ctrl+F on Windows, Cmd-F on macOS. Its menu line shows that key. Ctrl+E on Windows is no longer the command's key and still puts the keyboard in the window's `SearchField` with text selected.
- **`Popup.PressPasses bool`** in `layer.go`, for use as `w.Show(&kvitui.Popup{Panel: field, PressPasses: true, OnPressOutside: writeBack})`. A press outside runs `OnPressOutside` and still reaches whatever is under the pointer. If the same press also closes a popup without the flag, e.g. a menu above the field, the press goes no further per Q-43.
- Docs updated: `docs/ui-guidelines/rules.md` KEY-13/PTR-13 `kvit-ui now` lines, `agent/skills/kvit-ui/SKILL.md` and regenerated `catalog.md`.
- Tests: `TestAFindBarTakesOnlyFindsFirstKey` in `keyboard_test.go` on both forms; `TestAPopupCanLetItsClosingPressThrough` in `rest_test.go`. Full `./build.sh --test` passes after catalogue regeneration.

### Done differently and why
- Chose the brief's second shape for each. No `Window.OnFind`; the `FindBar` command keeps the menu key. No `OpensPopup(p, false)` or removal; `OpensPopup` is unchanged. The field is special, not the editor, so `PressPasses` on the in-place field replaces the editor-wide mark.

### Not done
- Ctrl+F while a keyboard-taking popup is open still does not run the command; your find-bar popup must answer it itself to reselect text. Ctrl+E in the editor still goes to the search field per KEY-13. Kvit Notes tests were not run here.

### To use it
Remove the editor-wide `OpensPopup(e)`; set `PressPasses` only on in-place fields. Drop the Ctrl+F half of `keepFindKeys`; keep a workaround only if Ctrl+E stays inline code.
