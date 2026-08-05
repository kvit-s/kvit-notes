// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// A stand-in for the QML a linked module draws between two blocks: a box of a
// fixed height, so a test can measure where it landed and what it did to the
// row above it. The two properties are the contract BlockDelegateBase fills
// in — a module's item may declare either, both or neither.
Rectangle {
    id: box
    objectName: "demoDecorationBox"

    property QtObject decorationContext: null
    property int decorationBlock: -1

    implicitHeight: 40
    height: implicitHeight
    color: "#3355ff"
}
