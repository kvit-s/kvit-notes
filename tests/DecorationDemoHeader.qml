// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// What a module puts in the pinned strip above the document (the
// `documentHeader` slot). Its implicit height is what the shell reserves, so
// the document scrolls under a strip of exactly this size.
Rectangle {
    objectName: "demoDocumentHeader"
    implicitHeight: 24
    color: "#eeddaa"
}
