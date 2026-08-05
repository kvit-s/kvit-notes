# QA run: Windows, pre-release-candidate

| Field | Value |
|---|---|
| Platform | Windows |
| Artifact | installed build from the Windows packaging path |
| Version / commit | untagged; the tree as of 2026-08-01, before `f2d570b` |
| Checksum | not published (no tag) |
| Machine | the development machine's Windows side |
| OS version | Windows 11 |
| Date | 2026-08-01 |
| Runner | owner |
| Result | pass with deviations, all fixed the same day |

Exploratory pass on the installed build rather than a walk of the
checklist. It is recorded because it found five defects that no automated
suite had reported, four of which are only reachable with a pointer on a
real desktop.

## Deviations

1. **A fitted diagram answered clicks on the left half of its canvas
   only.** The hit target did not extend across the read viewport, so
   clicking the whitespace beside a centred diagram did nothing instead of
   opening its source. Fixed in `91710f2`, which extends the hit target
   across the viewport.
2. **A plain click put the caret at the start of the block** rather than
   where the pointer landed, on every rendered block. Fixed in `f2d570b`.
3. **Ctrl+V did nothing in the gap between two blocks.** The caret sits
   there legitimately, and paste was the one thing it would not accept.
   Fixed in `6c6892d`.
4. **Relaunching started a second instance, and menus in the new window
   drew as disabled.** Windows is the first platform where close-to-tray
   actually engages, so each closed window left another process behind;
   three accumulated within minutes of ordinary use. Fixed in `a9ee9b1`.
5. **Default block spacing was too loose** at 8 px. Changed to 4 px in
   `f8af5d8`, still adjustable from 0–40 px in Settings.

## Not exercised

- The distribution section in full: install, upgrade in place, uninstall,
  the `.md` association, the offline pass, hostile paths and usernames, and
  first run against a corrupted settings file. The packaging path itself
  was validated separately on 2026-07-23.
- Signature verification, which cannot run until Windows signing exists.
- Screen readers (Narrator, NVDA).
- The standing features pass, beyond what the session happened to touch.

None of the fixes above have been re-checked on Windows against a later
build.
