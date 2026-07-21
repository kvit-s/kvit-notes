// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The Ctrl+K link dialog (features.md §2.4): display text and URL, prefilled
// when the caret is already inside a link, with "Remove link" replacing the
// span with its bare text. A document with headings also offers them as
// targets, which fills the URL with that heading's slug.
//
// Every edit goes through the model as one content replacement, like every
// other formatting command, so a link costs one undo step. The caret returns
// to the block by index afterwards.
Dialog {
    id: linkDialog
    objectName: "linkDialog"

    // Wired by main.qml.
    property var listView

    modal: true
    title: editing ? qsTr("Edit Link") : qsTr("Insert Link")
    anchors.centerIn: parent
    width: 380

    property alias textField: linkTextField
    property alias urlField: linkUrlField
    property int blockIndex: -1
    property int mdStart: -1
    property int mdEnd: -1
    property bool editing: false
    property bool removable: false
    // The document's headings for the "link to heading" mode
    // (features.md §2.4's deferred jump-to-heading). Refreshed on open.
    property var headingTargets: []

    function openForInsert(index, start, end, initialText) {
        editing = false
        removable = false
        blockIndex = index
        mdStart = start
        mdEnd = end
        headingTargets = DocumentOutline.headings()
        linkTextField.text = initialText
        linkUrlField.text = ""
        open()
        linkUrlField.forceActiveFocus()
    }

    function openForEdit(index, start, end, text, url, canRemove) {
        editing = true
        removable = canRemove
        blockIndex = index
        mdStart = start
        mdEnd = end
        headingTargets = DocumentOutline.headings()
        linkTextField.text = text
        linkUrlField.text = url
        open()
        linkUrlField.forceActiveFocus()
    }

    function spliceAndFocus(replacement, cursorMd) {
        var md = BlockModel.getContent(blockIndex)
        BlockModel.updateContent(blockIndex,
            md.substring(0, mdStart) + replacement + md.substring(mdEnd))
        var idx = blockIndex
        Qt.callLater(function() {
            linkDialog.listView.currentIndex = idx
            var item = (linkDialog.listView.itemAtIndex(idx) as BlockDelegateBase)
            if (item)
                item.focusAtPosition(cursorMd)
        })
    }

    function removeLink() {
        var text = linkTextField.text
        spliceAndFocus(text, mdStart + text.length)
        close()
    }

    onAccepted: {
        // Brackets in the text or spaces/parens in the URL would
        // produce markdown that no longer parses as one link.
        var text = linkTextField.text.replace(/[\[\]]/g, "")
        var url = linkUrlField.text.replace(/ /g, "%20").replace(/[()]/g, "")
        if (text.length === 0)
            text = url
        if (text.length === 0)
            return
        var link = "[" + text + "](" + url + ")"
        spliceAndFocus(link, mdStart + link.length)
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label { text: qsTr("Text") }
        TextField {
            id: linkTextField
            objectName: "linkDialogTextField"
            Layout.fillWidth: true
        }
        Label { text: qsTr("URL") }
        TextField {
            id: linkUrlField
            objectName: "linkDialogUrlField"
            placeholderText: "https:// or #heading"
            Layout.fillWidth: true
            onAccepted: linkDialog.accept()
        }
        // Link-to-heading mode (§2.4): choosing a heading fills the URL
        // with its #slug (and the text, if empty). Present only when the
        // document has headings.
        Label {
            text: qsTr("Or link to a heading")
            visible: linkDialog.headingTargets.length > 0
        }
        ComboBox {
            id: headingCombo
            objectName: "linkDialogHeadingCombo"
            visible: linkDialog.headingTargets.length > 0
            Layout.fillWidth: true
            textRole: "label"
            model: {
                var out = [{ label: qsTr("— choose a heading —"),
                             slug: "", text: "" }]
                var hs = linkDialog.headingTargets
                for (var i = 0; i < hs.length; i++) {
                    var indent = ""
                    for (var j = 1; j < hs[i].level; j++)
                        indent += "   "
                    out.push({ label: indent + hs[i].text,
                               slug: hs[i].slug, text: hs[i].text })
                }
                return out
            }
            currentIndex: 0
            onActivated: function(index) {
                if (index <= 0)
                    return
                var item = model[index]
                linkUrlField.text = "#" + item.slug
                if (linkTextField.text.length === 0)
                    linkTextField.text = item.text
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "linkDialogRemoveButton"
            text: qsTr("Remove link")
            visible: linkDialog.editing && linkDialog.removable
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: linkDialog.removeLink()
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
