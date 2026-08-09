// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The equation a `$$ … $$` block shows in a drawn document (selection.md
// "A document drawn read-only", features.md §1.2.15).
//
// A math block keeps its TeX as its content, so a surface that drew every
// verbatim block as text drew `\int_0^\infty e^x dx` in a code well where the
// note itself shows the integral. This is the read-only counterpart of the
// unfocused half of qml/MathBlock.qml: the same image provider, the same
// optical sizing against the prose x-height, and the same rule that an
// expression which does not parse shows its source and a named error rather
// than nothing. What it leaves out is everything that belongs to editing —
// the source area, the debounced live preview, the equation number, the drag
// proxy — none of which a surface has any way to act on.
//
// Display style rather than the text style inline spans use: `\int_0^\infty`
// stacks its limits and stands three lines tall, which is exactly what a
// block on its own line is for.
Item {
    id: equation

    // ---- what the row hands the equation ----

    // The block's stored TeX, as written between the fences.
    property string tex: ""
    // The colour and size the surrounding prose is set in. The formula is
    // drawn to match it, since an equation in a paragraph's flow that is
    // heavier or larger than the paragraph reads as a picture of one.
    property color textColor: Theme.textPrimary
    property int textPixelSize: Typography.bodySize
    // The screen's ratio, which the raster is produced at and the loaded
    // bitmap's pixel size is divided by. It is in the URL because it selects
    // the cached bitmap: the same TeX on a different screen is a different
    // image.
    property real devicePixelRatio: 1

    readonly property string trimmedTex: equation.tex.trim()
    readonly property string errorText: MathRenderer.errorFor(equation.tex)
    readonly property bool renderable:
        equation.trimmedTex !== "" && equation.errorText === ""

    // The letters stay at prose size and the layout supplies the large
    // operators, which is what LaTeX does; the optical match is against the
    // text font's x-height rather than its point size.
    readonly property int mathPixelSize: MathRenderer.opticalMathPixelSize(
        Typography.fontFamily, equation.textPixelSize)
    readonly property int verticalPadding:
        Math.max(2, Math.ceil(equation.mathPixelSize * 0.12))
    // Transparent margin on each side, so glyphs that overhang their advance
    // box are not cut off at the edge of their own bitmap.
    readonly property int horizontalPadding:
        MathRenderer.sideBearingPadding(equation.mathPixelSize)
    readonly property real renderDpr:
        equation.devicePixelRatio > 0
            ? Math.round(equation.devicePixelRatio * 100) / 100 : 1

    function sourceUrl() {
        if (!equation.renderable)
            return ""
        function hex(x) { return ("0" + Math.round(x * 255).toString(16)).slice(-2) }
        var c = equation.textColor
        var fg = hex(c.a) + hex(c.r) + hex(c.g) + hex(c.b)
        return "image://math/" + MathRenderer.encode(equation.tex)
             + "?fg=" + fg
             + "&size=" + equation.mathPixelSize
             + "&dpr=" + equation.renderDpr.toFixed(2)
             + "&vpad=" + equation.verticalPadding
             + "&hpad=" + equation.horizontalPadding
    }

    implicitHeight: equation.renderable
        ? Math.max(rendered.height, equation.textPixelSize)
        : Math.max(source.implicitHeight, 1)

    Image {
        id: rendered
        objectName: "readOnlyEquationImage"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        visible: equation.renderable
        source: equation.sourceUrl()
        // An Image takes its implicit size from the bitmap's PIXEL count and
        // ignores the ratio the provider set on it, so a raster made for a
        // 2x screen would draw at twice the prose size. Dividing by the ratio
        // the URL asked for restores the logical size and keeps every
        // physical pixel of the raster.
        width: implicitWidth / equation.renderDpr
        height: implicitHeight / equation.renderDpr
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("Equation: %1").arg(equation.trimmedTex)
    }

    // What does not parse is shown as what was written, with the renderer's
    // own message under it. A surface that drew nothing here would lose the
    // block altogether, and the reader would have no way to tell an empty
    // block from a broken one.
    Column {
        id: source
        objectName: "readOnlyEquationSource"
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !equation.renderable
        spacing: 2

        Text {
            width: parent.width
            visible: equation.trimmedTex !== ""
            text: equation.tex
            wrapMode: Text.Wrap
            color: equation.textColor
            font.family: Typography.monoFamily
            font.pixelSize: equation.textPixelSize
        }
        Text {
            width: parent.width
            visible: equation.errorText !== "" && equation.trimmedTex !== ""
            text: equation.errorText
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Interface.caption
        }
    }
}
