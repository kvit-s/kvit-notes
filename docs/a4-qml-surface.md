# A4: narrowing the QML surface

Written to be read cold: what the work was, what it produced, and what rests
on execution versus inspection. It is the record of architecture finding A4's
QML half, done across 30 commits and merged as one.

## What the finding asked

`AppContext` published 44 string-named context properties. QML resolves such a
name at runtime, so a misspelled or removed one yielded `undefined` and a subtly
wrong render, invisible to any static tool. The finding asked that such a
reference become a static error qmllint catches. "Done" is the two categories
that catch it — `unqualified` (a name resolving to nothing) and
`missing-property` (a name resolving to the wrong thing) — enabled and failing
the gate across the whole `qml/` tree.

They are, over all 51 files. The baseline with them enabled was 2,587 + 670
findings; now zero, with three line-scoped suppressions, each a gap in Qt's own
type description (`Qt.application.screens`, `Qt.application.font`,
`Text.positionAt`), none for first-party code.

## What replaced the 44 context properties

**Typed module singletons.** Every service is registered in a generated `Kvit`
QML module and reached as `Theme.accent`, `NoteCollection.isOpen`, and so on.
qmltyperegistrar writes the module's type description from `QML_ELEMENT` macros,
so the description carries the real property surface and cannot drift from the
C++ — it replaced a hand-written stub that listed type names and no members.
Registration is per-QML-engine through a `create()` factory reading a service
table the composing `AppContext` hangs on the engine, which preserves the
deliberately multi-instance composition that tests rely on.

One Qt trap is worth recording because it fails silently: `QQmlPrivate::`
`singletonConstructionMode()` tests `std::is_default_constructible<T>` before it
looks for a `create()` factory. Every service takes `QObject *parent = nullptr`,
so putting `QML_SINGLETON` on the class itself gets the factory ignored and Qt
default-constructs an instance wired to nothing — no warning, no null, a valid
object bound to a service that does not exist. The registrations live in
`src/qml/qmlsingletons.h` as `QML_FOREIGN` wrappers, whose branch Qt tests first.

**Three declared QML types at the window/delegate boundary.** `BlockDelegateBase`
is the interface every block delegate already implemented by convention;
`main.qml` casts `itemAtIndex(i) as BlockDelegateBase` to call into a row.
`KvitShell` is the window as a delegate sees it — the drag state, the
last-focused block, the editor modes — reached through `Window.window as
KvitShell`. `BlockDragState` is the drag interface, declared as a type but
implemented in `main.qml`, because its body drives that window's own view
objects and could not move without dragging four of them with it.

**`AppActions`.** The 20 commands a delegate sends up to the window (open a menu,
insert an image, scroll to a block) go through one typed singleton of named
signals; `main.qml` connects each to the function that already implemented it.
An unconnected signal is a no-op, which is exactly what the old
`if (win && win.x)` probe achieved, so behaviour is preserved by construction.

## The three defects the static gate caught, none of which the runtime gate could

These are the argument for the scope, and they are defects rather than lint
noise.

**1. `DocumentStats.DocumentStats()` — the statistics panel, dead.**
`DocumentStats` has a method also named `documentStats`; a rename hit both the
object and the method, producing a call to a member that does not exist. The
panel threw on every recompute and showed nothing. It sat green through four
commits, because `ShellTests` loads the shell but never opens that panel, so
`recompute()` never ran.

**2. `editorEngine.stripFormatting` — Ctrl+Shift+V pasted nothing.**
`editorEngine` is an id inside `EditableBlock.qml`; `main.qml` referenced it
across the file boundary, where ids do not reach. Paste-as-plain-text over a
block selection threw `ReferenceError` every time. Never executed at load, so
never seen.

**3. A 66-site `ReferenceError`, introduced during this work.** The shell-handle
rollout hardcoded `root` as the delegate root id; four delegates root at
`delegate`, so 66 reads referenced an id absent in them. Every one would have
thrown on first use, on drag, device-pixel-ratio and menu paths that do not run
while the shell loads.

All three are `missing-property` or `unqualified` findings — the categories that
were disabled. A warning gate observes only code that executes; a linter reads
code whether it runs or not. That difference is the whole case, demonstrated on
real defects, twice on ones that would have shipped.

## The cast, verified by experiment rather than cited

The 262 of 282 `Window.window` member accesses that were state reads, not
commands, could not be signalled away. The fix — `Window.window as KvitShell` —
had no prior art anyone could find, so it is not presented as known practice.
Three probes established it on this Qt (6.10.1): the property-handle form lints
clean; an uncast read still reports `missing-property`, so the check is not
silenced; and the handle resolves at runtime from inside a nested `ListView`
delegate two levels deep, returning the value. It rests on those three results
rather than on established practice. A null cast yields null, so where a null
guard existed it stayed: the cast makes the read typed without making the window
guaranteed to be there.

