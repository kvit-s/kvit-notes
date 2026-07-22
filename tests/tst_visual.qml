// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
// The application services this suite drives — BlockModel, DocumentManager,
// NoteCollection, Theme and the rest — reach QML as `Kvit` module singletons.
// Without this import every scenario that names one throws on an undefined
// variable before it captures a single frame, which is exactly what happened:
// the suite reported 62 failures that were all this one missing line.
import Kvit 1.0

// Visual storyboard suite for screen verification.
//
// Each test replays a canonical scenario from the specification as a
// scripted walkthrough, saving a numbered frame at every step. A directory
// listing of build/screenshots reads as a storyboard. Reviewing the saved
// frames is a mandatory part of completing any UI-affecting step — these
// tests capture the record; they do not by themselves prove the rendering
// is correct.
//
// The frames document the hybrid editing engine: blocks
// render with markers hidden; the span under the cursor reveals its
// markdown syntax (muted markers, content still styled) and re-hides when
// the cursor leaves — features.md §2.2.
Item {
    id: root
    width: 800
    height: 600

    Loader {
        id: appLoader
        anchors.fill: parent
        source: "qrc:/qml/main.qml"
        asynchronous: false
    }

    TestCase {
        id: testCase
        name: "VisualTests"
        when: windowShown && appLoader.status === Loader.Ready

        readonly property bool isHeadless: Qt.platform.pluginName === "offscreen"

        function findBlockDelegate(index) {
            var listView = findChild(appLoader.item, "blockListView")
            if (!listView) return null
            return listView.itemAtIndex(index)
        }

        function findTextAreaRaw(blockDelegate) {
            return findChild(blockDelegate, "blockTextArea")
        }

        // A text block renders as a read-only Text and only promotes to an
        // editable TextArea when activateEditor() is called, so a bare
        // findChild returns null for any block the user has not entered yet.
        // tst_integration.qml has carried the promotion step since it was
        // written; this suite did not, which is why scenarios that reach
        // straight for the editor failed on a null rather than on anything
        // they were meant to be testing.
        function findTextArea(blockDelegate) {
            var textArea = findTextAreaRaw(blockDelegate)
            if (textArea || !blockDelegate
                || typeof blockDelegate.activateEditor !== "function")
                return textArea
            blockDelegate.activateEditor()
            tryVerify(function() {
                return findTextAreaRaw(blockDelegate) !== null
            }, 1000, "Text block should promote to an editor TextArea")
            return findTextAreaRaw(blockDelegate)
        }

        function ensureFocus(textArea) {
            var p = textArea
            while (p && typeof p.activateEditor !== "function")
                p = p.parent
            if (p && typeof p.activateEditor === "function")
                p.activateEditor()
            textArea.forceActiveFocus()
            tryCompare(textArea, "activeFocus", true, 1000)
        }

        // Screenshot helper (mechanism proven 2026-07-06):
        // main.qml is an ApplicationWindow — inside a Loader it becomes a
        // SEPARATE window, so grab its contentItem; grabbing the test root
        // yields a blank image. grabImage is synchronous; the asynchronous
        // Item.grabToImage callback does not fire for that contentItem.
        function saveScreenshot(name) {
            var img = grabImage(appLoader.item.contentItem)
            var path = screenshotDir + "/" + name
            img.save(path)
            console.log("SCREENSHOT SAVED: " + path)
        }

        function saveItemScreenshot(item, name) {
            var img = grabImage(item)
            var path = screenshotDir + "/" + name
            img.save(path)
            console.log("SCREENSHOT SAVED: " + path)
        }

        // Remove focus from every block so the block under study renders
        // unfocused. Note: focusing the ListView instead does NOT work —
        // it is a FocusScope and hands active focus straight back to the
        // focused TextArea child.
        function clearFocus() {
            var listView = findChild(appLoader.item, "blockListView")
            for (var i = 0; i < listView.count; i++) {
                var item = listView.itemAtIndex(i)
                if (item) {
                    var ta = findTextArea(item)
                    if (ta && ta.activeFocus) {
                        ta.focus = false
                        tryCompare(ta, "activeFocus", false, 1000)
                    }
                }
            }
            wait(100)
        }

        function verifyInlineMathNativeBaseline(blockDelegate, label) {
            verify(blockDelegate !== null, label + " block exists")
            compare(blockDelegate.inlineMathBoxes.length, 1)
            var ta = findTextArea(blockDelegate)
            verify(ta !== null, label + " TextArea exists")
            var img = findChild(blockDelegate, "inlineMathImage")
            verify(img !== null, label + " overlay image exists")
            tryVerify(function() { return img.status === Image.Ready }, 1000,
                      label + " overlay image loads")

            var box = blockDelegate.inlineMathBoxes[0]
            verify(box.valid, label + " metrics valid: " + box.error)
            var expectedHeight = box.height
                + 2 * blockDelegate.inlineMathVerticalPadding
            verify(img.box.height + 1 >= expectedHeight,
                   label + " line reserves padded renderer height: "
                   + img.box.height.toFixed(2) + " vs "
                   + expectedHeight.toFixed(2))
            // The image is the renderer width plus the transparent side
            // margin that keeps overhanging glyphs (an italic `f`) inside the
            // bitmap, and it is shifted left by that same margin, so the
            // formula still starts where the reserved box does.
            var sideMargin = 2 * img.parent.horizontalPadding
            verify(Math.abs(img.width - (box.width + sideMargin)) <= 1,
                   label + " image keeps renderer width plus side margin: "
                   + img.width.toFixed(2) + " vs "
                   + (box.width + sideMargin).toFixed(2))
            verify(Math.abs(img.height - expectedHeight) <= 1,
                   label + " image keeps padded renderer height: "
                   + img.height.toFixed(2) + " vs "
                   + expectedHeight.toFixed(2))
            verify(Math.abs((img.y + img.mathBaseline)
                            - img.lineBaseline) <= 1,
                   label + " math baseline aligns to text baseline")

            console.log("MATH_CANARY " + label + " image="
                        + Math.round(img.width) + "x" + Math.round(img.height)
                        + " lineBox=" + Math.round(img.box.width) + "x"
                        + Math.round(img.box.height)
                        + " imageY=" + img.y.toFixed(2)
                        + " baselineY="
                        + (img.y + img.mathBaseline).toFixed(2)
                        + " lineBaseline=" + img.lineBaseline.toFixed(2))
        }

        function test_00_initial_state() {
            verify(appLoader.item !== null, "Application should load")
            wait(200)
            saveScreenshot("visual_00_initial_state.png")
        }

        // features.md §2.2.3 canonical walkthrough:
        //   cursor at line start (all rendered) → arrow into the bold word
        //   (markers visible, text still bold, everything else rendered) →
        //   arrow onward into the italic word (bold re-hidden, italic
        //   revealed) → click elsewhere (all rendered again).
        function test_01_reveal_walkthrough() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            BlockModel.updateContent(1, "The quick **brown** fox has *seven* cubs.")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)

            clearFocus()
            saveScreenshot("visual_01_reveal_01_cursor_outside.png")

            // Cursor at line start
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            wait(150)
            saveScreenshot("visual_01_reveal_02_cursor_at_line_start.png")

            // Arrow into the bold word ("brown": raw positions 12-17)
            for (var i = 0; i < 14; i++) keyClick(Qt.Key_Right)
            wait(150)
            saveScreenshot("visual_01_reveal_03_cursor_in_bold.png")

            // Arrow onward into the italic word ("seven": raw positions 29-34)
            for (var j = 0; j < 17; j++) keyClick(Qt.Key_Right)
            wait(150)
            saveScreenshot("visual_01_reveal_04_cursor_in_italic.png")

            // Click elsewhere — everything rendered again
            clearFocus()
            saveScreenshot("visual_01_reveal_05_cursor_elsewhere.png")
        }

        // Storyboard: one line carrying every new inline type —
        // strikethrough, inline code, highlight, underline — rendered, then
        // each span revealed in turn by moving the cursor into its word,
        // then everything rendered again.
        function test_03_new_inline_types() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            BlockModel.updateContent(1,
                "Strike ~~gone~~ code `x = 1` mark ==note== under ++line++.")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)

            clearFocus()
            saveScreenshot("visual_03_types_01_all_rendered.png")

            // Reveal each span by placing the cursor inside its word.
            // The word is located in the CURRENT text because each reveal
            // shifts later positions by the revealed marker characters.
            var stops = [
                { word: "gone",  shot: "visual_03_types_02_strike_revealed.png" },
                { word: "x = 1", shot: "visual_03_types_03_code_revealed.png" },
                { word: "note",  shot: "visual_03_types_04_highlight_revealed.png" },
                { word: "line",  shot: "visual_03_types_05_underline_revealed.png" }
            ]
            ensureFocus(textArea)
            for (var i = 0; i < stops.length; i++) {
                textArea.cursorPosition = textArea.text.indexOf(stops[i].word) + 1
                wait(150)
                saveScreenshot(stops[i].shot)
            }

            clearFocus()
            saveScreenshot("visual_03_types_06_all_rendered_again.png")
        }

        // Storyboard: nested formatting. Rendered, the inner word combines
        // both styles with no markers; revealed, the whole top-level span
        // shows every marker level muted (features.md §2.2.7 "reveals all
        // applicable syntax").
        function test_04_nested_spans() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            BlockModel.updateContent(1, "Nested: **bold *and italic* inside** here.")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)

            clearFocus()
            saveScreenshot("visual_04_nested_01_rendered.png")

            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.indexOf("and") + 1
            wait(150)
            saveScreenshot("visual_04_nested_02_revealed.png")

            clearFocus()
            saveScreenshot("visual_04_nested_03_rendered_again.png")
        }

        // Storyboard: a [text](url) link rendered as accent-colored text,
        // revealed with muted brackets/URL, and a bare autolink URL.
        function test_05_links() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            BlockModel.updateContent(1,
                "Docs at [Qt site](https://qt.io) and bare https://kde.org too.")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)

            clearFocus()
            saveScreenshot("visual_05_links_01_rendered.png")

            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.indexOf("Qt site") + 1
            wait(150)
            saveScreenshot("visual_05_links_02_revealed.png")

            clearFocus()
            saveScreenshot("visual_05_links_03_rendered_again.png")
        }

        // Storyboard: the Ctrl+K dialog, prefilled from the link under the
        // cursor, and the text after Remove link.
        function test_06_link_dialog() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            BlockModel.updateContent(1, "Visit [the Qt site](https://qt.io) today.")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)
            var dialog = appLoader.item.linkDialog

            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.indexOf("Qt") + 1
            wait(150)
            keyClick(Qt.Key_K, Qt.ControlModifier)
            tryCompare(dialog, "visible", true, 1000)
            wait(150)
            saveScreenshot("visual_06_linkdialog_01_edit_prefilled.png")

            dialog.removeLink()
            wait(200)
            clearFocus()
            saveScreenshot("visual_06_linkdialog_02_after_remove.png")
        }

        // Storyboard: one document holding every wave-1 block type —
        // nested bullets showing the glyph cycle, a numbered list, todos
        // checked and unchecked, a multi-line quote, a code block,
        // dividers — then a todo toggled, then a list item's inline bold
        // revealed.
        function test_07_block_types() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Block types")
            BlockModel.insertBlock(1, 4, "bullet at level zero")
            BlockModel.insertBlock(2, 4, "level one goes deeper", 1)
            BlockModel.insertBlock(3, 4, "level two deeper still", 2)
            BlockModel.insertBlock(4, 5, "first numbered item")
            BlockModel.insertBlock(5, 5, "second with **bold** text")
            BlockModel.insertBlock(6, 5, "nested child restarts", 1)
            BlockModel.insertBlock(7, 6, "an open task")
            BlockModel.insertBlock(8, 6, "a completed task")
            BlockModel.setChecked(8, true)
            BlockModel.insertBlock(9, 9, "")
            BlockModel.insertBlock(10, 7, "a quotation\nwith a second line")
            BlockModel.insertBlock(11, 8, "def area(r):\n    return 3.14 * r * r")
            BlockModel.insertBlock(12, 10, "A level-four heading (Phase 5)")
            wait(300)

            clearFocus()
            saveScreenshot("visual_07_types_01_all_block_types.png")

            // The quote, code block, divider, and the level-four heading
            // sit below the fold
            var listView = findChild(appLoader.item, "blockListView")
            listView.positionViewAtEnd()
            wait(200)
            saveScreenshot("visual_07_types_01b_quote_code_divider.png")
            listView.positionViewAtBeginning()
            wait(200)

            // Toggle the open task by clicking its checkbox
            var todoDelegate = findBlockDelegate(7)
            var checkbox = findChild(todoDelegate, "todoCheckbox")
            verify(checkbox !== null, "Todo checkbox should exist")
            mouseClick(checkbox)
            tryVerify(function() { return BlockModel.blockAt(7).checked }, 1000)
            wait(150)
            saveScreenshot("visual_07_types_02_todo_toggled.png")

            // Inline formatting reveals inside a numbered item
            var listDelegate = findBlockDelegate(5)
            var textArea = findTextArea(listDelegate)
            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.indexOf("bold") + 1
            wait(150)
            saveScreenshot("visual_07_types_03_list_item_bold_revealed.png")

            clearFocus()
            saveScreenshot("visual_07_types_04_all_rendered_again.png")
        }

        // Storyboard: the list keyboard flow. An Enter continuation on a
        // bullet, Tab nesting the new item (glyph changes), typing into
        // it, then Enter twice — the second, on the now-empty item, exits
        // the list to a paragraph.
        function test_08_list_keyboard_flow() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 4, "the first item")
            wait(200)

            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            wait(200)
            saveScreenshot("visual_08_listflow_01_enter_continues.png")

            keyClick(Qt.Key_Tab)
            wait(150)
            saveScreenshot("visual_08_listflow_02_tab_nests.png")

            typeText("a nested child")
            wait(150)
            saveScreenshot("visual_08_listflow_03_typed_in_child.png")

            keyClick(Qt.Key_Return)
            wait(200)
            keyClick(Qt.Key_Return)
            wait(200)
            saveScreenshot("visual_08_listflow_04_empty_item_exited.png")

            verify(BlockModel.blockAt(2).blockType === 0,
                   "The empty item should have exited to a paragraph")
        }

        function typeText(text) {
            for (var i = 0; i < text.length; i++)
                keyClick(text.charAt(i))
        }

        // Storyboard: the "- [ ] " typing progression — paragraph, then a
        // bullet at "- ", then a todo at "[ ] ", then typed task text.
        function test_09_prefix_conversion_flow() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)

            typeText("-")
            wait(150)
            saveScreenshot("visual_09_prefix_01_dash_still_paragraph.png")

            typeText(" ")
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 4 }, 1000)
            wait(150)
            saveScreenshot("visual_09_prefix_02_bullet_converted.png")

            typeText("[ ] ")
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 6 }, 1000)
            wait(150)
            saveScreenshot("visual_09_prefix_03_todo_converted.png")

            typeText("write the report")
            wait(150)
            saveScreenshot("visual_09_prefix_04_task_typed.png")

            compare(BlockModel.getContent(0), "write the report")
        }

        // Storyboard: the slash menu. Typing "/" opens the grouped catalog
        // with icons; "h1" filters it; arrows move the highlight; Enter
        // converts and clears the query.
        function test_10_slash_menu() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            var menu = appLoader.item.blockMenu

            typeText("/")
            tryCompare(menu, "visible", true, 1000)
            wait(200)
            saveScreenshot("visual_10_menu_01_open_grouped.png")

            typeText("h1")
            wait(200)
            saveScreenshot("visual_10_menu_02_filtered_h1.png")

            keyClick(Qt.Key_Backspace)
            keyClick(Qt.Key_Backspace)
            wait(150)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            wait(150)
            saveScreenshot("visual_10_menu_03_arrow_highlight.png")

            typeText("quote")
            wait(150)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 7 }, 1000)
            wait(200)
            saveScreenshot("visual_10_menu_04_converted_quote.png")
        }

        // The §4.3 viewport rule: near the window bottom the menu flips
        // above the cursor instead of clipping.
        function test_11_slash_menu_flip() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            for (var i = 1; i <= 12; i++)
                BlockModel.insertBlock(i, 0, "filler line " + i)
            BlockModel.insertBlock(13, 0, "")
            wait(200)
            var listView = findChild(appLoader.item, "blockListView")
            listView.positionViewAtEnd()
            wait(200)

            var lastArea = findTextArea(findBlockDelegate(13))
            ensureFocus(lastArea)
            var menu = appLoader.item.blockMenu
            typeText("/")
            tryCompare(menu, "visible", true, 1000)
            wait(200)
            saveScreenshot("visual_11_menu_05_flipped_at_bottom.png")
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        // Storyboard: the gutter plus-button. It appears on hover beside
        // the drag handle; clicking it adds an empty paragraph below and
        // opens the menu for it; filtering and Enter make the new block a
        // todo.
        function test_12_plus_button() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            BlockModel.updateContent(0, "An existing block of text")
            wait(200)

            var delegate = findBlockDelegate(0)
            mouseMove(delegate, 30, delegate.height / 2)
            tryCompare(delegate, "isHovered", true, 1000)
            wait(250)
            saveScreenshot("visual_12_plus_01_hover_shows_button.png")

            var plus = findChild(delegate, "plusButton")
            mouseClick(plus)
            var menu = appLoader.item.blockMenu
            tryCompare(menu, "visible", true, 1000)
            wait(200)
            saveScreenshot("visual_12_plus_02_menu_for_new_block.png")

            typeText("tod")
            wait(200)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 6 }, 1000)
            wait(200)
            saveScreenshot("visual_12_plus_03_new_todo_created.png")
        }

        // Phantom-gap scenario: under the old overlay design this block's
        // height followed a hidden second text and showed a gap. With the
        // engine there is only one text, so height always derives from what
        // is on screen and focus cannot change it (block-geometry
        // invariants).
        function test_02_phantom_gap() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            // Every inline type participates so the geometry invariants
            // (focus never resizes a block) hold for the full registry.
            BlockModel.updateContent(1,
                "**Alpha** *beta* ~~gamma~~ ==delta== ++epsilon++ `zeta` " +
                "[eta](http://e) __theta__ _iota_ **kappa *nested* mu** " +
                "https://nu.io **xi**")
            wait(200)

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Storyboard block should exist")
            var textArea = findTextArea(delegate)

            clearFocus()
            var heightUnfocused = delegate.height
            console.log("phantom_gap unfocused: delegate.height=" + delegate.height
                        + " textArea.implicitHeight=" + textArea.implicitHeight)
            saveScreenshot("visual_02_gap_01_unfocused.png")

            ensureFocus(textArea)
            // The invariant is about focus ALONE. Every word in this line
            // is a span, so park the cursor in the autolink — its markers
            // are zero-length and its reveal changes no text. (Any other
            // position legitimately reveals markers, and since the
            // gutter widened, that longer line wraps: a text change, not a
            // focus effect — the case test_22b/test_23 cover.)
            textArea.cursorPosition = textArea.text.indexOf("nu.io") + 2
            wait(150)
            console.log("phantom_gap focused: delegate.height=" + delegate.height
                        + " textArea.implicitHeight=" + textArea.implicitHeight)
            compare(delegate.height, heightUnfocused,
                    "Focus must not change the block height")
            saveScreenshot("visual_02_gap_02_focused.png")

            clearFocus()
            compare(delegate.height, heightUnfocused,
                    "Unfocus must not change the block height")
            saveScreenshot("visual_02_gap_03_unfocused_again.png")
        }

        // Block selection (features.md §3.1). Selected blocks
        // carry a tinted background and border; the selection can be a
        // contiguous range, a non-contiguous Ctrl+Click set (dividers
        // included), or the whole document (Ctrl+A twice).
        function test_13_block_selection() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 1, "Project notes")
            BlockModel.insertBlock(1, 0, "First paragraph with **bold** text")
            BlockModel.insertBlock(2, 4, "bullet one")
            BlockModel.insertBlock(3, 4, "bullet two")
            BlockModel.insertBlock(4, 9, "")
            BlockModel.insertBlock(5, 0, "Closing paragraph")
            wait(200)

            DocumentSelection.selectBlock(1)
            DocumentSelection.extendBlockSelectionTo(3)
            appLoader.item.selectionKeyHandler.forceActiveFocus()
            wait(200)
            saveScreenshot("visual_13_select_01_contiguous_range.png")

            DocumentSelection.clear()
            DocumentSelection.toggleBlock(0)
            DocumentSelection.toggleBlock(2)
            DocumentSelection.toggleBlock(4)
            wait(200)
            saveScreenshot("visual_13_select_02_non_contiguous_with_divider.png")

            DocumentSelection.selectAllBlocks()
            wait(200)
            saveScreenshot("visual_13_select_03_select_all.png")

            DocumentSelection.clear()
            wait(100)
        }

        // Operations on the block selection — duplicate (features.md §3.6)
        // and move (§3.2), each a single undo step.
        function test_14_block_operations() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 1, "Meeting agenda")
            BlockModel.insertBlock(1, 6, "prepare slides")
            BlockModel.insertBlock(2, 6, "book the room")
            BlockModel.insertBlock(3, 0, "Notes go here afterwards")
            wait(200)

            // Two todos selected, ready to duplicate
            DocumentSelection.selectBlock(1)
            DocumentSelection.extendBlockSelectionTo(2)
            appLoader.item.selectionKeyHandler.forceActiveFocus()
            wait(200)
            saveScreenshot("visual_14_ops_01_selection_before_duplicate.png")

            keyClick(Qt.Key_D, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 6 }, 1000)
            wait(200)
            saveScreenshot("visual_14_ops_02_clones_selected_below.png")

            keyClick(Qt.Key_Down, Qt.AltModifier)
            tryVerify(function() {
                return BlockModel.getContent(3) === "Notes go here afterwards"
            }, 1000)
            wait(200)
            saveScreenshot("visual_14_ops_03_selection_moved_down.png")

            DocumentSelection.clear()
            wait(100)
        }

        // Cross-block text selection (features.md §2.5, §21.3) — a mouse
        // drag across blocks selects character-precise text; every
        // touched block renders its portion; cut collapses the range.
        function test_15_cross_block_selection() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "The first paragraph has **bold** text inside")
            BlockModel.insertBlock(1, 4, "a bulleted item in between")
            BlockModel.insertBlock(2, 0, "and the closing paragraph ends here")
            wait(200)

            function dragAcross(fromIdx, fromPos, toIdx, toPos) {
                var fromTa = findTextArea(findBlockDelegate(fromIdx))
                var r1 = fromTa.positionToRectangle(fromPos)
                mousePress(fromTa, r1.x + 1, r1.y + r1.height / 2)
                mouseMove(fromTa, r1.x + 8, r1.y + r1.height / 2)
                var toTa = findTextArea(findBlockDelegate(toIdx))
                var r2 = toTa.positionToRectangle(toPos)
                mouseMove(toTa, r2.x + 1, r2.y + r2.height / 2)
                mouseRelease(toTa, r2.x + 1, r2.y + r2.height / 2)
            }

            dragAcross(0, 4, 2, 15)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            wait(200)
            saveScreenshot("visual_15_xsel_01_forward_range.png")

            keyClick(Qt.Key_Escape)
            tryCompare(DocumentSelection, "hasTextSelection", false, 1000)
            dragAcross(2, 15, 0, 4)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            wait(200)
            saveScreenshot("visual_15_xsel_02_backward_range.png")

            keyClick(Qt.Key_Escape)
            dragAcross(0, 4, 2, 15)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            keyClick(Qt.Key_X, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 1 }, 1000)
            wait(200)
            saveScreenshot("visual_15_xsel_03_after_cut.png")

            DocumentSelection.clear()
            wait(100)
        }

        // Drag-and-drop reordering (features.md §3.2, §21.4) — the floating
        // proxy under the pointer, the dimmed source row making room
        // live, the multi-drag drop indicator with its count badge, and
        // the document after the drop.
        // The find bar (features.md §7.1): the bar over a document
        // with matches tinted and the current one distinct; the current
        // match inside a revealed span; a regex query with its option
        // lit; the invalid-pattern error state.
        function test_17_find_bar() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "The quick **brown** fox jumps")
            BlockModel.insertBlock(1, 0, "One fox, two foxes, one Fox")
            BlockModel.insertBlock(2, 4, "fox item in a list")
            BlockModel.insertBlock(3, 8, "let fox = hunt()\nfox()")
            wait(200)

            var bar = appLoader.item.findBar
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(bar, "visible", true, 1000)
            for (var i = 0; i < 3; i++)
                keyClick("fox".charAt(i))
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            wait(250)
            saveScreenshot("visual_17_find_01_matches_and_count.png")

            // Click into the bold word while the bar stays open: the
            // span reveals and the match tint follows its content.
            bar.queryField.text = "brown"
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            var pos = textArea.positionToRectangle(12)
            mouseClick(textArea, pos.x, pos.y + 5)
            tryCompare(textArea, "activeFocus", true, 1000)
            wait(250)
            saveScreenshot("visual_17_find_02_match_in_revealed_span.png")

            // Regex mode: option lit, pattern matching the code call.
            var regexButton = findChild(bar, "findRegexButton")
            mouseClick(regexButton)
            bar.queryField.text = "f.x\\(\\)"
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            wait(250)
            saveScreenshot("visual_17_find_03_regex_option.png")

            // The invalid-pattern error state.
            bar.queryField.text = "(unclosed"
            tryCompare(DocumentSearch, "patternError", true, 1000)
            wait(250)
            saveScreenshot("visual_17_find_04_invalid_pattern.png")

            bar.close()
            regexButton.checked = false // leave the option off for later tests
            bar.queryField.text = ""
            tryCompare(bar, "visible", false, 1000)
        }

        // Replace (features.md §7.2): the bar in replace mode,
        // the replace-all preview panel, and the document after Confirm.
        function test_18_replace() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "The old fox and the old hound")
            BlockModel.insertBlock(1, 4, "old bullet entry")
            BlockModel.insertBlock(2, 0, "Nothing to replace here")
            wait(200)

            var bar = appLoader.item.findBar
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_H, Qt.ControlModifier)
            tryCompare(bar, "visible", true, 1000)
            tryCompare(bar, "replaceMode", true, 1000)
            for (var i = 0; i < 3; i++)
                keyClick("old".charAt(i))
            tryCompare(DocumentSearch, "matchCount", 3, 1000)
            bar.replaceField.text = "new"
            wait(250)
            saveScreenshot("visual_18_replace_01_replace_mode.png")

            mouseClick(findChild(bar, "replaceAllButton"))
            var panel = findChild(bar, "replacePreviewPanel")
            tryCompare(panel, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_18_replace_02_preview_panel.png")

            mouseClick(findChild(bar, "previewConfirmButton"))
            tryCompare(DocumentSearch, "matchCount", 0, 2000)
            wait(250)
            saveScreenshot("visual_18_replace_03_after_confirm.png")

            bar.close()
            bar.queryField.text = ""
            bar.replaceField.text = ""
            tryCompare(bar, "visible", false, 1000)
        }

        function test_16_drag_reorder() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 1, "Shopping list")
            BlockModel.insertBlock(1, 4, "apples")
            BlockModel.insertBlock(2, 4, "bread")
            BlockModel.insertBlock(3, 4, "cheese")
            BlockModel.insertBlock(4, 0, "Everything else goes below")
            wait(200)

            var listView = findChild(appLoader.item, "blockListView")

            // Single drag: lift "apples" and hold it past "cheese" —
            // the gap has opened, the proxy floats, the source is dimmed
            var delegate = findBlockDelegate(1)
            mouseMove(delegate, 30, delegate.height / 2)
            tryCompare(delegate, "isHovered", true, 1000)
            var handle = findChild(delegate, "dragHandle")
            tryVerify(function() { return handle.visible }, 1000)
            mousePress(handle, 4, 4)
            mouseMove(handle, 6, 30)
            var cheese = findBlockDelegate(3)
            var pastCheese = cheese.y - listView.contentY + cheese.height * 0.8
            mouseMove(listView, 100, pastCheese)
            tryVerify(function() { return BlockModel.getContent(3) === "apples" },
                      1000)
            wait(250)
            saveScreenshot("visual_16_drag_01_live_make_room.png")

            mouseRelease(listView, 100, pastCheese)
            wait(250)
            saveScreenshot("visual_16_drag_02_after_drop.png")

            // Multi drag: select the two bullets now at 1..2 and drag
            // them below the closing paragraph — indicator + badge
            DocumentSelection.selectBlock(1)
            DocumentSelection.extendBlockSelectionTo(2)
            appLoader.item.selectionKeyHandler.forceActiveFocus()
            wait(100)
            var first = findBlockDelegate(1)
            mouseMove(first, 30, first.height / 2)
            tryCompare(first, "isHovered", true, 1000)
            var handle2 = findChild(first, "dragHandle")
            tryVerify(function() { return handle2.visible }, 1000)
            mousePress(handle2, 4, 4)
            mouseMove(handle2, 6, 30)
            var target = findBlockDelegate(4)
            var dropY = target.y - listView.contentY + target.height - 4
            mouseMove(listView, 100, dropY)
            var indicator = findChild(appLoader.item, "dropIndicator")
            tryVerify(function() { return indicator && indicator.visible }, 1000)
            wait(250)
            saveScreenshot("visual_16_drag_03_multi_indicator.png")

            mouseRelease(listView, 100, dropY)
            tryVerify(function() {
                return BlockModel.getContent(3) === "bread"
                    && BlockModel.getContent(4) === "cheese"
            }, 1000)
            wait(250)
            saveScreenshot("visual_16_drag_04_multi_dropped.png")

            DocumentSelection.clear()
            wait(100)
        }

        // Storyboard: the three-pane shell — sidebar with the folder tree,
        // the note list, and the editor — over a seeded collection; a note
        // drag onto a folder with the floating proxy and drop highlight;
        // the inline rename editor; and the collapsed-panels state
        // (Ctrl+\).
        function test_19_collection_shell() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createFolder("Ideas", "Projects") !== "")
            verify(NoteCollection.createFolder("", "Journal") !== "")
            verify(NoteCollection.createNote("", "Welcome") !== "")
            verify(NoteCollection.createNote("Ideas", "Reading list") !== "")
            verify(NoteCollection.createNote("Ideas/Projects", "Kvit editor") !== "")
            NoteListModel.scope = "all"

            // Give the notes real content so snippets and dates render.
            function fill(relPath, text) {
                verify(appLoader.item.openNoteByPath(relPath))
                BlockModel.updateContent(0, text)
                verify(DocumentManager.save())
            }
            fill("Ideas/Reading list.md",
                 "Books to read this **summer**, in order")
            fill("Ideas/Projects/Kvit editor.md",
                 "The hybrid *markdown* block editor")
            fill("Welcome.md", "# Welcome to Kvit")
            verify(appLoader.item.openNoteByPath("Ideas/Reading list.md"))
            wait(250)
            saveScreenshot("visual_19_shell_01_three_panes.png")

            // Mid-drag: the floating proxy and the highlighted target.
            var listView = findChild(appLoader.item, "noteListView")
            var row = listView.itemAtIndex(NoteListModel.rowOf("Welcome.md"))
            verify(row !== null)
            var treeView = findChild(appLoader.item, "folderTreeView")
            var folderRow = treeView.itemAtIndex(FolderTreeModel.rowOf("Journal"))
            verify(folderRow !== null)
            mousePress(row, row.width / 2, row.height / 2)
            mouseMove(row, row.width / 2 - 20, row.height / 2)
            mouseMove(folderRow, folderRow.width / 2, folderRow.height / 2)
            wait(250)
            saveScreenshot("visual_19_shell_02_drag_note_to_folder.png")
            mouseRelease(folderRow, folderRow.width / 2, folderRow.height / 2)
            tryVerify(function() {
                return NoteCollection.noteInfo("Journal/Welcome.md").title
                       === "Welcome"
            }, 1000)

            // The moved note in its folder, list scoped to it.
            NoteListModel.folderPath = "Journal"
            NoteListModel.scope = "folder"
            wait(250)
            saveScreenshot("visual_19_shell_03_moved_into_folder.png")

            // Inline rename editor.
            var pane = findChild(appLoader.item, "noteListPane")
            var renameRow = listView.itemAtIndex(
                NoteListModel.rowOf("Journal/Welcome.md"))
            verify(renameRow !== null)
            pane.startRename("Journal/Welcome.md")
            var field = findChild(renameRow, "noteRenameField")
            tryCompare(field, "activeFocus", true, 1000)
            wait(250)
            saveScreenshot("visual_19_shell_04_inline_rename.png")
            keyClick(Qt.Key_Escape)

            // Collapsed panels (§13.4 Toggle Sidebar).
            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            var panels = findChild(appLoader.item, "sidePanels")
            tryCompare(panels, "visible", false, 1000)
            wait(250)
            saveScreenshot("visual_19_shell_05_panels_collapsed.png")
            keyClick(Qt.Key_Backslash, Qt.ControlModifier)

            NoteCollection.closeRoot()
            wait(100)
        }

        // Storyboard: the tag strip with chips and the
        // autocomplete popup; the sidebar tag list with counts and an
        // active filter; the merge confirmation dialog.
        function test_20_tags() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-tags"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createNote("", "Welcome") !== "")
            verify(NoteCollection.createNote("Ideas", "Reading list") !== "")
            verify(NoteCollection.createNote("Ideas", "Plans") !== "")
            verify(NoteCollection.addTag("Welcome.md", "work"))
            verify(NoteCollection.addTag("Welcome.md", "ideas"))
            verify(NoteCollection.addTag("Ideas/Reading list.md", "work"))
            verify(NoteCollection.addTag("Ideas/Reading list.md", "books"))
            verify(NoteCollection.addTag("Ideas/Plans.md", "someday"))
            NoteListModel.scope = "all"

            verify(appLoader.item.openNoteByPath("Welcome.md"))
            DocumentSerializer.loadIntoModel(BlockModel, "# Welcome to Kvit\n")
            verify(DocumentManager.save())

            // Chips + autocomplete open over the editor.
            var strip = findChild(appLoader.item, "tagStrip")
            var field = findChild(strip, "tagAddField")
            field.forceActiveFocus()
            tryCompare(field, "activeFocus", true, 1000)
            keyClick(Qt.Key_S)
            var popup = findChild(strip, "tagSuggestionsPopup")
            tryCompare(popup, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_20_tags_01_chips_and_autocomplete.png")
            keyClick(Qt.Key_Escape)

            // Sidebar tag list with counts; "work" filter active.
            NoteListModel.tagFilter = "work"
            wait(250)
            saveScreenshot("visual_20_tags_02_sidebar_filter_active.png")
            NoteListModel.tagFilter = ""

            // The merge confirmation with its blast radius.
            var mergeDialog = findChild(appLoader.item, "mergeTagDialog")
            mergeDialog.openFor("someday", "ideas")
            wait(250)
            saveScreenshot("visual_20_tags_03_merge_confirmation.png")
            mergeDialog.reject()

            NoteCollection.closeRoot()
            wait(100)
        }

        // Storyboard: the completed note list — title sort
        // with pinned notes floating; a bulk selection with its action
        // bar; a manual-order drag with the insertion indicator.
        function test_21_note_list() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-list"
            verify(NoteCollection.openRoot(root))
            var names = ["Apricot", "Blueberry", "Citrus", "Damson"]
            for (var i = 0; i < names.length; i++)
                verify(NoteCollection.createNote("", names[i]) !== "")
            verify(NoteCollection.setPinned("Damson.md", true))
            verify(NoteCollection.setFavorite("Blueberry.md", true))
            NoteListModel.scope = "all"
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true
            wait(250)
            saveScreenshot("visual_21_list_01_title_sort_pinned_floats.png")

            // Bulk selection with the action bar.
            var pane = findChild(appLoader.item, "noteListPane")
            pane.selectedPaths = ["Apricot.md", "Citrus.md"]
            wait(250)
            saveScreenshot("visual_21_list_02_bulk_selection_bar.png")
            pane.clearSelection()

            // Manual-order drag, mid-flight with the indicator (manual
            // reorder lives inside a folder scope; "" is the root).
            NoteListModel.scope = "folder"
            NoteListModel.folderPath = ""
            NoteListModel.sortMode = "manual"
            NoteListModel.ascending = true
            var listView = findChild(appLoader.item, "noteListView")
            var row0 = listView.itemAtIndex(0)
            var row2 = listView.itemAtIndex(2)
            verify(row0 !== null && row2 !== null)
            mousePress(row0, row0.width / 2, row0.height / 2)
            mouseMove(row0, row0.width / 2 - 20, row0.height / 2)
            mouseMove(row2, row2.width / 2, row2.height - 5)
            var indicator = findChild(appLoader.item, "reorderIndicator")
            tryCompare(indicator, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_21_list_03_manual_reorder_indicator.png")
            mouseRelease(row2, row2.width / 2, row2.height - 5)

            NoteCollection.closeRoot()
            wait(100)
        }

        // Storyboard: global-search results grouped under
        // note titles with bolded matches; the editor after a result
        // click — find bar open, the clicked occurrence current.
        function test_22_global_search() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-search"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createNote("", "Foxes") !== "")
            verify(NoteCollection.createNote("Ideas", "Field notes") !== "")
            verify(NoteCollection.createNote("Ideas", "Recipes") !== "")
            NoteListModel.scope = "all"

            function fill(relPath, markdown) {
                verify(appLoader.item.openNoteByPath(relPath))
                DocumentSerializer.loadIntoModel(BlockModel, markdown)
                verify(DocumentManager.save())
            }
            fill("Foxes.md",
                 "# Foxes\n\nThe quick **brown fox** jumps over the lazy dog\n")
            fill("Ideas/Field notes.md",
                 "Saw a fox near the fence today\n\nThe fox came back at dusk\n")
            fill("Ideas/Recipes.md", "Nothing furry in here\n")

            var searchField = findChild(appLoader.item, "globalSearchField")
            searchField.forceActiveFocus()
            tryCompare(searchField, "activeFocus", true, 1000)
            keyClick(Qt.Key_F)
            keyClick(Qt.Key_O)
            keyClick(Qt.Key_X)
            // 2 in Foxes.md (the heading text "Foxes" matches too) and
            // 2 in Field notes.
            tryVerify(function() { return CollectionSearch.matchCount === 4 },
                      1000)
            wait(250)
            saveScreenshot("visual_22_search_01_grouped_results.png")

            // Click through to the second occurrence in Field notes.
            appLoader.item.openSearchResult("Ideas/Field notes.md", 1, 4)
            var bar = findChild(appLoader.item, "findBar")
            tryCompare(bar, "visible", true, 1000)
            tryVerify(function() { return DocumentSearch.matchCount === 2 },
                      1000)
            wait(250)
            saveScreenshot("visual_22_search_02_opened_at_match.png")
            bar.close()

            CollectionSearch.query = ""
            searchField.text = ""
            NoteCollection.closeRoot()
            wait(100)
        }

        // Storyboard: the restore-from-backup dialog with
        // timestamped, previewed entries; the crash-recovery banner over
        // the note list.
        function test_23_backup_and_recovery() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-backup"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createNote("", "Journal") !== "")
            NoteListModel.scope = "all"

            verify(appLoader.item.openNoteByPath("Journal.md"))
            DocumentSerializer.loadIntoModel(BlockModel,
                "First draft of the journal entry\n")
            verify(DocumentManager.save())
            // Step past the rotation floor so the next save's hook backs
            // up the first draft.
            NoteCollection.setClockOffsetForTesting(11 * 60)
            DocumentSerializer.loadIntoModel(BlockModel,
                "A much better second draft\n")
            verify(DocumentManager.save())
            NoteCollection.setClockOffsetForTesting(0)

            var dialog = findChild(appLoader.item, "backupDialog")
            dialog.openForCurrentNote()
            tryCompare(dialog, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_23_backup_01_restore_dialog.png")
            dialog.reject()
            tryCompare(dialog, "visible", false, 1000)

            // A crashed session's journal → the banner.
            DocumentManager.setJournalDebounceMs(30)
            BlockModel.updateContent(0, "unsaved words typed before a crash")
            wait(300)
            NoteCollection.closeRoot()
            wait(20)
            verify(NoteCollection.openRoot(root))
            var banner = findChild(appLoader.item, "recoveryBanner")
            tryCompare(banner, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_23_backup_02_recovery_banner.png")

            var discardButton = findChild(banner, "recoveryDiscardButton")
            mouseClick(discardButton)
            DocumentManager.setJournalDebounceMs(2000)
            NoteCollection.closeRoot()
            wait(100)
        }

        // ============================================================
        // The four themes over the full shell and a formatted document.
        // Every token-driven surface — panels, note list, editor chrome,
        // the engine's inline styles, search tints — re-renders per theme.
        // ============================================================

        function test_24_themes() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-themes"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createNote("", "Style sample") !== "")
            verify(NoteCollection.createNote("Ideas", "Second note") !== "")
            NoteListModel.scope = "all"
            verify(appLoader.item.openNoteByPath("Style sample.md"))
            wait(100)
            BlockModel.updateContent(0, "Theme sample")
            BlockModel.updateType(0, 1)  // Heading 1
            BlockModel.insertBlock(1, 0, "Mixing **bold**, *italic*, "
                + "==highlight==, `code`, and a [link](https://kvit.example) "
                + "in one paragraph")
            BlockModel.insertBlock(2, 7, "A quote block keeps its accent bar")
            BlockModel.insertBlock(3, 6, "a todo item")
            BlockModel.insertBlock(4, 8, "let fox = hunt()")
            wait(200)

            var themes = ["light", "dark", "sepia"]
            for (var i = 0; i < themes.length; i++) {
                Theme.themeId = themes[i]
                wait(250)
                saveScreenshot("visual_24_theme_0" + (i + 1) + "_"
                               + themes[i] + ".png")
            }

            // "system" resolves to the OS scheme live; the frame proves
            // the mode renders as one of the two schemes above.
            Theme.themeId = "system"
            wait(250)
            saveScreenshot("visual_24_theme_04_system_"
                           + Theme.resolvedTheme + ".png")

            // Search tints on the dark editor surface: matches must
            // stay visible, the current match distinct.
            Theme.themeId = "dark"
            var bar = appLoader.item.findBar
            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(bar, "visible", true, 1000)
            keyClick(Qt.Key_I)
            keyClick(Qt.Key_N)
            tryVerify(function() { return DocumentSearch.matchCount > 0 },
                      1000)
            wait(250)
            saveScreenshot("visual_24_theme_05_dark_search_tints.png")
            bar.close()

            // Reset for whatever runs after this storyboard.
            Theme.themeId = "light"
            NoteCollection.closeRoot()
            wait(100)
        }

        // ============================================================
        // The settings dialog and the typography settings applied to a
        // live document.
        // ============================================================

        function test_25_typography_and_settings() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "Typography sample")
            BlockModel.updateType(0, 1)
            BlockModel.insertBlock(1, 0, "Body text with **bold**, "
                + "==highlight==, `inline code`, and a "
                + "[link](https://kvit.example) to preview the scale")
            BlockModel.insertBlock(2, 2, "A section heading")
            BlockModel.insertBlock(3, 8, "monospace = code()")
            wait(200)

            var dialog = findChild(appLoader.item, "settingsDialog")
            dialog.open()
            tryCompare(dialog, "opened", true, 1000)
            wait(250)
            saveScreenshot("visual_25_settings_01_appearance_page.png")

            var typographyTab = findChild(appLoader.item, "typographyTab")
            mouseClick(typographyTab)
            wait(250)
            saveScreenshot("visual_25_settings_02_typography_page.png")
            dialog.close()
            tryCompare(dialog, "visible", false, 1000)

            // A larger base size scales the whole scale coherently...
            Typography.baseSize = 20
            wait(250)
            saveScreenshot("visual_25_settings_03_base_size_20.png")

            // ...a capped content width centers the column...
            Typography.maxContentWidth = 500
            wait(250)
            saveScreenshot("visual_25_settings_04_max_width_500.png")

            // ...and an accent override recolors links and selection
            // chrome through the same tokens.
            Typography.resetToDefaults()
            Theme.accentOverride = "#c2543c"
            wait(250)
            saveScreenshot("visual_25_settings_05_custom_accent.png")

            Theme.accentOverride = ""
            wait(100)
        }

        // ============================================================
        // The resizable three-panel layout — seam
        // widths, and each panel collapsed to its expand strip.
        // ============================================================

        function test_26_panel_layout() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-panels"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createNote("", "Layout sample") !== "")
            verify(NoteCollection.createNote("Ideas", "Second") !== "")
            NoteListModel.scope = "all"
            verify(appLoader.item.openNoteByPath("Layout sample.md"))
            wait(100)
            BlockModel.updateContent(0,
                "Wide editor content with **formatting**")
            wait(200)

            // Custom widths, as a seam drag would leave them.
            appLoader.item.sidebarWidth = 280
            appLoader.item.noteListWidth = 320
            wait(250)
            saveScreenshot("visual_26_layout_01_resized_panels.png")

            appLoader.item.sidebarCollapsed = true
            wait(250)
            saveScreenshot("visual_26_layout_02_sidebar_collapsed.png")

            appLoader.item.noteListCollapsed = true
            wait(250)
            saveScreenshot("visual_26_layout_03_both_collapsed.png")

            appLoader.item.sidebarCollapsed = false
            appLoader.item.noteListCollapsed = false
            appLoader.item.sidebarWidth = 200
            appLoader.item.noteListWidth = 260
            NoteCollection.closeRoot()
            wait(100)
        }

        // ============================================================
        // The toolbar — state-reflecting formatting
        // buttons, the block dropdown, insert and customization menus —
        // and the new superscript/subscript inline types.
        // ============================================================

        function test_27_toolbar_and_supsub() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0,
                "Water is H~2~O and energy is E=mc^2^")
            BlockModel.insertBlock(1, 0,
                "Some **bold with *italic* inside** for button states")
            wait(200)

            // Caret inside the nested italic: B and I both light up.
            var ta = findTextArea(findBlockDelegate(1))
            ensureFocus(ta)
            ta.cursorPosition = 16
            wait(250)
            saveScreenshot("visual_27_toolbar_01_states_and_supsub.png")

            // The caret touching the sup span reveals its carets.
            var supTa = findTextArea(findBlockDelegate(0))
            ensureFocus(supTa)
            supTa.cursorPosition = 10   // inside "2"
            wait(250)
            saveScreenshot("visual_27_toolbar_02_sub_revealed.png")

            var insertBtn = findChild(appLoader.item, "toolbarInsertButton")
            mouseClick(insertBtn)
            wait(250)
            saveScreenshot("visual_27_toolbar_03_insert_menu.png")
            var insertMenu = findChild(appLoader.item, "toolbarInsertMenu")
            insertMenu.close()
            wait(100)

            var customizeMenu = findChild(appLoader.item,
                                          "toolbarCustomizeMenu")
            customizeMenu.popup()
            wait(250)
            saveScreenshot("visual_27_toolbar_04_customize_menu.png")
            customizeMenu.close()
            wait(100)
        }

        // ============================================================
        // The features.md §9.7 status bar (caret position,
        // selection-aware counts) and the §9.3 floating formatting bar.
        // ============================================================

        function test_28_statusbar_and_formatting_bar() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "A first paragraph up top")
            BlockModel.insertBlock(1, 0,
                "Select **a few** of these words to summon the bar")
            BlockModel.insertBlock(2, 0, "And one more block below")
            wait(200)

            // Caret position and document counts in the status bar.
            var ta = findTextArea(findBlockDelegate(1))
            ensureFocus(ta)
            ta.cursorPosition = 9
            wait(300)
            saveScreenshot("visual_28_status_01_caret_position.png")

            // A settled selection: the formatting bar floats above it
            // and the counts switch to the selection.
            ta.select(7, 22)
            var bar = findChild(appLoader.item, "formattingBar")
            tryCompare(bar, "visible", true, 2000)
            wait(300)
            saveScreenshot("visual_28_status_02_formatting_bar.png")

            // First-line selection: no room above, the bar flips below.
            var topTa = findTextArea(findBlockDelegate(0))
            ensureFocus(topTa)
            topTa.select(2, 7)
            tryCompare(bar, "visible", true, 2000)
            wait(300)
            saveScreenshot("visual_28_status_03_bar_flipped_below.png")
            topTa.deselect()
            wait(100)
        }

        // ============================================================
        // The features.md §9.5 context menus and the trash.
        // ============================================================

        function test_29_context_menus_and_trash() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0,
                "Right-click text with a [link](https://x) inside")
            BlockModel.insertBlock(1, 0, "A second block for the handle")
            wait(200)

            // The text menu over a selection.
            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.select(0, 11)
            mouseClick(ta, 30, 10, Qt.RightButton)
            var textMenu = findChild(appLoader.item, "textContextMenu")
            tryCompare(textMenu, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_29_menus_01_text_menu.png")
            textMenu.close()
            wait(100)

            // The link menu, by specificity.
            var linkPos = ta.text.indexOf("link")
            var rect = ta.positionToRectangle(linkPos + 1)
            mouseClick(ta, rect.x, rect.y + 5, Qt.RightButton)
            var linkMenu = findChild(appLoader.item, "linkContextMenu")
            tryCompare(linkMenu, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_29_menus_02_link_menu.png")
            linkMenu.close()
            wait(100)

            // The block handle menu with its Turn-into submenu.
            var delegateItem = findBlockDelegate(1)
            mouseMove(delegateItem, 30, delegateItem.height / 2)
            tryCompare(delegateItem, "isHovered", true, 1000)
            var handle = findChild(delegateItem, "dragHandle")
            tryCompare(handle, "visible", true, 1000)
            mouseClick(handle, 3, 3, Qt.RightButton)
            var blockMenu2 = findChild(appLoader.item, "blockContextMenu")
            tryCompare(blockMenu2, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_29_menus_03_block_menu.png")
            blockMenu2.close()
            wait(100)

            // Collection menus: a note row and the trash confirmation.
            var root = testCollectionDir + "/visual-menus"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createNote("", "Menu sample") !== "")
            verify(NoteCollection.createNote("", "Another note") !== "")
            NoteListModel.scope = "all"
            wait(200)

            var listView = findChild(appLoader.item, "noteListView")
            var row = listView.itemAtIndex(
                NoteListModel.rowOf("Menu sample.md"))
            mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton)
            var noteMenu = findChild(appLoader.item, "noteContextMenu")
            tryCompare(noteMenu, "visible", true, 1000)
            wait(250)
            saveScreenshot("visual_29_menus_04_note_menu.png")
            noteMenu.close()
            wait(100)

            verify(NoteCollection.deleteNote("Another note.md"))
            var dialog = findChild(appLoader.item, "emptyTrashDialog")
            dialog.openFor(NoteCollection.trashItemCount())
            tryCompare(dialog, "opened", true, 1000)
            wait(250)
            saveScreenshot("visual_29_menus_05_empty_trash_confirm.png")
            dialog.reject()
            NoteCollection.closeRoot()
            wait(100)
        }

        // ============================================================
        // The custom date-range picker over live global-search results.
        // ============================================================

        function test_30_date_range_picker() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            var root = testCollectionDir + "/visual-dates"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createNote("", "Meeting notes") !== "")
            verify(NoteCollection.createNote("", "Meeting agenda") !== "")
            NoteListModel.scope = "all"
            verify(appLoader.item.openNoteByPath("Meeting notes.md"))
            wait(100)
            BlockModel.updateContent(0, "meeting outcomes captured here")
            verify(DocumentManager.save())
            CollectionSearch.query = "meeting"
            wait(200)

            var picker = findChild(appLoader.item, "dateRangePicker")
            picker.openFor()
            tryCompare(picker, "opened", true, 1000)
            var today = new Date()
            picker.pickDay(new Date(today.getFullYear(), today.getMonth(),
                                    today.getDate() - 9))
            picker.pickDay(today)
            wait(250)
            saveScreenshot("visual_30_dates_01_picker_with_range.png")

            picker.close()
            CollectionSearch.query = ""
            CollectionSearch.datePreset = "any"
            NoteCollection.closeRoot()
            wait(100)
        }

        // Code-block syntax highlighting and chrome.
        function test_31_code_highlighting() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }

            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Code highlighting")
            BlockModel.insertBlock(1, 8,
                "def greet(name):  # say hello\n"
                + "    msg = f\"Hi {name}\"\n"
                + "    return msg  # 42 done")
            BlockModel.convertBlock(1, 8, BlockModel.getContent(1), false, "python")
            BlockModel.insertBlock(2, 8,
                "const nums = [1, 2, 3];  // a list\n"
                + "function total(xs) { return xs.reduce((a, b) => a + b, 0); }")
            BlockModel.convertBlock(2, 8, BlockModel.getContent(2), false, "javascript")
            BlockModel.insertBlock(3, 8,
                "#include <vector>\n"
                + "int main() {\n"
                + "    std::vector<int> v = {1, 2};  /* init */\n"
                + "    return 0;\n}")
            BlockModel.convertBlock(3, 8, BlockModel.getContent(3), false, "cpp")
            wait(300)
            clearFocus()

            // Each theme's highlighted Python / JS / C++ sample.
            var themes = ["light", "dark", "sepia"]
            for (var i = 0; i < themes.length; ++i) {
                Theme.themeId = themes[i]
                wait(200)
                saveScreenshot("visual_31_code_01_" + themes[i] + ".png")
            }
            Theme.themeId = "light"
            wait(150)

            // Line numbers on (view-menu toggle, persisted).
            AppSettings.setValue("view.codeLineNumbers", true)
            wait(200)
            // The gutter must actually create a row per code line (the
            // Repeater holds one Text per line, plus the Repeater itself) —
            // a screenshot alone cannot fail when the model breaks.
            var gutter = findChild(findBlockDelegate(1), "codeGutter")
            verify(gutter !== null, "code gutter exists")
            tryVerify(function() { return gutter.children.length >= 4 }, 1000,
                      "gutter creates one line-number row per code line")
            saveScreenshot("visual_31_code_02_line_numbers.png")

            // The language selector open on the Python block.
            var codeDelegate = findBlockDelegate(1)
            var langButton = findChild(codeDelegate, "codeLanguageButton")
            verify(langButton !== null, "code language button should exist")
            mouseClick(langButton)
            wait(250)
            saveScreenshot("visual_31_code_03_language_menu.png")
            // Dismiss the menu.
            keyClick(Qt.Key_Escape)
            wait(100)
            AppSettings.setValue("view.codeLineNumbers", false)

            // A long line scrolls horizontally (wrap off): the caret follows
            // to the end of a line wider than the panel.
            BlockModel.updateContent(1,
                "result = compute(alpha, beta, gamma, delta, epsilon, zeta, eta, theta)  "
                + "# a deliberately long single line that exceeds the panel width")
            var textArea = findTextArea(codeDelegate)
            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.length
            wait(250)
            saveScreenshot("visual_31_code_04_long_line_scrolled.png")

            clearFocus()
            wait(100)
        }

        // The text-color span (the HTML-subset doctrine).
        function test_32_text_color() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Text color")
            BlockModel.insertBlock(1, 0,
                "Some <span style=\"color:#e05c5c\">red text</span> and "
                + "<span style=\"color:#4a90d9\">blue</span> together.")
            // A near-miss (extra attribute) stays literal text — the doctrine.
            BlockModel.insertBlock(2, 0,
                "<span style=\"color:red\" class=\"x\">this stays literal</span>")
            wait(300)
            clearFocus()
            saveScreenshot("visual_32_color_01_rendered.png")

            // Reveal: cursor inside the red span shows its tags, muted.
            var para = findBlockDelegate(1)
            var textArea = findTextArea(para)
            ensureFocus(textArea)
            textArea.cursorPosition = textArea.text.indexOf("red") + 1
            wait(200)
            saveScreenshot("visual_32_color_02_revealed.png")

            // The toolbar color picker open over a selection.
            textArea.select(textArea.text.indexOf("together"),
                            textArea.text.indexOf("together") + 8)
            wait(150)
            var colorButton = findChild(appLoader.item, "toolbarColorButton")
            verify(colorButton !== null, "toolbar color button should exist")
            mouseClick(colorButton)
            wait(250)
            saveScreenshot("visual_32_color_03_palette.png")
            // The picker is non-focusable (it must not blur the block), so it
            // closes on an outside press rather than Escape.
            mouseClick(appLoader.item, 400, 420)
            wait(150)

            clearFocus()
            wait(100)
        }

        // Image blocks.
        function test_33_image_blocks() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Images")
            // A real image on disk (from the harness), with a caption.
            BlockModel.insertBlock(1, 11,
                "![A yellow circle|220](" + sampleImagePath + " \"A sample image\")")
            // A broken path → the placeholder.
            BlockModel.insertBlock(2, 11, "![missing](does/not/exist.png)")
            wait(600)   // async image load
            clearFocus()
            saveScreenshot("visual_33_image_01_rendered_and_placeholder.png")

            // Open the lightbox by clicking the image.
            var imgBlock = findBlockDelegate(1)
            var frame = findChild(imgBlock, "imageCaption")  // ensure it built
            verify(frame !== null, "image caption field should exist")
            // Click near the image center.
            mouseClick(imgBlock, imgBlock.width / 2, 60)
            wait(300)
            var lb = findChild(appLoader.item, "lightbox")
            verify(lb !== null && lb.shown, "lightbox should be shown")
            saveScreenshot("visual_33_image_02_lightbox.png")
            keyClick(Qt.Key_Escape)
            wait(150)

            clearFocus()
            wait(100)
        }

        // Image ingestion (the file-drop path end to end).
        function test_34_image_ingestion() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            var root = testCollectionDir + "/visual-ingest"
            verify(NoteCollection.openRoot(root))
            verify(NoteCollection.createNote("", "Ingested images") !== "")
            NoteListModel.scope = "all"
            verify(appLoader.item.openNoteByPath("Ingested images.md"))
            wait(150)

            // Simulate an OS file drop: ingest a file outside the vault, which
            // copies it into assets/ and returns a root-relative path.
            var stored = AssetStore.ingestLocalFile(sampleImagePath, "ingested-images",
                                                     root, root)
            verify(stored.indexOf("assets/") === 0)
            BlockModel.updateContent(0, "Dropped image, ingested into assets/:")
            BlockModel.insertBlock(1, 11, ImageAssets.build(stored, "", "", 0))
            wait(500)
            clearFocus()
            saveScreenshot("visual_34_ingest_01_dropped_image.png")

            NoteCollection.closeRoot()
            wait(100)
        }

        // Callouts and toggles.
        function test_35_callouts() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Callouts")
            BlockModel.insertBlock(1, 0, "")
            BlockModel.convertBlock(1, 12, "This is an info callout with **bold** text.",
                                    false, "info", "Information")
            BlockModel.insertBlock(2, 0, "")
            BlockModel.convertBlock(2, 12, "Be careful here.", false, "warning", "Warning")
            BlockModel.insertBlock(3, 0, "")
            BlockModel.convertBlock(3, 12, "It worked.", false, "success", "")
            BlockModel.insertBlock(4, 0, "")
            BlockModel.convertBlock(4, 12, "Something broke.", false, "error", "Error")
            BlockModel.insertBlock(5, 0, "")
            BlockModel.convertBlock(5, 12, "Pro tip inside.", false, "tip", "Tip")
            BlockModel.insertBlock(6, 0, "")
            // A foreign/unknown type degrades to note styling with its label.
            BlockModel.convertBlock(6, 12, "Foreign callout body.", false, "question", "")
            wait(300)
            clearFocus()
            saveScreenshot("visual_35_callouts_01_types_light.png")

            Theme.themeId = "dark"
            wait(200)
            saveScreenshot("visual_35_callouts_02_types_dark.png")
            Theme.themeId = "light"
            wait(150)

            // A toggle: collapsed then expanded.
            BlockModel.insertBlock(7, 0, "")
            BlockModel.convertBlock(7, 12, "Hidden content revealed when expanded.",
                                    true, "toggle", "Click to expand")
            wait(250)
            var lv = findChild(appLoader.item, "blockListView")
            lv.positionViewAtIndex(7, ListView.Contain)
            wait(200)
            clearFocus()
            saveScreenshot("visual_35_callouts_03_toggle_collapsed.png")

            // Expand it via the fold chevron.
            var toggle = findBlockDelegate(7)
            verify(toggle !== null, "toggle delegate exists")
            var chevron = findChild(toggle, "calloutFoldChevron")
            verify(chevron !== null, "fold chevron should exist")
            mouseClick(chevron)
            tryVerify(function() { return !BlockModel.blockAt(7).checked }, 1000)
            wait(200)
            saveScreenshot("visual_35_callouts_04_toggle_expanded.png")

            clearFocus()
            wait(100)
        }

        // Tables.
        function test_36_tables() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Tables")
            BlockModel.insertBlock(1, 0, "")
            BlockModel.convertBlock(1, 15,   // Block.Table
                "| Name | Role | Age |\n| :--- | :--- | ---: |\n"
                + "| Alice | **Lead** | 30 |\n| Bob | Dev | 25 |\n| Carol | Design | 41 |")
            wait(300)
            clearFocus()
            saveScreenshot("visual_36_tables_01_rendered.png")

            // Edit a cell (the single live cell) with reveal.
            var tbl = findBlockDelegate(1)
            var grid = findChild(tbl, "tableGrid")
            verify(grid !== null, "table grid should render")
            // Click a data cell to make it live.
            mouseClick(tbl, tbl.width / 2, 120)
            wait(200)
            saveScreenshot("visual_36_tables_02_cell_editing.png")
            keyClick(Qt.Key_Escape)
            wait(100)

            // Sort by the Age column (double-click its header).
            var stackBefore = UndoStack.count
            var contentBefore = BlockModel.getContent(1)
            tbl.sortBy(2)   // sort by column 2 (Age), one undo step
            tryVerify(function() { return BlockModel.getContent(1) !== contentBefore },
                      1000, "sort rewrites the table")
            compare(UndoStack.count, stackBefore + 1)
            wait(150)
            clearFocus()
            saveScreenshot("visual_36_tables_03_sorted.png")

            // The grid-size picker.
            BlockModel.insertBlock(2, 0, "")
            appLoader.item.insertTableIntoBlock(2)
            wait(250)
            saveScreenshot("visual_36_tables_04_grid_picker.png")
            keyClick(Qt.Key_Escape)

            clearFocus()
            wait(100)
        }

        // Todo metadata, quote attribution, nested quotes.
        function test_37_todo_meta_and_quotes() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Todos and quotes")
            // Parent todo with two sub-todos → progress 1/2.
            BlockModel.insertBlock(1, 6, "Ship the release")
            BlockModel.insertBlock(2, 6, "write the code", 1)
            BlockModel.setChecked(2, true)
            BlockModel.insertBlock(3, 6, "run the tests", 1)
            // A todo with an overdue due date and high priority.
            BlockModel.insertBlock(4, 6, "review the PR")
            BlockModel.updateContent(4, TodoMeta.build("review the PR", "2020-01-05", 2))
            // A todo with a future due date and medium priority.
            BlockModel.insertBlock(5, 6, "plan next sprint")
            BlockModel.updateContent(5, TodoMeta.build("plan next sprint", "2030-12-01", 1))
            // A nested quote (depth 2) after an outer quote.
            BlockModel.insertBlock(6, 7, "An outer quotation.")
            BlockModel.insertBlock(7, 7, "A deeper, nested reply.", 1)
            // A quote with an attribution line.
            BlockModel.insertBlock(8, 7, "To be or not to be.\n— Shakespeare")
            wait(300)
            clearFocus()
            saveScreenshot("visual_37_todo_meta_and_quotes.png")

            clearFocus()
            wait(100)
        }

        // The kanban board (a `kanban`-tagged code fence).
        function test_38_kanban() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Project board")
            BlockModel.insertBlock(1, 0, "")
            // A populated board: three columns, cards with labels, a due
            // date, a description, and a done card. The 📅 marks the due date.
            var board =
                "## To do\n"
                + "- [ ] Design the API #backend 📅 2026-08-01\n"
                + "  Sketch the endpoints and payloads\n"
                + "- [ ] Write the spec #docs\n"
                + "## In progress\n"
                + "- [ ] Build the parser #backend #urgent\n"
                + "## Done\n"
                + "- [x] Set up CI #infra"
            BlockModel.convertBlock(1, 8, board, false, "kanban")  // Block.CodeBlock=8
            wait(350)
            clearFocus()

            // The fence renders through KanbanBlock, not the code delegate.
            var kb = findBlockDelegate(1)
            verify(kb !== null, "kanban block should render")
            var focusItem = findChild(kb, "kanbanFocusItem")
            verify(focusItem !== null, "kanban delegate loaded")
            saveScreenshot("visual_38_kanban_01_board.png")

            // Add a card to "To do" as one undo step through the fence content.
            var stackBefore = UndoStack.count
            var before = BlockModel.getContent(1)
            kb.writeBoard(KanbanTools.addCard(kb.content, 0, "Triage bugs"))
            tryVerify(function() { return BlockModel.getContent(1) !== before },
                      1000, "adding a card rewrites the board")
            compare(UndoStack.count, stackBefore + 1)
            wait(200)
            clearFocus()
            saveScreenshot("visual_38_kanban_02_card_added.png")

            // Toggle a card done — the title gains a strikethrough.
            kb.writeBoard(KanbanTools.toggleCardDone(kb.content, 0, 0))
            wait(200)
            clearFocus()
            saveScreenshot("visual_38_kanban_03_card_done.png")

            // The card editor popover.
            var editor = findChild(kb, "kanbanCardEditor")
            verify(editor !== null, "card editor exists")
            editor.openFor(1, 0)   // the "Build the parser" card
            wait(250)
            saveScreenshot("visual_38_kanban_04_card_editor.png")
            editor.close()
            wait(100)

            // Column reorder via the header control (one undo step). The
            // first control found belongs to the first column; moving it
            // right swaps "To do" and "In progress".
            var kb2 = findBlockDelegate(1)
            var right = findChild(kb2, "kanbanColRight")
            verify(right !== null, "column move-right control exists")
            var colStack = UndoStack.count
            var colBefore = BlockModel.getContent(1)
            mouseClick(right, right.width / 2, right.height / 2)
            tryVerify(function() { return BlockModel.getContent(1) !== colBefore },
                      1000, "moving a column right rewrites the board")
            compare(UndoStack.count, colStack + 1)
            compare(KanbanTools.parse(BlockModel.getContent(1)).columns[0].name,
                    "In progress")
            wait(150)
            clearFocus()
            saveScreenshot("visual_38_kanban_05_column_moved.png")

            // Move a card between columns. Dragging is the primary gesture,
            // but a DragHandler drag cannot be reproduced by synthetic mouse
            // events, so the verified path is the editor's "Move to column"
            // control (same moveCard mutation, one undo step). The board is
            // now [In progress, "To do", Done]; move the sole "In progress"
            // card (col 0) to "Done" (col 2).
            var kb3 = findBlockDelegate(1)
            var editor2 = findChild(kb3, "kanbanCardEditor")
            var boardBefore = KanbanTools.parse(BlockModel.getContent(1))
            var col0Before = boardBefore.columns[0].cards.length
            var doneBefore = boardBefore.columns[2].cards.length
            editor2.openFor(0, 0)
            wait(150)
            saveScreenshot("visual_38_kanban_06_move_to_column.png")
            // The move-to buttons carry the column names; index 2 is "Done".
            editor2.moveToColumn(2)
            tryVerify(function() {
                var b = KanbanTools.parse(BlockModel.getContent(1))
                return b.columns[0].cards.length === col0Before - 1
                    && b.columns[2].cards.length === doneBefore + 1
            }, 1000, "moving a card between columns rewrites the board")
            wait(150)
            clearFocus()
            saveScreenshot("visual_38_kanban_07_card_moved.png")

            // The move is a reversible undo step. (Exact undo counts are not
            // asserted: successive updateContent edits to one block coalesce
            // under the text-change merge window, so the count
            // depends on timing, while undo/redo round-tripping does not.)
            var movedContent = BlockModel.getContent(1)
            UndoStack.undo()
            tryVerify(function() { return BlockModel.getContent(1) !== movedContent },
                      1000, "undo reverses the card move")
            UndoStack.redo()
            tryVerify(function() { return BlockModel.getContent(1) === movedContent },
                      1000, "redo re-applies the card move")

            clearFocus()
            wait(100)
        }

        // Display-math blocks rendered through MicroTeX.
        function test_39_math() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Equations")
            // Two rendered display-math blocks (Block.MathBlock == 13).
            BlockModel.insertBlock(1, 0, "")
            BlockModel.convertBlock(1, 13, "E = mc^2")
            BlockModel.insertBlock(2, 0, "")
            BlockModel.convertBlock(2, 13, "\\int_0^1 x^2\\,dx = \\frac{1}{3}")
            wait(500)   // the image://math provider renders asynchronously
            clearFocus()
            saveScreenshot("visual_39_math_01_rendered.png")

            var mb = findBlockDelegate(1)
            verify(mb !== null, "math block should render")
            var rendered = findChild(mb, "mathRenderedImage")
            verify(rendered !== null, "math rendered image exists")
            verify(String(rendered.source).indexOf("image://math/") === 0,
                   "display math uses the PNG image provider")
            var src = findChild(mb, "mathSourceArea")
            verify(src !== null, "math source editor exists")

            // Focus the first equation: source in monospace + live preview.
            mb.focusAtEnd()
            wait(500)
            var preview = findChild(mb, "mathPreviewImage")
            verify(preview !== null, "math preview image exists")
            verify(String(preview.source).indexOf("image://math/") === 0,
                   "preview math uses the PNG image provider")
            saveScreenshot("visual_39_math_02_editing_preview.png")

            // Error state: a structurally invalid expression shows source +
            // a named message, not a blank.
            src.text = "a & b"
            wait(500)
            verify(MathRenderer.errorFor("a & b") !== "",
                   "the invalid expression should report an error")
            saveScreenshot("visual_39_math_03_error.png")
            src.text = "E = mc^2"
            wait(300)
            // Move focus to the title (a text block) so both equations leave
            // edit mode and render, then blur it.
            var title = findBlockDelegate(0)
            if (title) title.focusAtEnd()
            wait(150)
            clearFocus()
            wait(200)

            // Equation numbering (a setting): numbered (1), (2) by position.
            compare(BlockModel.mathNumber(1), 1)
            compare(BlockModel.mathNumber(2), 2)
            AppSettings.setValue("view.equationNumbers", true)
            wait(400)
            clearFocus()
            saveScreenshot("visual_39_math_04_numbered.png")
            AppSettings.setValue("view.equationNumbers", false)

            clearFocus()
            wait(100)
        }

        // Local audio/video blocks over QtMultimedia.
        function test_40_media() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            if (typeof sampleAudioPath === "undefined") {
                skip("media fixtures unavailable")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            BlockModel.insertBlock(0, 1, "Media")
            // Audio, video, and a missing file (Block.Media == 14).
            BlockModel.insertBlock(1, 14,
                ImageAssets.build(sampleAudioPath, "Sample tone", "", 0))
            BlockModel.insertBlock(2, 14,
                ImageAssets.build(sampleVideoPath, "Test pattern", "", 320))
            BlockModel.insertBlock(3, 14, "![missing clip](does-not-exist.mp3)")
            wait(800)   // the FFmpeg backend opens the files and reports duration

            // Nudge the video so a frame decodes (VideoOutput is blank until
            // the first frame), then pause on it for the still.
            var video = findBlockDelegate(2)
            if (video) {
                video.togglePlay()
                wait(500)
                video.togglePlay()
                wait(200)
            }
            clearFocus()
            saveScreenshot("visual_40_media_01_blocks.png")

            var audio = findBlockDelegate(1)
            verify(audio !== null, "audio media block renders")
            var player = findChild(audio, "mediaPlayer")
            verify(player !== null, "audio block hosts a MediaPlayer")
            tryVerify(function() { return player.duration > 0 }, 4000,
                      "the backend reports the clip duration")

            // Play: state goes Playing and position advances. Audibility is out
            // of scope on WSL (no PCM device — spike c); the state and position
            // are what the feature guarantees.
            audio.togglePlay()
            tryVerify(function() { return audio.isPlaying }, 2000,
                      "playback starts")
            tryVerify(function() { return player.position > 0 }, 4000,
                      "position advances while playing")
            wait(300)
            clearFocus()
            saveScreenshot("visual_40_media_02_playing.png")
            audio.togglePlay()

            // The missing file shows the §1.2.14 fallback card, not a dead bar.
            var missing = findBlockDelegate(3)
            verify(missing !== null, "missing-media block renders")
            verify(missing.hasError === true,
                   "a missing file shows the fallback card")
            clearFocus()
            saveScreenshot("visual_40_media_03_fallback.png")

            clearFocus()
            wait(100)
        }

        // Closeout: the grown block-type catalog across the
        // slash menu and the toolbar Insert menu, including the Media group.
        function test_41_closeout_menus() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(200)
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            var menu = appLoader.item.blockMenu

            // The slash menu, grouped Basic / Lists / Advanced / Media.
            typeText("/")
            tryCompare(menu, "visible", true, 1000)
            wait(200)
            saveScreenshot("visual_41_menu_01_slash_catalog.png")

            // Filter to the wave-2 Media entry.
            typeText("video")
            wait(200)
            saveScreenshot("visual_41_menu_02_media_filtered.png")
            keyClick(Qt.Key_Escape)
            wait(150)
            clearFocus()

            // The toolbar Insert menu, now carrying the wave-2 insert types.
            var insertBtn = findChild(appLoader.item, "toolbarInsertButton")
            verify(insertBtn !== null, "toolbar Insert button exists")
            mouseClick(insertBtn, insertBtn.width / 2, insertBtn.height / 2)
            var insertMenu = findChild(appLoader.item, "toolbarInsertMenu")
            tryVerify(function() { return insertMenu && insertMenu.visible },
                      1000, "the Insert menu opens")
            wait(200)
            saveScreenshot("visual_41_menu_03_insert_menu.png")
            keyClick(Qt.Key_Escape)

            clearFocus()
            wait(100)
        }

        // The document outline and internal links.
        function test_42_outline_and_internal_links() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Project Plan\n\nJump to the [risks](#risks) section, or a "
                + "[missing](#nowhere) target.\n\n## Overview\n\nContext here.\n\n"
                + "## Milestones\n\n### Phase One\n\nWork.\n\n### Phase Two\n\n"
                + "More work.\n\n## Risks\n\nThe risk table.")
            DocumentOutline.rebuildNow()
            wait(150)

            // The outline pane beside a multi-heading document.
            appLoader.item.outlineVisible = true
            wait(200)
            clearFocus()
            saveScreenshot("visual_42_outline_01_pane.png")

            // Caret in the "Risks" section lights its outline row.
            var risks = findBlockDelegate(11)
            if (risks) risks.focusAtStart()
            wait(200)
            saveScreenshot("visual_42_outline_02_current_section.png")

            // Collapse the "Milestones" subtree (row 2).
            DocumentOutline.toggleCollapsed(2)
            wait(150)
            clearFocus()
            saveScreenshot("visual_42_outline_03_collapsed.png")
            DocumentOutline.toggleCollapsed(2)
            wait(100)

            // Internal-link jump: activating #overview scrolls there.
            appLoader.item.linkOpener.openExternally = false
            appLoader.item.linkOpener.activate("#overview")
            wait(250)
            clearFocus()
            saveScreenshot("visual_42_outline_04_internal_jump.png")

            // The unresolved [missing](#nowhere) link renders muted; focus its
            // paragraph so the source and the muted styling both show.
            var linkPara = findBlockDelegate(1)
            if (linkPara) linkPara.focusAtStart()
            wait(200)
            saveScreenshot("visual_42_outline_05_unresolved_link.png")

            appLoader.item.linkOpener.openExternally = true
            appLoader.item.outlineVisible = false
            clearFocus()
            wait(100)
        }

        // The table-of-contents block.
        function test_43_toc_block() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# User Guide\n\n```toc\n```\n\n## Installation\n\nSteps.\n\n"
                + "## Configuration\n\n### Settings\n\nDetails.\n\n## Troubleshooting\n\nHelp.")
            DocumentOutline.rebuildNow()
            wait(250)
            clearFocus()
            saveScreenshot("visual_43_toc_01_rendered.png")

            // Rename the "Configuration" heading (block 4); the TOC regenerates.
            BlockModel.updateContent(4, "Advanced Configuration")
            wait(300)
            clearFocus()
            saveScreenshot("visual_43_toc_02_after_rename.png")

            clearFocus()
            wait(100)
        }

        // Focus and typewriter modes.
        function test_44_focus_and_typewriter() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            var md = "# Chapter One\n\n"
            for (var i = 0; i < 24; i++)
                md += "This is paragraph " + i
                    + " with enough prose to fill the line comfortably.\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, md)
            wait(200)
            clearFocus()

            // Focus mode: all chrome hidden, the column centered.
            appLoader.item.focusMode = true
            wait(300)
            saveScreenshot("visual_44_focus_01_focus_mode.png")
            appLoader.item.focusMode = false
            wait(200)

            // Typewriter mode: caret line centered, surrounding blocks faded.
            appLoader.item.typewriterMode = true
            wait(150)
            var mid = findBlockDelegate(12)
            if (mid) mid.focusAtStart()
            wait(300)
            saveScreenshot("visual_44_focus_02_typewriter.png")

            // Both composed.
            appLoader.item.focusMode = true
            wait(300)
            saveScreenshot("visual_44_focus_03_composed.png")

            appLoader.item.focusMode = false
            appLoader.item.typewriterMode = false
            appLoader.item.visibility = Window.Windowed
            clearFocus()
            wait(150)
        }

        // Statistics popover and the writing-goal ring.
        function test_45_statistics_and_goal() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Field Notes\n\nThe morning was cold and the light was thin. "
                + "We walked the ridge line until the valley opened below us.\n\n"
                + "## Observations\n\nThree hawks circled the far treeline. "
                + "A fox crossed the trail without hurry.")
            wait(200)
            clearFocus()

            // Open the statistics popover from the status bar's word count.
            // Retried rather than clicked once: this scenario reaches the
            // status bar shortly after a fullscreen toggle, and the first
            // click can land while the window is still settling. A single
            // click plus an assertion made the case sensitive to anything
            // that shifted that timing by a frame — a modal popup earlier in
            // the suite was enough — which is a property of the storyboard,
            // not of the code under test.
            var wc = findChild(appLoader.item, "wordCountText")
            var panel = appLoader.item.statisticsPanel
            tryVerify(function() {
                if (panel.visible)
                    return true
                mouseClick(wc, wc.width / 2, wc.height / 2)
                return panel.visible
            }, 2000, "Clicking the word count should open the statistics panel")
            wait(250)
            saveScreenshot("visual_45_stats_01_popover.png")
            panel.close()
            wait(150)

            // A writing goal: open a collection, set a target partway met.
            var root = testCollectionDir + "/statsgoal"
            verify(NoteCollection.openRoot(root))
            var rel = NoteCollection.createNote("", "Draft")
            verify(rel !== "")
            verify(appLoader.item.openNoteByPath(rel))
            wait(200)
            BlockModel.updateContent(0,
                "This draft has a handful of words toward its target.")
            wait(250)
            NoteCollection.setGoal(rel, 20)
            wait(200)
            clearFocus()
            saveScreenshot("visual_45_stats_02_goal_partial.png")

            // Meet the goal.
            NoteCollection.setGoal(rel, 5)
            wait(200)
            clearFocus()
            saveScreenshot("visual_45_stats_03_goal_met.png")

            NoteCollection.closeRoot()
            wait(50)
        }

        // Templates.
        function test_46_templates() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            var root = testCollectionDir + "/templates"
            verify(NoteCollection.openRoot(root))
            NoteCollection.createNote("", "Welcome")
            NoteListModel.scope = "all"
            NoteTemplates.seedBuiltinsIfEmpty()
            wait(150)

            // The management dialog with the built-in templates.
            appLoader.item.templateDialog.openManage()
            wait(300)
            saveScreenshot("visual_46_templates_01_manage.png")
            appLoader.item.templateDialog.close()
            wait(150)

            // A note created from the Daily Journal template, date filled in.
            var rel = appLoader.item.createFromTemplate("Daily Journal")
            verify(rel !== "")
            wait(300)
            clearFocus()
            saveScreenshot("visual_46_templates_02_created.png")

            NoteCollection.closeRoot()
            wait(50)
        }

        // The export dialog.
        function test_47_export() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            var root = testCollectionDir + "/exportvault"
            verify(NoteCollection.openRoot(root))
            NoteCollection.createNote("", "Report")
            NoteListModel.scope = "all"
            verify(appLoader.item.openNoteByPath("Report.md"))
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Quarterly Report\n\nRevenue rose **12%** this quarter.\n\n"
                + "## Highlights\n\n- Shipped the new editor\n- Grew the team")
            wait(200)

            appLoader.item.exportDialog.openDialog()
            wait(300)
            saveScreenshot("visual_47_export_01_dialog.png")
            appLoader.item.exportDialog.close()
            wait(100)

            NoteCollection.closeRoot()
            wait(50)
        }

        // The import dialog and its dry-run summary.
        function test_48_import() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            var root = testCollectionDir + "/importvault"
            verify(NoteCollection.openRoot(root))
            NoteCollection.createNote("", "Existing")
            NoteListModel.scope = "all"
            wait(150)

            appLoader.item.importDialog.openDialog()
            wait(250)
            saveScreenshot("visual_48_import_01_dialog.png")

            // Drive a dry-run summary with a real file (the note we created).
            var dlg = appLoader.item.importDialog
            dlg.pendingKind = "files"
            dlg.pendingPaths = [NoteCollection.absolutePath("Existing.md")]
            dlg.showSummary()
            wait(250)
            saveScreenshot("visual_48_import_02_summary.png")
            var sum = findChild(appLoader.item, "importSummaryDialog")
            if (sum) sum.close()
            wait(100)
            dlg.close()

            NoteCollection.closeRoot()
            wait(50)
        }

        // Inline math.
        function test_49_inline_math() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Inline math\n\nThe relation $E = mc^2$ ties mass to energy, "
                + "and $\\frac{a}{b}$ is a fraction, with $\\sum_{i=1}^{n} i$ too.\n\n"
                + "It costs $5 and $6, so the dollars stay literal.")
            wait(600)   // the image://math provider renders asynchronously
            clearFocus()
            saveScreenshot("visual_49_math_01_inline_rendered.png")

            // Focus the math paragraph and move the caret into the first
            // equation: its $…$ source reveals while the others stay rendered.
            var para = findBlockDelegate(1)
            var ta = findTextArea(para)
            ensureFocus(ta)
            // Display text: "The relation " (13) then the transparent "E = mc^2";
            // put the caret inside the equation to reveal its $…$ source.
            ta.cursorPosition = 16
            wait(400)
            saveScreenshot("visual_49_math_02_revealed_source.png")

            clearFocus()
            wait(150)
        }

        // Embed preview cards.
        function test_50_embed() {
            if (isHeadless) {
                skip("Focus-dependent storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Reading list\n\nAn article worth a look:\n\n"
                + "![](https://example.com/great-article)\n\n"
                + "And a talk:\n\n![](https://youtube.com/watch?v=demo)")
            wait(600)   // metadata (fake fetcher) + thumbnails resolve
            clearFocus()
            saveScreenshot("visual_50_embed_01_cards.png")

            clearFocus()
            wait(150)
        }

        // ---- Block presentation, part 1 ----
        // These scenarios set presentation through the model (loading markdown
        // whose blocks carry <!--kvit ...--> tags, or calling setBlockAttributes
        // directly), so they need no keyboard focus and render reliably.

        // §9.2 alignment: a centered heading, a right-aligned paragraph, and a
        // right-aligned image, next to a left-aligned paragraph for contrast.
        function test_51_alignment() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Centered heading  <!--kvit align=center-->\n\n"
                + "This paragraph is pinned to the right margin, so its ragged "
                + "edge sits on the left.  <!--kvit align=right-->\n\n"
                + "A normal left-aligned paragraph for contrast.\n\n"
                + "![Sample|220](sample.png)  <!--kvit align=right-->")
            wait(300)
            // The tags parsed into model attributes.
            compare(BlockModel.blockAt(0).attributes, "align=center")
            compare(BlockModel.blockAt(1).attributes, "align=right")
            compare(BlockModel.blockAt(2).attributes, "")
            compare(BlockModel.blockAt(3).attributes, "align=right")
            clearFocus()
            saveScreenshot("visual_51_align_01_heading_paragraph_image.png")
        }

        // §1.2.9 divider styles: the four styles, two of them at partial width
        // and varied thickness, in one document.
        function test_52_divider_styles() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "Solid, full width\n\n"
                + "---  <!--kvit thickness=2-->\n\n"
                + "Dashed, half width\n\n"
                + "---  <!--kvit style=dashed width=50%-->\n\n"
                + "Dotted, thick\n\n"
                + "---  <!--kvit style=dotted thickness=4-->\n\n"
                + "Decorative, three-quarter width\n\n"
                + "---  <!--kvit style=decorative width=75%-->")
            wait(300)
            compare(BlockModel.blockAt(3).attributes, "style=dashed width=50%")
            compare(BlockModel.blockAt(5).attributes, "style=dotted thickness=4")
            compare(BlockModel.blockAt(7).attributes, "style=decorative width=75%")
            clearFocus()
            saveScreenshot("visual_52_divider_styles.png")
        }

        // §1.2.10 callout custom color: a callout recolored over its typed
        // accent, beside a default-colored callout for contrast.
        function test_53_callout_color() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "> [!info] Custom teal callout  <!--kvit color=#1f9e8b-->\n"
                + "> This callout is recolored over its typed accent.\n\n"
                + "> [!warning] Default warning\n"
                + "> This one keeps the typed accent for contrast.")
            wait(300)
            compare(BlockModel.blockAt(0).blockType, 12)   // Block.Callout
            compare(BlockModel.blockAt(0).attributes, "color=#1f9e8b")
            compare(BlockModel.blockAt(1).attributes, "")
            clearFocus()
            saveScreenshot("visual_53_callout_custom_color.png")

            // Reset the custom color through the model; it falls back to accent.
            BlockModel.setBlockAttributes(0, "")
            wait(150)
            compare(BlockModel.blockAt(0).attributes, "")
            saveScreenshot("visual_53_callout_reset_to_typed.png")
        }

        // ---- Block presentation, part 2 ----

        // §1.2.16 drop cap: an enlarged initial spanning three then five lines.
        function test_54_drop_cap() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "Once upon a time, in a land far beyond the reach of the "
                + "morning light, there lived a small fox who loved nothing "
                + "more than to read old books beside the slow green river.  "
                + "<!--kvit dropcap=3-->\n\n"
                + "Nevertheless the seasons turned, and the fox grew wiser with "
                + "each page it read, until the whole forest came to ask its "
                + "counsel on matters both large and small and in between.  "
                + "<!--kvit dropcap=5 dropcapcolor=#a371f7-->")
            wait(300)
            compare(BlockModel.blockAt(0).attributes, "dropcap=3")
            compare(BlockModel.blockAt(1).attributes, "dropcap=5 dropcapcolor=#a371f7")
            clearFocus()
            wait(100)
            saveScreenshot("visual_54_drop_cap.png")
        }

        // §1.2.8 image effects: rounded corners + shadow + border, beside a
        // plain image for contrast.
        function test_55_image_effects() {
            if (typeof sampleImagePath === "undefined" || sampleImagePath === "") {
                skip("sample image fixture not available")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "![Kvit|240](" + sampleImagePath + ")  <!--kvit rounded shadow border-->\n\n"
                + "![Kvit|240](" + sampleImagePath + ")")
            wait(500)   // let both images load
            compare(BlockModel.blockAt(0).attributes, "border rounded shadow")
            compare(BlockModel.blockAt(1).attributes, "")
            clearFocus()
            saveScreenshot("visual_55_image_effects.png")
        }

        // §1.2.14 configurable embed dimensions: a card sized down via width and
        // height attributes, beside a default full-width card.
        function test_56_embed_dimensions() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "![](https://example.com/article)  <!--kvit width=360 height=110-->\n\n"
                + "![](https://example.com/article)")
            wait(600)   // fake fetcher metadata + thumbnails
            compare(BlockModel.blockAt(0).blockType, 11)   // Image → embed by content
            compare(BlockModel.blockAt(0).attributes, "height=110 width=360")
            clearFocus()
            saveScreenshot("visual_56_embed_dimensions.png")
        }

        // ---- The discoverable shortcut reference ----
        function test_57_shortcut_reference() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            appLoader.item.openShortcutReference()
            wait(300)
            var ref = findChild(appLoader.item, "shortcutReference")
            verify(ref !== null, "shortcut reference dialog exists")
            tryVerify(function() { return ref.visible }, 1000,
                      "shortcut reference is shown")
            saveScreenshot("visual_57_shortcut_reference.png")
            ref.close()
            wait(150)
        }

        // §14.1 keyboard focus ring: the focused block shows a focus-ring
        // indicator. Best-effort capture (focus may not establish under WSLg);
        // the indicator's existence is asserted focus-independently.
        function test_58_focus_ring() {
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)
            BlockModel.insertBlock(0, 1, "Keyboard focus")
            BlockModel.insertBlock(1, 0,
                "This paragraph shows the focus-ring indicator when focused.")
            BlockModel.insertBlock(2, 0, "A neighbouring block for contrast.")
            wait(200)
            var d = findBlockDelegate(1)
            verify(d !== null)
            verify(findChild(d, "focusIndicator") !== null,
                   "block carries a focus indicator")
            var ta = findTextArea(d)
            if (ta) ta.forceActiveFocus()
            wait(300)
            saveScreenshot("visual_58_focus_ring.png")
        }

        // §14.3 high-contrast theme: the shell rendered in the high-contrast
        // token table (black ground, white text, bright accents, strong edges).
        function test_59_high_contrast() {
            DocumentManager.newDocument()
            wait(150)
            DocumentSerializer.loadIntoModel(BlockModel,
                "# High contrast\n\n"
                + "Body text stays **white on black** for the AAA contrast floor.\n\n"
                + "- A bulleted item\n- Another item\n\n"
                + "> [!warning] A callout keeps a bright, legible accent\n"
                + "> with strong borders around every structural edge.\n\n"
                + "---  <!--kvit style=dashed-->\n\n"
                + "`inline code` and a [link](https://example.com) stay legible.")
            wait(250)
            Theme.themeId = "highContrast"
            wait(250)
            compare(Theme.resolvedTheme, "highContrast")
            clearFocus()
            saveScreenshot("visual_59_high_contrast.png")
            Theme.themeId = "light"   // restore for later scenarios
            wait(100)
        }

        // §15.1 quick capture: the capture window with jotted text, then the
        // captured note landing in the collection.
        function test_60_quick_capture() {
            Theme.themeId = "light"
            var root = testCollectionDir + "/capshots"
            NoteCollection.openRoot(root)
            NoteCollection.createNote("", "Existing")
            NoteListModel.scope = "all"
            wait(150)
            appLoader.item.openQuickCapture()
            wait(250)
            var qc = findChild(appLoader.item, "quickCaptureWindow")
            verify(qc !== null, "quick capture window exists")
            var ta = findChild(qc, "quickCaptureText")
            ta.text = "Buy milk and eggs\nCall the dentist tomorrow morning"
            ta.forceActiveFocus()
            wait(200)
            // The capture window is a separate top-level Window, so grab its own
            // contentItem rather than the main window's.
            var capImg = grabImage(qc.contentItem)
            capImg.save(screenshotDir + "/visual_60_quick_capture_window.png")
            qc.save()
            wait(300)
            // The captured note now sits in the collection.
            var found = false
            var paths = NoteCollection.noteRelPaths()
            for (var i = 0; i < paths.length; i++)
                if (paths[i].indexOf("Buy milk and eggs") !== -1) found = true
            verify(found, "captured note landed in the collection")
            saveScreenshot("visual_60_captured_note_in_list.png")
        }

        // §12.1 external-change conflict banner: the open dirty note changed on
        // disk, offering keep-mine / load-theirs.
        function test_61_conflict_banner() {
            Theme.themeId = "light"
            var root = testCollectionDir + "/conflictshots"
            NoteCollection.openRoot(root)
            NoteCollection.createNote("", "Shared")
            NoteListModel.scope = "all"
            wait(150)
            verify(appLoader.item.openNoteByPath("Shared.md"))
            wait(150)
            BlockModel.updateContent(0,
                "My in-progress edit that has not been saved yet.")
            wait(80)
            verify(DocumentManager.isDirty)
            FileWatcher.noteChangedExternally(DocumentManager.currentFilePath)
            wait(150)
            var banner = findChild(appLoader.item, "conflictBanner")
            verify(banner !== null && banner.visible, "conflict banner shown")
            clearFocus()
            saveScreenshot("visual_61_conflict_banner.png")
            appLoader.item.keepMine()
            wait(100)
        }

        // Math render experiment canaries: exact inline baseline case, an
        // inline-dense paragraph, display math, and light/dark theme captures.
        function test_62_math_render_canaries() {
            if (isHeadless) {
                skip("Storyboard requires display")
            }
            Theme.themeId = "light"
            DocumentManager.newDocument()
            wait(150)

            var stress = "Inline stress"
            for (var i = 0; i < 10; i++)
                stress += " $x_" + i + "^2$"

            DocumentSerializer.loadIntoModel(BlockModel,
                "# Math render canaries\n\n"
                + "Text before $x^2$ text after\n\n"
                + stress + "\n\n"
                + "Fraction inline $\\frac{a}{b}$ text after\n\n"
                + "Sum inline $\\sum_{i=0}^n i^2$ text after\n\n"
                + "$$\n\\left(\\sum_{i=0}^{n} x_i\\right)^2\n$$")
            wait(600)
            clearFocus()

            var para = findBlockDelegate(1)
            verify(para !== null, "inline canary paragraph exists")
            compare(para.inlineMathBoxes.length, 1)
            var ta = findTextArea(para)
            verify(ta !== null, "inline canary TextArea exists")
            var box = para.inlineMathBoxes[0]
            var r1 = ta.positionToRectangle(box.docStart)
            var r2 = ta.positionToRectangle(box.docEnd)
            var metrics = MathRenderer.measure(box.tex, para.inlineMathPixelSize)
            var reservedWidth = Math.abs(r2.x - r1.x)
            verify(Math.abs(reservedWidth - metrics.width) <= 3,
                   "inline math reservation should match renderer width: "
                   + reservedWidth.toFixed(2) + " vs " + metrics.width.toFixed(2))
            console.log("MATH_CANARY x^2 reservedRect="
                        + Math.round(r1.x) + "," + Math.round(r1.y)
                        + " to " + Math.round(r2.x) + "," + Math.round(r2.y)
                        + " lineHeight=" + Math.round(r1.height)
                        + " metrics=" + Math.round(metrics.width) + "x"
                        + Math.round(metrics.height)
                        + " baseline=" + metrics.baseline.toFixed(2)
                        + " depth=" + metrics.depth.toFixed(2))

            verifyInlineMathNativeBaseline(findBlockDelegate(3), "fraction")
            verifyInlineMathNativeBaseline(findBlockDelegate(4), "sum")

            saveScreenshot("visual_62_math_canary_01_light.png")
            Theme.themeId = "dark"
            wait(250)
            clearFocus()
            saveScreenshot("visual_62_math_canary_02_dark.png")
            Theme.themeId = "light"
            wait(100)
        }
    }
}
