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
    // Transparent margin the renderer leaves on each side of the formula, so
    // glyphs that overhang their advance box (an italic `f` most of all) are
    // not cut off at the edge of their own bitmap. The image is that much
    // wider than the box the text reserved, and is shifted left by the same
    // amount so the formula still starts where the box does.
    readonly property int horizontalPadding:
        MathRenderer.sideBearingPadding(root.pixelSize)

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
             + "&hpad=" + root.horizontalPadding
    }

    // Where this layer sits in the scene, so a coordinate inside it can be
    // snapped against the real pixel grid rather than against the layer's own
    // origin, which is itself off the grid as often as not.
    readonly property point sceneOrigin: {
        var t = root.tick        // recompute on relayout and caret movement
        var w = root.width       // and when the block itself moves or resizes
        var h = root.height
        return root.mapToItem(null, 0, 0)
    }

    // Round a coordinate to a whole device pixel. An equation bitmap drawn at
    // a fractional offset is resampled across neighbouring pixels, which is
    // what made inline math look soft next to display math: the text layout
    // puts a span at whatever sub-pixel position its advances add up to,
    // whereas a display block sits at a whole pixel. Snapping moves an image
    // by less than one pixel and lets its texture land on the pixel grid.
    function snapToPixel(v, sceneOffset) {
        var dpr = root.devicePixelRatio > 0 ? root.devicePixelRatio : 1
        return Math.round((v + sceneOffset) * dpr) / dpr - sceneOffset
    }

    FontMetrics {
        id: editorMetrics
        font: root.editorFont
    }

    // Where one span sits, and how far above that its baseline is. Both are
    // asked of the editor, which is the only thing that knows how the text
    // was laid out.
    function rectFor(entry) {
        var r1 = root.editor.positionToRectangle(entry.docStart)
        var r2 = root.editor.positionToRectangle(entry.docEnd)
        var sameLine = Math.abs(r2.y - r1.y) < 2
        var w = sameLine ? Math.max(2, r2.x - r1.x) : Math.max(2, r1.width)
        return Qt.rect(r1.x, r1.y, w, r1.height)
    }
    function ascentFor(entry) {
        return entry.reservationValid
            && entry.reservationAscent > 0
            && entry.reservationHeight > 0
            ? entry.reservationAscent
            : editorMetrics.ascent
    }
    function lineStartOf(entry) {
        return entry.lineStart !== undefined ? entry.lineStart : entry.docStart
    }

    // The lowest baseline any span on a line asks for, keyed by line start.
    //
    // Each image used to work this out for itself by scanning every box and
    // asking the editor for two caret rectangles per box, so a paragraph with
    // M equations did M*M rectangle queries on every relayout and every caret
    // move. The answer is the same for every image on a line, so it is
    // computed once here in a single pass and looked up.
    readonly property var lineBaselines: {
        var t = root.tick   // recompute on relayout and caret movement
        var map = ({})
        if (!root.editor)
            return map
        for (var i = 0; i < root.boxes.length; ++i) {
            var entry = root.boxes[i]
            var key = String(root.lineStartOf(entry))
            var baseline = root.rectFor(entry).y + root.ascentFor(entry)
            if (map[key] === undefined || baseline > map[key])
                map[key] = baseline
        }
        return map
    }

    Repeater {
        model: root.boxes
        Image {
            id: mathImg
            objectName: "inlineMathImage"
            required property var modelData

            function lineBaselineY() {
                var shared = root.lineBaselines[String(root.lineStartOf(modelData))]
                var own = box.y + root.ascentFor(modelData)
                return shared !== undefined ? Math.max(shared, own) : own
            }
            property rect box: {
                var t = root.tick  // reposition on change
                return root.rectFor(modelData)
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
            x: root.snapToPixel(root.editor.x + box.x
                                - root.horizontalPadding,
                                root.sceneOrigin.x)
            y: root.snapToPixel(lineBaseline - mathBaseline,
                                root.sceneOrigin.y)
            // The loaded bitmap's own logical size, which on a fractional
            // device pixel ratio is not exactly the measured size rounded:
            // the provider rasterizes whole physical pixels. Drawing it at
            // any other size resamples it, so the item takes the image's
            // size and falls back to the measurement only until it loads.
            width: mathImg.implicitWidth > 0
                ? mathImg.implicitWidth
                : measuredWidth + 2 * root.horizontalPadding
            height: mathImg.implicitHeight > 0
                ? mathImg.implicitHeight : measuredHeight
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            verticalAlignment: Image.AlignTop
            smooth: true
            source: root.sourceFor(modelData.tex)
        }
    }
}