## The menu boundary, and the union that was avoided

The delegates query menu state (is this block's menu open) and then drive the
returned menu. The tempting move is one `CommandMenu` base type the three menus
implement — and it would have been wrong, because they do not share an
interface: `BlockMenu` has `targetIndex` and no `targets()`, the other two the
reverse. A base type declaring both would type `BlockMenu` as something with
`targets()`, and a call to it would lint clean and be `undefined` at runtime —
the exact `missing-property` failure this work enables the gate to catch.

No such type was built. `KvitShell` declares `activeBlockMenu(index)`,
`activeMathMenu(host)`, `activeWikiMenu(host)`, `contextMenuHoldsSelection` and
`openLink`; `main.qml` overrides each and reads the divergent members on the
concrete menu that has them. The delegate receives the menu as an untyped `var`
and drives it, holding an opaque handle rather than a typed menu that could lie.
Queries return through `KvitShell`, commands go out through `AppActions`, and the
delegate never names a menu. Mutating the block-menu override to call
`targets()` fails the gate (exit 255), because `blockMenu` is typed as the
concrete `BlockMenu`. There is no shared type to over-promise.

## Two hand-rolled workarounds that vanished

`EditableBlock` and two siblings carried `appTheme` aliases because a bare
`theme` inside the engine object resolved to the engine's own property; a
singleton cannot be shadowed by an object property, so the aliases went.
`BacklinksPanel` carried an `entryData` alias because a nested Repeater's
`modelData` shadowed the outer one; binding the scopes removed the shadowing and
the alias. Both fell out of the work rather than being sought — complexity
leaving the tree rather than moving within it, which a refactor of this size
usually cannot claim.

## What rests on execution versus inspection

Most of the conversion is verified by execution: `ShellTests` fails on any QML
warning at load and passed throughout, and 67 unit tests plus `QrcSyncGuard` are
green from a clean build. Two areas rest on inspection, and should be read as
such:

- **The drag state — 132 of the reads.** `blockDrag` drives block
  drag-and-drop, whose tests live in the Qt Quick integration suite, which does
  not run in this environment (it fails heavily under WSLg regardless of the
  code). qmllint proves the members resolve; `ShellTests` proves the shell
  loads; neither proves a drag still works. Each site is `win.` becoming
  `shell.` with no restructuring, which is what keeps it reviewable by eye.
- **`TagStrip.chipFor()`** now finds its chip by model index rather than reading
  an untyped property off `itemAt()`. Provably the same answer, three lines, but
  its only callers are in `tests/tst_integration.qml`, which this environment
  cannot run.

## One habit, stated plainly

Eight times during this work a rewrite matched more of the code than its author
had read. The pattern is always the same: a search-and-replace or a regular
expression written against what the code was assumed to look like, applied to
files that did not all look that way.

| Instance | What the rewrite hit that it should not have | Caught by |
|---|---|---|
| `documentStats` → `DocumentStats` | the invokable of the same name, not only the object | strict lint (later) |
| colour-menu id | cross-wired onto the span-type menu above it | strict lint |
| `root.selectRange` | a method that lives on `selectionKeyHandler`, not the root | strict lint |
| `win` → `root.shell` | the word "window" inside 33 comments | build (unused-id) / reading the diff |
| bare-method prefixing | the function *definitions*, producing `function delegate.x()` | build (syntax) |
| 66-site id | `root` in four files whose root id is `delegate` | strict lint |
| `TocBlock` id | `tocRow`, an id that does not exist in that file | build |
| `root.defaultFontFamily` | a property invented before it was a function | build |

Every instance was caught, three by tests, one by the build failing to compile,
four by the linter, and none shipped. The value is not any single catch; it is
that the rate is high, and the deepest-scoped files, where a delegate nests
components four levels down and `modelData` means a different object at each, are
where it bit hardest. Writing a rewrite is not the same as
reading the code it rewrites, and the static gate this work enables is now one
of the things that reads it.

## A note for whoever counts this area next

Four separate times a count here moved on closer reading: 13 files became 14 when
one was actually read, "add a pragma" became per-delegate role declarations
across 33 files, "113 calls to 13 functions" became 262 state reads, and 282
member accesses became 265 when a regex stopped double-counting call
parentheses. The counts were unreliable until the shapes were understood, which
is why every phase here was measured before it was scoped rather than estimated
from the finding.
