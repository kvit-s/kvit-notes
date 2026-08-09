# Accessibility

Kvit Notes is a block editor for Markdown notes built on Qt 6 and QML, shipping
on Windows, macOS and Linux. This document records what the application does
today for people who use a screen reader, work without a pointing device, need
larger text or stronger colour contrast, or are affected by animation.

`features.md` §14 states the intent in one screen; this document is where that
intent meets the actual code. It names the file each behaviour lives in, the
check that stops it regressing, and the review of 2026-08-04 that closed the
gaps between the two. Where a claim here was measured rather than reasoned
about, the measurement is described so it can be repeated.

## Status

All six phases of the work plan below were implemented on 2026-08-04. The
findings and the plan are kept as written, because each one records why a
change was made and what it was measured against; read them as the reasoning
behind the current code rather than as work waiting to be done.

What the tree now enforces, so that a regression fails a build rather than
being rediscovered by a person:

| Check | Where | What it refuses |
|---|---|---|
| `AccessibleNameGuard` | `tools/check-accessible-names.py` | a tap target with no accessible name, a glyph label with nothing spoken beside it, an animation duration that ignores `Theme.motionScale`, a popup nested in a dialog that would open into no window, and a menu entry that cannot show its own disabled state |
| Contrast floors | `testEveryTokenPairMeetsItsFloor` in `tests/test_theme.cpp` | any token pair below 4.5:1 for text or 3:1 for a control boundary, in any of the four themes |
| Runtime tree walk | `everyOperableNodeInTheAccessibilityTreeHasAName` in `tests/test_shell.cpp` | a button, checkbox, menu button, link or slider that the shipped shell serves with an empty name |
| Announcements | `test_zzl_screenReaderNamesRolesAndAnnouncements` in `tests/tst_integration.qml` | a conversion, search count or mode toggle that stops reaching the announcer |
| Default metrics | `testDefaultReproducesTheOldLiterals` in `tests/test_interfacemetrics.cpp` | a chrome role or geometry value that no longer matches the literal it replaced |

Four things are deliberately still open, and `docs/backlog.md` carries each
with its reason: nobody has yet heard the application through a screen reader,
macOS and Linux pick up a system preference change when the Settings dialog
next opens rather than when it happens, the macOS block-menu chord is
unconfirmed, and the Windows text-scale factor does not seed the interface
size. The per-platform tours that close the first are in
`docs/qa-checklist.md`.

## Contents

