// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// How a mouse wheel moves the block list.
//
// What this replaces. A QQuickFlickable handles the wheel itself. Measured
// through the shipped shell, by sending it wheel events and sampling where the
// view went, what it does with them is two separate problems.
//
// It moves too little. One notch of an ordinary wheel — 120 units of
// angleDelta — moved the view 26 pixels. The editor sets prose at 14 px on a
// line height of 1.3, so a line is about 18 px and a notch is not quite a
// line and a half; three lines, which is what the desktop's own
// wheelScrollLines setting asks for and what every other application gives,
// would be 55. A 616 px window therefore took about two dozen notches to turn
// over one screenful.
//
// It smears a small delta over a third of a second. A wheel that reports
// finer than one notch at a time — which is what a Logitech MX Master and
// anything else with a free-spinning wheel does, and what libinput's
// high-resolution scroll wheel axis carries — sends fractions of 120. Given
// 15 units, the flickable moved 9 px, delivered as an exponential decay
// running for 300 ms: 3.1, 2.1, 1.3, 0.5, 0.7, 0.5, 0.3, 0.1 and then a tail
// of movements below a tenth of a pixel. A spin sends those faster than they
// decay, so what the reader sees is dozens of overlapping decay curves rather
// than motion, and twenty notches sent 8 ms apart moved 245 px with nine of
// the twenty contributing nothing measurable at all.
//
// What this does instead. It takes the wheel event before the flickable sees
// it, converts the rotation to a distance in text lines, and adds that to a
// target. A separate frame-synchronised chase closes the gap between where
// the view is and where the target is, so every event contributes its whole
// distance whether or not a frame was drawn between it and the next one, and
// a burst of events adds up instead of each one replacing the last.
//
// The chase is exponential rather than a fixed-duration animation, for two
// reasons. A fixed duration restarted by each event never finishes while the
// wheel is turning, and its speed then depends on the event rate rather than
// on the rotation. And this machine draws in software (CLAUDE.md, "Rendering
// on this machine"), so frames are not evenly spaced; weighting each step by
// the time the frame actually took keeps the distance right when they are
// not.
Item {
    id: scroller

    // The list this scrolls. Nothing happens until it is set.
    property ListView listView: null

    // Whether to take the wheel at all. False leaves the flickable's own
    // handling in place, which is how the two were measured against each
    // other and how a host that wants the old behaviour asks for it.
    property bool enabled: true

    // How far one notch goes, in lines of the editor's own prose. The
    // desktop's own setting, which is three nearly everywhere, is what it is
    // asked for; a reader who has turned it up in their system settings is
    // then obeyed here as they are in every other application.
    //
    // Qt.styleHints is documented API whose type description says only
    // QObject, the same gap qml/TextBlockDelegate.qml works around for
    // Qt.application.font, so qmllint has to be told this property is real.
    // qmllint disable missing-property
    property int linesPerNotch: Qt.styleHints.wheelScrollLines > 0
                                ? Qt.styleHints.wheelScrollLines : 3
    // qmllint enable missing-property

    // The height of one line of prose, which is what a notch is counted in.
    readonly property real lineHeight:
        Math.max(1, Typography.baseSize * Typography.lineHeight)

    // How quickly the view closes the distance to the target, as the time
    // constant of an exponential in milliseconds: after this long the gap is
    // down to 37% of what it was. Below about 30 ms the motion is hard to
    // distinguish from an instant jump, and above about 80 ms the view feels
    // as though it is lagging behind the wheel.
    property real timeConstant: 45

    // Reduced motion (§14.3) turns the chase off and leaves the distance: the
    // view goes straight to the target on the frame the event arrives.
    readonly property bool animated: Theme.motionScale > 0

    // Where the view is heading. Meaningful only while `chasing`.
    property real targetY: 0
    property bool chasing: false
    // What this last wrote to contentY, so that a move made by anything else
    // — a scrollbar drag, a find-bar jump, the keyboard — is noticed and
    // abandons the chase rather than being dragged back.
    property real lastWritten: 0

    readonly property real minimumY: scroller.listView ? scroller.listView.originY : 0
    readonly property real maximumY: {
        var view = scroller.listView
        if (!view)
            return 0
        return Math.max(view.originY,
                        view.originY + view.contentHeight + view.bottomMargin
                        - view.height)
    }

    // Whether this list is a scrolling surface at all. An editor embedded as
    // a box that grows with its content — the read-only document surface in
    // qml/DocumentView.qml, the message-box editor in qml/CompactEditor.qml —
    // is as tall as its document, so its list has nothing to scroll and the
    // wheel over it belongs to whatever the host put it inside. Without this
    // the handler would accept the event, move nothing, and leave the dialog
    // around it unable to scroll.
    readonly property bool scrollable: scroller.maximumY > scroller.minimumY + 0.5

    // One wheel event's worth of distance, in pixels.
    //
    // A pixel delta is a distance already and is used as one: that is what a
    // touchpad and a compositor's continuous scroll axis report. A rotation
    // is converted through the line height, so the same physical turn of the
    // wheel moves the same number of lines whatever size the reader has set
    // the text to.
    function distanceFor(pixelDelta, angleDelta) {
        if (pixelDelta !== 0)
            return -pixelDelta
        return -(angleDelta / 120) * scroller.linesPerNotch * scroller.lineHeight
    }

    // `smoothed` false puts the whole distance on screen at once. A pixel
    // delta comes from a gesture that is already continuous — two fingers on
    // a touchpad move the page as far as they moved, and a compositor that
    // reports a free-spinning wheel as pixels does the same — so smoothing it
    // would only put the page behind the fingers. A rotation is a discrete
    // step and is what the chase exists for.
    function scrollBy(distance, smoothed) {
        var view = scroller.listView
        if (!view || distance === 0)
            return
        // A move by anything else since the last frame ends the old chase:
        // the target it was heading for is a position in a document the
        // reader has since left. Re-anchoring has to reset what this last
        // wrote as well as where it is heading — the chase below compares the
        // two on every frame to notice exactly this, and a stale one left
        // here made it abandon the chase on its first frame, so that the
        // first turn of the wheel after any other kind of scroll did nothing.
        if (!scroller.chasing || Math.abs(view.contentY - scroller.lastWritten) > 1) {
            scroller.targetY = view.contentY
            scroller.lastWritten = view.contentY
        }
        scroller.targetY = Math.max(scroller.minimumY,
                                    Math.min(scroller.targetY + distance,
                                             scroller.maximumY))
        if (!smoothed || !scroller.animated) {
            view.contentY = scroller.targetY
            scroller.lastWritten = view.contentY
            scroller.chasing = false
            return
        }
        scroller.chasing = true
    }

    // Frame-synchronised rather than a 16 ms timer: the step is weighted by
    // the time the frame actually took, and a software renderer's frames are
    // not 16 ms apart.
    FrameAnimation {
        id: chase
        running: scroller.chasing && scroller.listView !== null

        onTriggered: {
            var view = scroller.listView
            if (!view) {
                scroller.chasing = false
                return
            }
            if (Math.abs(view.contentY - scroller.lastWritten) > 1) {
                // Something else moved the view; let it have it.
                scroller.chasing = false
                return
            }
            // The end of the document moves as rows are built, so the target
            // is clamped again on every frame rather than only when it was set.
            scroller.targetY = Math.max(scroller.minimumY,
                                        Math.min(scroller.targetY,
                                                 scroller.maximumY))
            var gap = scroller.targetY - view.contentY
            if (Math.abs(gap) < 0.5) {
                view.contentY = scroller.targetY
                scroller.lastWritten = view.contentY
                scroller.chasing = false
                return
            }
            // 1 - e^(-dt/tau): the fraction of the remaining gap this frame
            // closes. frameTime is in seconds.
            var dt = Math.max(0.001, Math.min(chase.frameTime, 0.1)) * 1000
            var fraction = 1 - Math.exp(-dt / Math.max(1, scroller.timeConstant))
            view.contentY = view.contentY + gap * fraction
            scroller.lastWritten = view.contentY
        }
    }

    // The wheel is taken on the list itself rather than on an item over it, so
    // a block that scrolls something of its own — a task board's column, a
    // wide table — still gets its own wheel events first.
    WheelHandler {
        id: wheel
        objectName: "editorWheelHandler"
        // The handler belongs to the list, not to this item: a pointer
        // handler is offered the event before the item it is attached to sees
        // it, which is how the flickable's own wheel handling is bypassed
        // without an item drawn over the list to catch the event first.
        parent: scroller.listView
        // Not merely declining inside onWheel when there is nothing to
        // scroll: a handler that is enabled takes the event out of the
        // delivery that would have reached the host's own scrolling view,
        // and setting accepted false in the signal does not put it back.
        // Measured in a host that puts the read-only surface inside its own
        // ScrollView: the pane did not move at all for as long as this
        // handler was enabled over a list with nothing to scroll.
        enabled: scroller.enabled && scroller.listView !== null
                 && scroller.scrollable
        // Everything that turns: an ordinary wheel, a free-spinning one, and
        // a touchpad's two fingers.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        // A horizontal wheel has nothing to scroll here, and a modifier means
        // something else: Ctrl+wheel is the reading size.
        onWheel: function(event) {
            // A modifier means something else: Ctrl+wheel is the reading
            // size. The handler is disabled outright when the list has
            // nothing to scroll, which is what lets the host scroll instead.
            if (event.modifiers !== Qt.NoModifier) {
                event.accepted = false
                return
            }
            var distance = scroller.distanceFor(event.pixelDelta.y,
                                                event.angleDelta.y)
            if (distance === 0) {
                event.accepted = false
                return
            }
            scroller.scrollBy(distance, event.pixelDelta.y === 0)
            event.accepted = true
        }
    }
}
