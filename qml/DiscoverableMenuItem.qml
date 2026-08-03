// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick.Controls

// Fusion's MenuItem uses palette.text for both enabled and disabled entries.
// The window supplies its own themed text palette, so collection-only commands
// otherwise look active even though the menu correctly skips them. Keep them
// discoverable, but make their disabled state unmistakable in the File and
// View menus, wherever those two are shown.
MenuItem {
    opacity: enabled ? 1.0 : 0.42
}
