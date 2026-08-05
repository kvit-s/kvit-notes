// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick.Controls
import Kvit 1.0

// The menu entry every menu in the application is built from.
//
// A command that is present but cannot run right now — "Align" on a block
// that has no alignment, "Remove line breaks" where there is nothing to join,
// "Save" on an unchanged document — has to look unavailable before the
// pointer reaches it. Fusion draws a menu label, and a submenu row's arrow,
// with palette.text whatever the entry's state, and the window supplies its
// own themed text color, so a disabled entry came out the same color as a
// live one: the only sign it was disabled was that it refused to highlight
// under the pointer.
//
// The color is set on the entry rather than on the window's palette, which
// looks like the obvious place for it and does not hold. A palette carries
// active, inactive and disabled color groups, and writing one color into the
// active group writes it into all three (QPalette::setColor with no group
// does exactly that), so the window's `text: Theme.textPrimary` overwrites
// any disabled color set beside it, and the same happens again on every
// inherited palette the moment the theme changes. Measured on Qt 6.10.1:
// the disabled group survives startup and is lost on the first theme switch.
// Binding one color per entry, keyed to that entry's own enabled state, has
// no such ordering to get wrong.
//
// Every Menu in qml/ builds its entries from this type, including its
// `delegate`, which is what a submenu's own row in its parent menu is made
// from. tools/check-accessible-names.py (the AccessibleNameGuard test) is
// what catches a bare MenuItem that slips back in.
MenuItem {
    id: control

    palette.text: control.enabled ? Theme.textPrimary : Theme.textDisabled
}
