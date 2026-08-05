// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// A container with nothing to show. It reports no implicit height, and the
// point of the test that uses it is that the row it follows is then exactly as
// tall as it would have been with no container registered at all.
Item {
    objectName: "demoEmptyDecoration"
    implicitHeight: 0
}