- [What accessibility means for this application](#what-accessibility-means-for-this-application)
- [Where the application stands today](#where-the-application-stands-today)
- [How the review was carried out](#how-the-review-was-carried-out)
- [Finding 1: names and roles on custom-drawn controls](#finding-1-names-and-roles-on-custom-drawn-controls)
- [Finding 2: picker popups have no keyboard route](#finding-2-picker-popups-have-no-keyboard-route)
- [Finding 3: colour contrast](#finding-3-colour-contrast)
- [Finding 4: interface size is not a setting](#finding-4-interface-size-is-not-a-setting)
- [Finding 5: reduced motion is partial](#finding-5-reduced-motion-is-partial)
- [Finding 6: dialogs do not place or restore focus](#finding-6-dialogs-do-not-place-or-restore-focus)
- [Finding 7: the operating system's own settings are not followed](#finding-7-the-operating-systems-own-settings-are-not-followed)
- [Work plan](#work-plan)
- [Tests and gates to add](#tests-and-gates-to-add)
- [Manual checks, per platform](#manual-checks-per-platform)
- [Documentation to update](#documentation-to-update)

## What accessibility means for this application

Four capabilities carry nearly all of the value, and each fails in a different
way when it is missing.

**A screen reader can describe the interface.** The assistive technology asks
the application, through the operating system, for a tree of elements, each with
a role (button, checkbox, list item), a name, and state. Narrator and NVDA on
Windows read that tree through UI Automation, VoiceOver on macOS through
NSAccessibility, and Orca on Linux through AT-SPI over D-Bus. Qt builds and
serves the tree from `QAccessible`; what the application has to supply is the
role, the name and the state of anything it draws itself.

**Everything can be done from the keyboard.** Not only as a preference: a person
using a screen reader has no usable pointer, and a control that responds to
nothing but a click is unreachable for them regardless of how well it is named.

**Text and boundaries are legible.** This covers both the size of text, which is
a setting, and the contrast between foreground and background, which is a
property of the theme. The reference standard here is WCAG 2.1 level AA: a ratio
of at least 4.5:1 for ordinary text, and at least 3:1 for the parts of a control
that show where it is and what state it is in.

**Motion can be switched off.** Positional animation in particular can cause
nausea or disorientation, which is why every desktop platform has a system-level
setting for it.

## Where the application stands today

The parts listed here were checked against the code and work as described.

Keyboard navigation is broad. `F6` cycles the major panes, and `main.qml:416-441`
deliberately keeps the toolbar in that cycle so that Insert, Templates and View
stay reachable, since those have no other chord. `Menu` and `Shift+F10` open the
context menu for the focused block. `src/platform/shortcutcatalog.cpp` records
every command's chord and, for the handful that have none, the reason — a habit
worth keeping as new commands arrive.

Live announcements exist. `AccessibilityAnnouncer`
(`src/platform/accessibilityannouncer.{h,cpp}`) records the last message, emits a
signal that tests can assert on, and posts a real `QAccessibleAnnouncementEvent`
so an attached screen reader speaks it. It is called for save state, search match
counts, focus and typewriter mode, block conversions, external file changes and
quick capture.

Roles and names are already set on the sidebar folder tree, the note list, image
blocks with their alternative text, diagram blocks, rendered text runs and the
editable block itself, whose name carries the block kind ("Heading 2 block").
`qml/Toolbar.qml:146-164` is the pattern the rest of the application should
follow: the toolbar's `BarButton` binds `Accessible.name` to its tooltip text and
mirrors its checked state into `Accessible.checked`.

The theme system has the pieces needed for low vision. A high-contrast theme
exists and holds a 7:1 floor for body text; `Theme` follows the operating
system's light/dark setting live through `QStyleHints::colorScheme`; and
`tests/test_theme.cpp` enforces a 3:1 floor on the keyboard focus ring in every
theme.

Menu access keys — the underlined letter that runs a command when typed, spelled
`&Copy` in a label — are handled by `src/platform/menuaccesskeys.{h,cpp}`, which
strips the markers on macOS where the convention does not exist, and by
`tools/check-menu-access-keys.py`, which fails the build when two commands in one
menu claim the same letter. A probe against the Qt 6.10.1 kit confirmed that Qt
removes the `&` before handing a label to assistive technology, so a menu item
written `"&Copy"` reports its name as `Copy`. Access keys and screen readers do
not interfere with each other.

## How the review was carried out

Three methods, in decreasing order of certainty.

**Measurement against the Qt kit.** A short program built against Qt 6.10.1
loaded a `Button` whose text was `&Copy`, a `Button` whose text was the glyph
`✕`, and a `MenuItem`, then queried each through `QAccessible` with the
accessibility layer switched on. The results — names `Copy`, `✕` and `Paste` —
establish both that mnemonics are stripped and that a glyph label is passed
through verbatim as the spoken name.

**Computation over the theme tokens.** A script parses the four token tables in
`src/platform/theme.cpp` and computes WCAG 2.1 contrast ratios for every pair
that appears together on screen. This is what Finding 3 rests on, and it becomes
a permanent test in the work plan below.

**Reading the source.** Counts such as "70 `MouseArea`s and 53 `TapHandler`s" or
"303 literal `font.pixelSize` values against 6 that read from `Typography`" come
from searching the QML tree and are accurate for the tree as it stood on
2026-08-04.

What could not be checked from the development machine is whether a screen reader
actually speaks the interface. The WSL session used for development has no
accessibility bus, which `tests/test_accessibility.cpp` already notes. Every
statement about Narrator, VoiceOver or Orca behaviour in this document is
therefore about what the application supplies to them, not about what was heard;
the manual checks at the end exist to close that distance.

## Finding 1: names and roles on custom-drawn controls

### What is wrong

Qt Quick Controls supply accessibility on their own: a `Button` reports its role
and its text as its name with no help from the application, and a `Dialog`
reports its `title`. What supplies nothing is an item drawn from a `Rectangle`
with a `MouseArea` or a `TapHandler` on it, and the tree holds 70 of the former
and 53 of the latter. A control drawn that way is absent from the tree entirely,
so a screen reader cannot announce it, find it, or activate it.

A second, quieter version of the same problem is a real control whose label is a
glyph. The probe result above shows Qt hands `✕` through unchanged, so a screen
reader says whatever its dictionary has for that character. Around 40 glyph-only
labels exist across the tree.

The specific places that matter:

| Where | File | What a screen reader gets now |
|---|---|---|
| To-do checkbox | `qml/TodoDelegate.qml:61-90` | Nothing: no checkbox, no checked state |
| Block gutter: add, delete, drag handle, menu | `qml/BlockGutter.qml:85-260` | Nothing |
| Find bar: previous, next, regular expression, close | `qml/FindBar.qml:305-372` | The glyphs `▲`, `▼`, `.*`, `✕` |
| Formatting bar | `qml/FormattingBar.qml:93-130` | The letters B, I, U; no checked state |
| Colour and style pickers | see Finding 2 | Nothing |
| Kanban card controls | `qml/KanbanBlock.qml` | Nothing |
| Table row and column handles | `qml/TableBlock.qml` | Nothing |

The to-do checkbox deserves separate mention because its visual design is sound —
a completed item is struck through rather than only tinted — while nothing about
its state reaches the accessibility layer at all. A keyboard route exists
(`Ctrl+Enter` inside a to-do block toggles it, `qml/EditableBlock.qml:2756`) but
it is absent from the shortcut catalog and therefore from the in-app reference.

### How to fix it

Introduce one component, `qml/IconButton.qml`, and use it everywhere a rectangle
is currently acting as a button. It should wrap the drawing, take focus, react to
`Space` and `Return`, and set the accessibility properties from one label:

```qml
// qml/IconButton.qml — a glyph-labelled button that a screen reader can see.
// The glyph is decoration; `label` is what the control is called, and it feeds
// both the tooltip and the accessible name so the two cannot drift apart.
AbstractButton {
    id: control
    property string glyph: ""
    property string label: ""

    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    Accessible.role: control.checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: control.label
    Accessible.checkable: control.checkable
    Accessible.checked: control.checked
    ToolTip.text: control.label
    ToolTip.visible: control.hovered || control.activeFocus
    // ... background, focus ring from Theme.focusRing, glyph rendering
}
```

Two details worth fixing in place rather than by substitution. The to-do block
should carry its state on the block itself, since that is the element a screen
reader lands on while navigating the note: on a `Block.Todo`, set
`Accessible.checkable: true` and `Accessible.checked: delegate.checked` alongside
the existing `Accessible.role: Accessible.EditableText`. And heading blocks
should keep the `EditableText` role rather than moving to `Accessible.Heading`:
the role tells the reader the text can be edited, which matters more inside an
editor, and the heading level already rides in the name. Qt Quick's `Accessible`
attached type has no heading-level property, so the name is the only place it can
go.

Tooltips should appear on keyboard focus as well as hover, as sketched above.
Today they are hover-only, which means a keyboard user never sees the explanation
that a mouse user gets for free.

Add `IconButton.qml` to `resources.qrc`; `QrcSyncGuard` fails the build
otherwise, and a QML file missing from the resource list hangs the Qt Quick
harness rather than failing it.

## Finding 2: picker popups have no keyboard route

### What is wrong

`qml/ColorPicker.qml` is the clearest case and the others follow its shape. The
popup sets `focus: false`, the swatches are `Rectangle` items with a
`TapHandler`, and both buttons set `focusPolicy: Qt.NoFocus`. Nothing inside it
can be reached without a pointer, and because the popup has neither a title nor an
`Accessible` role, opening it announces nothing.

The same pattern appears in `CalloutColorPicker.qml`, `CalloutTypePicker.qml`,
`DividerStylePicker.qml`, `DateRangePicker.qml`, `ImageEffectsPopover.qml` and
the tag popup in `TagStrip.qml`. `TableSizePicker.qml` and `DayPicker.qml` take
focus but have no accessible name.

The consequence is that text colour and callout colour cannot be set from the
keyboard at all — there is no menu item or chord that does the same job.

### How to fix it

Every popup that offers a choice needs four things:

1. `focus: true` and `closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside`.
2. `Accessible.role: Accessible.Dialog` with a name, so opening it is announced.
3. Focusable, named items instead of tap targets. For a grid of swatches, a
   `GridView` with `keyNavigationEnabled` and an `Accessible.name` per swatch
   ("Red", "Theme default", "No colour") reads better than a `Repeater` of
   rectangles and gives arrow-key movement for free.
4. Focus returned to whatever opened the popup when it closes. Record the opener
   on `opened` and call `forceActiveFocus()` on it from `onClosed`.

Colour names are worth doing properly rather than reporting hex values: the
palette comes from `Theme.colorPalette`, so a parallel list of names belongs
beside it in `src/platform/theme.cpp` and can be reused by the folder and tag
colour pickers.

## Finding 3: colour contrast

### What is wrong

The existing tests check a luminance gap for body text and a proper 3:1 ratio for
the focus ring. Computing full WCAG ratios across the token table finds failures
those floors do not reach, concentrated in the light and sepia themes. Columns
are light / dark / sepia / high-contrast, and `*` marks a value below the floor.

| Pair | Floor | light | dark | sepia | HC | Where it shows |
|---|---|---|---|---|---|---|
| `onAccent` on `accent` | 4.5 | 3.34* | 2.80* | 4.24* | 11.22 | any accent-filled button; the to-do tick |
| `warning` on `windowBackground` | 4.5 | 2.19* | 7.55 | 3.24* | 12.41 | warning text |
| `success` on `windowBackground` | 4.5 | 2.87* | 7.17 | 4.40* | 16.01 | success text |
| `textFaint` on `windowBackground` | 4.5 | 2.85* | 4.46* | 3.52* | 11.06 | 73 call sites: dates, counts, hints |
| `codeComment` on `codePanelBackground` | 4.5 | 2.38* | 4.04* | 2.74* | 12.84 | code comments |
| `codeString` on `codePanelBackground` | 4.5 | 2.96* | 7.52 | 4.08* | 16.14 | code strings |
| `codeType` on `codePanelBackground` | 4.5 | 3.74* | 6.42 | 4.47* | 13.33 | code types |
| `bannerText` on `bannerBackground` | 4.5 | 4.43* | 7.19 | 4.33* | 16.41 | banner text |
| `borderStrong` on `windowBackground` | 3.0 | 2.17* | 2.42* | 2.04* | 21.00 | the unchecked to-do box outline |
| `mutedGlyph` on `panelBackground` | 3.0 | 1.97* | 3.63 | 2.39* | 19.75 | muted icons |
| `quoteBar` on `windowBackground` | 3.0 | 1.82* | 2.42* | 1.78* | 21.00 | the bar beside a quote |
| `marker` on `windowBackground` | 3.0 | 1.98* | 3.32 | 2.20* | 19.75 | list bullets and numbers |
| `textPrimary` on `searchCurrentBackground` | 4.5 | 9.87 | 4.30* | 5.94 | 3.38* | the current search match |
| `textPrimary` on `selectionActiveTint` | 4.5 | 12.70 | 6.83 | 7.27 | 4.48* | actively selected text |

`textDisabled` also falls short, at 2.32–2.61:1, but WCAG exempts disabled
controls and the greyed-out appearance is itself the signal, so no change is
proposed.

That signal has to be drawn for it to exist. Fusion paints a menu label, and
the arrow on a submenu's row, with `palette.text` whatever the entry's state,
so an unavailable command came out in the same color as a live one and the
only way to tell was that it refused to highlight under the pointer. Every
menu entry in `qml/` is therefore a `DiscoverableMenuItem`, which binds its
label color to its own `enabled` state, and every menu holding a submenu names
that type as its `delegate`, since Qt builds a submenu's row itself. The
window palette is the wrong place for it: writing one color into a palette's
active group writes it into all three, so a disabled color set beside
`text: Theme.textPrimary` is overwritten there and again on every inherited
palette at the next theme change. `qml/DiscoverableMenuItem.qml` carries the
detail; `AccessibleNameGuard` refuses a bare `MenuItem`.

Two entries need more than a new value.

**`onAccent` on `accent` is unfixable as a stored pair**, because the label is
already pure white and cannot go lighter. It is also the one pair a user can
break on their own: `theme.accent` is overridable from the Settings dialog
(`accentOverride`), while `onAccent` is fixed, so choosing a pale accent produces
white text on a pale fill with no check anywhere.

**`border` at 1.2–1.5:1** is on the list of computed failures but is deliberately
left out of the table above. WCAG requires 3:1 only for the parts of a control
that identify it or show its state, and a rule drawn between two panels falls
outside that requirement. The token serves
both purposes today, which is the actual defect: `borderStrong` should be the
control-boundary token held to 3:1, `border` should be documented as decorative
and left alone, and the call sites that use `border` to outline an interactive
control should move to `borderStrong`.

### How to fix it

Derive the accent label instead of storing it. Replace the `onAccent` token with
a computed value: given the effective accent (theme token or user override),
return near-black or white, whichever contrasts more. For the default accents
this picks near-black in light, dark and high-contrast, and white in sepia, all
comfortably above 4.5:1. The Settings dialog's accent picker should also warn
when a chosen accent falls below 3:1 against the window background, since that
one is a fill and not a label.

For the rest, the values below meet the floor with the same hue and saturation as
the current token, so the appearance shifts as little as the requirement allows:

| Theme | Token | Now | Proposed | Ratio after |
|---|---|---|---|---|
| light | `warning` | `#f39c12` | `#a66908` | 4.52 |
| light | `success` | `#27ae60` | `#1e874b` | 4.54 |
| light | `textFaint` | `#999999` | `#767676` | 4.54 |
| light | `codeComment` | `#a0a1a7` | `#6f7178` | 4.50 |
| light | `codeString` | `#50a14f` | `#3f7e3e` | 4.55 |
| light | `codeType` | `#4078f2` | `#2967f0` | 4.52 |
| light | `bannerText` | `#8a6d1a` | `#886c1a` | 4.51 |
| light | `borderStrong` | `#b0b0b0` | `#949494` | 3.03 |
| light | `mutedGlyph` | `#a5b2bd` | `#7d909f` | 3.00 |
| light | `quoteBar` | `#c0c0c0` | `#949494` | 3.03 |
| light | `marker` | `#b8b8b8` | `#949494` | 3.03 |
| dark | `textFaint` | `#848484` | `#858585` | 4.52 |
| dark | `codeComment` | `#7f848e` | `#888c96` | 4.50 |
| dark | `borderStrong` | `#5a5a5a` | `#696969` | 3.04 |
| dark | `quoteBar` | `#5a5a5a` | `#696969` | 3.04 |
| dark | `textPrimary` | `#e8e8e8` | `#eeeeee` | 4.54 on the current match |
| sepia | `warning` | `#b07a1f` | `#91641a` | 4.54 |
| sepia | `success` | `#4e7a3a` | `#4d7839` | 4.51 |
| sepia | `textFaint` | `#8a7d68` | `#776b59` | 4.55 |
| sepia | `codeComment` | `#9a8a6a` | `#73674e` | 4.50 |
| sepia | `codeString` | `#4e7a3a` | `#497236` | 4.55 |
| sepia | `codeType` | `#2f6ab0` | `#2f69af` | 4.53 |
| sepia | `bannerText` | `#7a6318` | `#776017` | 4.52 |
| sepia | `borderStrong` | `#b8a888` | `#9c875d` | 3.04 |
| sepia | `mutedGlyph` | `#a3937a` | `#938166` | 3.01 |
| sepia | `quoteBar` | `#c4b492` | `#9f8756` | 3.02 |
| sepia | `marker` | `#b0a284` | `#9a8863` | 3.02 |
| high contrast | `searchCurrentBackground` | `#cc7700` | `#995a00` | 5.50 |
| high contrast | `selectionActiveTint` | `#0077dd` | `#0070d0` | 4.97 |

Several of these move by one or two steps only, because the current value sits
just under the line; those are worth taking anyway, since a value that passes by
0.02 today fails the moment anything else shifts.

Recapture the committed screenshots after this phase. The only ones in the
tree are the four press stills under `screenshots/press/`, regenerated by
`tools/capture-press-stills.sh`, which already stages the demo vault at the
fixed neutral path `/tmp/kvit-notes-demo` for the reason `CLAUDE.md` gives:
the status bar renders the note's absolute path into the pixels, so a capture
from a home or scratch directory bakes that machine's paths into a committed
image. The visual storyboards write to a directory outside the repository and
need nothing done to them.

## Finding 4: interface size is not a setting

### What is wrong

`Typography.baseSize` scales the document, and the Settings dialog labels it
"Editor font". The interface around the document does not follow it: the chrome
carries 303 literal `font.pixelSize` values against 6 that read from
`Typography`, heaviest in `NoteListPane.qml` (26), `Sidebar.qml` (23),
`KanbanBlock.qml` (20), `FindBar.qml` (19) and `SettingsDialog.qml` (15), and
many of those literals are 10 to 12 pixels. Someone who enlarges the editor font
because they need to still reads a 10-pixel status bar and a 12-pixel note list.

Operating-system display scaling covers part of this, since Qt honours it on all
three platforms, but it enlarges everything at once. There is no way to ask for
larger chrome specifically, and no way to ask for larger chrome with unchanged
document text.

### How to fix it

Interface size should be its own setting rather than a second effect of the
editor font, for two reasons. The needs differ — a person who wants a small,
dense note list with large body text is asking for something coherent, and so is
the reverse — and `Typography` deliberately freezes its ratios so that document
rendering stays pixel-identical at the default, a promise that gets harder to
keep if chrome metrics ride on the same base.

Add `src/platform/interfacemetrics.{h,cpp}`, exposed to QML as the `Interface`
singleton through the `X(InterfaceMetrics, Interface)` entry in
`src/qml/qmlsingletons.h`, and list the new source at `CMakeLists.txt:383`
beside `theme.cpp` and `typography.cpp`. It follows the shape `Typography`
already uses: state in the settings store under an `interface.` key prefix,
clamped setters, one change signal, and role sizes derived from one base by
ratios frozen from today's values so the default renders unchanged.

```cpp
// The chrome's type scale and metrics, separate from the document's
// (Typography). One setting, `interface.fontSize`, with the role sizes and the
// geometry scale derived from it. The ratios are frozen from the values the
// chrome used before this existed — at the default base of 12 every role
// reproduces its old literal exactly, so the default build is pixel-identical.
class InterfaceMetrics : public QObject
{
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(qreal scale READ scale NOTIFY changed)     // fontSize / 12.0
    Q_PROPERTY(int caption READ caption NOTIFY changed)   // 10 at the default
    Q_PROPERTY(int small READ small NOTIFY changed)       // 11
    Q_PROPERTY(int body READ body NOTIFY changed)         // 12
    Q_PROPERTY(int strong READ strong NOTIFY changed)     // 13
    Q_PROPERTY(int title READ title NOTIFY changed)       // 15
public:
    static constexpr int DefaultFontSize = 12, MinFontSize = 10, MaxFontSize = 24;
    // Geometry: a design pixel value scaled and rounded, for control heights,
    // paddings and icon boxes. `implicitHeight: 28` becomes
    // `implicitHeight: Interface.px(28)`.
    Q_INVOKABLE int px(int designPx) const;
};
```

Font size alone is not enough. A 24-pixel label inside a 28-pixel button clips,
so the migration has to carry the geometry with it: control heights, paddings,
icon boxes and row heights go through `Interface.px()`, and fixed row heights
such as `height: 28` in the sidebar's tree delegate become content-derived with a
scaled floor, the way `NoteListPane.qml:619` already computes its row height from
its content.

Because the work is mechanical and large, do it pane by pane in this order, each
one shippable on its own: the status bar and find bar, then the note list and
sidebar, then the toolbar and dialogs, then the block chrome (code block headers,
callout headers, table and kanban furniture). Block content itself is already
driven by `Typography` and must stay that way.

In the Settings dialog, the control belongs on the Appearance tab rather than
Typography, since Typography is about the document. Label it "Interface size"
with the editor font setting left where it is, and state the relationship in one
line so neither is mistaken for the other.

Two optional extras once the mechanism exists: Windows exposes a text-scale
factor separate from display scaling that Qt does not apply, and it could seed
the default; and a "Reset interface size" action beside the existing "Reset
Typography" keeps the two symmetrical.

## Finding 5: reduced motion is partial

`Theme.motionScale` is 0 when reduced motion is on and 1 when off, so a duration
written `150 * Theme.motionScale` becomes instant. Eleven files honour it and 15
animation durations bypass it.

The one that matters is the block list's transitions in `qml/main.qml:2047-2071`:
a 200 ms positional move on every block insert, delete or reorder. Positional
motion is the category reduced-motion settings exist for, so this is the first
one to scale. The remaining bypasses are opacity fades in `ImageBlock.qml`,
`EmbedBlock.qml`, `CalloutBlockChrome.qml`, `DiagramBlock.qml`, `TableBlock.qml`,
`DividerDelegate.qml`, `DropCapOverlay.qml`, `EditableBlock.qml` and
`BlockGutter.qml`; fades are far less likely to cause trouble, but leaving them
unscaled contradicts what the setting promises.

Prevent the next one with a check in the guard script described below: a
`duration:` in `qml/` that is a bare number, rather than an expression involving
`motionScale`, fails unless the file is on a named exemption list.

## Finding 6: dialogs do not place or restore focus

Only `LinkDialog.qml` and `QuickSwitcher.qml` decide where focus lands when they
open. Elsewhere a screen reader announces the dialog's title and then falls
silent, and a keyboard user has to guess how many `Tab` presses reach the first
control. Nothing restores focus to the control that opened the dialog when it
closes.

`usage.md:463` currently states that dialogs "trap and return focus". The trap
comes from modality and is real; the return is not implemented, so either the
behaviour or the sentence has to change, and the behaviour is the better one to
change.

The fix is a small pattern applied to each dialog: name the control that should
receive focus, call `forceActiveFocus()` on it from `onOpened`, record
`Window.activeFocusItem` before opening, and restore it from `onClosed`. A shared
`FocusScopeDialog.qml` base is worth it at this count — 32 `Dialog` instances
across the tree — but the pattern is small enough that copying it into each file
is also defensible.

## Finding 7: the operating system's own settings are not followed

`Theme` follows the operating system's light/dark preference through
`QStyleHints::colorScheme`, and that is the only system preference the
application reads. Two more matter and are exposed differently on each platform:

| Preference | Windows | macOS | Linux (GNOME) |
|---|---|---|---|
| High contrast | `SystemParametersInfo(SPI_GETHIGHCONTRAST)`, changes signalled by `WM_SETTINGCHANGE` | `NSWorkspace.accessibilityDisplayShouldIncreaseContrast` plus its notification | `org.gnome.desktop.a11y.interface high-contrast`, or the GTK theme name |
| Reduced motion | `SystemParametersInfo(SPI_GETCLIENTAREAANIMATION)` | `NSWorkspace.accessibilityDisplayShouldReduceMotion` | `org.gnome.desktop.interface enable-animations` |

Windows high contrast is the most valuable of the four, because a person who
turns it on system-wide expects every application to follow, and Kvit already has
a high-contrast theme that passes a 7:1 floor. Following the setting is mostly a
matter of resolving to those existing tokens.

Add `src/platform/systemappearance.{h,cpp}` with one class, a small
platform-specific implementation behind `#ifdef`, and two notifying properties:
`highContrast` and `reducedMotion`, each of which is false where the platform
gives no answer. `Theme` then gains a third value for its theme setting —
`system` already means "follow light or dark", so high contrast folds in as: when
the system reports high contrast and the user has not chosen a theme explicitly,
resolve to `highContrast`. Reduced motion becomes a three-way setting (on, off,
follow the system) with "follow the system" as the default for new installations
and the current explicit value preserved for existing ones.

The new files belong in `src/platform/` under the layering rules, which is also
the only place platform-specific code is allowed; `python3 tools/check-layering.py`
enforces that.

## Work plan

Six phases, ordered so that each one is worth shipping on its own and so that the
work a screen reader depends on lands before the work that refines it.

**Phase 1: names, roles and state.** Add `qml/IconButton.qml` and register it in
`resources.qrc`. Convert the block gutter, the find bar buttons, the formatting
bar, the kanban card controls and the table handles. Put `Accessible.checkable`
and `Accessible.checked` on to-do blocks. Make tooltips appear on focus as well
as hover. Add the `Ctrl+Enter` to-do toggle to `shortcutcatalog.cpp`. This is the
phase everything else depends on.

**Phase 2: keyboard routes into the pickers.** Rework `ColorPicker.qml` first as
the model, then `CalloutColorPicker`, `CalloutTypePicker`, `DividerStylePicker`,
`DateRangePicker`, `ImageEffectsPopover` and the tag popup. Name the palette
colours in `theme.cpp` so the swatches can be announced. Give
`TableSizePicker` and `DayPicker` accessible names.

**Phase 3: contrast.** Apply the token changes from the table above, replace the
stored `onAccent` with a derived value, split the decorative `border` token from
the control-boundary `borderStrong` token and move the call sites that need 3:1,
warn on a low-contrast custom accent, and recapture the reference screenshots.

**Phase 4: dialog focus and reduced motion.** Both are small and mechanical:
initial and restored focus across 32 dialogs, and `motionScale` on the 15
animations that bypass it, starting with the block list transitions.

**Phase 5: follow the system.** Add `SystemAppearance` with the three platform
implementations, fold high contrast into the theme resolution, and make reduced
motion a three-way setting.

**Phase 6: interface size.** Add `InterfaceMetrics`, then migrate the chrome
pane by pane in the order given in Finding 4, adding the settings control once
the first pane is converted so the effect is visible while the rest lands.

Phases 1 through 4 are the ones that change what a person with a disability can
do with the application; 5 and 6 are the ones that make it comfortable.

## Tests and gates to add

The current automated coverage is `tests/test_accessibility.cpp`, which checks
the announcer's wording and nothing else, plus the contrast and focus-ring floors
in `tests/test_theme.cpp`. Nothing fails when a new control ships without a name,
which is exactly the mistake this document is about.

**Full contrast floors, in `tests/test_theme.cpp`.** Turn the computation
described above into a data-driven case covering every pair in the Finding 3
table, at 4.5:1 for text and 3:1 for control boundaries, with the high-contrast
theme held to its existing stricter floors. The script that produced the table is
ready to be turned into this test.

**`tools/check-accessible-names.py`, wired as an `AccessibleNameGuard` CTest
entry** beside `MenuAccessKeyGuard` at `tests/CMakeLists.txt:710`. It should fail
on three things, each with a named exemption list so that an exception is a
decision somebody made rather than a check nobody notices is off:

- a `MouseArea` or `TapHandler` with an `onClicked` or `onTapped` handler whose
  enclosing item declares no `Accessible.name`;
- a control whose `text` is a single non-alphanumeric character and which
  declares no `Accessible.name`;
- a `duration:` given as a bare number rather than an expression involving
  `motionScale`.

**A runtime tree walk**, as a new case in `tests/test_accessibility.cpp` or a
Qt Quick case: build the window, call `QAccessible::setActive(true)`, walk the
tree from the root, and assert that no node which is focusable or has an
activation action reports an empty name. This catches what the source-level guard
cannot, namely a name bound to an expression that evaluates to nothing.

**An announcement case per category.** The announcer is already unit-tested; what
is missing is that the call sites fire. A Qt Quick case that toggles focus mode,
runs a search and converts a block, asserting on `A11y.lastMessage` each time,
would notice a call site that gets dropped in a refactor.

Read `CLAUDE.md` on reading a Qt Quick suite result before judging any of these:
`IntegrationTestsIsolated` carries the verdict, and the failure count on the
developer's own display carries no information at all.

## Manual checks, per platform

No automated suite in this repository can hear a screen reader, so these belong
in `docs/qa-checklist.md` and have to be run by a person before a release.

**Windows, with Narrator or NVDA.** Tab through the toolbar, the note list and a
note, confirming every stop is announced with a name that says what it does
rather than a glyph name. Toggle a to-do from the keyboard and confirm the state
change is spoken. Open Settings and confirm the initial focus is announced. Then
turn on High Contrast in Windows settings and confirm the application follows
(after Phase 5). Also confirm the access-key underlines still look right, which
the checklist already covers.

**macOS, with VoiceOver.** The same tour. Two macOS-specific points: the context
menu is bound to `Menu` and `Shift+F10` (`qml/AppShortcuts.qml:49-54`), and Mac
keyboards have no `Menu` key, so confirm whether `Shift+F10` reaches it and add a
chord that exists on Apple hardware if not. The native menu bar carries File and
View only, so confirm that Insert and the block commands are reachable through
the toolbar via `F6` and that VoiceOver announces them there.

**Linux, with Orca.** This is the check that has never been run, because the
development machine's WSL session has no accessibility bus. The packaging side
was verified: in Qt 6.10 the AT-SPI bridge is compiled into `libQt6Gui` rather
than shipped as a separate plugin, the AppImage manifest bundles
`libQt6DBus.so.6`, which is what the bridge needs, and the Flatpak runs on the
KDE runtime with the accessibility bus proxied by default. What that establishes
is that the bridge can load, not that the tree it serves is any good. Run the
tour from the AppImage rather than a development build, since the AppImage is
what most Linux users will get.

## Documentation to update

- `features.md` §14 currently promises "ARIA labels on interactive elements" and
  "semantic HTML structure", which are web vocabulary in a Qt application. Replace
  them with roles and names on interactive elements, and add the interface size
  setting to §14.3 beside the existing font-size line.
- `usage.md:461-467` claims dialogs return focus, which becomes true in Phase 4;
  the sentence can stay if the phase lands, and has to be corrected otherwise. Add
  the interface size setting once Phase 6 ships.
- `docs/qa-checklist.md` gains the three screen-reader tours above.
- `CLAUDE.md` and `devel.md` should list this file alongside `block-arch.md` and
  `selection.md` in their descriptions of where the documentation lives.
- `docs/backlog.md` should carry whichever phases are not scheduled, so that a
  known gap stays visible rather than being rediscovered.
