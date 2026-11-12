# Result of kvit-ui-ctrl-e-for-a-focused-control.md

Project kvit-ui, track kvit-ui-ctrl-e-for-a-focused-control. On its main branch as commit 5faae058d552bfa7ab1cfdc78a2cdb5734f8c490, 2026-11-12T06:48:59.687Z.

## Done: focused control can keep Ctrl+E

Added `KeepsFindKey(p unison.Paneler, keys ...Shortcut)` in `windowkeys.go`.

Kvit Notes calls it once on the editor and can remove the wrapper in `keepFindKeys` in `app/window.go`:

```go
kvitui.KeepsFindKey(editor, kvitui.Keys(unison.KeyE, kvitui.ModPrimary))
```

While the keyboard is in the marked panel or anything inside it, `windowKeys` returns `false` for a matching Find key instead of focusing the window's `SearchField`, so unison offers the key to the control's own handler. From any other control Ctrl+E still focuses the search field. Only the search-field fallback gives way; popup, menu-bar, access-key and command handling run first, and a key a registered command has stays the command's. Only named keys are kept. Calling again adds to the keys already kept.

Covered by `TestAFocusedControlKeepsAFindKey` in `keyboard_test.go`, beside `TestAFindBarTakesOnlyFindsFirstKey`: Ctrl+E in the marked control reaches it with focus staying there; Ctrl+F from the same control still focuses the search field; Ctrl+E from another control focuses the search field. Documented in KEY-13 in `docs/ui-guidelines/rules.md` and item 19 in `agent/skills/kvit-ui/SKILL.md`.

## Differently from the brief and why

Chose the panel-mark shape, in the manner of `OpensPopup`, checked on the focused control and its ancestors, rather than offering the key to the focused control first from inside the window handler, which would have offered the key twice.

## Not done

Kvit Notes' `TestCtrlEIsInlineCodeInTheEditor` was not run against this change.

## To use

- Mark the editor as above. `ModPrimary` is Ctrl on the Windows form; on the macOS form the same shortcut is Cmd-E, which is not a Find key, so the mark does nothing there.
- `./build.sh --test` passes; the new test fails with the check disabled.
