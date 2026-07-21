// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The equation images are a Repeater delegate, which is its own component
// scope and reads the box list and the editor from this file's root by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The rendered equations drawn over a block's inline `$…$` spans
// (features.md §1.2.15). The editing engine hides the markers and reserves a
// transparent box of the renderer's width in their place; this layer paints
// one image per box on top, aligned to the line the span sits on.
//
// Unlike the other block chrome this file does need the editor object, not
// only numbers from it. A box's position is `positionToRectangle` of two
// document offsets, and there is no way to pass a caret-geometry query as a
// value — it has to be asked of the item that laid the text out.
//
// Baselines are per line rather than per span. Two equations on one line can
// reserve different ascents, and aligning each to its own box would leave them
// visibly stepped, so every image on a line sits on the lowest baseline any
// span of that line asks for.
Item {
    id: root

    // The editor whose text these equations overlay. Read for caret
    // rectangles and for the offset of its text within this layer.
    property TextArea editor: null
    // The editor's font, which supplies the ascent for a span the renderer
    // has not measured yet.
    property font editorFont

    // The hidden spans, as the engine reports them: document range, rendered
    // metrics, and the width/ascent it reserved for each.
    property var boxes: []
    // Bumped by the delegate whenever the text relayouts or the caret moves,
    // both of which move every rectangle without changing the box list.
    property int tick: 0

    // Rendering parameters for the equation images.
    property color textColor: Theme.textPrimary
    property int pixelSize: 15
    property int verticalPadding: 2
    property real devicePixelRatio: 1

    objectName: "mathOverlayLayer"
    z: 3

    // The image-provider URL for one equation. Color, size, and device pixel
    // ratio are in the URL because they select the cached bitmap: the same TeX
    // at a different size or on a different screen is a different image.
    function sourceFor(tex) {
        if (!tex || tex.trim().length === 0)
            return ""
        function hex(x) { return ("0" + Math.round(x * 255).toString(16)).slice(-2) }
        var c = root.textColor
        var fg = hex(c.a) + hex(c.r) + hex(c.g) + hex(c.b)
        var dpr = Math.round(root.devicePixelRatio * 100) / 100
        return "image://math/" + MathRenderer.encode(tex)
             + "?fg=" + fg + "&size=" + root.pixelSize
             + "&dpr=" + dpr.toFixed(2)
             + "&vpad=" + root.verticalPadding
    }

    FontMetrics {
        id: editorMetrics
        font: root.editorFont
    }

    Repeater {
        model: root.boxes
        Image {
            id: mathImg
            objectName: "inlineMathImage"
            required property var modelData

            function boxFor(entry) {
                var r1 = root.editor.positionToRectangle(entry.docStart)
                var r2 = root.editor.positionToRectangle(entry.docEnd)
                var sameLine = Math.abs(r2.y - r1.y) < 2
                var w = sameLine ? Math.max(2, r2.x - r1.x)
                                 : Math.max(2, r1.width)
                return Qt.rect(r1.x, r1.y, w, r1.height)
            }
            function reservationAscentFor(entry) {
                return entry.reservationValid
                    && entry.reservationAscent > 0
                    && entry.reservationHeight > 0
                    ? entry.reservationAscent
                    : editorMetrics.ascent
            }
            function lineBaselineY() {
                var t = root.tick  // reposition on change
                var lineStart = modelData.lineStart !== undefined
                    ? modelData.lineStart : modelData.docStart
                var baseline = box.y + reservationAscentFor(modelData)
                for (var i = 0; i < root.boxes.length; ++i) {
                    var entry = root.boxes[i]
                    var entryLineStart = entry.lineStart !== undefined
                        ? entry.lineStart : entry.docStart
                    if (entryLineStart !== lineStart)
                        continue
                    var r = boxFor(entry)
                    baseline = Math.max(baseline,
                                        r.y + reservationAscentFor(entry))
                }
                return baseline
            }
            property rect box: {
                var t = root.tick  // reposition on change
                return boxFor(modelData)
            }
            property bool metricsValid: modelData.valid
                && modelData.width > 0
                && modelData.height > 0
                && modelData.baseline > 0
            property bool reservationValid: modelData.reservationValid
                && modelData.reservationAscent > 0
                && modelData.reservationHeight > 0
            property real mathVerticalPadding:
                modelData.inlineVerticalPadding !== undefined
                ? modelData.inlineVerticalPadding
                : root.verticalPadding
            property real measuredWidth: metricsValid
                ? modelData.width : box.width
            property real measuredHeight: metricsValid
                ? modelData.height
                  + 2 * mathVerticalPadding
                : box.height
            property real mathBaseline: metricsValid
                ? modelData.baseline
                  + mathVerticalPadding
                : measuredHeight
            property real lineBaseline: root.editor.y + lineBaselineY()
            x: root.editor.x + box.x
            y: lineBaseline - mathBaseline
            width: measuredWidth
            height: measuredHeight
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            verticalAlignment: Image.AlignTop
            smooth: true
            source: root.sourceFor(modelData.tex)
        }
    }
}
