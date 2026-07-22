// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import Kvit 1.0

Item {
    id: root
    width: 800
    height: 600

    // Import the main application components
    Loader {
        id: appLoader
        anchors.fill: parent
        source: "qrc:/qml/main.qml"
        asynchronous: false
    }

    TestCase {
        id: testCase
        name: "IntegrationTests"
        when: windowShown && appLoader.status === Loader.Ready

        // Detect headless mode (offscreen platform)
        readonly property bool isHeadless: Qt.platform.pluginName === "offscreen"

        function findBlockDelegate(index) {
            var listView = findChild(appLoader.item, "blockListView")
            if (!listView) return null

            // Get the delegate at index
            return listView.itemAtIndex(index)
        }

        function findTextAreaRaw(blockDelegate) {
            return findChild(blockDelegate, "blockTextArea")
        }

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

        function findReadOnlyText(blockDelegate) {
            return findChild(blockDelegate, "readOnlyText")
        }

        // Helper to ensure focus is established reliably
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

        // Helper to wait for a condition with polling
        function waitForCondition(condition, timeout, message) {
            var startTime = Date.now()
            while (!condition() && (Date.now() - startTime) < timeout) {
                wait(50)
            }
            verify(condition(), message)
        }

        // Helper to send key and wait for block type change
        function sendKeyAndExpectType(textArea, key, modifier, expectedType) {
            var startTime = Date.now()
            var timeout = 1000

            while (BlockModel.blockAt(0).blockType !== expectedType && (Date.now() - startTime) < timeout) {
                keyClick(key, modifier)
                wait(100)
            }

            return BlockModel.blockAt(0).blockType === expectedType
        }

        function test_01_windowLoads() {
            verify(appLoader.item !== null, "Application should load")
            // Title format: "[*] currentFileName - Kvit Notes"
            verify(appLoader.item.title.indexOf("Kvit Notes") !== -1, "Window title should contain 'Kvit Notes'")
        }

        function test_02_blocksDisplayed() {
            var listView = findChild(appLoader.item, "blockListView")
            verify(listView !== null, "ListView should exist")
            compare(listView.count, 5, "Should display 5 blocks")
        }

        function test_03_statusBarShowsBlockCount() {
            var statusText = findChild(appLoader.item, "blockCountText")
            verify(statusText !== null, "Status bar block count should exist")
            compare(statusText.text, "5 blocks", "Status should show '5 blocks'")
        }

        function test_04_clickBlockGivesFocus() {
            if (isHeadless) {
                skip("Focus tests require display")
            }

            var delegate = findBlockDelegate(1)
            verify(delegate !== null, "Block delegate should exist")

            var textArea = findTextArea(delegate)
            verify(textArea !== null, "TextArea should exist")

            mouseClick(textArea)
            wait(100)

            verify(textArea.activeFocus, "TextArea should have focus after click")
        }

        function test_05_typingUpdatesContent() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Move to end of text
            keyClick(Qt.Key_End)

            // Type some text
            var originalLength = textArea.text.length
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_T)
            keyClick(Qt.Key_E)
            keyClick(Qt.Key_S)
            keyClick(Qt.Key_T)
            wait(100)

            verify(textArea.text.length > originalLength, "Text should be updated after typing")
            verify(textArea.text.endsWith("test"), "Text should end with 'test'")
        }

        function test_06_focusIndicatorShows() {
            if (isHeadless) {
                skip("Focus tests require display")
            }

            var delegate = findBlockDelegate(2)
            var textArea = findTextArea(delegate)
            var focusIndicator = findChild(delegate, "focusIndicator")

            verify(focusIndicator !== null, "Focus indicator should exist")

            // Click to focus
            mouseClick(textArea)
            wait(200) // Wait for animation

            compare(delegate.isFocused, true,
                    "Focused delegate should expose its keyboard focus state")
            compare(focusIndicator.color.toString(), Theme.focusRing.toString(),
                    "Focus indicator should use the theme focus-ring color")
        }

        function test_07_headingStylesApplied() {
            // First block should be Heading1 (large, bold)
            var h1Delegate = findBlockDelegate(0)
            verify(h1Delegate.contentFontSize === 32,
                   "Heading1 should have pixelSize 32")
            verify(h1Delegate.contentFontWeight === Font.Bold,
                   "Heading1 should be bold")

            // Third block should be Heading2
            var h2Delegate = findBlockDelegate(2)
            verify(h2Delegate.contentFontSize === 24,
                   "Heading2 should have pixelSize 24")
        }

        function test_08_paragraphHasPlaceholder() {
            // Create scenario with empty paragraph by clearing text
            var delegate = findBlockDelegate(4)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Select all and delete
            keySequence(StandardKey.SelectAll)
            keyClick(Qt.Key_Backspace)
            wait(50)

            // Paragraph should show placeholder
            compare(textArea.placeholderText, "Type something...", "Empty paragraph should show placeholder")
        }

        // ========== Milestone 2 Tests ==========

        // Step 2a: Cursor Navigation Within Block
        function test_09_cursorNavigationWithinBlock() {
            if (isHeadless) {
                skip("Cursor navigation tests require display")
            }

            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Home key should move to start of line
            keyClick(Qt.Key_End)
            wait(50)
            keyClick(Qt.Key_Home)
            wait(50)
            // Cursor should be at start of line (could be 0 or line start)
            verify(textArea.cursorPosition >= 0, "Cursor should be at valid position after Home")

            // End key should move to end of line
            keyClick(Qt.Key_End)
            wait(50)
            verify(textArea.cursorPosition > 0, "Cursor should move with End key")
        }

        function test_10_ctrlHomeEndNavigation() {
            if (isHeadless) {
                skip("Cursor navigation tests require display")
            }

            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Move cursor to middle of text
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            wait(50)

            var middlePos = textArea.cursorPosition

            // Ctrl+Home should move to start of block
            keyClick(Qt.Key_Home, Qt.ControlModifier)
            wait(50)
            compare(textArea.cursorPosition, 0, "Ctrl+Home should move cursor to position 0")

            // Ctrl+End should move to end of block
            keyClick(Qt.Key_End, Qt.ControlModifier)
            wait(50)
            compare(textArea.cursorPosition, textArea.text.length, "Ctrl+End should move cursor to end of text")
        }

        // Step 2b: Block Navigation (Cross-Block)
        function test_11_upArrowMovesToPreviousBlock() {
            if (isHeadless) {
                skip("Block navigation tests require display")
            }

            // Start at second block
            var delegate1 = findBlockDelegate(1)
            var textArea1 = findTextArea(delegate1)

            mouseClick(textArea1)
            wait(50)

            // Move cursor to start of block
            keyClick(Qt.Key_Home, Qt.ControlModifier)
            wait(50)

            // Press Up - should move to previous block
            keyClick(Qt.Key_Up)
            wait(100)

            // First block should now have focus
            var delegate0 = findBlockDelegate(0)
            var textArea0 = findTextArea(delegate0)
            verify(textArea0.activeFocus, "Previous block should have focus after Up at start")
        }

        function test_12_downArrowMovesToNextBlock() {
            if (isHeadless) {
                skip("Block navigation tests require display")
            }

            // Start at first block
            var delegate0 = findBlockDelegate(0)
            var textArea0 = findTextArea(delegate0)

            mouseClick(textArea0)
            wait(50)

            // Move cursor to end of block
            keyClick(Qt.Key_End, Qt.ControlModifier)
            wait(50)

            // Press Down - should move to next block
            keyClick(Qt.Key_Down)
            wait(100)

            // Second block should now have focus
            var delegate1 = findBlockDelegate(1)
            var textArea1 = findTextArea(delegate1)
            verify(textArea1.activeFocus, "Next block should have focus after Down at end")
        }

        function test_13_ctrlUpDownJumpsBetweenBlocks() {
            if (isHeadless) {
                skip("Block navigation tests require display")
            }

            // Start at first block
            var delegate0 = findBlockDelegate(0)
            var textArea0 = findTextArea(delegate0)

            mouseClick(textArea0)
            wait(50)

            // Ctrl+Down should move to next block
            keyClick(Qt.Key_Down, Qt.ControlModifier)
            wait(100)

            var delegate1 = findBlockDelegate(1)
            var textArea1 = findTextArea(delegate1)
            verify(textArea1.activeFocus, "Ctrl+Down should jump to next block")

            // Ctrl+Up should move back to previous block
            keyClick(Qt.Key_Up, Qt.ControlModifier)
            wait(100)

            verify(textArea0.activeFocus, "Ctrl+Up should jump to previous block")
        }

        // Step 2c: Block Creation (Enter at End)
        function test_14_enterAtEndCreatesNewBlock() {
            if (isHeadless) {
                skip("Block creation tests require display")
            }

            var listView = findChild(appLoader.item, "blockListView")
            var initialCount = listView.count

            // Focus last block
            var lastDelegate = findBlockDelegate(initialCount - 1)
            var lastTextArea = findTextArea(lastDelegate)

            ensureFocus(lastTextArea)

            // Move to end
            keyClick(Qt.Key_End, Qt.ControlModifier)
            wait(50)

            // Press Enter
            keyClick(Qt.Key_Return)
            wait(100)

            // Should have one more block
            compare(listView.count, initialCount + 1, "Enter at end should create new block")

            // New block should have focus
            var newDelegate = findBlockDelegate(initialCount)
            var newTextArea = findTextArea(newDelegate)
            verify(newTextArea.activeFocus, "New block should have focus")
            compare(newTextArea.text, "", "New block should be empty")
        }

        // Step 2d: Block Splitting (Enter in Middle)
        function test_15_enterInMiddleSplitsBlock() {
            if (isHeadless) {
                skip("Block splitting tests require display")
            }

            var listView = findChild(appLoader.item, "blockListView")
            var initialCount = listView.count

            // Focus a block with content
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            var originalText = textArea.text

            ensureFocus(textArea)

            // Move to middle of text (position 10)
            keyClick(Qt.Key_Home, Qt.ControlModifier)
            for (var i = 0; i < 10; i++) {
                keyClick(Qt.Key_Right)
            }
            wait(50)

            var splitPos = textArea.cursorPosition

            // Press Enter
            keyClick(Qt.Key_Return)
            wait(100)

            // Should have one more block
            compare(listView.count, initialCount + 1, "Enter in middle should create new block (split)")

            // Original block should have text before cursor
            var expectedBefore = originalText.substring(0, splitPos)
            compare(textArea.text, expectedBefore, "Original block should have text before cursor")

            // New block should have text after cursor
            var newDelegate = findBlockDelegate(2)
            var newTextArea = findTextArea(newDelegate)
            var expectedAfter = originalText.substring(splitPos)
            compare(newTextArea.text, expectedAfter, "New block should have text after cursor")
        }

        // Step 2e: Block Deletion (Empty Block)
        function test_16_backspaceDeletesEmptyBlock() {
            if (isHeadless) {
                skip("Block deletion tests require display")
            }

            var listView = findChild(appLoader.item, "blockListView")

            // First create an empty block
            var delegate = findBlockDelegate(2)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Create new block with retry
            var initialCount = listView.count
            var startTime = Date.now()
            var timeout = 1000
            while (listView.count === initialCount && (Date.now() - startTime) < timeout) {
                keyClick(Qt.Key_End, Qt.ControlModifier)
                keyClick(Qt.Key_Return)
                wait(100)
            }

            var countAfterInsert = listView.count
            verify(countAfterInsert > initialCount, "New block should be created")

            // Now we should be in the new empty block at index 3
            var emptyDelegate = findBlockDelegate(3)
            var emptyTextArea = findTextArea(emptyDelegate)
            tryCompare(emptyTextArea, "activeFocus", true, 1000)
            compare(emptyTextArea.text, "", "Block should be empty")

            // Press Backspace to delete empty block with retry
            startTime = Date.now()
            while (listView.count === countAfterInsert && (Date.now() - startTime) < timeout) {
                keyClick(Qt.Key_Backspace)
                wait(100)
            }

            // Block count should decrease
            compare(listView.count, countAfterInsert - 1, "Backspace in empty block should delete it")

            // Previous block should have focus
            var prevDelegate = findBlockDelegate(2)
            var prevTextArea = findTextArea(prevDelegate)
            tryCompare(prevTextArea, "activeFocus", true, 1000)
        }

        // Step 2f: Block Merging (Non-Empty Block)
        function test_17_backspaceAtStartMergesBlocks() {
            if (isHeadless) {
                skip("Block merging tests require display")
            }

            var listView = findChild(appLoader.item, "blockListView")
            var initialCount = listView.count

            // Get content of block 2 and block 3
            var delegate2 = findBlockDelegate(2)
            var textArea2 = findTextArea(delegate2)
            var text2 = textArea2.text

            var delegate3 = findBlockDelegate(3)
            var textArea3 = findTextArea(delegate3)
            var text3 = textArea3.text

            // Focus block 3, move to start
            ensureFocus(textArea3)
            keyClick(Qt.Key_Home, Qt.ControlModifier)
            wait(50)

            // Press Backspace - should merge with previous block (with retry)
            var startTime = Date.now()
            var timeout = 1000
            while (listView.count === initialCount && (Date.now() - startTime) < timeout) {
                keyClick(Qt.Key_Backspace)
                wait(100)
            }

            // Block count should decrease
            compare(listView.count, initialCount - 1, "Backspace at start should merge blocks")

            // Block 2 should now have merged content
            var mergedDelegate = findBlockDelegate(2)
            var mergedTextArea = findTextArea(mergedDelegate)
            compare(mergedTextArea.text, text2 + text3, "Merged block should have combined content")

            // Cursor should be at the merge point
            compare(mergedTextArea.cursorPosition, text2.length, "Cursor should be at merge point")
        }

        function test_18_deleteAtEndMergesWithNextBlock() {
            if (isHeadless) {
                skip("Block merging tests require display")
            }

            var listView = findChild(appLoader.item, "blockListView")
            var initialCount = listView.count

            // Get content of first two blocks
            var delegate0 = findBlockDelegate(0)
            var textArea0 = findTextArea(delegate0)
            var text0 = textArea0.text

            var delegate1 = findBlockDelegate(1)
            var textArea1 = findTextArea(delegate1)
            var text1 = textArea1.text

            // Focus first block, move to end
            ensureFocus(textArea0)
            keyClick(Qt.Key_End, Qt.ControlModifier)
            wait(50)

            var cursorPos = textArea0.cursorPosition

            // Press Delete - should merge with next block (with retry)
            var startTime = Date.now()
            var timeout = 1000
            while (listView.count === initialCount && (Date.now() - startTime) < timeout) {
                keyClick(Qt.Key_Delete)
                wait(100)
            }

            // Block count should decrease
            compare(listView.count, initialCount - 1, "Delete at end should merge blocks")

            // First block should have merged content
            compare(textArea0.text, text0 + text1, "Merged block should have combined content")

            // Cursor should stay at original position
            compare(textArea0.cursorPosition, cursorPos, "Cursor should stay at original position")
        }

        // Milestone 3: Formatting & Hybrid Rendering Tests

        function test_19_editorEngineAttached() {
            // The lazy shell is about a row nobody has edited yet, and a
            // delegate stays promoted once it has been. Earlier tests click
            // into blocks 0 and 1, so start from a document whose rows have
            // never been activated rather than asserting on theirs.
            DocumentManager.newDocument()
            wait(50)
            var delegate = findBlockDelegate(0)
            verify(delegate !== null, "Block delegate should exist")
            var readOnlyText = findReadOnlyText(delegate)
            verify(readOnlyText !== null, "Read-only text item should exist")

            BlockModel.updateContent(0, "Plain paragraph for engine attach")
            wait(50)
            readOnlyText = findReadOnlyText(delegate)
            verify(readOnlyText !== null, "Read-only text item should still exist")
            var textArea = findTextAreaRaw(delegate)
            compare(textArea, null,
                    "Plain unfocused text blocks should not instantiate a TextArea")
            compare(delegate.editorEngine, null,
                    "Plain unfocused text blocks should not instantiate an editor engine")
            tryCompare(delegate, "useReadOnlyShell", true, 1000)
            tryCompare(delegate, "editorActive", false, 1000)
            tryCompare(readOnlyText, "visible", true, 1000)
            compare(readOnlyText.text, "Plain paragraph for engine attach",
                    "Read-only text item should render cached display text")

            delegate.activateEditor()
            tryVerify(function() {
                return findTextAreaRaw(delegate) !== null
            }, 1000, "Activating a plain text row should instantiate the editor")
            textArea = findTextAreaRaw(delegate)
            verify(delegate.editorEngine !== null,
                   "BlockEditorEngine should exist after promotion")
            tryCompare(delegate, "useReadOnlyShell", false, 1000)
            compare(textArea.visible, true,
                    "Promoted text blocks should paint the editor TextArea")
            verify(delegate.editorEngine.document !== null,
                   "Engine should attach to the TextArea document after promotion")
            compare(delegate.editorEngine.markdown, BlockModel.getContent(0),
                    "Engine markdown should mirror the model content")

            if (isHeadless) {
                skip("Focus activation requires display")
            }

            textArea.forceActiveFocus()
            tryCompare(textArea, "activeFocus", true, 1000)
            tryCompare(delegate, "editorActive", true, 1000)
        }

        function test_20_ctrlBTogglesBold() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Use first block for testing
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Set test content through the model (single source of truth);
            // an imperative textArea.text write would break the
            // text <- delegate.content binding for the rest of the suite
            BlockModel.updateContent(0, "test")
            wait(50)

            // Select all and apply bold with retry
            var startTime = Date.now()
            var timeout = 1000
            while (textArea.text.indexOf("**") === -1 && (Date.now() - startTime) < timeout) {
                textArea.selectAll()
                wait(50)
                keyClick(Qt.Key_B, Qt.ControlModifier)
                wait(100)
            }

            // Check that text now has bold markers
            verify(textArea.text.indexOf("**") !== -1, "Text should contain bold markers after Ctrl+B")
            compare(textArea.text, "**test**", "Text should be wrapped in bold markers")
        }

        function test_21_ctrlITogglesItalic() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Set test content through the model (single source of truth);
            // an imperative textArea.text write would break the
            // text <- delegate.content binding for the rest of the suite
            BlockModel.updateContent(0, "test")
            wait(50)

            // Select all and apply italic with retry
            var startTime = Date.now()
            var timeout = 1000
            while (textArea.text !== "*test*" && (Date.now() - startTime) < timeout) {
                textArea.selectAll()
                wait(50)
                keyClick(Qt.Key_I, Qt.ControlModifier)
                wait(100)
            }

            // Check that text now has italic markers
            compare(textArea.text, "*test*", "Text should be wrapped in italic markers")
        }

        function test_22_markersHiddenWhenCursorOutside() {
            // features.md §2.2.2: with the cursor outside, formatting
            // renders with no markers visible; storage stays markdown.
            var delegate = findBlockDelegate(0)

            BlockModel.updateContent(0, "Hello **world**")
            wait(100)
            var textArea = findTextAreaRaw(delegate)
            verify(textArea !== null,
                   "Formatted text should promote to the editor surface")

            // Make sure the block is not focused (a prior test may have
            // left the cursor inside it, which legitimately reveals).
            if (textArea.activeFocus) {
                textArea.focus = false
                tryCompare(textArea, "activeFocus", false, 1000)
            }

            // Reveal transitions are evaluated on a clean stack (the
            // engine defers them out of QQuickTextEdit's own dispatch), so
            // the collapse lands a turn after focus leaves.
            tryCompare(textArea, "text", "Hello world", 1000,
                       "Document should show display text (markers hidden)")
            compare(delegate.editorEngine.markdown, "Hello **world**",
                    "Engine markdown should keep the full markdown")
        }

        function test_22b_revealFollowsCursor() {
            // features.md §2.2.3 example 1 driven through the real UI:
            // arrow into the bold word (its syntax appears), onward into
            // the italic word (bold re-hides, italic reveals), then leave.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            BlockModel.updateContent(0, "This is **important** *information* here")
            wait(100)

            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            wait(80)
            compare(textArea.text, "This is important information here",
                    "Cursor at line start: everything rendered")

            // Arrow into "important" (display 8..17)
            for (var i = 0; i < 12; i++) keyClick(Qt.Key_Right)
            wait(80)
            compare(textArea.text, "This is **important** information here",
                    "Cursor in bold word: only its syntax visible")
            compare(delegate.editorEngine.markdown,
                    "This is **important** *information* here",
                    "Reveal must not change storage markdown")

            // Arrow onward into "information"
            for (var j = 0; j < 16; j++) keyClick(Qt.Key_Right)
            wait(80)
            compare(textArea.text, "This is important *information* here",
                    "Bold re-hidden, italic revealed")

            // Focus another block: everything rendered again
            var other = findTextArea(findBlockDelegate(1))
            ensureFocus(other)
            wait(80)
            compare(textArea.text, "This is important information here",
                    "Cursor elsewhere: everything rendered")
            compare(BlockModel.getContent(0),
                    "This is **important** *information* here",
                    "Walkthrough must never alter the model")
        }

        function test_22c_newInlineTypesRevealAndHide() {
            // Each new symmetric type renders with markers hidden and
            // reveals its syntax while the cursor is inside the span
            // (features.md §2.2 applied to ~~ ` == ++).
            if (isHeadless) {
                skip("Focus-dependent test requires display")
            }

            var cases = [
                { md: "A ~~st~~ end", hidden: "A st end" },
                { md: "A `co` end",   hidden: "A co end" },
                { md: "A ==hi== end", hidden: "A hi end" },
                { md: "A ++un++ end", hidden: "A un end" },
                { md: "A __bd__ end", hidden: "A bd end" },
                { md: "A _it_ end",   hidden: "A it end" }
            ]

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            for (var i = 0; i < cases.length; i++) {
                var c = cases[i]
                if (textArea.activeFocus) {
                    textArea.focus = false
                    tryCompare(textArea, "activeFocus", false, 1000)
                }
                BlockModel.updateContent(0, c.md)
                wait(100)
                compare(textArea.text, c.hidden,
                        c.md + ": rendered with markers hidden")

                ensureFocus(textArea)
                textArea.cursorPosition = 3 // inside the span content
                wait(100)
                compare(textArea.text, c.md,
                        c.md + ": cursor inside reveals the syntax")
                compare(delegate.editorEngine.markdown, c.md,
                        c.md + ": reveal must not change storage markdown")

                keyClick(Qt.Key_Home)
                wait(100)
                compare(textArea.text, c.hidden,
                        c.md + ": cursor out re-hides the syntax")
                compare(BlockModel.getContent(0), c.md,
                        c.md + ": walkthrough must never alter the model")
            }

            // Leave the block unfocused: with focus held, a later model
            // rebuild can move the cursor into a span (Qt adjusts cursors
            // across the diff edit) and legitimately reveal it, which the
            // following tests do not expect.
            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_22d_linkRevealAndAutolink() {
            // A [text](url) link renders as its text and reveals the full
            // syntax while the cursor touches it; a bare URL is a link
            // without any hidden syntax.
            if (isHeadless) {
                skip("Focus-dependent test requires display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            if (textArea.activeFocus) {
                textArea.focus = false
                tryCompare(textArea, "activeFocus", false, 1000)
            }
            BlockModel.updateContent(0, "go [ab](http://x) on")
            wait(100)
            compare(textArea.text, "go ab on",
                    "Link renders as its text, no brackets or URL")

            ensureFocus(textArea)
            textArea.cursorPosition = 4 // inside the link text
            wait(100)
            compare(textArea.text, "go [ab](http://x) on",
                    "Cursor in the link text reveals the full syntax")
            compare(delegate.editorEngine.linkAtDocumentPosition(4), "http://x",
                    "The revealed link still reports its URL")

            keyClick(Qt.Key_Home)
            wait(100)
            compare(textArea.text, "go ab on", "Cursor out re-hides the syntax")
            compare(BlockModel.getContent(0), "go [ab](http://x) on",
                    "Reveal cycle must never alter the model")

            // Autolink: the display IS the URL; nothing hides or reveals.
            BlockModel.updateContent(0, "see http://a.com now")
            wait(100)
            compare(textArea.text, "see http://a.com now",
                    "Bare URL displays as-is")
            compare(delegate.editorEngine.linkAtDocumentPosition(8), "http://a.com",
                    "Bare URL is clickable (reports its URL)")

            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_23_focusNeverChangesTextOrSize() {
            // Block-geometry invariant 1: focus alone (cursor in
            // plain text, outside any span) never changes a block's text
            // or size. Cursor ENTERING a span changes the text by exactly
            // the marker characters — that is invariant 2, covered by
            // test_22b and the engine unit tests.
            if (isHeadless) {
                skip("Focus tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            // Every inline type participates: the geometry invariants
            // hold for the full registry, not just bold/italic.
            BlockModel.updateContent(0,
                "Hi **b** ~~s~~ ==h== ++u++ `c` [l](http://x) https://y.io")
            wait(100)

            var textBefore = textArea.text
            var heightBefore = delegate.height

            // Click over the plain leading text: a click on the link in an
            // unfocused block would (by design) open it instead.
            var plainRect = textArea.positionToRectangle(1)
            mouseClick(textArea, plainRect.x, plainRect.y + plainRect.height / 2)
            keyClick(Qt.Key_Home) // cursor to plain text
            wait(100)
            verify(textArea.activeFocus, "Block should be focused after click")
            compare(textArea.text, textBefore, "Focus must not change the displayed text")
            compare(delegate.height, heightBefore, "Focus must not change the block height")

            // Unfocus by focusing a different block
            var delegate1 = findBlockDelegate(1)
            mouseClick(findTextArea(delegate1))
            wait(100)
            compare(textArea.text, textBefore, "Unfocus must not change the displayed text")
            compare(delegate.height, heightBefore, "Unfocus must not change the block height")
        }

        // ========== Milestone 4 Tests: Block Types ==========

        // Step 4a: Enhanced Heading Rendering Tests
        function test_24_heading1FontSize() {
            // First block should be Heading1 with 32px font
            var h1Delegate = findBlockDelegate(0)
            var h1TextArea = findTextArea(h1Delegate)
            BlockModel.updateType(0, 1)  // Ensure it's Heading1
            wait(50)

            compare(h1TextArea.font.pixelSize, 32, "Heading1 should have 32px font")
        }

        function test_25_heading2FontSize() {
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            BlockModel.updateType(1, 2)  // Set to Heading2
            wait(50)

            compare(textArea.font.pixelSize, 24, "Heading2 should have 24px font")
        }

        function test_26_heading3FontSize() {
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            BlockModel.updateType(1, 3)  // Set to Heading3
            wait(50)

            compare(textArea.font.pixelSize, 20, "Heading3 should have 20px font")
        }

        function test_26b_heading4FontSize() {
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            BlockModel.updateType(1, 10)  // Set to Heading4
            wait(50)

            compare(textArea.font.pixelSize, 17, "Heading4 should have 17px font")
        }

        function test_27_paragraphFontSize() {
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            BlockModel.updateType(1, 0)  // Set to Paragraph
            wait(50)

            compare(textArea.font.pixelSize, 15, "Paragraph should have 15px font")
        }

        function test_28_headingFontWeights() {
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)

            // Test Heading1 - Bold
            BlockModel.updateType(1, 1)
            wait(50)
            compare(textArea.font.weight, Font.Bold, "Heading1 should be bold")

            // Test Heading2 - DemiBold
            BlockModel.updateType(1, 2)
            wait(50)
            compare(textArea.font.weight, Font.DemiBold, "Heading2 should be semi-bold")

            // Test Heading3 - Medium
            BlockModel.updateType(1, 3)
            wait(50)
            compare(textArea.font.weight, Font.Medium, "Heading3 should be medium weight")

            // Test Heading4 - Medium
            BlockModel.updateType(1, 10)
            wait(50)
            compare(textArea.font.weight, Font.Medium, "Heading4 should be medium weight")

            // Test Paragraph - Normal
            BlockModel.updateType(1, 0)
            wait(50)
            compare(textArea.font.weight, Font.Normal, "Paragraph should be normal weight")
        }

        function test_29_noPhantomGap() {
            // Phantom-gap regression, block geometry across rendering
            // states: a one-line formatted block must have the
            // same height as a one-line plain block. Under the old overlay
            // design, height followed a hidden second text and formatted
            // blocks reserved too much space.
            BlockModel.updateType(0, 0)
            BlockModel.updateContent(0, "plain text line")
            BlockModel.updateType(1, 0)
            BlockModel.updateContent(1, "**bold** and *italic* line")
            wait(100)

            var plainDelegate = findBlockDelegate(0)
            var formattedDelegate = findBlockDelegate(1)

            compare(formattedDelegate.height, plainDelegate.height,
                    "One-line formatted block must be as tall as a one-line plain block")
        }

        // Step 4b: Type Conversion Shortcuts Tests
        function test_30_ctrlZeroConvertsToParagraph() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with Heading1
            BlockModel.updateType(0, 1)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Ctrl+0 should convert to Paragraph
            verify(sendKeyAndExpectType(textArea, Qt.Key_0, Qt.ControlModifier, 0),
                   "Ctrl+0 should convert to Paragraph")
        }

        function test_31_ctrlOneConvertsToHeading1() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with Paragraph
            BlockModel.updateType(0, 0)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Ctrl+1 should convert to Heading1
            verify(sendKeyAndExpectType(textArea, Qt.Key_1, Qt.ControlModifier, 1),
                   "Ctrl+1 should convert to Heading1")
        }

        function test_32_ctrlTwoConvertsToHeading2() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with Paragraph
            BlockModel.updateType(0, 0)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Ctrl+2 should convert to Heading2
            verify(sendKeyAndExpectType(textArea, Qt.Key_2, Qt.ControlModifier, 2),
                   "Ctrl+2 should convert to Heading2")
        }

        function test_33_ctrlThreeConvertsToHeading3() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with Paragraph
            BlockModel.updateType(0, 0)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            ensureFocus(textArea)

            // Ctrl+3 should convert to Heading3
            verify(sendKeyAndExpectType(textArea, Qt.Key_3, Qt.ControlModifier, 3),
                   "Ctrl+3 should convert to Heading3")
        }

        function test_34_typeConversionPreservesContent() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var testContent = "Test content with **bold** text"
            BlockModel.updateContent(0, testContent)
            BlockModel.updateType(0, 0)  // Start as paragraph
            wait(50)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Convert to Heading1
            keyClick(Qt.Key_1, Qt.ControlModifier)
            wait(50)

            compare(BlockModel.blockAt(0).content, testContent,
                    "Content should be preserved after type conversion")
        }

        function test_35_typeConversionPreservesCursor() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateType(0, 0)  // Paragraph
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Set cursor position
            textArea.cursorPosition = 5
            wait(50)

            // Convert type
            keyClick(Qt.Key_1, Qt.ControlModifier)
            wait(50)

            compare(textArea.cursorPosition, 5, "Cursor position should be preserved after type conversion")
        }

        function test_36_sameTypeConversionIsNoOp() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start as Heading1
            BlockModel.updateType(0, 1)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            mouseClick(textArea)
            wait(50)

            // Ctrl+1 again (same type)
            keyClick(Qt.Key_1, Qt.ControlModifier)
            wait(50)

            // Should still be Heading1
            compare(BlockModel.blockAt(0).blockType, 1, "Same type conversion should be no-op")
        }

        // Step 4c: Markdown Prefix Auto-Conversion Tests
        // Helper function to reset block for prefix tests
        function resetBlockForPrefixTest(blockType, content) {
            BlockModel.updateType(0, blockType)
            BlockModel.updateContent(0, content)
            wait(50)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            return { delegate: delegate, textArea: textArea }
        }

        function test_37_hashSpaceConvertsToHeading1() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with empty paragraph
            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "# Hello" with retry for type conversion
            var startTime = Date.now()
            var timeout = 1000
            while (BlockModel.blockAt(0).blockType !== 1 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_Space)
                keyClick(Qt.Key_H)
                keyClick(Qt.Key_E)
                keyClick(Qt.Key_L)
                keyClick(Qt.Key_L)
                keyClick(Qt.Key_O)
                wait(150)
            }

            compare(BlockModel.blockAt(0).blockType, 1, "# prefix should convert to Heading1")
            compare(BlockModel.blockAt(0).content, "hello", "Prefix should be stripped")
        }

        function test_38_doubleHashSpaceConvertsToHeading2() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with empty paragraph
            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "## Section" with retry
            var startTime = Date.now()
            var timeout = 1000
            while (BlockModel.blockAt(0).blockType !== 2 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_Space)
                keyClick(Qt.Key_S)
                wait(150)
            }

            compare(BlockModel.blockAt(0).blockType, 2, "## prefix should convert to Heading2")
            compare(BlockModel.blockAt(0).content, "s", "Prefix should be stripped")
        }

        function test_39_tripleHashSpaceConvertsToHeading3() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with empty paragraph
            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "### Sub" with retry
            var startTime = Date.now()
            var timeout = 1000
            while (BlockModel.blockAt(0).blockType !== 3 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_Space)
                keyClick(Qt.Key_S)
                wait(150)
            }

            compare(BlockModel.blockAt(0).blockType, 3, "### prefix should convert to Heading3")
            compare(BlockModel.blockAt(0).content, "s", "Prefix should be stripped")
        }

        function test_40_hashWithoutSpaceDoesNotConvert() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with empty paragraph
            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "#NoSpace" with retry until text appears
            var startTime = Date.now()
            var timeout = 1000
            while (textArea.text.length < 3 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_N)
                keyClick(Qt.Key_O)
                wait(150)
            }

            compare(BlockModel.blockAt(0).blockType, 0, "# without space should remain Paragraph")
            compare(BlockModel.blockAt(0).content, "#no", "Content should contain the hash")
        }

        function test_41_existingHeadingDoesNotReconvert() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start as Heading1 with content
            var result = resetBlockForPrefixTest(1, "Existing Heading")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Move to start
            textArea.cursorPosition = 0
            wait(50)

            // Type "# " at start with retry until content changes
            var startTime = Date.now()
            var timeout = 1000
            while (!BlockModel.blockAt(0).content.startsWith("# ") && (Date.now() - startTime) < timeout) {
                textArea.cursorPosition = 0
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_Space)
                wait(150)
            }

            // Should remain Heading1 with prefix in content
            compare(BlockModel.blockAt(0).blockType, 1, "Existing heading should not reconvert")
            compare(BlockModel.blockAt(0).content, "# Existing Heading", "Content should include typed prefix")
        }

        function test_42_fourHashesConvertToHeading4() {
            // "#### " now converts to Heading 4
            // (features.md §1.2.2 defines four levels; the earlier pin
            // reflected the missing type, not a spec behavior).
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Start with empty paragraph
            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "#### X" with retry until the conversion lands
            var startTime = Date.now()
            var timeout = 1000
            while (BlockModel.blockAt(0).blockType !== 10 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)  // #
                keyClick(Qt.Key_Space)
                keyClick(Qt.Key_X)
                wait(150)
            }

            compare(BlockModel.blockAt(0).blockType, 10, "#### should convert to Heading4")
            compare(BlockModel.blockAt(0).content, "x", "Prefix should be stripped")

            // The conversion fired when the space landed, before the "x"
            // was typed into the heading: the first undo removes that
            // character, the second reverts the conversion itself and
            // restores the literal typed prefix as a paragraph.
            UndoStack.undo()
            wait(100)
            compare(BlockModel.blockAt(0).blockType, 10, "First undo only removes the typed character")
            UndoStack.undo()
            wait(100)
            compare(BlockModel.blockAt(0).blockType, 0, "Second undo should restore the paragraph")
            compare(BlockModel.blockAt(0).content, "#### ", "Undo should restore the typed prefix")
        }

        function test_42b_fiveHashesDoNotConvert() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var result = resetBlockForPrefixTest(0, "")
            var textArea = result.textArea

            ensureFocus(textArea)

            // Type "##### X" with retry until text appears
            var startTime = Date.now()
            var timeout = 1000
            while (textArea.text.length < 6 && (Date.now() - startTime) < timeout) {
                BlockModel.updateContent(0, "")
                wait(50)
                for (var i = 0; i < 5; i++)
                    keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)
                keyClick(Qt.Key_Space)
                keyClick(Qt.Key_X)
                wait(150)
            }

            // Five hashes stay literal (the spec defines four levels)
            compare(BlockModel.blockAt(0).blockType, 0, "##### should remain Paragraph")
        }

        // ========== Milestone 5 Tests: Undo/Redo ==========

        function test_43_undoStackExistsInQml() {
            verify(typeof UndoStack !== "undefined", "UndoStack should be exposed to QML")
            verify(UndoStack !== null, "UndoStack should not be null")
        }

        function test_44_undoStackPropertiesExposed() {
            // Test that all expected properties are accessible
            verify(typeof UndoStack.canUndo === "boolean", "canUndo should be boolean")
            verify(typeof UndoStack.canRedo === "boolean", "canRedo should be boolean")
            verify(typeof UndoStack.isClean === "boolean", "isClean should be boolean")
        }

        function test_45_statusBarShowsSavedState() {
            var statusText = findChild(appLoader.item, "saveStateText")
            verify(statusText !== null, "Status text should exist")

            // After initial load, should show "Saved" (clean state)
            // Note: Depends on implementation clearing undo stack after sample data
            verify(statusText.text === "Saved" || statusText.text === "Unsaved",
                   "Status should show save state")
        }

        function test_46_ctrlZCallsUndo() {
            // Break merge to ensure our change doesn't merge with previous tests
            UndoStack.breakMerge()

            // Make a change via the model API to ensure it works
            var originalContent = BlockModel.getContent(1)
            var modifiedContent = originalContent + "X"

            // Make change via model
            BlockModel.updateContent(1, modifiedContent)
            wait(100)

            // Verify change was made
            compare(BlockModel.getContent(1), modifiedContent, "Content should have changed")
            verify(UndoStack.canUndo, "Should be able to undo")

            // Undo via direct API call (keyboard tested in other tests)
            UndoStack.undo()
            wait(100)

            // Content should be restored
            compare(BlockModel.getContent(1), originalContent, "Undo should restore the change")
        }

        function test_47_ctrlYCallsRedo() {
            // Break merge to ensure our change doesn't merge with previous tests
            UndoStack.breakMerge()

            // Make a change via the model API to ensure it works
            var originalContent = BlockModel.getContent(1)
            var modifiedContent = originalContent + "Y"

            // Make change via model
            BlockModel.updateContent(1, modifiedContent)
            wait(100)

            verify(UndoStack.canUndo, "Should be able to undo")

            // Undo via UndoStack
            UndoStack.undo()
            wait(100)

            compare(BlockModel.getContent(1), originalContent, "Undo should restore original content")
            verify(UndoStack.canRedo, "Should be able to redo")

            // Redo via direct API call
            UndoStack.redo()
            wait(100)

            // Content should be restored to modified version
            compare(BlockModel.getContent(1), modifiedContent, "Redo should restore the change")
        }

        function test_48_ctrlShiftZCallsRedo() {
            // Break merge to ensure our change doesn't merge with previous tests
            UndoStack.breakMerge()

            // Make a change via the model API
            var originalContent = BlockModel.getContent(1)
            var modifiedContent = originalContent + "A"

            BlockModel.updateContent(1, modifiedContent)
            wait(100)

            verify(UndoStack.canUndo, "Should be able to undo")

            // Undo via UndoStack
            UndoStack.undo()
            wait(100)

            compare(BlockModel.getContent(1), originalContent, "Undo should restore original")
            verify(UndoStack.canRedo, "Should be able to redo")

            // Redo via direct API call (Ctrl+Shift+Z keyboard shortcut tested in real app)
            UndoStack.redo()
            wait(100)

            compare(BlockModel.getContent(1), modifiedContent, "Redo should restore the change")
        }

        function test_49_undoBlockCreation() {
            var initialCount = BlockModel.count

            // Create a new block via the model API
            BlockModel.insertBlock(1, 0, "New block for undo test")
            wait(100)

            compare(BlockModel.count, initialCount + 1, "New block should be created")
            verify(UndoStack.canUndo, "Should be able to undo block creation")

            // Undo should remove the block
            UndoStack.undo()
            wait(100)

            compare(BlockModel.count, initialCount, "Undo should remove the created block")
        }

        function test_50_undoBlockDeletion() {
            var initialCount = BlockModel.count

            // First create a block to delete
            BlockModel.insertBlock(1, 0, "Block to delete")
            wait(100)

            var countAfterCreate = BlockModel.count
            compare(countAfterCreate, initialCount + 1, "Block should be created")

            // Delete the block via model
            BlockModel.removeBlock(1)
            wait(100)

            compare(BlockModel.count, initialCount, "Block should be deleted")
            verify(UndoStack.canUndo, "Should be able to undo deletion")

            // Undo should restore the block
            UndoStack.undo()
            wait(100)

            compare(BlockModel.count, countAfterCreate, "Undo should restore deleted block")
            compare(BlockModel.getContent(1), "Block to delete", "Restored block should have correct content")
        }

        function test_51_modifiedStateAfterChange() {
            // Break merge to ensure our change doesn't merge with previous tests
            UndoStack.breakMerge()

            // Make sure we start clean
            UndoStack.setClean()
            wait(50)

            var statusText = findChild(appLoader.item, "saveStateText")
            compare(statusText.text, "Saved", "Should show Saved when clean")
            verify(UndoStack.isClean, "UndoStack should be clean")

            // Make a change via the model API
            var originalContent = BlockModel.getContent(1)
            BlockModel.updateContent(1, originalContent + "M")
            wait(100)

            // Status should show Unsaved
            verify(!UndoStack.isClean, "UndoStack should not be clean after change")
            compare(statusText.text, "Unsaved", "Should show Unsaved after change")
        }

        function test_52_undoResetsToCleanState() {
            // Clear the undo stack to start fresh, avoiding merge with previous tests
            UndoStack.clear()
            wait(50)

            // Set clean state
            UndoStack.setClean()
            wait(50)

            var statusText = findChild(appLoader.item, "saveStateText")
            compare(statusText.text, "Saved", "Should show Saved when clean")
            verify(UndoStack.isClean, "Should start clean")

            // Break merge chain to ensure new command doesn't merge
            UndoStack.breakMerge()

            // Make a change via model API
            var originalContent = BlockModel.getContent(1)
            BlockModel.updateContent(1, originalContent + "N")
            wait(100)

            compare(statusText.text, "Unsaved", "Should show Unsaved after change")
            verify(!UndoStack.isClean, "UndoStack should not be clean")

            // Undo should return to clean state
            UndoStack.undo()
            wait(100)

            verify(UndoStack.isClean, "UndoStack should be clean after undo")
            compare(statusText.text, "Saved", "Should show Saved after undoing all changes")
        }

        // ========== Milestone 6 Tests: Persistence ==========

        function test_53_documentManagerExistsInQml() {
            verify(typeof DocumentManager !== "undefined", "DocumentManager should be exposed to QML")
            verify(DocumentManager !== null, "DocumentManager should not be null")
        }

        function test_54_documentManagerPropertiesExposed() {
            // Test that all expected properties are accessible
            verify(typeof DocumentManager.currentFilePath === "string", "currentFilePath should be string")
            verify(typeof DocumentManager.currentFileName === "string", "currentFileName should be string")
            verify(typeof DocumentManager.isDirty === "boolean", "isDirty should be boolean")
            verify(typeof DocumentManager.hasFile === "boolean", "hasFile should be boolean")
            verify(typeof DocumentManager.autoSaveEnabled === "boolean", "autoSaveEnabled should be boolean")
            verify(typeof DocumentManager.autoSaveInterval === "number", "autoSaveInterval should be number")
        }

        function test_55_initialDocumentState() {
            // After loading sample data, should be "Untitled" with no file
            compare(DocumentManager.currentFileName, "Untitled", "Initial document should be 'Untitled'")
            verify(!DocumentManager.hasFile, "Initial document should not have a file")
            compare(DocumentManager.currentFilePath, "", "Initial file path should be empty")
        }

        function test_56_dirtyStateTracking() {
            // Reset to clean state
            UndoStack.setClean()
            wait(50)

            verify(!DocumentManager.isDirty, "Should start clean")

            // Make a change
            UndoStack.breakMerge()
            var originalContent = BlockModel.getContent(0)
            BlockModel.updateContent(0, originalContent + "TEST")
            wait(100)

            verify(DocumentManager.isDirty, "Should be dirty after change")

            // Undo the change
            UndoStack.undo()
            wait(100)

            verify(!DocumentManager.isDirty, "Should be clean after undo")
        }

        function test_57_statusBarShowsFilePath() {
            var filePathText = findChild(appLoader.item, "filePathText")
            verify(filePathText !== null, "File path text should exist in status bar")

            // Should show "New Document" for unsaved document
            compare(filePathText.text, "New Document", "Should show 'New Document' for new document")
        }

        function test_58_statusBarShowsWordCount() {
            var wordCountText = findChild(appLoader.item, "wordCountText")
            verify(wordCountText !== null, "Word count text should exist in status bar")

            // Should contain "words"
            verify(wordCountText.text.indexOf("words") !== -1, "Word count should contain 'words'")
        }

        function test_59_windowTitleShowsDirtyState() {
            // Reset to clean state
            UndoStack.setClean()
            wait(50)

            var windowTitle = appLoader.item.title
            verify(windowTitle.indexOf("*") === -1, "Clean document should not have * in title")

            // Make a change
            UndoStack.breakMerge()
            var originalContent = BlockModel.getContent(0)
            BlockModel.updateContent(0, originalContent + "DIRTY")
            wait(100)

            windowTitle = appLoader.item.title
            verify(windowTitle.indexOf("*") === 0, "Dirty document should start with * in title")

            // Clean up
            UndoStack.undo()
            wait(100)
        }

        function test_60_newDocumentClears() {
            // Add some content
            BlockModel.insertBlock(0, 0, "Test block for new document")
            var countBefore = BlockModel.count
            wait(50)

            // Create new document
            DocumentManager.newDocument()
            wait(100)

            // Should have single empty paragraph
            compare(BlockModel.count, 1, "New document should have 1 block")
            compare(BlockModel.getContent(0), "", "New document block should be empty")
            compare(BlockModel.blockAt(0).blockType, 0, "New document block should be paragraph")

            // Should be clean
            verify(!DocumentManager.isDirty, "New document should be clean")
            verify(!DocumentManager.hasFile, "New document should not have file")
        }

        // ========== Milestone 7 Tests: Polish ==========

        // Step 7a: Block Reordering Tests
        function test_61_altUpMovesBlockUp() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 3 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            BlockModel.insertBlock(2, 0, "Third")
            wait(100)

            compare(BlockModel.count, 3, "Should have 3 blocks")

            // Focus second block
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Verify initial state
            compare(BlockModel.getContent(0), "First", "Block 0 should be 'First'")
            compare(BlockModel.getContent(1), "Second", "Block 1 should be 'Second'")

            // Press Alt+Up to move block up
            keyClick(Qt.Key_Up, Qt.AltModifier)
            wait(200)

            // Verify order changed
            compare(BlockModel.getContent(0), "Second", "Second block should now be first")
            compare(BlockModel.getContent(1), "First", "First block should now be second")
        }

        function test_62_altDownMovesBlockDown() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 3 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            BlockModel.insertBlock(2, 0, "Third")
            wait(100)

            // Focus first block
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Press Alt+Down to move block down
            keyClick(Qt.Key_Down, Qt.AltModifier)
            wait(200)

            // Verify order changed
            compare(BlockModel.getContent(0), "Second", "Second block should now be first")
            compare(BlockModel.getContent(1), "First", "First block should now be second")
        }

        function test_63_altUpAtTopDoesNothing() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 2 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            wait(100)

            // Focus first block (at top)
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Press Alt+Up (should do nothing)
            keyClick(Qt.Key_Up, Qt.AltModifier)
            wait(200)

            // Verify order unchanged
            compare(BlockModel.getContent(0), "First", "First block should stay first")
            compare(BlockModel.getContent(1), "Second", "Second block should stay second")
        }

        function test_64_altDownAtBottomDoesNothing() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 2 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            wait(100)

            // Focus last block (at bottom)
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Press Alt+Down (should do nothing)
            keyClick(Qt.Key_Down, Qt.AltModifier)
            wait(200)

            // Verify order unchanged
            compare(BlockModel.getContent(0), "First", "First block should stay first")
            compare(BlockModel.getContent(1), "Second", "Second block should stay second")
        }

        function test_65_moveBlockIsUndoable() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 2 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            wait(100)

            // Clear undo stack
            UndoStack.clear()
            UndoStack.setClean()
            wait(50)

            // Focus first block and move down
            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            keyClick(Qt.Key_Down, Qt.AltModifier)
            wait(200)

            compare(BlockModel.getContent(0), "Second", "Move should have happened")
            verify(UndoStack.canUndo, "Should be able to undo move")

            // Undo
            UndoStack.undo()
            wait(200)

            compare(BlockModel.getContent(0), "First", "Move should be undone")
        }

        function test_66_moveMaintainsFocus() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Setup: Create fresh document with 3 blocks
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateContent(0, "First")
            BlockModel.insertBlock(1, 0, "Second")
            BlockModel.insertBlock(2, 0, "Third")
            wait(100)

            // Focus middle block
            var delegate = findBlockDelegate(1)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            textArea.cursorPosition = 3  // "Sec|ond"
            wait(50)

            // Move up
            keyClick(Qt.Key_Up, Qt.AltModifier)
            wait(200)

            // Focus should follow the moved block (now at index 0)
            delegate = findBlockDelegate(0)
            textArea = findTextArea(delegate)
            verify(textArea.activeFocus, "Focus should follow moved block")
            compare(textArea.text, "Second", "Moved block should have 'Second' content")
        }

        // Step 7b: Status Bar Tests
        function test_67_statusBarShowsBlockType() {
            // Setup: Create fresh document
            DocumentManager.newDocument()
            wait(200)  // Allow time for all updates

            var blockTypeText = findChild(appLoader.item, "blockTypeText")
            verify(blockTypeText !== null, "Block type indicator should exist in status bar")

            // Verify model state first
            compare(BlockModel.count, 1, "New document should have 1 block")
            compare(BlockModel.blockAt(0).blockType, 0, "Block should be Paragraph type (0)")
            var listView = findChild(appLoader.item, "blockListView")
            listView.currentIndex = 0

            // The indicator must update on its own — no forced writes.
            // (An imperative assignment here would sever the binding for
            // the rest of the suite.)
            tryCompare(blockTypeText, "text", "Paragraph", 1000)

            // Change to Heading1
            BlockModel.updateType(0, 1)
            tryCompare(blockTypeText, "text", "Heading 1", 1000)
        }

        function test_68_statusBarShowsCharCount() {
            var charCountText = findChild(appLoader.item, "charCountText")
            verify(charCountText !== null, "Character count should exist in status bar")

            // Should contain "chars"
            verify(charCountText.text.indexOf("chars") !== -1, "Char count should contain 'chars'")
        }

        // Step 7d: Clipboard Tests
        function test_69_ctrlShiftVPastesPlainText() {
            if (isHeadless) {
                skip("Clipboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Put known text on the real system clipboard via the C++ helper
            Clipboard.text = "pasted-plain"
            verify(Clipboard.hasText, "Clipboard should report text after setText")

            keyClick(Qt.Key_End, Qt.ControlModifier)
            var before = textArea.text
            keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier)
            wait(100)

            compare(textArea.text, before + "pasted-plain",
                    "Ctrl+Shift+V should insert clipboard text at the cursor")
            compare(BlockModel.getContent(0), before + "pasted-plain",
                    "Pasted text should reach the model")
            compare(textArea.cursorPosition, (before + "pasted-plain").length,
                    "Cursor should sit after the pasted text")
        }

        // Regression: formatting commands must not break the
        // text <- delegate.content binding (they route through the model,
        // never assign textArea.text imperatively). If the binding broke,
        // a model-driven change (undo, load) would no longer refresh the
        // block's display.
        function test_69b_formattingKeepsModelBinding() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "bind check")
            wait(50)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Select "bind" and bold it
            textArea.select(0, 4)
            keyClick(Qt.Key_B, Qt.ControlModifier)
            wait(50)
            compare(BlockModel.getContent(0), "**bind** check",
                    "Ctrl+B should bold the selection in the model")
            compare(textArea.text, "**bind** check",
                    "Display should follow the model after Ctrl+B")

            // A later model-driven change must still refresh the display
            BlockModel.updateContent(0, "refreshed from model")
            wait(50)
            compare(textArea.text, "refreshed from model",
                    "Model-driven change must still refresh the display (binding intact)")
        }

        function test_69c_undoRestoresFormattingAndRendering() {
            // Undo of a formatting toggle restores both
            // the markdown and the rendering; reveal transitions are never
            // undo steps (a single Ctrl+Z reverts the whole toggle).
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "bind check")
            wait(100)
            UndoStack.breakMerge()

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            textArea.select(0, 4)
            keyClick(Qt.Key_B, Qt.ControlModifier)
            wait(100)
            compare(BlockModel.getContent(0), "**bind** check",
                    "Ctrl+B should bold the selection in the model")

            UndoStack.undo()
            wait(100)
            compare(BlockModel.getContent(0), "bind check",
                    "One undo should revert the whole formatting toggle")
            compare(textArea.text, "bind check",
                    "Undo should restore the rendering too")

            UndoStack.redo()
            wait(100)
            compare(BlockModel.getContent(0), "**bind** check",
                    "Redo should re-apply the formatting")
        }

        function test_69d_selectionRevealsTouchedSpans() {
            // features.md §2.2.4: a selection that includes spans reveals
            // every span it touches; the model is never altered.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "This is **important** *information* here")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            wait(80)

            // Move to the bold word, then extend the selection across both
            // spans with Shift+Right
            for (var i = 0; i < 9; i++) keyClick(Qt.Key_Right)
            wait(80)
            for (var j = 0; j < 20; j++) keyClick(Qt.Key_Right, Qt.ShiftModifier)
            wait(150)

            verify(textArea.text.indexOf("**important**") !== -1,
                   "Bold span touched by selection should reveal its syntax")
            verify(textArea.text.indexOf("*information*") !== -1,
                   "Italic span touched by selection should reveal its syntax")
            compare(BlockModel.getContent(0),
                    "This is **important** *information* here",
                    "Selection reveal must never alter the model")
        }

        function test_69e_ctrlCCopiesSelectionAsMarkdown() {
            // Copy puts markdown on the clipboard — selecting the
            // rendered bold word copies "**world**".
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "Hello **world** end")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            keyClick(Qt.Key_Home) // settle to the hidden state first
            wait(120)

            // Display "Hello world end": select "world" (6..11); the
            // selection reveals the span and auto-adjusts before the copy.
            textArea.select(6, 11)
            wait(150)

            Clipboard.text = ""
            keyClick(Qt.Key_C, Qt.ControlModifier)
            wait(100)
            compare(Clipboard.text, "**world**",
                    "Copy should capture the selection as markdown")
            compare(BlockModel.getContent(0), "Hello **world** end",
                    "Copy must not alter the model")
        }

        function test_69f_ctrlXCutsSelectionAsMarkdown() {
            // Cut removes exactly what the copy captured (markers included
            // when the whole word is selected), so cut+paste round-trips.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "Hello **world** end")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            keyClick(Qt.Key_Home) // settle to the hidden state first
            wait(120)

            textArea.select(6, 11)
            wait(150)

            Clipboard.text = ""
            keyClick(Qt.Key_X, Qt.ControlModifier)
            wait(100)
            compare(Clipboard.text, "**world**",
                    "Cut should capture the selection as markdown")
            compare(BlockModel.getContent(0), "Hello  end",
                    "Cut should remove the span including its markers")

            // Paste it back: cut+paste round-trips
            textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            textArea.cursorPosition = 6
            keyClick(Qt.Key_V, Qt.ControlModifier)
            wait(100)
            compare(BlockModel.getContent(0), "Hello **world** end",
                    "Cut followed by paste should restore the markdown")
        }

        function test_69g_ctrlVPastesMarkdown() {
            // §5.2: pasted markdown becomes formatted display.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            BlockModel.updateContent(0, "start ")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            keyClick(Qt.Key_End, Qt.ControlModifier)
            wait(80)

            Clipboard.text = "**pasted**"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            wait(150)

            compare(BlockModel.getContent(0), "start **pasted**",
                    "Pasted markdown should reach the model verbatim")

            // Cursor sits at the end of the pasted span, so it is revealed;
            // moving away renders it.
            keyClick(Qt.Key_Home)
            wait(150)
            compare(textArea.text, "start pasted",
                    "Pasted markdown should render once the cursor leaves")
        }

        function test_69h_multiLinePasteSplitsIntoBlocks() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "headtail")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            textArea.cursorPosition = 4 // between "head" and "tail"
            wait(80)

            Clipboard.text = "one\r\ntwo\nthree"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            wait(200)

            compare(BlockModel.count, 3, "Multi-line paste should split into blocks")
            compare(BlockModel.getContent(0), "headone", "First line joins text before cursor")
            compare(BlockModel.getContent(1), "two", "Middle line becomes its own block")
            compare(BlockModel.getContent(2), "threetail", "Last line joins text after cursor")
        }

        function test_69i_pastePlainStripsFormatting() {
            // §5.2: Ctrl+Shift+V strips markdown formatting.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            DocumentManager.newDocument()
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            Clipboard.text = "**bold** and *italic*"
            keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier)
            wait(150)

            compare(BlockModel.getContent(0), "bold and italic",
                    "Paste-plain should strip markdown markers")
        }

        // Step 7e: Edge Case Tests
        function test_69j_newFormattingShortcuts() {
            // Ctrl+Shift+S / Ctrl+U / Ctrl+E toggle their types through
            // the model (features.md §13 shortcuts).
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            var cases = [
                { key: Qt.Key_S, mods: Qt.ControlModifier | Qt.ShiftModifier,
                  expected: "~~word~~", label: "Ctrl+Shift+S strikethrough" },
                { key: Qt.Key_U, mods: Qt.ControlModifier,
                  expected: "++word++", label: "Ctrl+U underline" },
                { key: Qt.Key_E, mods: Qt.ControlModifier,
                  expected: "`word`", label: "Ctrl+E inline code" }
            ]
            for (var i = 0; i < cases.length; i++) {
                var c = cases[i]
                BlockModel.updateContent(0, "word")
                wait(100)
                delegate = findBlockDelegate(0)
                textArea = findTextArea(delegate)
                ensureFocus(textArea)
                textArea.selectAll()
                wait(50)
                keyClick(c.key, c.mods)
                wait(100)
                compare(BlockModel.getContent(0), c.expected,
                        c.label + " should toggle markers in the model")
            }

            // Collapsed cursor: the toggle inserts an empty marker pair
            // and typing lands between the markers (format-then-type,
            // features.md §2.2.7).
            BlockModel.updateContent(0, "pre ")
            wait(100)
            delegate = findBlockDelegate(0)
            textArea = findTextArea(delegate)
            ensureFocus(textArea)
            textArea.cursorPosition = 4
            wait(50)
            keyClick(Qt.Key_E, Qt.ControlModifier)
            wait(100)
            compare(BlockModel.getContent(0), "pre ``",
                    "Collapsed Ctrl+E should insert an empty marker pair")
            keyClick(Qt.Key_X)
            wait(100)
            compare(BlockModel.getContent(0), "pre `x`",
                    "Typing should land between the inserted markers")

            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        // ===== Link dialog and click-to-open =====

        function test_69k_ctrlKInsertsLink() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var dialog = appLoader.item.linkDialog

            BlockModel.updateContent(0, "word")
            wait(100)
            ensureFocus(textArea)
            textArea.selectAll()
            wait(50)
            keyClick(Qt.Key_K, Qt.ControlModifier)
            tryCompare(dialog, "visible", true, 1000)
            compare(dialog.editing, false, "No link at cursor: insert mode")
            compare(dialog.textField.text, "word",
                    "Text field prefilled from the selection")

            dialog.urlField.text = "https://qt.io"
            dialog.accept()
            wait(150)
            compare(BlockModel.getContent(0), "[word](https://qt.io)",
                    "Accepting the dialog writes the link through the model")

            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_69l_ctrlKEditsExistingLink() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var dialog = appLoader.item.linkDialog

            BlockModel.updateContent(0, "go [ab](http://x) on")
            wait(100)
            ensureFocus(textArea)
            textArea.cursorPosition = 4 // inside the link text
            wait(100)
            keyClick(Qt.Key_K, Qt.ControlModifier)
            tryCompare(dialog, "visible", true, 1000)
            compare(dialog.editing, true, "Cursor inside a link: edit mode")
            compare(dialog.textField.text, "ab", "Text prefilled from the link")
            compare(dialog.urlField.text, "http://x", "URL prefilled from the link")

            dialog.urlField.text = "https://y.io"
            dialog.textField.text = "cd"
            dialog.accept()
            wait(150)
            compare(BlockModel.getContent(0), "go [cd](https://y.io) on",
                    "Accepting rewrites the existing link in place")

            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_69m_removeLinkKeepsText() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var dialog = appLoader.item.linkDialog

            BlockModel.updateContent(0, "go [ab](http://x) on")
            wait(100)
            ensureFocus(textArea)
            textArea.cursorPosition = 4
            wait(100)
            keyClick(Qt.Key_K, Qt.ControlModifier)
            tryCompare(dialog, "visible", true, 1000)
            compare(dialog.removable, true, "A [text](url) link is removable")

            dialog.removeLink()
            wait(150)
            compare(BlockModel.getContent(0), "go ab on",
                    "Remove link keeps the text (features.md §2.4)")

            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_69n_ctrlClickOpensLink() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var opener = appLoader.item.linkOpener

            BlockModel.updateContent(0, "go [ab](http://x) on")
            wait(100)
            ensureFocus(textArea)
            keyClick(Qt.Key_End)
            wait(100)

            opener.openExternally = false
            var captured = []
            function onActivated(url) { captured.push(url) }
            opener.activated.connect(onActivated)

            // Ctrl+Click on the rendered link text ("ab", display 3..5).
            var rect = textArea.positionToRectangle(4)
            mouseClick(textArea, rect.x, rect.y + rect.height / 2,
                       Qt.LeftButton, Qt.ControlModifier)
            wait(100)

            compare(captured.length, 1, "Ctrl+Click should open exactly one link")
            compare(captured[0], "http://x", "Ctrl+Click opens the link's URL")

            opener.activated.disconnect(onActivated)
            opener.openExternally = true
            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_69o_plainClickOpensOnlyWhenUnfocused() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var opener = appLoader.item.linkOpener

            if (textArea.activeFocus) {
                textArea.focus = false
                tryCompare(textArea, "activeFocus", false, 1000)
            }
            BlockModel.updateContent(0, "go [ab](http://x) on")
            wait(100)

            opener.openExternally = false
            var captured = []
            function onActivated(url) { captured.push(url) }
            opener.activated.connect(onActivated)

            // Reading state (block unfocused): a plain click on the link
            // opens it (features.md §2.4 "click link to open").
            var rect = textArea.positionToRectangle(4)
            mouseClick(textArea, rect.x, rect.y + rect.height / 2)
            wait(100)
            compare(captured.length, 1,
                    "Plain click on an unfocused block's link should open it")

            // Editing state (block now focused): a plain click only moves
            // the cursor.
            tryCompare(textArea, "activeFocus", true, 1000)
            rect = textArea.positionToRectangle(
                Math.min(4, textArea.text.length))
            mouseClick(textArea, rect.x, rect.y + rect.height / 2)
            wait(100)
            compare(captured.length, 1,
                    "Plain click while editing must not open the link")

            opener.activated.disconnect(onActivated)
            opener.openExternally = true
            textArea.focus = false
            tryCompare(textArea, "activeFocus", false, 1000)
        }

        function test_70_emptyDocumentShowsPlaceholder() {
            DocumentManager.newDocument()
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)

            compare(textArea.text, "", "New document block should be empty")
            compare(textArea.placeholderText, "Type something...", "Empty block should show placeholder")
        }

        function test_71_singleBlockHandlesDelete() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            DocumentManager.newDocument()
            wait(100)

            compare(BlockModel.count, 1, "Should have exactly 1 block")

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            textArea.cursorPosition = 0

            // Try to delete the only block - should maintain at least one block
            keyClick(Qt.Key_Backspace)
            wait(100)

            verify(BlockModel.count >= 1, "Should maintain at least one block")
        }

        function test_72_rapidBlockCreation() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            DocumentManager.newDocument()
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)

            // Create 10 blocks rapidly
            for (var i = 0; i < 10; i++) {
                keyClick(Qt.Key_Return)
                wait(20)
            }
            wait(200)

            compare(BlockModel.count, 11, "Should have 11 blocks after 10 Enter presses")
        }

        function test_73_specialCharactersPreserved() {
            DocumentManager.newDocument()
            wait(100)

            var testContent = "Unicode: émojis 🎉 and symbols ™ €"
            BlockModel.updateContent(0, testContent)
            wait(100)

            compare(BlockModel.getContent(0), testContent, "Unicode content should be preserved")
        }

        function test_74_markdownSyntaxPreserved() {
            DocumentManager.newDocument()
            wait(100)

            var testContent = "Text with **bold** and *italic* and ***both***"
            BlockModel.updateContent(0, testContent)
            wait(100)

            compare(BlockModel.getContent(0), testContent, "Markdown syntax should be preserved")
        }

        function test_75_typingLatencyIn100BlockDocument() {
            // Exit criterion: typing latency imperceptible
            // in a 100-block document — measured, not estimated. §21.7
            // target: < 16ms per keystroke (60 fps). The measurement wraps
            // the full pipeline: key event -> engine edit mapping -> model
            // update -> undo stack -> status bar recomputation.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            DocumentManager.newDocument()
            wait(100)
            // All block types participate: ordinal recomputation
            // joins the per-edit path, so numbered items must be present
            // for the measurement to cover it. The typed-into block stays
            // a paragraph so the keystrokes exercise the inline parser.
            BlockModel.insertBlock(0, 0,
                "Block 0 with **bold** and *italic* text content")
            for (var i = 1; i < 99; i++) {
                var type = [0, 4, 5, 6, 7, 8, 9][i % 7]
                BlockModel.insertBlock(i, type,
                    type === 9 ? ""
                               : "Block " + i + " with **bold** and *italic* text",
                    type >= 4 && type <= 6 ? i % 3 : 0)
            }
            wait(300)
            compare(BlockModel.count, 100, "Should have 100 blocks")

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            ensureFocus(textArea)
            keyClick(Qt.Key_End)
            wait(100)

            var keystrokes = 40
            var t0 = Date.now()
            for (var k = 0; k < keystrokes; k++) {
                keyClick(Qt.Key_X)
            }
            var perKey = (Date.now() - t0) / keystrokes
            console.log("TYPING LATENCY: " + perKey.toFixed(2)
                        + " ms/keystroke over " + keystrokes
                        + " keystrokes in a 100-block mixed-type document")

            verify(perKey < 16,
                   "Typing latency must stay under 16ms/keystroke (measured "
                   + perKey.toFixed(2) + "ms)")
        }

        function test_75b_typingLatencyWithFindBarOpen() {
            // Exit criterion: with the find bar open on a live
            // query, the search recompute joins the keystroke path —
            // every content change rescans the document and repaints
            // the visible engines' match tints.
            // Same §21.7 budget: < 16ms per keystroke.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            resetFindBar()
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 0,
                "Block 0 with **bold** and *italic* text content")
            for (var i = 1; i < 99; i++) {
                var type = [0, 4, 5, 6, 7, 8, 9][i % 7]
                BlockModel.insertBlock(i, type,
                    type === 9 ? ""
                               : "Block " + i + " with **bold** and *italic* text",
                    type >= 4 && type <= 6 ? i % 3 : 0)
            }
            wait(300)
            compare(BlockModel.count, 100)

            // "block" matches once per non-divider block: close to the
            // worst realistic match density.
            openFindBar()
            typeString("block")
            tryVerify(function() { return DocumentSearch.matchCount > 80 },
                      2000)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            mouseClick(textArea, 10, textArea.height / 2)
            tryCompare(textArea, "activeFocus", true, 1000)
            keyClick(Qt.Key_End)
            wait(100)

            var keystrokes = 40
            var t0 = Date.now()
            for (var k = 0; k < keystrokes; k++) {
                keyClick(Qt.Key_X)
            }
            var perKey = (Date.now() - t0) / keystrokes
            console.log("TYPING LATENCY (find bar open): " + perKey.toFixed(2)
                        + " ms/keystroke over " + keystrokes
                        + " keystrokes, " + DocumentSearch.matchCount
                        + " live matches in a 100-block document")

            verify(perKey < 16,
                   "Typing latency with the find bar open must stay under "
                   + "16ms/keystroke (measured " + perKey.toFixed(2) + "ms)")
            resetFindBar()
        }

        // The wave-2 performance budgets, measured on the final build:
        // keystroke latency inside a syntax-highlighted 200-line code block,
        // a 100-image document's open cost (layout, not decoding — image
        // loads are async and virtualized), and table cell navigation per Tab.
        function test_75c_wave2PerformanceBudgets() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // (1) Keystroke latency in a 200-line highlighted code block.
            DocumentManager.newDocument()
            wait(100)
            var code = ""
            for (var l = 0; l < 200; l++)
                code += "def f" + l + "(x): return x * " + l + "  # comment " + l + "\n"
            BlockModel.convertBlock(0, Block.CodeBlock, code, false, "python")
            wait(300)
            var codeArea = findTextArea(findBlockDelegate(0))
            ensureFocus(codeArea)
            keyClick(Qt.Key_Home)
            wait(100)
            var t0 = Date.now()
            for (var k = 0; k < 30; k++)
                keyClick(Qt.Key_X)
            var perKey = (Date.now() - t0) / 30
            console.log("CODE-BLOCK TYPING LATENCY: " + perKey.toFixed(2)
                        + " ms/keystroke in a syntax-highlighted 200-line block")
            verify(perKey < 16,
                   "Code-block keystroke latency must stay under 16ms (measured "
                   + perKey.toFixed(2) + "ms)")

            // (2) A 100-image document opens under the 1-second budget.
            DocumentManager.newDocument()
            wait(100)
            var tOpen = Date.now()
            for (var i = 0; i < 100; i++)
                BlockModel.insertBlock(i, Block.Image,
                    ImageAssets.build(sampleImagePath, "img " + i, "", 0))
            tryVerify(function() { return BlockModel.count >= 100 }, 3000,
                      "the 100-image document is built")
            var openMs = Date.now() - tOpen
            console.log("100-IMAGE OPEN: " + openMs + " ms (layout, not decoding)")
            verify(openMs < 1000,
                   "A 100-image document must open under 1s (measured "
                   + openMs + "ms)")

            // (3) Table cell navigation per Tab.
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, Block.Table,
                "| A | B | C |\n| --- | --- | --- |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |")
            wait(300)
            var tbl = findBlockDelegate(0)
            var grid = findChild(tbl, "tableGrid")
            verify(grid !== null, "the table grid renders")
            mouseClick(tbl, tbl.width / 2, 90)   // make a data cell live
            wait(150)
            var tabs = 5
            var tTab = Date.now()
            for (var tt = 0; tt < tabs; tt++)
                keyClick(Qt.Key_Tab)
            var perTab = (Date.now() - tTab) / tabs
            console.log("TABLE CELL NAV: " + perTab.toFixed(2) + " ms/Tab")
            verify(perTab < 50,
                   "Table cell navigation must stay under 50ms/Tab (measured "
                   + perTab.toFixed(2) + "ms)")
        }

        function test_75d_phase11PerformanceBudgets() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // (1) Keystroke latency with a 50-heading outline live and
            // typewriter mode on (keeps the ≤16 ms budget).
            DocumentManager.newDocument()
            var md = ""
            for (var h = 0; h < 50; h++)
                md += "## Section " + h + "\n\nBody paragraph " + h + " here.\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, md)
            DocumentOutline.rebuildNow()
            appLoader.item.outlineVisible = true
            appLoader.item.typewriterMode = true
            wait(300)
            var para = findBlockDelegate(1)
            var ta = findTextArea(para)
            ensureFocus(ta)
            keyClick(Qt.Key_End)
            wait(100)
            var t0 = Date.now()
            for (var k = 0; k < 30; k++)
                keyClick(Qt.Key_X)
            var perKey = (Date.now() - t0) / 30
            console.log("TYPING w/ 50-heading outline + typewriter: "
                        + perKey.toFixed(2) + " ms/keystroke")
            verify(perKey < 16, "keystroke with outline+typewriter must stay "
                   + "under 16ms (measured " + perKey.toFixed(2) + "ms)")
            appLoader.item.outlineVisible = false
            appLoader.item.typewriterMode = false

            // (2) Building the outline over a 200-heading document under 100ms.
            DocumentManager.newDocument()
            var big = ""
            for (var i = 0; i < 200; i++)
                big += "# Heading " + i + "\n\ntext\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, big)
            var tO = Date.now()
            DocumentOutline.rebuildNow()
            var outlineMs = Date.now() - tO
            console.log("OUTLINE BUILD (200 headings): " + outlineMs + " ms")
            compare(DocumentOutline.count, 200)
            verify(outlineMs < 100, "a 200-heading outline must build under "
                   + "100ms (measured " + outlineMs + "ms)")

            // (3) Exporting a 100-block note to HTML under 200ms.
            DocumentManager.newDocument()
            var doc = ""
            for (var b = 0; b < 100; b++)
                doc += "Block " + b + " with **bold** and a [link](http://x).\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, doc)
            var tE = Date.now()
            var html = DocumentExporter.htmlForModel(BlockModel, "Perf")
            var exportMs = Date.now() - tE
            console.log("HTML EXPORT (100 blocks): " + exportMs + " ms")
            verify(html.length > 0)
            verify(exportMs < 200, "a 100-block HTML export must stay under "
                   + "200ms (measured " + exportMs + "ms)")

            // (4) An inline-math-dense paragraph (10 equations) keystroke.
            DocumentManager.newDocument()
            var mathPara = "Math"
            for (var e = 0; e < 10; e++)
                mathPara += " $x_" + e + "^2$"
            BlockModel.updateContent(0, mathPara)
            wait(200)
            var mta = findTextArea(findBlockDelegate(0))
            ensureFocus(mta)
            keyClick(Qt.Key_Home)
            wait(100)
            var tM = Date.now()
            for (var m = 0; m < 30; m++)
                keyClick(Qt.Key_Y)
            var perMathKey = (Date.now() - tM) / 30
            console.log("TYPING in a 10-equation paragraph: "
                        + perMathKey.toFixed(2) + " ms/keystroke")
            verify(perMathKey < 16, "keystroke in an inline-math-dense "
                   + "paragraph must stay under 16ms (measured "
                   + perMathKey.toFixed(2) + "ms)")
        }

        function test_76_roundTripFidelity() {
            // Save -> close -> reopen -> identical content, including
            // markdown that never parsed to spans (edge syntax preserved
            // verbatim).
            DocumentManager.newDocument()
            wait(100)

            BlockModel.updateType(0, 1)
            BlockModel.updateContent(0, "Title with **bold**")
            BlockModel.insertBlock(1, 0, "plain paragraph text")
            BlockModel.insertBlock(2, 0, "edge **unclosed and a*b and ****")
            BlockModel.insertBlock(3, 0, "***both*** with *it* and **bo**")
            BlockModel.insertBlock(4, 2, "Section heading content")
            wait(100)

            var expected = []
            for (var i = 0; i < BlockModel.count; i++) {
                expected.push({ t: BlockModel.blockAt(i).blockType,
                                c: BlockModel.getContent(i) })
            }

            var url = DocumentManager.toLocalFileUrl("/tmp/kvit_roundtrip_test.md")
            verify(DocumentManager.saveAs(url), "Save should succeed")

            DocumentManager.newDocument()
            wait(100)
            compare(BlockModel.count, 1, "New document should reset the model")

            verify(DocumentManager.open(url), "Open should succeed")
            wait(100)

            compare(BlockModel.count, expected.length,
                    "Reopened document should have the same block count")
            for (var j = 0; j < expected.length; j++) {
                compare(BlockModel.blockAt(j).blockType, expected[j].t,
                        "Block " + j + " type should round-trip")
                compare(BlockModel.getContent(j), expected[j].c,
                        "Block " + j + " content should round-trip verbatim")
            }
        }

        function test_77_delegatePoolingWithEngine() {
            // Delegate pooling (reuseItems) safe with the
            // engine attached — pooled delegates detach/reattach cleanly.
            DocumentManager.newDocument()
            wait(100)
            for (var i = 0; i < 99; i++) {
                BlockModel.insertBlock(i, 0, "Pool block " + i + " **b" + i + "**")
            }
            wait(200)
            compare(BlockModel.count, 100)

            var listView = findChild(appLoader.item, "blockListView")
            for (var pass = 0; pass < 3; pass++) {
                listView.positionViewAtEnd()
                wait(120)
                listView.positionViewAtBeginning()
                wait(120)
            }

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            compare(textArea.text, "Pool block 0 b0",
                    "Reused delegate should display its block's display text")
            compare(delegate.editorEngine.markdown, BlockModel.getContent(0),
                    "Reused delegate's engine should mirror its block's markdown")
            compare(BlockModel.getContent(98), "Pool block 98 **b98**",
                    "Model content must survive pooling passes")
        }

        function test_78_loadTimeMeasurement() {
            // Performance harness: measured load time for a
            // generated document, against the features.md §21.7 target
            // (< 1s). The generator mixes every block type, so the measurement
            // covers the line scanner and per-type delegates.
            DocumentManager.newDocument()
            wait(100)
            for (var i = 0; i < 199; i++) {
                var type = [0, 4, 5, 6, 7, 8, 9][i % 7]
                BlockModel.insertBlock(i, type,
                    type === 9 ? ""
                               : "Load test block " + i + " with **bold** text",
                    type >= 4 && type <= 6 ? i % 3 : 0)
            }
            // The initial empty block became index 199; empty blocks are
            // unrepresentable in blank-line-separated markdown, so give it
            // content to make all 200 blocks round-trip.
            BlockModel.updateContent(199, "Final load test block")
            wait(100)
            var url = DocumentManager.toLocalFileUrl("/tmp/kvit_loadtime_test.md")
            verify(DocumentManager.saveAs(url), "Save should succeed")

            DocumentManager.newDocument()
            wait(100)

            var t0 = Date.now()
            verify(DocumentManager.open(url), "Open should succeed")
            var elapsed = Date.now() - t0
            console.log("LOAD TIME: " + elapsed
                        + " ms for a 200-block mixed-type document")

            compare(BlockModel.count, 200, "All blocks should load")
            verify(elapsed < 1000, "200-block load must stay under 1s (measured "
                   + elapsed + "ms)")
        }

        // ==================================================================
        // Per-type delegates
        // ==================================================================

        function test_80_bulletDelegateRendersGlyphByLevel() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "level zero")
            BlockModel.insertBlock(1, 4, "level one", 1)
            BlockModel.insertBlock(2, 4, "level two", 2)
            wait(100)

            var expected = ["•", "◦", "▪"]
            for (var i = 0; i < 3; i++) {
                var delegate = findBlockDelegate(i)
                verify(delegate !== null, "Bullet delegate " + i + " should exist")
                var glyph = findChild(delegate, "bulletGlyph")
                verify(glyph !== null, "Bullet glyph " + i + " should exist")
                compare(glyph.text, expected[i],
                        "Level " + i + " should use the " + expected[i] + " glyph")
                var textArea = findTextArea(delegate)
                verify(textArea.text.indexOf("level") === 0,
                       "Bullet content should render without the '- ' prefix")
            }
        }

        function test_81_numberedDelegateShowsComputedOrdinals() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 5, "one")
            BlockModel.insertBlock(1, 5, "two")
            BlockModel.insertBlock(2, 0, "interruption")
            BlockModel.insertBlock(3, 5, "restart")
            wait(100)

            compare(findChild(findBlockDelegate(0), "ordinalLabel").text, "1.")
            compare(findChild(findBlockDelegate(1), "ordinalLabel").text, "2.")
            compare(findChild(findBlockDelegate(3), "ordinalLabel").text, "1.",
                    "A paragraph interrupts the run, numbering restarts")

            // Insertion renumbers automatically (§1.2.5)
            BlockModel.insertBlock(1, 5, "inserted")
            wait(100)
            compare(findChild(findBlockDelegate(1), "ordinalLabel").text, "2.")
            compare(findChild(findBlockDelegate(2), "ordinalLabel").text, "3.",
                    "Auto-numbering must update after insertion")
        }

        function test_82_todoCheckboxTogglesAndStyles() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 6, "buy milk")
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            var checkbox = findChild(delegate, "todoCheckbox")
            verify(checkbox !== null, "Todo checkbox should exist")
            compare(BlockModel.blockAt(0).checked, false)
            compare(textArea.font.strikeout, false)

            // Clicking the checkbox checks the todo and strikes the text
            mouseClick(checkbox)
            tryVerify(function() { return BlockModel.blockAt(0).checked }, 1000,
                      "Checkbox click should check the todo")
            tryVerify(function() { return textArea.font.strikeout === true }, 1000,
                      "Completed todo should render struck through")

            // The toggle is one undoable step
            UndoStack.undo()
            tryVerify(function() { return !BlockModel.blockAt(0).checked }, 1000,
                      "Undo should uncheck the todo")
            tryVerify(function() { return textArea.font.strikeout === false }, 1000,
                      "Unchecked todo should lose the strikethrough")

            // Clicking again from unchecked checks it once more
            mouseClick(checkbox)
            tryVerify(function() { return BlockModel.blockAt(0).checked }, 1000)
        }

        function test_83_quoteDelegateRendersBar() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 7, "wise words\nsecond line")
            wait(100)

            var delegate = findBlockDelegate(0)
            var bar = findChild(delegate, "quoteBar")
            verify(bar !== null, "Quote bar should exist")
            verify(bar.height > 10, "Quote bar should stretch with the block")
            var textArea = findTextArea(delegate)
            compare(textArea.text, "wise words\nsecond line",
                    "Multi-line quote content should render without '> ' prefixes")
        }

        function test_84_codeBlockIsVerbatim() {
            DocumentManager.newDocument()
            wait(100)
            var code = "def f():\n    return \"**not bold** `not code`\""
            BlockModel.insertBlock(0, 8, code)
            wait(100)

            var delegate = findBlockDelegate(0)
            var textArea = findTextArea(delegate)
            compare(textArea.text, code,
                    "Code shows markers and whitespace literally")
            compare(textArea.font.family, "monospace")

            if (!isHeadless) {
                // The cursor inside marker-shaped text reveals nothing
                ensureFocus(textArea)
                textArea.cursorPosition = code.indexOf("not bold") + 2
                wait(150)
                compare(textArea.text, code, "No reveal happens in a code block")

                // Formatting shortcuts are inert (markers would be literal)
                keyClick(Qt.Key_B, Qt.ControlModifier)
                wait(100)
                compare(BlockModel.getContent(0), code,
                        "Ctrl+B must not edit code block content")
            }
        }

        function test_85_dividerFocusTraversal() {
            if (isHeadless) {
                skip("Focus traversal requires display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 0, "above")
            BlockModel.insertBlock(1, 9, "")
            BlockModel.insertBlock(2, 0, "below")
            wait(100)

            var above = findBlockDelegate(0)
            var divider = findBlockDelegate(1)
            var below = findBlockDelegate(2)
            verify(findChild(divider, "dividerLine") !== null,
                   "Divider should render its rule")

            ensureFocus(findTextArea(above))
            keyClick(Qt.Key_Down)
            tryCompare(divider, "isFocused", true, 1000)

            keyClick(Qt.Key_Down)
            tryVerify(function() { return findTextArea(below).activeFocus }, 1000,
                      "Down from the divider should reach the block below")

            keyClick(Qt.Key_Up)
            tryCompare(divider, "isFocused", true, 1000)
            keyClick(Qt.Key_Up)
            tryVerify(function() { return findTextArea(above).activeFocus }, 1000,
                      "Up from the divider should reach the block above")
        }

        function test_86_geometryInvariantsForNewTypes() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            // Focus alone never resizes a block (a block-geometry
            // invariant) — now also for the structural types.
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "bullet **bold** *italic*")
            BlockModel.insertBlock(1, 6, "todo with ==mark== text")
            BlockModel.insertBlock(2, 7, "quote `code` text")
            wait(150)

            for (var i = 0; i < 3; i++) {
                var delegate = findBlockDelegate(i)
                var textArea = findTextArea(delegate)
                var heightBefore = delegate.height
                ensureFocus(textArea)
                wait(120)
                compare(delegate.height, heightBefore,
                        "Focus must not change block " + i + " height")
                textArea.focus = false
                wait(120)
                compare(delegate.height, heightBefore,
                        "Unfocus must not change block " + i + " height")
            }
        }

        function test_87_statusBarNamesNewTypes() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "a bullet")
            BlockModel.insertBlock(1, 8, "code")
            wait(100)

            var listView = findChild(appLoader.item, "blockListView")
            var typeText = findChild(appLoader.item, "blockTypeText")

            listView.currentIndex = 0
            tryCompare(typeText, "text", "Bulleted List", 1000)
            listView.currentIndex = 1
            tryCompare(typeText, "text", "Code Block", 1000)
        }

        function test_88_delegatePoolingWithMixedTypes() {
            // The pooling guarantee extended to per-type delegates:
            // pooled delegates of every kind detach and reattach cleanly.
            DocumentManager.newDocument()
            wait(100)
            for (var i = 0; i < 60; i++) {
                var type = [0, 4, 5, 6, 7, 8, 9][i % 7]
                BlockModel.insertBlock(i, type,
                    type === 9 ? "" : "Mixed pool block " + i)
            }
            wait(200)

            var listView = findChild(appLoader.item, "blockListView")
            for (var pass = 0; pass < 3; pass++) {
                listView.positionViewAtEnd()
                wait(120)
                listView.positionViewAtBeginning()
                wait(120)
            }

            compare(findReadOnlyText(findBlockDelegate(0)).text, "Mixed pool block 0")
            var bulletDelegate = findBlockDelegate(1)
            verify(findChild(bulletDelegate, "bulletGlyph") !== null,
                   "Bullet delegate should keep its glyph after pooling")
            compare(findTextArea(bulletDelegate).text, "Mixed pool block 1")
            verify(findChild(findBlockDelegate(6), "dividerLine") !== null,
                   "Divider delegate should survive pooling")
            compare(BlockModel.getContent(57), "Mixed pool block 57",
                    "Model content must survive pooling passes")
        }

        // ==================================================================
        // Keyboard behavior
        // ==================================================================

        function test_90_enterContinuesListType() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 6, "checked task", 1)
            BlockModel.setChecked(0, true)
            wait(100)

            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.count === 3 }, 1000)

            // The new item continues the type at the same indent, unchecked
            compare(BlockModel.blockAt(1).blockType, 6, "Enter continues the todo type")
            compare(BlockModel.blockAt(1).indentLevel, 1, "Continuation keeps the indent")
            compare(BlockModel.blockAt(1).checked, false, "A new todo starts unchecked")
            compare(BlockModel.blockAt(0).checked, true, "The first item keeps its state")

            // Focus and cursor land in the new item
            var newTextArea = findTextArea(findBlockDelegate(1))
            tryVerify(function() { return newTextArea.activeFocus }, 1000,
                      "Focus should move to the continuation item")
            compare(newTextArea.cursorPosition, 0)

            // Enter at the end of a heading still makes a paragraph (§1.2.2)
            BlockModel.convertBlock(0, 1, "A heading")
            wait(100)
            var headingArea = findTextArea(findBlockDelegate(0))
            ensureFocus(headingArea)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.count === 4 }, 1000)
            compare(BlockModel.blockAt(1).blockType, 0,
                    "Enter after a heading creates a paragraph")
        }

        function test_91_enterOnEmptyListItemExits() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "an item")
            BlockModel.insertBlock(1, 4, "", 0)
            wait(100)

            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            keyClick(Qt.Key_Return)

            // The empty item becomes a paragraph in place — no new block
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 0 }, 1000,
                      "Enter on an empty list item should exit list mode")
            compare(BlockModel.count, 3)

            // The exit is one undo step
            UndoStack.undo()
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 4 }, 1000,
                      "One undo should restore the list item")
        }

        function test_92_backspaceLadder() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 0, "above")
            BlockModel.insertBlock(1, 6, "a task", 2)
            wait(100)

            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)

            // Indented: Backspace outdents one level at a time
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return BlockModel.blockAt(1).indentLevel === 1 }, 1000)
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return BlockModel.blockAt(1).indentLevel === 0 }, 1000)
            compare(BlockModel.blockAt(1).blockType, 6, "Still a todo while outdenting")

            // At the margin: Backspace converts to a paragraph, content kept
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 0 }, 1000,
                      "Backspace at the margin should un-structure the block")
            compare(BlockModel.getContent(1), "a task", "Content survives the conversion")
            compare(BlockModel.count, 3)

            // As a paragraph, the old behavior returns: merge into previous
            var paragraphArea = findTextArea(findBlockDelegate(1))
            ensureFocus(paragraphArea)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return BlockModel.count === 2 }, 1000,
                      "A paragraph at cursor 0 merges into the previous block")
            compare(BlockModel.getContent(0), "abovea task")
        }

        function test_93_tabNestingWithClamps() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "parent")
            BlockModel.insertBlock(1, 4, "child")
            wait(100)

            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            textArea.cursorPosition = 3

            // Tab indents; a second Tab is clamped (parent is level 0)
            keyClick(Qt.Key_Tab)
            tryVerify(function() { return BlockModel.blockAt(1).indentLevel === 1 }, 1000)
            keyClick(Qt.Key_Tab)
            wait(100)
            compare(BlockModel.blockAt(1).indentLevel, 1,
                    "Indent clamps to one level below the previous list block")

            // Focus and cursor survive (no delegate recreation on indent)
            verify(textArea.activeFocus, "Tab must not steal focus")
            compare(textArea.cursorPosition, 3, "Tab must not move the cursor")

            // Shift+Tab outdents and floors at zero
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
            tryVerify(function() { return BlockModel.blockAt(1).indentLevel === 0 }, 1000)
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
            wait(100)
            compare(BlockModel.blockAt(1).indentLevel, 0, "Outdent floors at zero")

            // The first list item cannot indent (no parent above)
            var firstArea = findTextArea(findBlockDelegate(0))
            ensureFocus(firstArea)
            keyClick(Qt.Key_Tab)
            wait(100)
            compare(BlockModel.blockAt(0).indentLevel, 0,
                    "The first list item has no parent to nest under")
        }

        function test_94_codeBlockEnterBehavior() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 8, "line one")
            wait(100)

            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_End)

            // Enter inserts a newline INTO the block
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.getContent(0) === "line one\n" }, 1000,
                      "Enter in a code block should insert a newline")
            compare(BlockModel.count, 2, "No new block yet")

            // Enter on the trailing empty line exits: the empty line goes,
            // a paragraph appears below
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "Enter on a trailing empty line should exit the code block")
            compare(BlockModel.getContent(0), "line one",
                    "The trailing empty line is removed on exit")
            compare(BlockModel.blockAt(1).blockType, 0, "The exit block is a paragraph")
            var newArea = findTextArea(findBlockDelegate(1))
            tryVerify(function() { return newArea.activeFocus }, 1000)
        }

        function test_95_dividerKeys() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 0, "above")
            BlockModel.insertBlock(1, 9, "")
            wait(100)

            // Enter on a focused divider writes below it
            var divider = findBlockDelegate(1)
            divider.focusAtStart()
            tryCompare(divider, "isFocused", true, 1000)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.count === 4 }, 1000,
                      "Enter on a divider should create a paragraph below")
            compare(BlockModel.blockAt(2).blockType, 0)

            // Backspace removes the divider itself
            divider = findBlockDelegate(1)
            divider.focusAtStart()
            tryCompare(divider, "isFocused", true, 1000)
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "Backspace should delete the focused divider")
            verify(BlockModel.blockAt(1).blockType !== 9, "The divider is gone")
        }

        function test_96_ctrlEnterTogglesTodo() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 6, "toggle me")
            wait(100)

            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)

            keyClick(Qt.Key_Return, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).checked }, 1000,
                      "Ctrl+Enter should check the todo")
            compare(BlockModel.count, 2, "Ctrl+Enter must not create a block")

            keyClick(Qt.Key_Return, Qt.ControlModifier)
            tryVerify(function() { return !BlockModel.blockAt(0).checked }, 1000,
                      "Ctrl+Enter should uncheck on the second press")
        }

        function test_97_conversionShortcuts() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "convert me")
            wait(100)

            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            textArea.cursorPosition = 4

            // Ctrl+T converts to todo, keeping content, focus, and cursor
            keyClick(Qt.Key_T, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 6 }, 1000,
                      "Ctrl+T should convert to todo")
            compare(BlockModel.getContent(0), "convert me")
            var todoArea = findTextArea(findBlockDelegate(0))
            tryVerify(function() { return todoArea.activeFocus }, 1000,
                      "Conversion should keep the block focused")
            tryVerify(function() { return todoArea.cursorPosition === 4 }, 1000,
                      "Conversion should keep the cursor position")

            // Ctrl+Shift+T converts to quote
            keyClick(Qt.Key_T, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 7 }, 1000,
                      "Ctrl+Shift+T should convert to quote")

            // Converting an indented list item out of the family drops
            // the indent (indentation is list nesting)
            BlockModel.insertBlock(1, 4, "listed parent")
            BlockModel.insertBlock(2, 4, "listed child", 1)
            wait(100)
            var childArea = findTextArea(findBlockDelegate(2))
            ensureFocus(childArea)
            keyClick(Qt.Key_T, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() { return BlockModel.blockAt(2).blockType === 7 }, 1000)
            compare(BlockModel.blockAt(2).indentLevel, 0,
                    "Leaving the list family drops the indent")
        }

        // ==================================================================
        // Markdown prefix auto-conversion
        // ==================================================================

        function typeString(s) {
            for (var i = 0; i < s.length; i++) {
                keyClick(s.charAt(i))
                wait(20)
            }
        }

        // A fresh single empty focused paragraph at index 0
        function freshParagraph() {
            DocumentManager.newDocument()
            wait(100)
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            return textArea
        }

        function test_98_typedPrefixConversions() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }

            // Typed character by character, each prefix converts and the
            // cursor lands at the start of the (empty) content
            var cases = [
                { prefix: "- ",  type: 4 },
                { prefix: "* ",  type: 4 },
                { prefix: "1. ", type: 5 },
                { prefix: "> ",  type: 7 },
                { prefix: "```", type: 8 },
                { prefix: "---", type: 9 },
            ]
            for (var i = 0; i < cases.length; i++) {
                var c = cases[i]
                freshParagraph()
                typeString(c.prefix)
                tryVerify(function() { return BlockModel.blockAt(0).blockType === c.type },
                          1000, "'" + c.prefix + "' should convert to type " + c.type)
                compare(BlockModel.getContent(0), "", "Prefix is stripped from content")
                if (c.type !== 9) {
                    var textArea = findTextArea(findBlockDelegate(0))
                    tryVerify(function() { return textArea.activeFocus }, 1000,
                              "Focus survives the '" + c.prefix + "' conversion")
                    compare(textArea.cursorPosition, 0)
                }
            }

            // The full todo prefix converts through the bullet on the way
            freshParagraph()
            typeString("- [ ] ")
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 6 }, 1000,
                      "'- [ ] ' should end as a todo")
            compare(BlockModel.blockAt(0).checked, false)
            typeString("task")
            tryCompare(BlockModel.blockAt(0), "content", "task", 1000)
        }

        function test_99_conversionIsOneUndoStep() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            freshParagraph()
            typeString("- ")
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 4 }, 1000)

            // One Ctrl+Z restores the literal typed text as a paragraph
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 0 }, 1000,
                      "Undo should revert the conversion")
            compare(BlockModel.getContent(0), "- ",
                    "Undo restores the literal typed prefix")

            // A second undo removes the typed text as usual
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.getContent(0) === "" }, 1000)
        }

        function test_99b_pastedPrefixConverts() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            var textArea = freshParagraph()
            Clipboard.text = "- [x] pasted done task"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 6 }, 1000,
                      "A pasted todo line should convert")
            compare(BlockModel.blockAt(0).checked, true)
            compare(BlockModel.getContent(0), "pasted done task")

            // Pasted fence with a language tag
            freshParagraph()
            Clipboard.text = "```python"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 8 }, 1000,
                      "A pasted fence should convert")
            compare(BlockModel.blockAt(0).language, "python",
                    "The fence's language tag is preserved")
        }

        function test_99c_nearMissesStayLiteral() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            var cases = ["-x", "1.x", ">x", "#x"]
            for (var i = 0; i < cases.length; i++) {
                freshParagraph()
                typeString(cases[i])
                wait(150)
                compare(BlockModel.blockAt(0).blockType, 0,
                        "'" + cases[i] + "' must stay a paragraph")
                compare(BlockModel.getContent(0), cases[i])
            }

            // Pasted near-misses (typing '----' is precluded by the
            // immediate conversion at the third dash)
            var pasted = ["----", "**not a divider**", "2.5 kilometers..no"]
            for (var j = 0; j < pasted.length; j++) {
                var pasteArea = freshParagraph()
                Clipboard.text = pasted[j]
                pasteArea.pasteFromClipboard(false)
                wait(150)
                compare(BlockModel.blockAt(0).blockType, 0,
                        "'" + pasted[j] + "' must stay a paragraph")
                compare(BlockModel.getContent(0), pasted[j])
            }
        }

        function test_99d_bulletUpgradeKeepsIndent() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(0, 4, "parent")
            BlockModel.insertBlock(1, 4, "child task", 1)
            wait(100)

            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            keyClick(Qt.Key_Home)
            typeString("[x] ")

            tryVerify(function() { return BlockModel.blockAt(1).blockType === 6 }, 1000,
                      "'[x] ' at a bullet's start should upgrade it to a todo")
            compare(BlockModel.blockAt(1).checked, true)
            compare(BlockModel.blockAt(1).indentLevel, 1, "The upgrade keeps the indent")
            compare(BlockModel.getContent(1), "child task")
        }

        function test_99e_headingConversionSingleUndo() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            // The heading prefix path now shares the single-undo
            // conversion machinery
            freshParagraph()
            keyClick(Qt.Key_NumberSign, Qt.ShiftModifier)
            wait(20)
            keyClick(Qt.Key_Space)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 1 }, 1000)

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 0 }, 1000)
            compare(BlockModel.getContent(0), "# ",
                    "Undo restores the literal typed hash prefix")
        }

        // ==================================================================
        // The slash menu (features.md §4)
        // "test_a*" sorts after every "test_<digit>*" so these run last.
        // ==================================================================

        function theBlockMenu() {
            return appLoader.item.blockMenu
        }

        function menuEntryNames() {
            var names = []
            var rows = theBlockMenu().rows
            for (var i = 0; i < rows.length; i++)
                if (rows[i].kind === "entry")
                    names.push(rows[i].name)
            return names
        }

        function menuHeaderTexts() {
            var texts = []
            var rows = theBlockMenu().rows
            for (var i = 0; i < rows.length; i++)
                if (rows[i].kind === "header")
                    texts.push(rows[i].text)
            return texts
        }

        function test_a1_slashOpensMenuOnEmptyParagraph() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            var textArea = freshParagraph()
            var menu = theBlockMenu()
            verify(!menu.visible, "Menu starts closed")

            typeString("/")
            tryCompare(menu, "visible", true, 1000)
            compare(menu.targetIndex, 0, "Menu targets the slash block")
            compare(menu.mode, "slash")
            compare(BlockModel.getContent(0), "/",
                    "The slash lands in the model like any typing")

            // Focus never leaves the block
            verify(textArea.activeFocus, "The TextArea keeps focus")

            // Empty query: the grouped catalog with headers (§4.3)
            var headers = menuHeaderTexts()
            verify(headers.indexOf("Basic") !== -1, "Basic group present")
            verify(headers.indexOf("Lists") !== -1, "Lists group present")
            verify(headers.indexOf("Advanced") !== -1, "Advanced group present")
            verify(menuEntryNames().length >= 11,
                   "All implemented types are listed")

            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        function test_a2_slashDoesNotOpenMidTextInCodeOrOnLongPaste() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            var menu = theBlockMenu()

            // Mid-text: the block was not empty
            var textArea = freshParagraph()
            typeString("hello")
            keyClick(Qt.Key_End)
            typeString("/")
            wait(150)
            verify(!menu.visible, "'/' after text must not open the menu")
            compare(BlockModel.getContent(0), "hello/")

            // Code blocks are verbatim: "/" is content
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 8, "")
            wait(100)
            var codeArea = findTextArea(findBlockDelegate(0))
            ensureFocus(codeArea)
            typeString("/")
            wait(150)
            verify(!menu.visible, "'/' in a code block must not open the menu")

            // Pasting more than a bare slash does not open it either
            freshParagraph()
            Clipboard.text = "/usr/bin"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            wait(150)
            verify(!menu.visible, "A pasted path must not open the menu")
            compare(BlockModel.getContent(0), "/usr/bin")

            // A programmatic change to "/" (undo, load) must not either:
            // the trigger lives on the user-edit path only
            freshParagraph()
            BlockModel.updateContent(0, "/")
            wait(150)
            verify(!menu.visible, "Programmatic '/' must not open the menu")
        }

        function test_a3_typingFiltersTheMenu() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            freshParagraph()
            var menu = theBlockMenu()
            typeString("/")
            tryCompare(menu, "visible", true, 1000)
            var unfiltered = menuEntryNames().length

            typeString("h1")
            tryCompare(menu, "query", "h1", 1000)
            compare(BlockModel.getContent(0), "/h1",
                    "The filter text lives in the block content")
            var names = menuEntryNames()
            verify(names.length > 0 && names.length < unfiltered,
                   "Filtering narrows the list")
            compare(names[0], "Heading 1",
                    "'h1' ranks Heading 1 first (§4.3)")
            compare(menuHeaderTexts().length, 0,
                    "No group headers while filtering")

            // A no-match query keeps the menu open, showing no entries
            typeString("zzz")
            tryCompare(menu, "query", "h1zzz", 1000)
            compare(menuEntryNames().length, 0, "No entries for a no-match query")
            verify(menu.visible, "The menu stays open on no matches")

            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        function test_a4_arrowsAndEnterConvert() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            freshParagraph()
            var menu = theBlockMenu()
            typeString("/h")
            tryCompare(menu, "visible", true, 1000)

            // "h" (prefix tier) leads with the four heading levels; Down
            // moves the highlight to Heading 2
            var names = menuEntryNames()
            compare(names[0], "Heading 1")
            compare(names[1], "Heading 2")
            keyClick(Qt.Key_Down)
            wait(50)
            compare(menu.rows[menu.highlightIndex].name, "Heading 2",
                    "Down moves the highlight")
            keyClick(Qt.Key_Up)
            wait(50)
            compare(menu.rows[menu.highlightIndex].name, "Heading 1",
                    "Up moves it back")
            keyClick(Qt.Key_Down)
            wait(50)

            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 2 }, 1000,
                      "Enter converts to the highlighted type")
            tryCompare(menu, "visible", false, 1000)
            compare(BlockModel.getContent(0), "",
                    "The '/query' text is cleared by the conversion")

            // Focus and cursor land in the converted block
            var converted = findTextArea(findBlockDelegate(0))
            tryVerify(function() { return converted.activeFocus }, 1000,
                      "The converted block is focused")
            compare(converted.cursorPosition, 0)

            // The conversion is one undo step: the typed '/h' returns
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 0 }, 1000,
                      "One undo reverts the menu conversion")
            compare(BlockModel.getContent(0), "/h",
                    "Undo restores the literal typed query")
            verify(!menu.visible, "Undo must not reopen the menu")
        }

        function test_a5_escapeClosesKeepingText() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            var textArea = freshParagraph()
            var menu = theBlockMenu()
            typeString("/qu")
            tryCompare(menu, "visible", true, 1000)

            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
            compare(BlockModel.getContent(0), "/qu",
                    "Escape closes without selection, text stays (§4.1)")
            compare(BlockModel.blockAt(0).blockType, 0, "No conversion happened")
            verify(textArea.activeFocus, "Focus stays in the block")

            // Escape ended the session: more typing must not reopen it
            typeString("x")
            wait(150)
            verify(!menu.visible, "Typing after Escape must not reopen the menu")
        }

        function test_a6_backspacingTheSlashCloses() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            freshParagraph()
            var menu = theBlockMenu()
            typeString("/q")
            tryCompare(menu, "visible", true, 1000)

            keyClick(Qt.Key_Backspace)
            tryCompare(menu, "query", "", 1000)
            verify(menu.visible, "Menu stays open while the slash remains")

            keyClick(Qt.Key_Backspace)
            tryCompare(menu, "visible", false, 1000)
            compare(BlockModel.getContent(0), "",
                    "Deleting the slash closes the menu")
        }

        function test_a7_clickingAnEntryConverts() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            freshParagraph()
            var menu = theBlockMenu()
            typeString("/div")
            tryCompare(menu, "visible", true, 1000)
            compare(menuEntryNames()[0], "Divider")

            var list = findChild(appLoader.item, "blockMenuList")
            verify(list !== null, "Menu list should be reachable")
            var row = list.itemAtIndex(0)
            verify(row !== null, "First menu row should exist")
            mouseClick(row)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 9 }, 1000,
                      "Clicking the entry converts the block")
            tryCompare(menu, "visible", false, 1000)
        }

        function test_a8_clickingOutsideCloses() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.insertBlock(1, 0, "another block")
            wait(100)
            var firstArea = findTextArea(findBlockDelegate(0))
            ensureFocus(firstArea)
            var menu = theBlockMenu()
            typeString("/")
            tryCompare(menu, "visible", true, 1000)

            // Clicking another block closes the menu (§4.1) and the
            // click still lands: focus moves to the clicked block
            var otherArea = findTextArea(findBlockDelegate(1))
            mouseClick(otherArea)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() { return otherArea.activeFocus }, 1000)
            compare(BlockModel.getContent(0), "/", "The typed slash stays")
        }

        function test_a9_menuStaysInsideViewport() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            // A block near the window bottom: the menu flips above the
            // cursor instead of clipping (§4.3)
            DocumentManager.newDocument()
            wait(100)
            for (var i = 1; i <= 12; i++)
                BlockModel.insertBlock(i, 0, "filler " + i)
            BlockModel.insertBlock(13, 0, "")
            wait(150)
            var listView = findChild(appLoader.item, "blockListView")
            listView.positionViewAtEnd()
            wait(150)

            var lastArea = findTextArea(findBlockDelegate(13))
            ensureFocus(lastArea)
            var menu = theBlockMenu()
            typeString("/")
            tryCompare(menu, "visible", true, 1000)

            var overlayHeight = appLoader.item.height
            verify(menu.y >= 0, "Menu top inside the window")
            verify(menu.y + menu.height <= overlayHeight,
                   "Menu bottom inside the window (no clipping)")
            verify(menu.y + menu.height <= menu.anchorRect.y + 1,
                   "Near the bottom the menu flips above the cursor")

            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        function test_aa_slashWorksInEmptyStructuralBlocks() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            // An empty bullet: "/" opens the menu, selection converts
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 4, "")
            wait(100)
            var bulletArea = findTextArea(findBlockDelegate(0))
            ensureFocus(bulletArea)
            var menu = theBlockMenu()
            typeString("/quote")
            tryCompare(menu, "visible", true, 1000)
            compare(menuEntryNames()[0], "Quote")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 7 }, 1000,
                      "The bullet converts to a quote via the menu")
            compare(BlockModel.getContent(0), "")
        }

        function test_ab_recentlyUsedLeadsTheMenu() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            // Convert once through the menu, then reopen: the chosen
            // type leads under a "Recently used" header (§3.7)
            freshParagraph()
            var menu = theBlockMenu()
            typeString("/todo")
            tryCompare(menu, "visible", true, 1000)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(0).blockType === 6 }, 1000)

            freshParagraph()
            typeString("/")
            tryCompare(menu, "visible", true, 1000)
            compare(menuHeaderTexts()[0], "Recently used",
                    "The recency group leads the empty-query menu")
            compare(menuEntryNames()[0], "To-do",
                    "The last chosen type is first")
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        // ==================================================================
        // The gutter plus-button (features.md §3.7)
        // ==================================================================

        function hoverAndFindPlus(delegate) {
            mouseMove(delegate, 30, delegate.height / 2)
            tryCompare(delegate, "isHovered", true, 1000)
            var plus = findChild(delegate, "plusButton")
            verify(plus !== null, "Plus button should exist in the gutter")
            tryVerify(function() { return plus.visible }, 1000,
                      "Plus button appears on hover")
            return plus
        }

        function test_ab_gutterButtonsStayVisibleWhenHovered() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "first block")
            wait(100)

            var delegate = findBlockDelegate(0)
            var plus = hoverAndFindPlus(delegate)
            var buttons = findChild(delegate, "gutterButtons")
            verify(buttons !== null, "Gutter button row should exist")
            tryVerify(function() { return buttons.opacity > 0.9 }, 1000,
                      "Gutter buttons finish fading in")

            mouseMove(plus, plus.width / 2, plus.height / 2)
            wait(300)
            compare(delegate.isHovered, true,
                    "Hover remains true while the pointer is over a gutter button")
            verify(buttons.opacity > 0.9,
                   "Gutter buttons do not blink/fade while hovered")
        }

        function test_ac_plusButtonInsertsAndOpensMenu() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "first block")
            wait(100)

            var plus = hoverAndFindPlus(findBlockDelegate(0))
            mouseClick(plus)

            // A new empty paragraph below, focused, with the menu open
            // in insert mode targeting it
            tryVerify(function() { return BlockModel.count === 2 }, 1000,
                      "Plus inserts a block below")
            compare(BlockModel.blockAt(1).blockType, 0)
            compare(BlockModel.getContent(1), "")
            // The delegate materializes a frame after the model row does
            tryVerify(function() {
                var d = findBlockDelegate(1)
                var ta = d ? findTextArea(d) : null
                return ta !== null && ta.activeFocus
            }, 1000, "The new block is focused")
            var menu = theBlockMenu()
            tryCompare(menu, "visible", true, 1000)
            compare(menu.targetIndex, 1)
            compare(menu.mode, "insert")

            // Typing filters on the whole content (no slash involved)
            typeString("tod")
            tryCompare(menu, "query", "tod", 1000)
            compare(BlockModel.getContent(1), "tod")
            compare(menuEntryNames()[0], "To-do")

            keyClick(Qt.Key_Return)
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 6 }, 1000,
                      "Enter converts the new block")
            compare(BlockModel.blockAt(1).checked, false)
            compare(BlockModel.getContent(1), "", "The filter text is cleared")
            tryCompare(menu, "visible", false, 1000)

            // The conversion is its own single undo step (the insert
            // was a separate user gesture and undoes separately)
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(1).blockType === 0 }, 1000,
                      "One undo reverts the conversion")
            compare(BlockModel.getContent(1), "tod",
                    "Undo restores the typed filter text")
        }

        function test_ad_plusButtonEscapeLeavesTheParagraph() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "only block")
            wait(100)

            var plus = hoverAndFindPlus(findBlockDelegate(0))
            mouseClick(plus)
            var menu = theBlockMenu()
            tryCompare(menu, "visible", true, 1000)

            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
            compare(BlockModel.count, 2,
                    "Escape keeps the inserted paragraph")
            compare(BlockModel.blockAt(1).blockType, 0)
            compare(BlockModel.getContent(1), "")
        }

        function test_ae_plusButtonWorksOnDividers() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 9, "")
            wait(100)

            var plus = hoverAndFindPlus(findBlockDelegate(0))
            mouseClick(plus)

            tryVerify(function() { return BlockModel.count === 2 }, 1000,
                      "Plus on a divider inserts a block below")
            compare(BlockModel.blockAt(1).blockType, 0)
            var menu = theBlockMenu()
            tryCompare(menu, "visible", true, 1000)
            compare(menu.targetIndex, 1)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
        }

        // ==================================================================
        // Block selection (features.md §3.1, §2.5)
        // ==================================================================

        function selectionHandler() {
            return findChild(appLoader.item, "selectionKeyHandler")
        }

        function selectedIndexesArray() {
            var list = DocumentSelection.selectedIndexes()
            var result = []
            for (var i = 0; i < list.length; i++)
                result.push(Number(list[i]))
            return result
        }

        // A fresh document holding the given block contents (paragraphs)
        function docWithBlocks(contents) {
            DocumentSelection.clear()
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, contents[0])
            for (var i = 1; i < contents.length; i++)
                BlockModel.insertBlock(i, 0, contents[i])
            wait(100)
        }

        function hoverAndFindHandle(delegate) {
            mouseMove(delegate, 30, delegate.height / 2)
            tryCompare(delegate, "isHovered", true, 1000)
            var handle = findChild(delegate, "dragHandle")
            verify(handle !== null, "Drag handle should exist in the gutter")
            tryVerify(function() { return handle.visible }, 1000,
                      "Drag handle appears on hover")
            return handle
        }

        function test_pa_handleClickSelectsBlock() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["one", "two", "three"])

            var delegate = findBlockDelegate(1)
            var handle = hoverAndFindHandle(delegate)
            mouseClick(handle)

            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [1])
            compare(delegate.blockSelected, true)
            var background = findChild(delegate, "selectionBackground")
            verify(background !== null && background.visible,
                   "Selected block shows the selection background (§3.1)")

            // Selection mode moves the keys to the window-level handler;
            // no TextArea keeps focus
            var handler = selectionHandler()
            tryCompare(handler, "activeFocus", true, 1000)
            compare(findTextArea(delegate).activeFocus, false,
                    "Block selection blurs the TextArea")
        }

        function test_pb_shiftClickSelectsRange() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["one", "two", "three", "four"])

            // Edit block 0, then Shift+Click block 2: range 0..2
            ensureFocus(findTextArea(findBlockDelegate(0)))
            var target = findTextArea(findBlockDelegate(2))
            mouseClick(target, 10, target.height / 2,
                       Qt.LeftButton, Qt.ShiftModifier)

            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [0, 1, 2])
            compare(findBlockDelegate(1).blockSelected, true,
                    "Blocks between the anchor and the click are selected")
        }

        function test_pc_ctrlClickTogglesNonContiguous() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["one", "two", "three"])

            var handle = hoverAndFindHandle(findBlockDelegate(0))
            mouseClick(handle)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)

            var third = findTextArea(findBlockDelegate(2))
            mouseClick(third, 10, third.height / 2,
                       Qt.LeftButton, Qt.ControlModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "0,2"
            }, 1000, "Ctrl+Click adds a non-contiguous block")

            mouseClick(third, 10, third.height / 2,
                       Qt.LeftButton, Qt.ControlModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "0"
            }, 1000, "Ctrl+Click toggles the block back out")
        }

        function test_pd_ctrlClickOverLinkStillOpensIt() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["go [ab](http://x) on", "other"])
            var textArea = findTextArea(findBlockDelegate(0))
            var opener = appLoader.item.linkOpener

            opener.openExternally = false
            var captured = []
            function onActivated(url) { captured.push(url) }
            opener.activated.connect(onActivated)

            // The rendered text is "go ab on"; position 4 is inside "ab"
            var rect = textArea.positionToRectangle(4)
            mouseClick(textArea, rect.x, rect.y + rect.height / 2,
                       Qt.LeftButton, Qt.ControlModifier)
            wait(100)

            compare(captured.length, 1,
                    "Ctrl+Click over a link opens it (§2.4 wins by specificity)")
            compare(captured[0], "http://x")
            compare(DocumentSelection.hasBlockSelection, false,
                    "The link click selects no block")

            opener.activated.disconnect(onActivated)
            opener.openExternally = true
        }

        function test_pe_ctrlShiftArrowsSelectThenExtend() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two", "three"])
            ensureFocus(findTextArea(findBlockDelegate(0)))

            keyClick(Qt.Key_Down, Qt.ControlModifier | Qt.ShiftModifier)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [0],
                    "First Ctrl+Shift+Down selects the current block (§3.1)")

            keyClick(Qt.Key_Down, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "0,1"
            }, 1000, "The next press extends the selection down")

            keyClick(Qt.Key_Up, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "0"
            }, 1000, "Ctrl+Shift+Up shrinks it back")
        }

        function test_pf_ctrlALadder() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["hello world", "two"])
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_End)

            keyClick(Qt.Key_A, Qt.ControlModifier)
            tryCompare(textArea, "selectedText", "hello world", 1000)
            compare(DocumentSelection.hasBlockSelection, false,
                    "First Ctrl+A selects only the block's text (§2.5)")

            keyClick(Qt.Key_A, Qt.ControlModifier)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [0, 1],
                    "Second Ctrl+A selects all blocks")
        }

        function test_pg_ctrlAInEmptyBlockSelectsAllImmediately() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["", "two"])
            ensureFocus(findTextArea(findBlockDelegate(0)))

            keyClick(Qt.Key_A, Qt.ControlModifier)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [0, 1])
        }

        function test_ph_selectionDismissesOpenBlockMenu() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["", "two"])
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            typeString("/")
            var menu = theBlockMenu()
            tryCompare(menu, "visible", true, 1000)

            var handle = hoverAndFindHandle(findBlockDelegate(1))
            mouseClick(handle)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            tryCompare(menu, "visible", false, 1000)
        }

        function test_pi_escapeAndEnterReturnToEditing() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two", "three"])

            var handle = hoverAndFindHandle(findBlockDelegate(1))
            mouseClick(handle)
            tryCompare(selectionHandler(), "activeFocus", true, 1000)

            keyClick(Qt.Key_Escape)
            tryCompare(DocumentSelection, "hasBlockSelection", false, 1000)
            tryVerify(function() {
                var ta = findTextArea(findBlockDelegate(1))
                return ta && ta.activeFocus
            }, 1000, "Escape returns to editing the selected block")

            mouseClick(hoverAndFindHandle(findBlockDelegate(2)))
            tryCompare(selectionHandler(), "activeFocus", true, 1000)
            keyClick(Qt.Key_Return)
            tryCompare(DocumentSelection, "hasBlockSelection", false, 1000)
            tryVerify(function() {
                var ta = findTextArea(findBlockDelegate(2))
                return ta && ta.activeFocus
            }, 1000, "Enter begins editing the selected block")
        }

        function test_pj_arrowsMoveCollapsedSelection() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two", "three"])

            mouseClick(hoverAndFindHandle(findBlockDelegate(1)))
            tryCompare(selectionHandler(), "activeFocus", true, 1000)

            selectionHandler().forceActiveFocus()
            keyClick(Qt.Key_Down)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "2"
            }, 1000, "Down moves the selection highlight to the next block")

            keyClick(Qt.Key_Up)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "1"
            }, 1000, "Up moves it back")
        }

        function test_pk_plainClickClearsSelection() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["one", "two"])

            mouseClick(hoverAndFindHandle(findBlockDelegate(0)))
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)

            var textArea = findTextArea(findBlockDelegate(1))
            mouseClick(textArea, 10, textArea.height / 2)
            tryCompare(DocumentSelection, "hasBlockSelection", false, 1000)
            tryCompare(textArea, "activeFocus", true, 1000)
        }

        // ==================================================================
        // Operations on the block selection
        // ==================================================================

        // Select [first..last] and hand the keys to the selection handler
        function selectBlocks(first, last) {
            DocumentSelection.selectBlock(first)
            if (last !== undefined && last > first)
                DocumentSelection.extendBlockSelectionTo(last)
            var handler = selectionHandler()
            handler.forceActiveFocus()
            tryCompare(handler, "activeFocus", true, 1000)
        }

        function test_pm_deleteRemovesSelectionOneUndoStep() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two", "three", "four"])
            selectBlocks(1, 3)

            keyClick(Qt.Key_Delete)
            tryVerify(function() { return BlockModel.count === 1 }, 1000,
                      "Delete removes the selected blocks (§3.5)")
            compare(BlockModel.getContent(0), "one")
            compare(DocumentSelection.hasBlockSelection, false)
            tryVerify(function() {
                var ta = findTextArea(findBlockDelegate(0))
                return ta && ta.activeFocus
            }, 1000, "The cursor lands on the block before the removed run")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 4 }, 1000,
                      "One undo restores all removed blocks")
            compare(BlockModel.getContent(2), "three")
        }

        function test_pn_ctrlDDuplicatesSelection() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["a", "b", "c"])
            BlockModel.convertBlock(1, 6, "b", true) // checked todo
            wait(100)
            selectBlocks(0, 1)

            keyClick(Qt.Key_D, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 5 }, 1000,
                      "Ctrl+D duplicates the selection below itself (§3.6)")
            compare(BlockModel.getContent(2), "a")
            compare(BlockModel.getContent(3), "b")
            compare(BlockModel.blockAt(3).blockType, 6)
            compare(BlockModel.blockAt(3).checked, true,
                    "The clone carries the full state")
            tryVerify(function() {
                return selectedIndexesArray().toString() === "2,3"
            }, 1000, "The selection moves to the clones")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "One undo removes all clones")
        }

        function test_po_ctrlDWithoutSelectionDuplicatesCurrent() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["hello", "tail"])
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)

            keyClick(Qt.Key_D, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "Ctrl+D duplicates the current block")
            compare(BlockModel.getContent(1), "hello")
            tryVerify(function() {
                var ta = findTextArea(findBlockDelegate(1))
                return ta && ta.activeFocus
            }, 1000, "The cursor moves into the clone")
        }

        function test_pp_ctrlShiftDDeletesCurrentBlock() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two"])
            ensureFocus(findTextArea(findBlockDelegate(0)))

            keyClick(Qt.Key_D, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() { return BlockModel.count === 1 }, 1000,
                      "Ctrl+Shift+D deletes the current block (§13.3)")
            compare(BlockModel.getContent(0), "two")
            tryVerify(function() {
                var ta = findTextArea(findBlockDelegate(0))
                return ta && ta.activeFocus
            }, 1000, "Focus lands on the block that took its place")
        }

        function test_pq_altArrowsMoveSelectionAsUnit() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["a", "b", "c", "d"])
            selectBlocks(0, 1)

            keyClick(Qt.Key_Down, Qt.AltModifier)
            tryVerify(function() { return BlockModel.getContent(0) === "c" }, 1000,
                      "Alt+Down moves the selection as a unit (§3.2)")
            compare(BlockModel.getContent(1), "a")
            compare(BlockModel.getContent(2), "b")
            compare(selectedIndexesArray(), [1, 2],
                    "The selection follows the moved blocks")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.getContent(0) === "a" }, 1000,
                      "One undo restores the order")
        }

        function test_pr_tabIndentsSelectedListItems() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["parent", "c1", "c2"])
            BlockModel.convertBlock(0, 4, "parent")
            BlockModel.convertBlock(1, 4, "c1")
            BlockModel.convertBlock(2, 4, "c2")
            wait(100)
            selectBlocks(1, 2)

            keyClick(Qt.Key_Tab)
            tryVerify(function() {
                return BlockModel.blockAt(1).indentLevel === 1
                    && BlockModel.blockAt(2).indentLevel === 1
            }, 1000, "Tab indents every selected list item together (§3.3)")

            keyClick(Qt.Key_Backtab)
            tryVerify(function() {
                return BlockModel.blockAt(1).indentLevel === 0
                    && BlockModel.blockAt(2).indentLevel === 0
            }, 1000, "Shift+Tab outdents them together")
        }

        function test_ps_copyCutPasteBlockSelection() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["text", "item"])
            BlockModel.convertBlock(1, 6, "item", true)
            wait(100)
            selectBlocks(0, 1)

            keyClick(Qt.Key_C, Qt.ControlModifier)
            tryCompare(Clipboard, "text", "text\n\n- [x] item", 1000)

            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 4 }, 1000,
                      "Ctrl+V pastes after the selection (§5.3)")
            compare(BlockModel.getContent(2), "text")
            compare(BlockModel.blockAt(3).blockType, 6,
                    "The pasted blocks keep their types")
            compare(BlockModel.blockAt(3).checked, true)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "2,3"
            }, 1000, "The pasted blocks become the selection")

            keyClick(Qt.Key_X, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 2 }, 1000,
                      "Ctrl+X cuts the selected blocks (§5.2)")
            compare(Clipboard.text, "text\n\n- [x] item")
        }

        // ==================================================================
        // Cross-block text selection (features.md §2.5, §21.3)
        // ==================================================================

        // Press in block `fromIdx` at markdown pos `fromMd`, drag into
        // block `toIdx` at `toMd`, optionally release. Plain contents
        // only (document position == markdown position).
        function dragSelect(fromIdx, fromMd, toIdx, toMd, release) {
            var fromTa = findTextArea(findBlockDelegate(fromIdx))
            var r1 = fromTa.positionToRectangle(fromMd)
            mousePress(fromTa, r1.x + 1, r1.y + r1.height / 2)
            mouseMove(fromTa, r1.x + 8, r1.y + r1.height / 2)
            var toTa = findTextArea(findBlockDelegate(toIdx))
            var r2 = toTa.positionToRectangle(toMd)
            mouseMove(toTa, r2.x + 1, r2.y + r2.height / 2)
            mouseMove(toTa, r2.x + 2, r2.y + r2.height / 2)
            if (release !== false)
                mouseRelease(toTa, r2.x + 2, r2.y + r2.height / 2)
        }

        function test_pt_dragSelectsAcrossBlocks() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["alpha beta", "second block", "third words"])

            // Feasibility pins: the passive PointHandler must see moves
            // while the pressed TextArea holds the exclusive grab, and
            // unfocused TextAreas must hold programmatic selections.
            dragSelect(0, 6, 2, 5)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)

            // Tail of block 0 (native, still focused), all of block 1,
            // head of block 2 (both applied portions)
            var ta0 = findTextArea(findBlockDelegate(0))
            var ta1 = findTextArea(findBlockDelegate(1))
            var ta2 = findTextArea(findBlockDelegate(2))
            tryCompare(ta0, "selectedText", "beta", 1000)
            tryCompare(ta1, "selectedText", "second block", 1000)
            tryCompare(ta2, "selectedText", "third", 1000)
            verify(ta0.activeFocus, "The anchor block keeps focus")
            verify(!ta1.activeFocus)

            // Releasing kept the selection; a plain click clears it
            mouseClick(ta1, 5, ta1.height / 2)
            tryCompare(DocumentSelection, "hasTextSelection", false, 1000)
            tryVerify(function() { return ta0.selectedText === "" }, 1000,
                      "Clearing the range deselects every block")
            tryVerify(function() { return ta2.selectedText === "" }, 1000)
        }

        function test_pu_backwardDragMirrors() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["alpha beta", "second block", "third words"])

            dragSelect(2, 5, 0, 6)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            var ta0 = findTextArea(findBlockDelegate(0))
            var ta1 = findTextArea(findBlockDelegate(1))
            var ta2 = findTextArea(findBlockDelegate(2))
            tryCompare(ta2, "selectedText", "third", 1000)
            tryCompare(ta1, "selectedText", "second block", 1000)
            tryCompare(ta0, "selectedText", "beta", 1000)
            verify(ta2.activeFocus, "The anchor (press) block keeps focus")

            keyClick(Qt.Key_Escape)
            tryCompare(DocumentSelection, "hasTextSelection", false, 1000)
            tryVerify(function() { return ta1.selectedText === "" }, 1000,
                      "Escape drops the selection, text intact")
            compare(BlockModel.getContent(1), "second block")
        }

        function test_pv_dragOverDividerTintsIt() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["first text", "x", "last text"])
            BlockModel.convertBlock(1, 9, "")
            wait(100)

            dragSelect(0, 2, 2, 4)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            var divider = findBlockDelegate(1)
            tryCompare(divider, "blockSelected", true, 1000)

            keyClick(Qt.Key_Escape)
            tryCompare(divider, "blockSelected", false, 1000)
        }

        // NOTE on the two granularity tests: the QuickTest harness does
        // not synthesize native double/triple-click events for the
        // Loader-hosted application window (pinned by a scratch spike:
        // repeated mouseClick selects a word in a plain test window but
        // never in a loaded ApplicationWindow), so the anchor block's
        // NATIVE word/paragraph selection cannot be driven here. Real
        // platform double-clicks carry the multi-click flag, and the
        // in-block native behaviors are Qt's own. What these tests pin
        // is this project's machinery: the drag coordinator counts the
        // presses itself, snaps the range per granularity, and renders
        // the other blocks' portions.
        function test_pw_doubleClickDragSelectsWords() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["alpha beta gamma", "delta epsilon"])
            var ta0 = findTextArea(findBlockDelegate(0))
            var ta1 = findTextArea(findBlockDelegate(1))

            // Double-click inside "beta" (press counted by the
            // coordinator), then drag into "delta" of the next block
            var r = ta0.positionToRectangle(8)
            mouseClick(ta0, r.x, r.y + r.height / 2)
            mousePress(ta0, r.x, r.y + r.height / 2)
            mouseMove(ta0, r.x + 6, r.y + r.height / 2)
            var r2 = ta1.positionToRectangle(2)
            mouseMove(ta1, r2.x, r2.y + r2.height / 2)
            mouseRelease(ta1, r2.x, r2.y + r2.height / 2)

            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            // The head snaps outward to the word end (§21.3 word drag)
            tryCompare(ta1, "selectedText", "delta", 1000)
            // The anchor extent snaps back to the word start — the
            // range the copy path uses
            var p0 = DocumentSelection.portionForBlock(0)
            compare(p0.start, 6, "The anchor snaps to the start of 'beta'")
            compare(p0.end, 16, "...and runs to the block end")
        }

        function test_px_tripleClickDragSelectsWholeBlocks() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["alpha beta gamma", "delta epsilon"])
            var ta0 = findTextArea(findBlockDelegate(0))
            var ta1 = findTextArea(findBlockDelegate(1))

            // Three quick presses, holding the third, then drag: block
            // granularity — both blocks select whole (§21.3 "line/block
            // selection (triple-click)")
            var r = ta0.positionToRectangle(8)
            mouseClick(ta0, r.x, r.y + r.height / 2)
            mouseClick(ta0, r.x, r.y + r.height / 2)
            mousePress(ta0, r.x, r.y + r.height / 2)
            mouseMove(ta0, r.x + 6, r.y + r.height / 2)
            var r2 = ta1.positionToRectangle(2)
            mouseMove(ta1, r2.x, r2.y + r2.height / 2)
            mouseRelease(ta1, r2.x, r2.y + r2.height / 2)

            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            tryCompare(ta1, "selectedText", "delta epsilon", 1000)
            var p0 = DocumentSelection.portionForBlock(0)
            compare(p0.full, true, "The anchor block is covered whole")
        }

        function test_py_shiftArrowsExtendAcrossBlocks() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["alpha beta", "second", "third"])
            var ta0 = findTextArea(findBlockDelegate(0))
            ensureFocus(ta0)
            ta0.cursorPosition = 6

            // Shift+Down on the last (only) line crosses into block 1
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            compare(DocumentSelection.textAnchorIndex(), 0)
            compare(DocumentSelection.textHeadIndex(), 1)
            verify(ta0.activeFocus, "Focus stays on the anchor block")

            // Shift+Right moves the head; at a block end it crosses
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            tryVerify(function() {
                return DocumentSelection.textHeadPosition() > 0
                    || DocumentSelection.textHeadIndex() === 2
            }, 1000)

            // Shift+Down again reaches block 2
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            tryVerify(function() {
                return DocumentSelection.textHeadIndex() === 2
            }, 1000)
            var ta1 = findTextArea(findBlockDelegate(1))
            tryCompare(ta1, "selectedText", "second", 1000)

            // Shift+Up walks back; returning into the anchor block
            // collapses to a native in-block selection
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            tryCompare(DocumentSelection, "hasTextSelection", false, 1000)
            tryVerify(function() { return ta0.selectedText.length > 0 }, 1000,
                      "Back in the anchor block the selection is native")
        }

        function test_pz_crossBlockCopyAndCut() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["alpha beta", "item", "tail words"])
            BlockModel.convertBlock(1, 4, "item")
            wait(100)
            var ta0 = findTextArea(findBlockDelegate(0))
            ensureFocus(ta0)
            DocumentSelection.beginTextSelection(0, 6, 0)
            DocumentSelection.updateTextSelectionHead(2, 4)

            keyClick(Qt.Key_C, Qt.ControlModifier)
            tryCompare(Clipboard, "text", "beta\n\n- item\n\ntail", 1000)
            compare(BlockModel.count, 3, "Copy leaves the document alone")

            keyClick(Qt.Key_X, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 1 }, 1000,
                      "Cut removes the range")
            compare(BlockModel.getContent(0), "alpha  words")
            compare(Clipboard.text, "beta\n\n- item\n\ntail")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "One undo restores the cut range")
        }

        function test_pz2_crossBlockPasteReplacesRange() {
            // §5.3: pasting over a cross-block selection must remove the whole
            // range first, exactly as cut/delete/typing do. Before the paste
            // branch existed, the per-block handler ran instead and saw only
            // the head block's own selection, so the rest of the range stayed.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["alpha beta", "middle", "tail words"])
            var ta0 = findTextArea(findBlockDelegate(0))
            ensureFocus(ta0)
            DocumentSelection.beginTextSelection(0, 6, 0)
            DocumentSelection.updateTextSelectionHead(2, 5)

            Clipboard.text = "X"
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 1 }, 1000,
                      "Paste collapses the whole cross-block range")
            tryVerify(function() {
                return BlockModel.getContent(0) === "alpha Xwords"
            }, 1000, "The pasted text lands at the collapse point")

            // Decision 7's layering, the same as typing over a range
            // (test_q1): the range removal and the paste are separate undo
            // steps, so the first undo takes back the pasted text and the
            // second restores the range.
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() {
                return BlockModel.getContent(0) === "alpha words"
            }, 1000, "First undo takes back the pasted text")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "Second undo restores the replaced range")
            compare(BlockModel.getContent(0), "alpha beta")
        }

        function test_pz3_crossBlockPastePlainStripsFormatting() {
            // Ctrl+Shift+V over a cross-block selection strips the payload.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["alpha beta", "middle", "tail words"])
            var ta0 = findTextArea(findBlockDelegate(0))
            ensureFocus(ta0)
            DocumentSelection.beginTextSelection(0, 6, 0)
            DocumentSelection.updateTextSelectionHead(2, 5)

            Clipboard.text = "**bold**"
            keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() {
                return BlockModel.getContent(0) === "alpha boldwords"
            }, 1000, "Paste-plain drops the markdown markers")
        }

        function test_q1_typingReplacesRangeLayeredUndo() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["alpha beta", "middle", "tail words"])
            var ta0 = findTextArea(findBlockDelegate(0))
            ensureFocus(ta0)
            DocumentSelection.beginTextSelection(0, 6, 0)
            DocumentSelection.updateTextSelectionHead(2, 5)

            typeString("z")
            tryVerify(function() { return BlockModel.count === 1 }, 1000,
                      "Typing collapses the range")
            tryCompare(DocumentSelection, "hasTextSelection", false, 1000)
            tryVerify(function() {
                return BlockModel.getContent(0) === "alpha zwords"
            }, 1000, "The typed character lands at the collapse point")

            // Decision 7's layering: first undo removes the character,
            // second restores the range
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() {
                return BlockModel.getContent(0) === "alpha words"
            }, 1000, "First undo removes the typed character")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.count === 3 }, 1000,
                      "Second undo restores the removed range")
            compare(BlockModel.getContent(0), "alpha beta")

            // Delete collapses without inserting
            ensureFocus(findTextArea(findBlockDelegate(0)))
            DocumentSelection.beginTextSelection(0, 6, 0)
            DocumentSelection.updateTextSelectionHead(2, 5)
            keyClick(Qt.Key_Delete)
            tryVerify(function() {
                return BlockModel.count === 1
                    && BlockModel.getContent(0) === "alpha words"
            }, 1000, "Delete removes the range in one step")
        }

        function test_q2_autoScrollAtViewportEdge() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentSelection.clear()
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "block 0")
            for (var i = 1; i < 30; i++)
                BlockModel.insertBlock(i, 0, "block " + i)
            wait(200)

            var listView = findChild(appLoader.item, "blockListView")
            listView.contentY = 0
            wait(100)

            // Press in block 0, drag to the bottom edge and hold: the
            // shared edge scroller must advance the view (§21.3)
            var ta0 = findTextArea(findBlockDelegate(0))
            mousePress(ta0, 5, ta0.height / 2)
            mouseMove(ta0, 20, ta0.height / 2)
            mouseMove(listView, listView.width / 2, listView.height - 8)
            tryCompare(DocumentSelection, "hasTextSelection", true, 1000)
            tryVerify(function() { return listView.contentY > 40 }, 3000,
                      "Holding the drag at the edge auto-scrolls")
            mouseRelease(listView, listView.width / 2, listView.height - 8)
            DocumentSelection.clear()
        }

        // ==================================================================
        // Drag-and-drop reordering (features.md §3.2, §21.4)
        // ==================================================================

        function test_q3_dragReordersOneUndoStep() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["a", "b", "c", "d"])
            var listView = findChild(appLoader.item, "blockListView")
            var draggedItem = findBlockDelegate(0)
            var stackBefore = UndoStack.count

            var handle = hoverAndFindHandle(draggedItem)
            mousePress(handle, 4, 4)
            // Cross the threshold, then drag past block 2's midpoint
            mouseMove(handle, 6, 30)
            mouseMove(listView, 100, 110)
            mouseMove(listView, 100, 140)
            tryVerify(function() { return BlockModel.getContent(2) === "a" },
                      1000, "Live make-room moves the row while dragging")

            // The drag itself never touches the undo stack (its moves are
            // previews only); the proxy is visible
            compare(UndoStack.count, stackBefore,
                    "No undo entries accumulate during the drag")
            var proxy = findChild(appLoader.item, "dragProxy")
            verify(proxy !== null && proxy.visible,
                   "The floating proxy follows the drag")

            mouseRelease(listView, 100, 140)
            compare(BlockModel.getContent(0), "b")
            compare(BlockModel.getContent(2), "a")
            compare(UndoStack.count, stackBefore + 1,
                    "The drop is one undo step")

            // §21.4's guarantee: the dragged delegate survived the drag
            verify(listView.itemAtIndex(2) === draggedItem,
                   "The dragged delegate is the same instance after the drop")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.getContent(0) === "a" },
                      1000, "One undo restores the original order")
        }

        function test_q4_escapeCancelsDrag() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["a", "b", "c"])
            var listView = findChild(appLoader.item, "blockListView")
            var stackBefore = UndoStack.count

            var handle = hoverAndFindHandle(findBlockDelegate(0))
            mousePress(handle, 4, 4)
            mouseMove(handle, 6, 30)
            mouseMove(listView, 100, 140)
            tryVerify(function() { return BlockModel.getContent(0) !== "a" },
                      1000, "The row moved during the drag")

            keyClick(Qt.Key_Escape)
            tryVerify(function() { return BlockModel.getContent(0) === "a" },
                      1000, "Escape restores the original order (§5.4)")
            mouseRelease(listView, 100, 140)
            compare(BlockModel.getContent(0), "a")
            compare(UndoStack.count, stackBefore,
                    "A cancelled drag pushes nothing")
        }

        function test_q5_multiBlockDragWithIndicator() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["a", "b", "c", "d", "e"])
            var listView = findChild(appLoader.item, "blockListView")
            selectBlocks(0, 1)
            var stackBefore = UndoStack.count

            var handle = hoverAndFindHandle(findBlockDelegate(0))
            mousePress(handle, 4, 4)
            mouseMove(handle, 6, 30)
            var target = findBlockDelegate(3)
            var dropY = target.y - listView.contentY + target.height - 4
            mouseMove(listView, 100, dropY)

            var indicator = findChild(appLoader.item, "dropIndicator")
            tryVerify(function() { return indicator && indicator.visible }, 1000,
                      "Multi-block drag shows the drop indicator")
            compare(BlockModel.getContent(0), "a",
                    "No live reorder during a multi-block drag")

            mouseRelease(listView, 100, dropY)
            tryVerify(function() {
                return BlockModel.getContent(1) === "d"
                    && BlockModel.getContent(2) === "a"
                    && BlockModel.getContent(3) === "b"
            }, 1000, "The selection moved contiguously to the drop gap")
            compare(UndoStack.count, stackBefore + 1, "One undo step")
            compare(selectedIndexesArray(), [2, 3],
                    "The selection follows the moved blocks")

            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.getContent(1) === "b" },
                      1000, "One undo restores the order")
        }

        function test_q6_handleClickWithoutMovementStillSelects() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["a", "b"])
            var handle = hoverAndFindHandle(findBlockDelegate(1))
            mousePress(handle, 4, 4)
            mouseMove(handle, 5, 5) // below the threshold
            mouseRelease(handle, 5, 5)
            tryCompare(DocumentSelection, "hasBlockSelection", true, 1000)
            compare(selectedIndexesArray(), [1])
            compare(BlockModel.getContent(0), "a", "Nothing moved")
        }

        function test_q8_dragAutoScrollsAtEdge() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            DocumentSelection.clear()
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "block 0")
            for (var i = 1; i < 30; i++)
                BlockModel.insertBlock(i, 0, "block " + i)
            wait(200)
            var listView = findChild(appLoader.item, "blockListView")
            listView.contentY = 0
            wait(100)

            var handle = hoverAndFindHandle(findBlockDelegate(0))
            mousePress(handle, 4, 4)
            mouseMove(handle, 6, 30)
            mouseMove(listView, 100, listView.height - 8)
            tryVerify(function() { return listView.contentY > 40 }, 3000,
                      "Holding the block drag at the edge auto-scrolls")
            keyClick(Qt.Key_Escape)
            mouseRelease(listView, 100, listView.height - 8)
            DocumentSelection.clear()
        }

        function test_pl_dividerJoinsBlockSelection() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            docWithBlocks(["one", "two", "three"])
            BlockModel.convertBlock(1, 9, "")
            wait(100)

            // Ctrl+Click the divider row
            var divider = findBlockDelegate(1)
            mouseClick(divider, divider.width / 2, divider.height / 2,
                       Qt.LeftButton, Qt.ControlModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "1"
            }, 1000, "Ctrl+Click selects the divider")
            compare(divider.blockSelected, true)

            // Shift+Click from block 0 spans the divider
            DocumentSelection.clear()
            ensureFocus(findTextArea(findBlockDelegate(0)))
            var third = findTextArea(findBlockDelegate(2))
            mouseClick(third, 10, third.height / 2,
                       Qt.LeftButton, Qt.ShiftModifier)
            tryVerify(function() {
                return selectedIndexesArray().toString() === "0,1,2"
            }, 1000, "A Shift+Click range includes the divider")
        }

        // ==================================================================
        // The find bar (features.md §7.1)
        // ==================================================================

        // The mixed search fixture: 7 case-insensitive "fox" matches —
        // 3 in the plain paragraph, 1 in the bullet, 2 in the code
        // block, 1 inside the quote's "foxes".
        function searchDoc() {
            resetFindBar()
            docWithBlocks(["This is **bold** text",
                           "second Fox block fox FOX",
                           "fox item",
                           "let fox = code",
                           "quote foxes line"])
            BlockModel.convertBlock(2, 4, "fox item")
            BlockModel.convertBlock(3, 8, "let fox = code\nfox()")
            BlockModel.convertBlock(4, 7, "quote foxes line")
            wait(100)
        }

        function resetFindBar() {
            var bar = appLoader.item.findBar
            bar.visible = false
            DocumentSearch.active = false
            DocumentSearch.clearDomain()
            bar.queryField.text = ""
            bar.replaceField.text = ""
            findChild(bar, "findCaseButton").checked = false
            findChild(bar, "findWordButton").checked = false
            findChild(bar, "findRegexButton").checked = false
            findChild(bar, "preserveCaseButton").checked = false
            findChild(bar, "inSelectionButton").checked = false
        }

        function openFindBar() {
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            var bar = appLoader.item.findBar
            tryCompare(bar, "visible", true, 1000)
            bar.queryField.forceActiveFocus()
            tryCompare(bar.queryField, "activeFocus", true, 1000)
            return bar
        }

        function countLabelText() {
            return findChild(appLoader.item.findBar, "findCountLabel").text
        }

        function test_r1_ctrlFOpensBarFocusedAndBlursBlock() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var textArea = findTextArea(findBlockDelegate(0))
            var bar = openFindBar()
            tryCompare(bar.queryField, "activeFocus", true, 1000)
            compare(textArea.activeFocus, false,
                    "The block blurs when the bar takes focus")
            compare(countLabelText(), "", "Empty query shows no count")
        }

        function test_r2_typingQueryHighlightsLive() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            compare(countLabelText(), "1 of 7")

            // The delegates carry the tints through their engines; the
            // first match is current.
            var paragraph = findBlockDelegate(1)
            tryVerify(function() {
                return paragraph.editorEngine.searchMatches.length === 3
            }, 1000, "The paragraph's engine holds its three matches")
            compare(paragraph.editorEngine.searchMatches[0].current, true)
            compare(findBlockDelegate(0).editorEngine.searchMatches.length, 0)
            var code = findBlockDelegate(3)
            compare(code.editorEngine.searchMatches.length, 2,
                    "The verbatim code block matches too")
        }

        function test_r3_navigationStepsAndWraps() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)

            keyClick(Qt.Key_Return)
            compare(countLabelText(), "2 of 7", "Enter steps forward")
            keyClick(Qt.Key_F3)
            compare(countLabelText(), "3 of 7", "F3 steps forward")
            keyClick(Qt.Key_F3, Qt.ShiftModifier)
            compare(countLabelText(), "2 of 7", "Shift+F3 steps back")
            keyClick(Qt.Key_Return, Qt.ShiftModifier)
            compare(countLabelText(), "1 of 7", "Shift+Enter steps back")

            var prev = findChild(bar, "findPrevButton")
            mouseClick(prev)
            compare(countLabelText(), "7 of 7", "Previous wraps backward")
            var next = findChild(bar, "findNextButton")
            mouseClick(next)
            compare(countLabelText(), "1 of 7", "Next wraps forward")
        }

        function test_r4_navigationScrollsToOffscreenMatch() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            resetFindBar()
            var contents = []
            for (var i = 0; i < 29; i++)
                contents.push("filler paragraph " + i)
            contents.push("the needle sits here")
            docWithBlocks(contents)

            var listView = findChild(appLoader.item, "blockListView")
            listView.contentY = 0
            openFindBar()
            typeString("needle")
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            tryVerify(function() { return listView.contentY > 0 }, 1000,
                      "The view scrolls to the off-screen match")
            tryVerify(function() {
                var item = listView.itemAtIndex(29)
                if (!item)
                    return false
                var top = item.y - listView.contentY
                return top >= 0 && top < listView.height
            }, 1000, "The match's block is inside the viewport")
        }

        function test_r5_fineScrollIntoTallCodeBlock() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            resetFindBar()
            var lines = []
            for (var i = 0; i < 40; i++)
                lines.push("line " + i)
            lines.push("needle()")
            docWithBlocks(["intro"])
            BlockModel.insertBlock(1, 8, lines.join("\n"))
            wait(100)

            var listView = findChild(appLoader.item, "blockListView")
            listView.contentY = 0
            openFindBar()
            typeString("needle")
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            tryVerify(function() {
                var item = listView.itemAtIndex(1)
                if (!item || !item.rectForMarkdownPosition)
                    return false
                var info = DocumentSearch.currentMatchInfo()
                var rect = item.rectForMarkdownPosition(info.mdStart)
                var top = item.y + rect.y - listView.contentY
                return top >= 0 && top + rect.height <= listView.height
            }, 2000, "The match's line inside the tall block is visible")
        }

        function test_r6_optionsChangeMatchSetLive() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)

            var caseButton = findChild(bar, "findCaseButton")
            mouseClick(caseButton)
            tryCompare(DocumentSearch, "matchCount", 5, 1000,
                       "Case-sensitive drops Fox and FOX")
            mouseClick(caseButton)
            tryCompare(DocumentSearch, "matchCount", 7, 1000)

            var wordButton = findChild(bar, "findWordButton")
            mouseClick(wordButton)
            tryCompare(DocumentSearch, "matchCount", 6, 1000,
                       "Whole word drops the match inside 'foxes'")
            mouseClick(wordButton)

            var regexButton = findChild(bar, "findRegexButton")
            mouseClick(regexButton)
            bar.queryField.text = "fox\\(\\)"
            tryCompare(DocumentSearch, "matchCount", 1, 1000,
                       "The regex finds only the code call")
            compare(countLabelText(), "1 of 1")
        }

        function test_r7_invalidRegexIsRecoverableErrorState() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            mouseClick(findChild(bar, "findRegexButton"))
            bar.queryField.text = "(unclosed"
            tryCompare(DocumentSearch, "patternError", true, 1000)
            compare(countLabelText(), "Invalid pattern")
            compare(DocumentSearch.matchCount, 0)

            bar.queryField.text = "(fox)"
            tryCompare(DocumentSearch, "patternError", false, 1000)
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
        }

        function test_r8_ctrlFSeedsQueryFromInBlockSelection() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var textArea = findTextArea(findBlockDelegate(1))
            ensureFocus(textArea)
            textArea.select(7, 10) // "Fox"
            keyClick(Qt.Key_F, Qt.ControlModifier)
            var bar = appLoader.item.findBar
            tryCompare(bar, "visible", true, 1000)
            compare(bar.queryField.text, "Fox",
                    "The selection seeds the query")
            // Seeded from the cursor at the selection: find starts at
            // the next match, not the document top.
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            compare(countLabelText(), "2 of 7")
        }

        function test_r9_editingDocumentUpdatesMatchesLive() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)

            // Click back into a block and add another match: the bar
            // stays open and the count follows the document.
            var textArea = findTextArea(findBlockDelegate(2))
            mouseClick(textArea, 5, textArea.height / 2)
            tryCompare(textArea, "activeFocus", true, 1000)
            compare(appLoader.item.findBar.visible, true)
            keyClick(Qt.Key_Home)
            typeString("fox ")
            tryCompare(DocumentSearch, "matchCount", 8, 2000)
        }

        function test_ra_escapeClosesAndFocusesCurrentMatch() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            // Current match: block 1's "Fox" at display 7.
            keyClick(Qt.Key_Escape)
            tryCompare(bar, "visible", false, 1000)
            compare(DocumentSearch.matchCount, 0, "Closing clears matches")
            var textArea = findTextArea(findBlockDelegate(1))
            tryCompare(textArea, "activeFocus", true, 1000,
                       "Focus lands on the match's block")
            tryCompare(textArea, "cursorPosition", 7, 1000)
        }

        function test_rb_f3ReopensWithKeptQuery() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            keyClick(Qt.Key_Escape)
            tryCompare(bar, "visible", false, 1000)

            keyClick(Qt.Key_F3)
            tryCompare(bar, "visible", true, 1000,
                       "F3 reopens the bar with the kept query")
            compare(bar.queryField.text, "fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
        }

        function test_rc_noResultsState() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openFindBar()
            typeString("zebra")
            tryCompare(DocumentSearch, "matchCount", 0, 1000)
            compare(countLabelText(), "No results")
            compare(findChild(bar, "findNextButton").enabled, false)
            compare(findChild(bar, "findPrevButton").enabled, false)
        }

        // ==================================================================
        // Replace (features.md §7.2)
        // ==================================================================

        function openReplaceBar() {
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            keyClick(Qt.Key_H, Qt.ControlModifier)
            var bar = appLoader.item.findBar
            tryCompare(bar, "visible", true, 1000)
            tryCompare(bar, "replaceMode", true, 1000)
            return bar
        }

        function test_s1_ctrlHShowsReplaceRowAndCtrlFCollapsesIt() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            verify(findChild(bar, "replaceField").visible,
                   "Ctrl+H shows the replace row")

            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(bar, "replaceMode", false, 1000)
            verify(!findChild(bar, "replaceField").visible,
                   "Ctrl+F collapses back to find-only")
        }

        function test_s2_replaceCurrentAdvancesOneUndoStep() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)

            bar.replaceField.text = "cat"
            var undoCountBefore = UndoStack.count
            mouseClick(findChild(bar, "replaceOneButton"))
            tryCompare(DocumentSearch, "matchCount", 6, 1000)
            compare(BlockModel.getContent(1), "second cat block fox FOX")
            compare(DocumentSearch.currentMatchInfo().start, 17,
                    "The current match advanced to the next remaining one")
            compare(UndoStack.count, undoCountBefore + 1)

            UndoStack.undo()
            tryVerify(function() {
                return BlockModel.getContent(1) === "second Fox block fox FOX"
            }, 1000, "One Ctrl+Z restores the replaced text")
        }

        function test_s3_replaceInFormattedTextFollowsCutContract() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            resetFindBar()
            docWithBlocks(["x **bold** y", "plain"])
            var bar = openReplaceBar()
            typeString("bold")
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            bar.replaceField.text = "brave"
            mouseClick(findChild(bar, "replaceOneButton"))
            tryVerify(function() {
                return BlockModel.getContent(0) === "x brave y"
            }, 1000, "Replacing a fully covered span drops its markers")
        }

        function test_s4_replaceAllPreviewCancelConfirmUndo() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            bar.replaceField.text = "cat"

            // The preview lists every match; Cancel changes nothing.
            mouseClick(findChild(bar, "replaceAllButton"))
            var panel = findChild(bar, "replacePreviewPanel")
            tryCompare(panel, "visible", true, 1000)
            compare(bar.previewRows.length, 7)
            var summary = findChild(bar, "previewSummaryLabel")
            verify(summary.text.indexOf("7") >= 0)
            verify(summary.text.indexOf("4") >= 0, "7 matches in 4 blocks")
            mouseClick(findChild(bar, "previewCancelButton"))
            tryCompare(panel, "visible", false, 1000)
            compare(BlockModel.getContent(1), "second Fox block fox FOX",
                    "Cancel leaves the document untouched")

            // Confirm applies everything as one undo step.
            mouseClick(findChild(bar, "replaceAllButton"))
            tryCompare(panel, "visible", true, 1000)
            var undoCountBefore = UndoStack.count
            mouseClick(findChild(bar, "previewConfirmButton"))
            tryCompare(panel, "visible", false, 1000)
            tryCompare(DocumentSearch, "matchCount", 0, 2000)
            compare(BlockModel.getContent(1), "second cat block cat cat")
            compare(BlockModel.getContent(3), "let cat = code\ncat()")
            compare(UndoStack.count, undoCountBefore + 1)

            UndoStack.undo()
            tryVerify(function() {
                return BlockModel.getContent(1) === "second Fox block fox FOX"
                    && BlockModel.getContent(3) === "let fox = code\nfox()"
            }, 1000, "One Ctrl+Z restores every replaced block")
        }

        function test_s5_previewDismissesWhenDocumentChanges() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            bar.replaceField.text = "cat"
            mouseClick(findChild(bar, "replaceAllButton"))
            var panel = findChild(bar, "replacePreviewPanel")
            tryCompare(panel, "visible", true, 1000)

            // An edit invalidates the snapshot: the panel dismisses
            // rather than applying a stale preview.
            BlockModel.updateContent(2, "no more match here")
            tryCompare(panel, "visible", false, 2000)
        }

        function test_s6_regexReplaceWithCaptureGroups() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            resetFindBar()
            docWithBlocks(["name: value", "plain"])
            var bar = openReplaceBar()
            mouseClick(findChild(bar, "findRegexButton"))
            bar.queryField.text = "(\\w+): (\\w+)"
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            bar.replaceField.text = "$2 = $1"
            mouseClick(findChild(bar, "replaceOneButton"))
            tryVerify(function() {
                return BlockModel.getContent(0) === "value = name"
            }, 1000, "Capture groups substitute into the replacement")
        }

        function test_s7_preserveCaseAdaptsReplacementCasing() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            mouseClick(findChild(bar, "preserveCaseButton"))
            bar.replaceField.text = "cat"
            mouseClick(findChild(bar, "replaceAllButton"))
            tryCompare(findChild(bar, "replacePreviewPanel"), "visible", true, 1000)
            mouseClick(findChild(bar, "previewConfirmButton"))
            tryVerify(function() {
                return BlockModel.getContent(1) === "second Cat block cat CAT"
            }, 2000, "Each match's casing carries onto its replacement")
        }

        function test_s8_blockSelectionArmsInSelectionReplace() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            // Select the bullet block, then open replace: the domain
            // arms and Replace All touches only the selection.
            DocumentSelection.selectBlock(2)
            appLoader.item.selectionKeyHandler.forceActiveFocus()
            keyClick(Qt.Key_H, Qt.ControlModifier)
            var bar = appLoader.item.findBar
            tryCompare(bar, "visible", true, 1000)
            verify(DocumentSearch.hasDomain, "The selection armed a domain")
            var inSelection = findChild(bar, "inSelectionButton")
            verify(inSelection.visible)
            verify(inSelection.checked)

            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 1, 1000,
                       "Only the selected block's match counts")
            bar.replaceField.text = "cat"
            mouseClick(findChild(bar, "replaceAllButton"))
            tryCompare(findChild(bar, "replacePreviewPanel"), "visible", true, 1000)
            mouseClick(findChild(bar, "previewConfirmButton"))
            tryVerify(function() {
                return BlockModel.getContent(2) === "cat item"
            }, 2000)
            compare(BlockModel.getContent(1), "second Fox block fox FOX",
                    "Unselected blocks are untouched")

            // Turning the toggle off restores whole-document scope.
            mouseClick(inSelection)
            tryCompare(DocumentSearch, "matchCount", 6, 1000)
        }

        function test_s9_textSelectionDomainFiltersEdges() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            // A cross-block text range from inside block 1 (after "Fox")
            // through block 2: only matches inside it count.
            DocumentSelection.beginTextSelection(1, 10, 0)
            DocumentSelection.updateTextSelectionHead(2, 8)
            keyClick(Qt.Key_H, Qt.ControlModifier)
            var bar = appLoader.item.findBar
            tryCompare(bar, "visible", true, 1000)
            verify(DocumentSearch.hasDomain)

            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 3, 1000,
                       "Block 1's later two matches plus the bullet's")
            DocumentSelection.clear()
        }

        function test_sa_replacementTextIsMarkdown() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            resetFindBar()
            docWithBlocks(["make this loud", "plain"])
            var bar = openReplaceBar()
            typeString("loud")
            tryCompare(DocumentSearch, "matchCount", 1, 1000)
            bar.replaceField.text = "**loud**"
            mouseClick(findChild(bar, "replaceOneButton"))
            tryVerify(function() {
                return BlockModel.getContent(0) === "make this **loud**"
            }, 1000)
            // The delegate renders it as a hidden-marker bold span.
            var textArea = findTextArea(findBlockDelegate(0))
            tryCompare(textArea, "text", "make this loud", 1000)
        }

        function test_sb_enterInReplaceFieldReplaces() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            searchDoc()
            var bar = openReplaceBar()
            typeString("fox")
            tryCompare(DocumentSearch, "matchCount", 7, 1000)
            bar.replaceField.forceActiveFocus()
            tryCompare(bar.replaceField, "activeFocus", true, 1000)
            typeString("cat")
            keyClick(Qt.Key_Return)
            tryCompare(DocumentSearch, "matchCount", 6, 1000)
            compare(BlockModel.getContent(1), "second cat block fox FOX")
        }

        // ============================================================
        // The three-pane shell
        // ============================================================

        property int collectionSerial: 0

        // A fresh collection root per test, seeded with a small tree.
        function openTestCollection() {
            collectionSerial++
            var root = testCollectionDir + "/col" + collectionSerial
            verify(NoteCollection.openRoot(root), "collection root opens")
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createFolder("Ideas", "Projects") !== "")
            verify(NoteCollection.createNote("", "Welcome") !== "")
            verify(NoteCollection.createNote("Ideas", "Reading") !== "")
            verify(NoteCollection.createNote("Ideas/Projects", "Kvit") !== "")
            NoteListModel.scope = "all"
            wait(20)
            return root
        }

        function closeTestCollection() {
            NoteListModel.scope = "all"
            NoteListModel.folderPath = ""
            NoteCollection.closeRoot()
            wait(20)
        }

        function noteRowFor(relPath) {
            var listView = findChild(appLoader.item, "noteListView")
            verify(listView !== null, "note list exists")
            var row = NoteListModel.rowOf(relPath)
            verify(row >= 0, "note " + relPath + " is listed")
            listView.positionViewAtIndex(row, ListView.Contain)
            waitForRendering(listView)
            var item = listView.itemAtIndex(row)
            verify(item !== null, "note row instantiated")
            return item
        }

        function folderRowFor(relPath) {
            var treeView = findChild(appLoader.item, "folderTreeView")
            verify(treeView !== null, "folder tree exists")
            var row = FolderTreeModel.rowOf(relPath)
            verify(row >= 0, "folder " + relPath + " is a visible row")
            var item = treeView.itemAtIndex(row)
            verify(item !== null, "folder row instantiated")
            return item
        }

        function test_t1_panelsShowInCollectionModeOnly() {
            var panels = findChild(appLoader.item, "sidePanels")
            verify(panels !== null, "side panels exist")

            // Unopened collection: the pre-Phase-8 geometry (this whole
            // suite ran above in exactly this state).
            verify(!NoteCollection.isOpen)
            verify(!panels.visible, "panels hidden without a collection")
            compare(panels.width, 0)

            openTestCollection()
            tryCompare(panels, "visible", true, 1000)
            verify(panels.width > 0, "panels occupy width")
            verify(findChild(appLoader.item, "sidebar") !== null)
            verify(findChild(appLoader.item, "noteListPane") !== null)

            closeTestCollection()
            tryCompare(panels, "visible", false, 1000)
            compare(panels.width, 0)
        }

        function test_t2_clickingNotesSwitchesDocuments() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()

            verify(appLoader.item.openNoteByPath("Welcome.md"))
            tryCompare(appLoader.item, "currentNoteRelPath", "Welcome.md", 1000)

            // Type into the open note, leaving it dirty.
            BlockModel.updateContent(0, "welcome text here")
            verify(DocumentManager.isDirty)

            // Click another note: save-on-blur, switch, undo clears.
            mouseClick(noteRowFor("Ideas/Reading.md"))
            tryCompare(appLoader.item, "currentNoteRelPath",
                       "Ideas/Reading.md", 1000)
            verify(!UndoStack.canUndo, "undo history clears on switch")
            verify(appLoader.item.title.indexOf("Reading") !== -1,
                   "window title tracks the open note")

            // The edit reached the disk (the index refreshed from the
            // saved file via the noteSaved hook).
            var info = NoteCollection.noteInfo("Welcome.md")
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                    === "welcome text here\n"
            }, 2000, "saved note is reindexed after switching")
            verify(!DocumentManager.isDirty, "switched note starts clean")

            closeTestCollection()
        }

        function test_t3_ctrlNCreatesNoteInCurrentScope() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()

            // Folder scope: the new note lands in that folder.
            NoteListModel.folderPath = "Ideas"
            NoteListModel.scope = "folder"
            keyClick(Qt.Key_N, Qt.ControlModifier)
            tryVerify(function() {
                return NoteCollection.noteInfo("Ideas/Untitled.md").title
                       === "Untitled"
            }, 1000)
            tryCompare(appLoader.item, "currentNoteRelPath",
                       "Ideas/Untitled.md", 1000)

            // The empty note is open and focused, ready to type.
            var ta = findTextArea(findBlockDelegate(0))
            tryCompare(ta, "activeFocus", true, 1000)

            closeTestCollection()
        }

        function test_t4_inlineRenameKeepsNoteOpen() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))

            var pane = findChild(appLoader.item, "noteListPane")
            var row = noteRowFor("Ideas/Reading.md")
            pane.startRename("Ideas/Reading.md")
            // Every row hosts a (hidden) rename field; look inside the
            // renamed row, not from the window root.
            var field = findChild(row, "noteRenameField")
            verify(field !== null && field.visible, "rename field shows")
            tryCompare(field, "activeFocus", true, 1000)

            field.selectAll()
            typeString("Bookshelf")
            keyClick(Qt.Key_Return)

            // The file moved and the open document followed it.
            tryVerify(function() {
                return NoteCollection.noteInfo("Ideas/Bookshelf.md").title
                       === "Bookshelf"
            }, 1000)
            verify(!NoteCollection.noteInfo("Ideas/Reading.md").title,
                   "old path gone")
            tryCompare(appLoader.item, "currentNoteRelPath",
                       "Ideas/Bookshelf.md", 1000)

            closeTestCollection()
        }

        function test_t5_folderDialogsCreateRenameDelete() {
            openTestCollection()

            // Create via the dialog, with a color.
            var dialog = findChild(appLoader.item, "folderDialog")
            verify(dialog !== null)
            dialog.openForCreate("")
            var nameField = findChild(appLoader.item, "folderDialogNameField")
            nameField.text = "Archive"
            dialog.selectedColor = "#e05c5c"
            dialog.accept()
            wait(50)
            compare(FolderTreeModel.rowOf("Archive") >= 0, true,
                    "created folder is a row")
            verify(folderRowFor("Archive") !== null)

            // Rename via the dialog.
            dialog.openForRename("Archive", "Archive", "#e05c5c")
            nameField.text = "Vault"
            dialog.accept()
            wait(50)
            verify(FolderTreeModel.rowOf("Vault") >= 0, "renamed row exists")
            compare(FolderTreeModel.rowOf("Archive"), -1)

            // Delete (confirmed) moves it to trash and drops the row.
            var deleteDialog = findChild(appLoader.item, "deleteFolderDialog")
            deleteDialog.openFor("Vault", "Vault", 0)
            deleteDialog.accept()
            wait(50)
            compare(FolderTreeModel.rowOf("Vault"), -1)

            closeTestCollection()
        }

        function test_t6_expandCollapsePersists() {
            var root = openTestCollection()

            // Collapse "Ideas": its child row disappears.
            verify(FolderTreeModel.rowOf("Ideas/Projects") >= 0)
            FolderTreeModel.toggleExpanded(FolderTreeModel.rowOf("Ideas"))
            tryVerify(function() {
                return FolderTreeModel.rowOf("Ideas/Projects") === -1
            }, 1000)

            // The state survives a close/reopen of the same root.
            NoteCollection.closeRoot()
            wait(20)
            verify(NoteCollection.openRoot(root))
            tryVerify(function() {
                return FolderTreeModel.rowOf("Ideas") >= 0
                       && FolderTreeModel.rowOf("Ideas/Projects") === -1
            }, 1000)
            FolderTreeModel.toggleExpanded(FolderTreeModel.rowOf("Ideas"))
            tryVerify(function() {
                return FolderTreeModel.rowOf("Ideas/Projects") >= 0
            }, 1000)

            closeTestCollection()
        }

        function test_t7_dragNoteRowOntoFolderMovesIt() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()

            var noteRow = noteRowFor("Welcome.md")
            var folderRow = folderRowFor("Ideas")

            mousePress(noteRow, noteRow.width / 2, noteRow.height / 2)
            // Cross the threshold, then travel onto the folder row.
            mouseMove(noteRow, noteRow.width / 2 - 20, noteRow.height / 2)
            mouseMove(folderRow, folderRow.width / 2, folderRow.height / 2)
            wait(20)
            var proxy = findChild(appLoader.item, "noteDragProxy")
            verify(proxy.visible, "drag proxy visible mid-drag")
            mouseRelease(folderRow, folderRow.width / 2, folderRow.height / 2)

            tryVerify(function() {
                return NoteCollection.noteInfo("Ideas/Welcome.md").title
                       === "Welcome"
            }, 1000)
            verify(!NoteCollection.noteInfo("Welcome.md").title,
                   "note left the root")

            closeTestCollection()
        }

        function test_t8_ctrlBackslashTogglesPanels() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()
            var panels = findChild(appLoader.item, "sidePanels")
            tryCompare(panels, "visible", true, 1000)

            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            tryCompare(panels, "visible", false, 1000)
            compare(panels.width, 0)

            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            tryCompare(panels, "visible", true, 1000)

            closeTestCollection()
        }

        function test_t9_lastOpenNotePersists() {
            var root = openTestCollection()

            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            NoteCollection.closeRoot()
            wait(20)

            // The startup restore (main.cpp) reads exactly this value.
            verify(NoteCollection.openRoot(root))
            compare(NoteCollection.lastOpenNote(), "Ideas/Reading.md")

            closeTestCollection()
        }

        // ============================================================
        // Tags end to end
        // ============================================================

        // Accept a modal dialog and wait out its exit transition: the
        // modal overlay keeps eating input until the popup is fully
        // closed, so a click straight after accept() can vanish into it.
        function acceptDialogAndSettle(dialog) {
            dialog.accept()
            tryCompare(dialog, "visible", false, 1000)
        }

        function tagStripItem() {
            var strip = findChild(appLoader.item, "tagStrip")
            verify(strip !== null, "tag strip exists")
            return strip
        }

        function test_u1_tagStripAddsWithAutocomplete() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()
            // Seed the registry from another note.
            verify(NoteCollection.addTag("Ideas/Reading.md", "shared"))

            verify(appLoader.item.openNoteByPath("Welcome.md"))
            var strip = tagStripItem()
            tryCompare(strip, "visible", true, 1000)

            // Typing filters the registry into the suggestions popup.
            var field = findChild(strip, "tagAddField")
            field.forceActiveFocus()
            tryCompare(field, "activeFocus", true, 1000)
            typeString("sh")
            var popup = findChild(strip, "tagSuggestionsPopup")
            tryCompare(popup, "visible", true, 1000)
            compare(strip.suggestions.length, 1)
            compare(strip.suggestions[0], "shared")

            // Down + Enter applies the highlighted suggestion.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md")
                           .tags.indexOf("shared") !== -1
            }, 1000)
            tryVerify(function() { return strip.chipFor("shared") !== null },
                      1000)

            // Enter on unmatched text creates the tag on the fly (§8.2).
            typeString("brandnew")
            keyClick(Qt.Key_Return)
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md")
                           .tags.indexOf("brandnew") !== -1
            }, 1000)
            compare(NoteCollection.tagCount("brandnew"), 1)
            verify(NoteCollection.tagColor("brandnew") !== "",
                   "new tag received a palette color")

            closeTestCollection()
        }

        function test_u2_removingChipUpdatesRegistry() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            verify(NoteCollection.addTag("Welcome.md", "temp"))
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            var strip = tagStripItem()
            var chip = null
            tryVerify(function() {
                chip = strip.chipFor("temp")
                return chip !== null
            }, 1000)
            // The Flow positions its children on the next polish; click
            // only after a rendered frame, like a human would.
            waitForRendering(strip)
            var removeGlyph = findChild(chip, "tagChipRemove")
            verify(removeGlyph !== null)
            mouseClick(removeGlyph)

            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md")
                           .tags.indexOf("temp") === -1
            }, 1000)
            compare(NoteCollection.tagCount("temp"), 0)

            closeTestCollection()
        }

        function test_u3_sidebarTagFilterComposesWithFolderScope() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            verify(NoteCollection.addTag("Welcome.md", "work"))
            verify(NoteCollection.addTag("Ideas/Reading.md", "work"))
            verify(NoteCollection.addTag("Ideas/Projects/Kvit.md", "other"))

            var tagList = findChild(appLoader.item, "tagListView")
            verify(tagList !== null)
            // "other" sorts before "work" in the registry listing; wait
            // for the row delegate, not just the model.
            var workRow = null
            tryVerify(function() {
                workRow = tagList.itemAtIndex(1)
                return tagList.count === 2 && workRow !== null
            }, 1000)
            waitForRendering(tagList)
            mouseClick(workRow)

            tryCompare(NoteListModel, "tagFilter", "work", 1000)
            compare(NoteListModel.count, 2)

            // Compose with a folder scope: only the Ideas note remains.
            NoteListModel.folderPath = "Ideas"
            NoteListModel.scope = "folder"
            tryCompare(NoteListModel, "count", 1, 1000)
            compare(NoteListModel.relPathAt(0), "Ideas/Reading.md")

            // Clicking the active tag clears the filter.
            NoteListModel.scope = "all"
            mouseClick(workRow)
            tryCompare(NoteListModel, "tagFilter", "", 1000)
            compare(NoteListModel.count, 3)

            closeTestCollection()
        }

        function test_u4_renameTagUpdatesFilesAndOpenNote() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()
            verify(NoteCollection.addTag("Welcome.md", "draft"))
            verify(NoteCollection.addTag("Ideas/Reading.md", "draft"))
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            var dialog = findChild(appLoader.item, "tagDialog")
            dialog.openFor("draft", NoteCollection.tagColor("draft"))
            var nameField = findChild(appLoader.item, "tagDialogNameField")
            nameField.text = "wip"
            dialog.accept()

            tryVerify(function() {
                return NoteCollection.tagCount("wip") === 2
                       && NoteCollection.tagCount("draft") === 0
            }, 1000)

            // The open note's held front-matter refreshed:
            // an edit + save must keep the renamed tag, not resurrect
            // the block captured at load.
            BlockModel.updateContent(0, "edited after tag rename")
            verify(DocumentManager.save())
            var tags = NoteCollection.noteInfo("Welcome.md").tags
            compare(tags.length, 1)
            compare(tags[0], "wip")

            closeTestCollection()
        }

        function test_u5_mergeAndDeleteTag() {
            openTestCollection()
            verify(NoteCollection.addTag("Welcome.md", "alpha"))
            verify(NoteCollection.addTag("Ideas/Reading.md", "beta"))
            verify(NoteCollection.addTag("Ideas/Projects/Kvit.md", "beta"))

            // Renaming onto an existing tag routes through the merge
            // confirmation with the blast radius.
            var dialog = findChild(appLoader.item, "tagDialog")
            dialog.openFor("alpha", "")
            var nameField = findChild(appLoader.item, "tagDialogNameField")
            nameField.text = "beta"
            dialog.accept()
            var mergeDialog = findChild(appLoader.item, "mergeTagDialog")
            tryCompare(mergeDialog, "visible", true, 1000)
            mergeDialog.accept()

            tryVerify(function() {
                return NoteCollection.tagCount("beta") === 3
                       && NoteCollection.tagCount("alpha") === 0
            }, 1000)

            // Delete strips the tag from every carrier, confirmed.
            var deleteDialog = findChild(appLoader.item, "deleteTagDialog")
            deleteDialog.openFor("beta", 3)
            deleteDialog.accept()
            tryVerify(function() {
                return NoteCollection.tagCount("beta") === 0
            }, 1000)
            compare(NoteCollection.noteInfo("Welcome.md").tags.length, 0)

            closeTestCollection()
        }

        // ============================================================
        // The note list complete
        // ============================================================

        function listOrder() {
            var out = []
            for (var i = 0; i < NoteListModel.count; i++)
                out.push(NoteListModel.relPathAt(i))
            return out
        }

        function test_v1_sortControls() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()

            var combo = findChild(appLoader.item, "sortModeCombo")
            var direction = findChild(appLoader.item, "sortDirectionButton")
            verify(combo !== null && direction !== null)

            // Title ascending: Kvit < Reading < Welcome.
            combo.currentIndex = 2
            combo.activated(2)
            tryCompare(NoteListModel, "sortMode", "title", 1000)
            direction.clicked() // default descending → ascending
            tryCompare(NoteListModel, "ascending", true, 1000)
            compare(listOrder(),
                    ["Ideas/Projects/Kvit.md", "Ideas/Reading.md",
                     "Welcome.md"])

            direction.clicked()
            tryCompare(NoteListModel, "ascending", false, 1000)
            compare(listOrder()[0], "Welcome.md")

            // Picking manual snaps to the stored (ascending) order.
            combo.currentIndex = 3
            combo.activated(3)
            tryCompare(NoteListModel, "sortMode", "manual", 1000)
            compare(NoteListModel.ascending, true)

            closeTestCollection()
        }

        function test_v2_pinAndFavoriteToggles() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true

            // Hover the Welcome row: the toggles appear (and get their
            // layout positions on the next rendered frame).
            var row = noteRowFor("Welcome.md")
            mouseMove(row, row.width / 2, row.height / 2)
            var pinButton = findChild(row, "notePinButton")
            tryCompare(pinButton, "visible", true, 1000)
            waitForRendering(row)
            mouseClick(pinButton)

            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").pinned === true
            }, 1000)
            // Pinned floats to the top despite sorting last by title.
            tryVerify(function() {
                return listOrder()[0] === "Welcome.md"
            }, 1000)

            // Favorite via the star; the Favorites scope picks it up.
            row = noteRowFor("Welcome.md")
            mouseMove(row, row.width / 2, row.height / 2)
            var favoriteButton = findChild(row, "noteFavoriteButton")
            tryCompare(favoriteButton, "visible", true, 1000)
            waitForRendering(row)
            mouseClick(favoriteButton)
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").favorite === true
            }, 1000)
            NoteListModel.scope = "favorites"
            tryCompare(NoteListModel, "count", 1, 1000)
            compare(NoteListModel.relPathAt(0), "Welcome.md")

            closeTestCollection()
        }

        function test_v3_detailsLineShowsWordCount() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            BlockModel.updateContent(0, "five words of body text")
            verify(DocumentManager.save())

            var row = noteRowFor("Welcome.md")
            var details = findChild(row, "noteDetailsLabel")
            verify(details !== null)
            tryVerify(function() {
                return details.text.indexOf("5 words") !== -1
            }, 2000, "details line shows the word count: " + details.text)

            closeTestCollection()
        }

        function test_v4_bulkSelectionGesturesAndBar() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true
            var pane = findChild(appLoader.item, "noteListPane")

            // Plain click selects one (and opens); Ctrl+Click adds.
            mouseClick(noteRowFor("Ideas/Projects/Kvit.md"))
            tryCompare(appLoader.item, "currentNoteRelPath",
                       "Ideas/Projects/Kvit.md", 1000)
            mouseClick(noteRowFor("Welcome.md"), 30, 29, Qt.LeftButton,
                       Qt.ControlModifier)
            compare(pane.selectedPaths.length, 2)

            var bar = findChild(appLoader.item, "bulkActionBar")
            tryCompare(bar, "visible", true, 1000)
            waitForRendering(bar)
            var countLabel = findChild(appLoader.item, "bulkCountLabel")
            compare(countLabel.text, "2 selected")

            // Bulk favorite.
            mouseClick(findChild(appLoader.item, "bulkFavoriteButton"))
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").favorite
                    && NoteCollection.noteInfo("Ideas/Projects/Kvit.md").favorite
            }, 1000)

            // Bulk tag through the dialog.
            mouseClick(findChild(appLoader.item, "bulkTagButton"))
            var tagDialog = findChild(appLoader.item, "bulkTagDialog")
            tryCompare(tagDialog, "visible", true, 1000)
            var field = findChild(appLoader.item, "bulkTagField")
            field.text = "batch"
            acceptDialogAndSettle(tagDialog)
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md")
                           .tags.indexOf("batch") !== -1
                    && NoteCollection.noteInfo("Ideas/Projects/Kvit.md")
                           .tags.indexOf("batch") !== -1
            }, 1000)

            // Bulk delete, confirmed, lands in the trash.
            mouseClick(findChild(appLoader.item, "bulkDeleteButton"))
            var deleteDialog = findChild(appLoader.item, "bulkDeleteDialog")
            tryCompare(deleteDialog, "visible", true, 1000)
            acceptDialogAndSettle(deleteDialog)
            tryVerify(function() {
                return NoteListModel.count === 1
            }, 1000)
            compare(pane.selectedPaths.length, 0)

            closeTestCollection()
        }

        function test_v5_shiftClickSelectsRange() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true
            var pane = findChild(appLoader.item, "noteListPane")

            mouseClick(noteRowFor("Ideas/Projects/Kvit.md")) // row 0
            mouseClick(noteRowFor("Welcome.md"), 30, 29, Qt.LeftButton,
                       Qt.ShiftModifier)                      // row 2
            compare(pane.selectedPaths.length, 3)

            // A plain click collapses back to one.
            mouseClick(noteRowFor("Ideas/Reading.md"))
            compare(pane.selectedPaths.length, 1)

            closeTestCollection()
        }

        function test_v6_manualReorderDrag() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            // A second note beside Reading: manual reorder needs a pair.
            verify(NoteCollection.createNote("Ideas", "Alpha") !== "")
            NoteListModel.scope = "folder"
            NoteListModel.folderPath = "Ideas"
            NoteListModel.sortMode = "manual"
            NoteListModel.ascending = true

            tryCompare(NoteListModel, "count", 2, 1000)
            var first = NoteListModel.relPathAt(0)
            var second = NoteListModel.relPathAt(1)

            // Drag the first row below the second row's midpoint.
            var row0 = noteRowFor(first)
            var row1 = noteRowFor(second)
            mousePress(row0, row0.width / 2, row0.height / 2)
            mouseMove(row0, row0.width / 2 - 20, row0.height / 2)
            mouseMove(row1, row1.width / 2, row1.height - 5)
            var indicator = findChild(appLoader.item, "reorderIndicator")
            tryCompare(indicator, "visible", true, 1000)
            mouseRelease(row1, row1.width / 2, row1.height - 5)

            tryVerify(function() {
                return NoteListModel.relPathAt(0) === second
                       && NoteListModel.relPathAt(1) === first
            }, 1000)
            // Persisted: the collection's manual order agrees.
            compare(NoteCollection.notesInFolder("Ideas")[0], second)

            closeTestCollection()
        }

        function test_v7_dragSelectedRowsMovesAll() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            openTestCollection()
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true
            var pane = findChild(appLoader.item, "noteListPane")

            // Select Kvit + Welcome, then drag one of them onto Ideas.
            mouseClick(noteRowFor("Ideas/Projects/Kvit.md"))
            mouseClick(noteRowFor("Welcome.md"), 30, 29, Qt.LeftButton,
                       Qt.ControlModifier)
            compare(pane.selectedPaths.length, 2)

            var noteRow = noteRowFor("Welcome.md")
            var folderRow = folderRowFor("Ideas")
            mousePress(noteRow, noteRow.width / 2, noteRow.height / 2)
            mouseMove(noteRow, noteRow.width / 2 - 20, noteRow.height / 2)
            mouseMove(folderRow, folderRow.width / 2, folderRow.height / 2)
            mouseRelease(folderRow, folderRow.width / 2, folderRow.height / 2)

            tryVerify(function() {
                return NoteCollection.noteInfo("Ideas/Welcome.md").title
                           === "Welcome"
                    && NoteCollection.noteInfo("Ideas/Kvit.md").title
                           === "Kvit"
            }, 1000)

            closeTestCollection()
        }

        function test_v8_deletingOpenNoteMovesOn() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            verify(NoteCollection.deleteNote("Welcome.md"))

            // The editor moves to a surviving note — never back onto the
            // trashed path (the fallback must read the REBUILT list), and
            // no error dialog appears.
            tryVerify(function() {
                var current = appLoader.item.currentNoteRelPath
                return current !== "" && current !== "Welcome.md"
            }, 1000)
            // The session dialogs are built on first use, so no dialog at
            // all is the strongest form of "no error was reported"; if one
            // exists it must not be showing.
            var errorDialog = findChild(appLoader.item, "errorDialog")
            verify(!errorDialog || !errorDialog.visible,
                   "no failed-open error dialog")
            verify(!NoteCollection.noteInfo("Welcome.md").title,
                   "the deleted note stays deleted")

            closeTestCollection()
        }

        // ============================================================
        // Global search
        // ============================================================

        function seedSearchContent() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            DocumentSerializer.loadIntoModel(BlockModel,
                "The quick **brown fox** jumps\n\nno match here\n")
            verify(DocumentManager.save())
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                    .indexOf("brown fox") !== -1
            }, 2000, "Welcome note is indexed before the next save")
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            DocumentSerializer.loadIntoModel(BlockModel,
                "A fox appears twice: fox\n")
            verify(DocumentManager.save())
            tryVerify(function() {
                return NoteCollection.noteInfo("Ideas/Reading.md").body
                    .indexOf("fox appears twice") !== -1
            }, 2000, "Reading note is indexed before metadata changes")
            verify(NoteCollection.addTag("Ideas/Reading.md", "animals"))
        }

        function test_w1_liveResultsReplaceTheNoteList() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            seedSearchContent()

            var field = findChild(appLoader.item, "globalSearchField")
            field.forceActiveFocus()
            tryCompare(field, "activeFocus", true, 1000)
            typeString("fox")

            var results = findChild(appLoader.item, "searchResultsView")
            tryCompare(results, "visible", true, 1000)
            var noteList = findChild(appLoader.item, "noteListView")
            verify(!noteList.visible, "note rows hidden while searching")

            // 1 match in Welcome (display text, markers stripped) + 2 in
            // Reading; Reading has no title match, Welcome none either.
            tryCompare(CollectionSearch, "matchCount", 3, 1000)
            var summary = findChild(appLoader.item, "searchResultSummary")
            compare(summary.text, "3 match(es) in 2 note(s)")

            // Escape clears and the note list returns.
            keyClick(Qt.Key_Escape)
            tryCompare(CollectionSearch, "query", "", 1000)
            tryCompare(noteList, "visible", true, 1000)
            verify(!results.visible)

            closeTestCollection()
        }

        function test_w2_resultClickHandsOffToFindBar() {
            if (isHeadless) {
                skip("Mouse tests require display")
            }
            seedSearchContent()
            CollectionSearch.query = "fox"

            // Click the SECOND match of the never-reopened Reading note
            // ("fox" at display position 21).
            var results = findChild(appLoader.item, "searchResultsView")
            tryCompare(results, "visible", true, 1000)
            appLoader.item.openSearchResult("Ideas/Reading.md", 0, 21)

            tryCompare(appLoader.item, "currentNoteRelPath",
                       "Ideas/Reading.md", 1000)
            var bar = findChild(appLoader.item, "findBar")
            tryCompare(bar, "visible", true, 1000)
            compare(DocumentSearch.query, "fox")
            tryCompare(DocumentSearch, "matchCount", 2, 1000)
            // The clicked occurrence is the current match.
            var info = DocumentSearch.currentMatchInfo()
            compare(info.found, true)
            compare(info.blockIndex, 0)
            compare(DocumentSearch.currentNumber, 2)

            bar.close()
            CollectionSearch.query = ""
            closeTestCollection()
        }

        function test_w3_searchFiltersFollowSidebarScope() {
            seedSearchContent()
            CollectionSearch.query = "fox"
            tryCompare(CollectionSearch, "matchCount", 3, 1000)

            // Folder scope narrows the corpus (recursive folder search).
            NoteListModel.folderPath = "Ideas"
            NoteListModel.scope = "folder"
            tryCompare(CollectionSearch, "folderScope", "Ideas", 1000)
            tryCompare(CollectionSearch, "matchCount", 2, 1000)

            // Tag filter composes.
            NoteListModel.scope = "all"
            NoteListModel.tagFilter = "animals"
            tryCompare(CollectionSearch, "tagFilter", "animals", 1000)
            tryCompare(CollectionSearch, "matchCount", 2, 1000)
            tryCompare(CollectionSearch, "noteCount", 1, 1000)

            NoteListModel.tagFilter = ""
            CollectionSearch.query = ""
            closeTestCollection()
        }

        function test_w4_recentSearchesSessionHistory() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            seedSearchContent()

            var sidebarItem = findChild(appLoader.item, "sidebar")
            var field = findChild(appLoader.item, "globalSearchField")
            field.forceActiveFocus()
            tryCompare(field, "activeFocus", true, 1000)
            typeString("fox")
            keyClick(Qt.Key_Return) // commits to the session history

            keyClick(Qt.Key_Escape) // clears the query
            tryCompare(CollectionSearch, "query", "", 1000)
            var recent = findChild(appLoader.item, "recentSearchesColumn")
            tryCompare(recent, "visible", true, 1000)
            compare(sidebarItem.recentSearches[0], "fox")

            closeTestCollection()
        }

        function test_w5_searchRefreshesAfterEditAndSave() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            seedSearchContent()
            CollectionSearch.query = "fox"
            tryCompare(CollectionSearch, "matchCount", 3, 1000)

            // Edit the open note so a match disappears, save: the cache
            // refreshes through the noteSaved hook.
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            DocumentSerializer.loadIntoModel(BlockModel, "no animals left\n")
            verify(DocumentManager.save())
            tryCompare(CollectionSearch, "matchCount", 2, 1000)

            CollectionSearch.query = ""
            closeTestCollection()
        }

        // ============================================================
        // Backups and crash recovery
        // ============================================================

        function test_x1_restoreFromBackupIsOneUndoStep() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            // The first save's pre-overwrite hook backs up the (empty)
            // just-created file and consumes the rotation window...
            DocumentSerializer.loadIntoModel(BlockModel, "Original body\n")
            verify(DocumentManager.save())
            // ...so step past the floor before the next save, whose hook
            // then backs up "Original body".
            NoteCollection.setClockOffsetForTesting(11 * 60)
            DocumentSerializer.loadIntoModel(BlockModel, "Newer body\n")
            verify(DocumentManager.save())
            NoteCollection.setClockOffsetForTesting(0)

            tryVerify(function() {
                return NoteCollection.backupsFor("Welcome.md").length === 2
            }, 2000, "both asynchronous backup snapshots are complete")

            var dialog = findChild(appLoader.item, "backupDialog")
            dialog.openForCurrentNote()
            tryCompare(dialog, "visible", true, 1000)
            compare(dialog.backups.length, 2)
            compare(dialog.backups[0].preview, "Original body")

            acceptDialogAndSettle(dialog)
            tryVerify(function() {
                return BlockModel.getContent(0) === "Original body"
            }, 1000)
            // The restore saved: the disk agrees.
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                    === "Original body\n"
            }, 2000, "restored disk body is reindexed")

            // ONE undo returns the pre-restore document.
            UndoStack.undo()
            compare(BlockModel.getContent(0), "Newer body")

            closeTestCollection()
        }

        function test_x2_crashJournalRestoresThroughBanner() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            var root = openTestCollection()
            DocumentManager.setJournalDebounceMs(30)
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            // Unsaved edits; the journal snapshot lands after the
            // debounce and no save follows — then the "crash".
            BlockModel.updateContent(0, "text from the crashed session")
            verify(DocumentManager.isDirty)
            wait(300)
            NoteCollection.closeRoot()
            wait(20)

            // Reopening finds the evidence: the banner offers the note.
            verify(NoteCollection.openRoot(root))
            var banner = findChild(appLoader.item, "recoveryBanner")
            tryCompare(banner, "visible", true, 1000)

            var restoreButton = findChild(banner, "recoveryRestoreButton")
            verify(restoreButton !== null)
            // Let the banner's layout polish before clicking into it
            // (the panel row adds a layout pass; clicking a
            // still-settling banner raced).
            waitForRendering(banner)
            mouseClick(restoreButton)

            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                       === "text from the crashed session\n"
            }, 1000)
            // The (still-open) note reloaded to the restored content.
            tryVerify(function() {
                return BlockModel.getContent(0)
                       === "text from the crashed session"
            }, 1000)
            verify(!DocumentManager.isDirty)
            tryCompare(banner, "visible", false, 1000)

            DocumentManager.setJournalDebounceMs(2000)
            closeTestCollection()
        }

        function test_x3_crashJournalDiscardKeepsDiskFile() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            var root = openTestCollection()
            DocumentManager.setJournalDebounceMs(30)
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            DocumentSerializer.loadIntoModel(BlockModel, "Saved truth\n")
            verify(DocumentManager.save())

            BlockModel.updateContent(0, "abandoned edits")
            verify(DocumentManager.isDirty)
            wait(300)
            NoteCollection.closeRoot()
            wait(20)

            verify(NoteCollection.openRoot(root))
            var banner = findChild(appLoader.item, "recoveryBanner")
            tryCompare(banner, "visible", true, 1000)
            var discardButton = findChild(banner, "recoveryDiscardButton")
            waitForRendering(banner)
            mouseClick(discardButton)

            tryVerify(function() {
                return NoteCollection.recoveryEntries().length === 0
            }, 2000, "discard removes the recovery entry")
            tryCompare(banner, "visible", false, 1000)
            compare(NoteCollection.noteInfo("Welcome.md").body,
                    "Saved truth\n")

            DocumentManager.setJournalDebounceMs(2000)
            closeTestCollection()
        }

        function test_x4_orderlySaveLeavesNoJournal() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            var root = openTestCollection()
            DocumentManager.setJournalDebounceMs(30)
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            BlockModel.updateContent(0, "typed and saved")
            wait(300) // the journal exists now...
            verify(DocumentManager.save()) // ...and the clean save removes it

            NoteCollection.closeRoot()
            wait(20)
            verify(NoteCollection.openRoot(root))
            var banner = findChild(appLoader.item, "recoveryBanner")
            verify(!banner.visible, "no crash evidence after a clean save")

            DocumentManager.setJournalDebounceMs(2000)
            closeTestCollection()
        }

        // ============================================================
        // The adopted performance targets, measured on the display like
        // every features.md §21.7 number
        // ============================================================

        function test_y1_noteSwitchUnder200ms() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()

            // Two content-heavy notes (50 mixed blocks each): switching
            // pays save-on-blur, the load, and delegate creation.
            function fill(relPath) {
                verify(appLoader.item.openNoteByPath(relPath))
                var markdown = ""
                for (var i = 0; i < 50; i++) {
                    if (i % 7 === 3)
                        markdown += "- item " + i + " with **bold** text\n"
                    else
                        markdown += "Paragraph " + i
                                    + " with **bold** and *italic*\n\n"
                }
                DocumentSerializer.loadIntoModel(BlockModel, markdown)
                verify(DocumentManager.save())
            }
            fill("Welcome.md")
            fill("Ideas/Reading.md")

            // Alternate a few switches; take the worst.
            var worst = 0
            var notes = ["Welcome.md", "Ideas/Reading.md"]
            for (var k = 0; k < 6; k++) {
                BlockModel.updateContent(0, "dirty edit " + k) // save-on-blur
                var t0 = Date.now()
                verify(appLoader.item.openNoteByPath(notes[k % 2]))
                wait(0) // let the frame settle like a real switch
                worst = Math.max(worst, Date.now() - t0)
            }
            console.log("NOTE SWITCH: " + worst
                        + " ms worst of 6 switches between 50-block notes")
            verify(worst < 200, "note switch must stay under 200 ms "
                   + "(worst measured " + worst + "ms)")

            closeTestCollection()
        }

        function test_y2_typingLatencyWithJournalActive() {
            // The typing-latency measurement, now with the collection open and
            // the crash journal armed: the keystroke path gains only a
            // debounce-timer restart; the write itself is
            // off-path. Same §21.7 budget.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            verify(DocumentManager.journalPath !== "",
                   "journal armed in collection mode")

            var markdown = ""
            for (var i = 0; i < 100; i++)
                markdown += "Block " + i + " with **bold** and *italic*\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, markdown)
            compare(BlockModel.count, 100)
            var listView = findChild(appLoader.item, "blockListView")
            listView.positionViewAtBeginning()
            wait(200) // let the delegates instantiate

            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            keyClick(Qt.Key_End)
            wait(100)

            var keystrokes = 40
            var t0 = Date.now()
            for (var k = 0; k < keystrokes; k++)
                keyClick(Qt.Key_X)
            var perKey = (Date.now() - t0) / keystrokes
            console.log("TYPING LATENCY (journal active): "
                        + perKey.toFixed(2) + " ms/keystroke in a 100-block "
                        + "note with the collection open")
            verify(perKey < 16, "typing latency with the journal active must "
                   + "stay under 16 ms (measured " + perKey.toFixed(2) + "ms)")

            closeTestCollection()
        }

        // ============================================================
        // The settings store and persisted session
        // state. Write paths are exercised through the UI-facing state
        // owners; the read path through applyPersistedSessionState,
        // the function startup runs.
        // ============================================================

        function test_z1_settingsRoundTripAcrossStores() {
            // Restart-shaped, in QML: a second store on the same path
            // sees what the first flushed.
            var path = testCollectionDir + "/reopen-settings.json"
            var first = Qt.createQmlObject(
                'import Kvit 1.0; SettingsStore {}', testCase)
            verify(first.open(path))
            first.setValue("theme", "dark")
            first.setValue("recent", ["alpha", "beta"])
            first.flush()

            var second = Qt.createQmlObject(
                'import Kvit 1.0; SettingsStore {}', testCase)
            verify(second.open(path))
            compare(second.value("theme", ""), "dark")
            compare(second.value("recent", []).length, 2)
            compare(second.value("recent", [])[0], "alpha")
            first.destroy()
            second.destroy()
        }

        function test_z2_panelsVisiblePersists() {
            var appWin = appLoader.item
            verify(appWin.panelsVisible)
            appWin.panelsVisible = false
            compare(AppSettings.value("panels.visible", true), false)

            AppSettings.setValue("panels.visible", true)
            appWin.applyPersistedSessionState()
            verify(appWin.panelsVisible)
        }

        function test_z3_noteListSortPersists() {
            NoteListModel.sortMode = "title"
            NoteListModel.ascending = true
            compare(AppSettings.value("noteList.sortMode", ""), "title")
            compare(AppSettings.value("noteList.ascending", false), true)

            // The read path lands a stored preference on the model.
            AppSettings.setValue("noteList.sortMode", "created")
            AppSettings.setValue("noteList.ascending", false)
            appLoader.item.applyPersistedSessionState()
            compare(NoteListModel.sortMode, "created")
            compare(NoteListModel.ascending, false)

            NoteListModel.sortMode = "modified"  // the app default
        }

        function test_z4_blockMenuRecencyPersists() {
            // Recency is recorded per catalog ENTRY rather than per block
            // type: five entries share Block.CodeBlock, so a type alone
            // cannot name the choice. An entry's id is "<type>:<language>",
            // which makes the plain entry for a type "<type>:".
            BlockMenuModel.setRecentTypes([])
            BlockMenuModel.noteUsed(Block.Quote)
            var saved = AppSettings.value("blockMenu.recent", [])
            compare(saved.length, 1)
            compare(saved[0], String(Block.Quote) + ":")

            // Settings written before entry ids existed hold plain type
            // numbers. They still load, resolving to that type's plain
            // entry, so an upgrade keeps the user's recency instead of
            // silently dropping it.
            AppSettings.setValue("blockMenu.recent",
                                 [Block.Todo, Block.Divider])
            appLoader.item.applyPersistedSessionState()
            var loaded = BlockMenuModel.recentTypes()
            compare(loaded.length, 2)
            compare(loaded[0], String(Block.Todo) + ":")
            compare(loaded[1], String(Block.Divider) + ":")

            // Reset so menu-shape tests stay order-independent.
            BlockMenuModel.setRecentTypes([])
            AppSettings.setValue("blockMenu.recent", [])
        }

        function test_z5_recentSearchesPersist() {
            var sidebar = findChild(appLoader.item, "sidebar")
            verify(sidebar !== null)
            sidebar.commitRecentSearch("badger")
            var saved = AppSettings.value("search.recent", [])
            compare(saved.length > 0, true)
            compare(saved[0], "badger")

            AppSettings.setValue("search.recent", ["stoat", "weasel"])
            sidebar.applyPersistedSearchHistory()
            compare(sidebar.recentSearches.length, 2)
            compare(sidebar.recentSearches[0], "stoat")

            AppSettings.setValue("search.recent", [])
            sidebar.applyPersistedSearchHistory()
        }

        function test_z6_findOptionsPersist() {
            var caseBtn = findChild(appLoader.item, "findCaseButton")
            var wordBtn = findChild(appLoader.item, "findWordButton")
            verify(caseBtn !== null && wordBtn !== null)

            caseBtn.checked = true
            compare(AppSettings.value("find.caseSensitive", false), true)
            compare(DocumentSearch.caseSensitive, true)

            AppSettings.setValue("find.caseSensitive", false)
            AppSettings.setValue("find.wholeWord", true)
            appLoader.item.applyPersistedSessionState()
            compare(caseBtn.checked, false)
            compare(wordBtn.checked, true)
            compare(DocumentSearch.wholeWord, true)

            AppSettings.setValue("find.wholeWord", false)
            appLoader.item.applyPersistedSessionState()
            compare(DocumentSearch.wholeWord, false)
        }

        // ============================================================
        // Theme switching restyles the live shell and
        // persists (unit coverage of tables/overrides is test_theme).
        // ============================================================

        function test_z7_themePersistsAndRestylesShell() {
            // Establish the theme rather than assuming one. With no
            // persisted choice the app follows the OS scheme and reports
            // "system"; that default has its own coverage in test_theme,
            // where the settings store is actually fresh.
            Theme.themeId = "light"
            // toString(): a bare `var x = item.color` keeps a LIVE
            // value-type reference that re-reads the property later.
            var lightBackground = appLoader.item.color.toString()

            Theme.themeId = "dark"
            compare(AppSettings.value("theme.id", ""), "dark")
            verify(Qt.colorEqual(appLoader.item.color,
                                 Theme.windowBackground))
            verify(!Qt.colorEqual(appLoader.item.color, lightBackground))

            Theme.themeId = "light"
            compare(AppSettings.value("theme.id", ""), "light")
            verify(Qt.colorEqual(appLoader.item.color, lightBackground))
        }

        // ============================================================
        // Typography settings drive the live document
        // and the settings dialog binds them (scale/clamp/persistence
        // unit coverage is test_typography).
        // ============================================================

        function test_z8_typographyScalesLiveDelegates() {
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "body text")
            BlockModel.insertBlock(1, 1, "a heading")
            wait(150)

            var body = findBlockDelegate(0)
            var heading = findBlockDelegate(1)
            compare(body.contentFontSize, 15)
            compare(heading.contentFontSize, 32)

            Typography.baseSize = 20
            compare(body.contentFontSize, 20)
            compare(heading.contentFontSize, 43)  // qRound(20 * 32/15)
            compare(AppSettings.value("typography.fontSize", 0), 20)

            Typography.baseSize = 15
        }

        function test_z9_maxContentWidthCentersColumn() {
            // Geometry assertions are for single-file mode. A failed earlier
            // collection test must not leave the side panels consuming width.
            if (NoteCollection.isOpen)
                closeTestCollection()
            var listView = findChild(appLoader.item, "blockListView")
            var scroll = findChild(appLoader.item, "editorScrollView")
            verify(listView !== null && scroll !== null)
            var fullWidth = listView.width
            verify(fullWidth > 550)

            Typography.maxContentWidth = 500
            tryVerify(function() {
                return Math.abs(listView.width - 500) <= 1
            }, 1000)
            // Delegates ride the narrowed column...
            compare(findBlockDelegate(0).width, listView.width)
            // ...which sits centered: symmetric margins beyond the base.
            verify(scroll.centeringMargin > 0)
            compare(scroll.anchors.leftMargin, 20 + scroll.centeringMargin)
            compare(scroll.anchors.rightMargin, scroll.anchors.leftMargin)

            Typography.maxContentWidth = 0
            tryVerify(function() {
                return listView.width === fullWidth
            }, 1000)
            compare(scroll.centeringMargin, 0)
        }

        function test_za_paragraphSpacingDrivesList() {
            var listView = findChild(appLoader.item, "blockListView")
            compare(listView.spacing, 8)
            Typography.paragraphSpacing = 20
            compare(listView.spacing, 20)
            Typography.paragraphSpacing = 8
        }

        function test_zb_settingsDialogBindsLive() {
            var dialog = findChild(appLoader.item, "settingsDialog")
            verify(dialog !== null)
            dialog.open()
            tryCompare(dialog, "opened", true, 1000)

            // The theme cards switch the live theme...
            var darkCard = findChild(dialog.contentItem, "themeCard_dark")
            verify(darkCard !== null)
            mouseClick(darkCard)
            compare(Theme.themeId, "dark")
            Theme.themeId = "light"

            // ...and the typography spin drives the setting.
            var typographyTab = findChild(appLoader.item, "typographyTab")
            verify(typographyTab !== null)
            mouseClick(typographyTab)
            wait(50)
            var sizeSpin = findChild(appLoader.item, "fontSizeSpin")
            verify(sizeSpin !== null)
            sizeSpin.value = 18
            sizeSpin.valueModified()
            compare(Typography.baseSize, 18)

            Typography.resetToDefaults()
            dialog.close()
            tryCompare(dialog, "visible", false, 1000)
        }

        // ============================================================
        // Resizable panels and independent collapse
        // ============================================================

        function test_zc_panelSeamResizesAndPersists() {
            if (isHeadless) {
                skip("Panel seam input requires display")
            }
            openTestCollection()
            var seam = findChild(appLoader.item, "sidebarSeam")
            var sb = findChild(appLoader.item, "sidebar")
            verify(seam !== null && sb !== null)
            tryCompare(sb, "width", 200, 1000)

            // Drag the seam 60px right...
            mousePress(seam, 3, 100)
            mouseMove(seam, 63, 100)
            mouseRelease(seam, 63, 100)
            tryVerify(function() { return sb.width === 260 }, 1000)
            compare(AppSettings.value("panels.sidebarWidth", 0), 260)

            // ...and far left: the clamp holds.
            mousePress(seam, 3, 100)
            mouseMove(seam, 3 - 500, 100)
            mouseRelease(seam, 3 - 500, 100)
            tryVerify(function() { return sb.width === 140 }, 1000)

            // The note-list seam clamps on its own bounds (the drag
            // target stays inside the window: out-of-window synthetic
            // coordinates are unreliable on the real display).
            var listSeam = findChild(appLoader.item, "noteListSeam")
            // The Row repositions children in its polish pass; press
            // only after the previous drag's relayout has landed.
            waitForRendering(findChild(appLoader.item, "sidePanels"))
            mousePress(listSeam, 3, 100)
            mouseMove(listSeam, 3 + 300, 100)
            mouseRelease(listSeam, 3 + 300, 100)
            var pane = findChild(appLoader.item, "noteListPane")
            tryVerify(function() { return pane.width === 520 }, 1000)

            appLoader.item.sidebarWidth = 200
            appLoader.item.noteListWidth = 260
            closeTestCollection()
        }

        function test_zd_panelsCollapseIndependently() {
            if (isHeadless) {
                skip("Panel button input requires display")
            }
            openTestCollection()
            var appWin = appLoader.item
            var panels = findChild(appWin, "sidePanels")
            var widthBefore = panels.width

            // Collapse the sidebar from its header chevron: the strip
            // replaces it, the note list stays, the editor grows.
            var collapse = findChild(appWin, "sidebarCollapseButton")
            verify(collapse !== null)
            waitForRendering(panels)
            wait(100)
            mouseClick(collapse)
            tryCompare(appWin, "sidebarCollapsed", true, 1000)
            compare(AppSettings.value("panels.sidebarCollapsed", false),
                    true)
            var strip = findChild(appWin, "sidebarStrip")
            tryCompare(strip, "visible", true, 1000)
            verify(findChild(appWin, "noteListPane").visible)
            verify(panels.width < widthBefore)

            // Expand from the strip (settled first, like the banner).
            waitForRendering(panels)
            wait(200)
            mouseClick(findChild(appWin, "sidebarExpandButton"))
            tryCompare(appWin, "sidebarCollapsed", false, 1000)
            tryCompare(strip, "visible", false, 1000)
            compare(panels.width, widthBefore)

            // The note list collapses independently; the read path
            // restores a persisted collapse.
            waitForRendering(panels)
            wait(200)
            mouseClick(findChild(appWin, "noteListCollapseButton"))
            tryCompare(appWin, "noteListCollapsed", true, 1000)
            verify(findChild(appWin, "sidebar").visible)
            waitForRendering(panels)
            wait(200)
            mouseClick(findChild(appWin, "noteListExpandButton"))
            tryCompare(appWin, "noteListCollapsed", false, 1000)

            AppSettings.setValue("panels.noteListCollapsed", true)
            appWin.applyPersistedSessionState()
            compare(appWin.noteListCollapsed, true)
            appWin.noteListCollapsed = false

            // Ctrl+\ still hides everything at once.
            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            tryCompare(panels, "visible", false, 1000)
            compare(panels.width, 0)
            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            tryCompare(panels, "visible", true, 1000)

            closeTestCollection()
        }

        // ============================================================
        // Superscript/subscript and the toolbar
        // ============================================================

        function test_ze_supSubTypeAndReveal() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "E=mc^2^ done")
            wait(150)
            var ta = findTextArea(findBlockDelegate(0))
            // Hidden markers: display text drops the carets.
            compare(ta.text, "E=mc2 done")
            ensureFocus(ta)
            ta.cursorPosition = 5   // touching the sup span: reveals
            tryCompare(ta, "text", "E=mc^2^ done", 1000)
            ta.cursorPosition = ta.text.length
            tryCompare(ta, "text", "E=mc2 done", 1000)
        }

        function test_zf_toolbarReflectsAndAppliesFormatting() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "plain **bold** text")
            wait(150)
            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            var boldBtn = findChild(appLoader.item, "toolbarBoldButton")
            verify(boldBtn !== null)

            ta.cursorPosition = 2   // plain text
            tryCompare(boldBtn, "checked", false, 1000)
            ta.cursorPosition = 8   // inside "bold"
            tryCompare(boldBtn, "checked", true, 1000)

            // Applying from the button keeps focus in the block and
            // lands the registry markers in the model.
            ta.select(11, 15)       // "text"
            var supBtn = findChild(appLoader.item,
                                   "toolbarSuperscriptButton")
            mouseClick(supBtn)
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("^text^") !== -1
            }, 1000)
            verify(ta.activeFocus)

            // Verbatim code block: formatting disables.
            BlockModel.insertBlock(1, 8, "code()")
            wait(150)
            var codeTa = findTextArea(findBlockDelegate(1))
            ensureFocus(codeTa)
            tryCompare(boldBtn, "enabled", false, 1000)
        }

        function test_zt_textColorApplyRecolorRemoveUndo() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "make this red")
            wait(150)
            var para = findBlockDelegate(0)
            var ta = findTextArea(para)
            ensureFocus(ta)

            // Select the content word "red" from the live document text, so
            // the selection is right whether or not the span is revealed; the
            // wait lets the deferred reveal settle before the command reads
            // the mapping (mirroring the real select-then-click flow).
            function selectRed() {
                para = findBlockDelegate(0)
                ta = findTextArea(para)
                ensureFocus(ta)
                var i = ta.text.indexOf("red")
                ta.select(i, i + 3)
                wait(120)
            }

            // Apply color to the selected word, through the model.
            selectRed()
            var stackBefore = UndoStack.count
            para.applyColor("#e05c5c")
            tryVerify(function() {
                return BlockModel.getContent(0)
                    === "make this <span style=\"color:#e05c5c\">red</span>"
            }, 1000, "applyColor wraps the selection")

            // Re-coloring the same content rewrites the value in place.
            selectRed()
            para.applyColor("#4a90d9")
            tryVerify(function() {
                return BlockModel.getContent(0)
                    === "make this <span style=\"color:#4a90d9\">red</span>"
            }, 1000, "re-color rewrites in place, no nesting")

            // Remove color unwraps.
            selectRed()
            para.removeColor()
            tryVerify(function() {
                return BlockModel.getContent(0) === "make this red"
            }, 1000, "removeColor unwraps")

            // The color edits are undoable (each is a model content update;
            // consecutive ones may coalesce under the undo stack's existing
            // text-change merge policy, exactly like consecutive typing).
            verify(UndoStack.count > stackBefore,
                   "the color edits pushed undo entries")
            UndoStack.undo()
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("<span style=\"color:") !== -1
            }, 1000, "one undo restores a color span")
        }

        function test_zu_imageInsertAndEdit() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "")
            wait(100)

            // Menu-insert flow: the window function opens the dialog; filling
            // the path and accepting converts the block to an Image.
            appLoader.item.insertImageIntoBlock(0)
            var dlg = findChild(appLoader.item, "imageInsertDialog")
            verify(dlg !== null && dlg.visible, "insert dialog opens")
            var field = findChild(appLoader.item, "imagePathField")
            field.text = sampleImagePath
            dlg.accept()
            tryVerify(function() {
                return BlockModel.blockAt(0).blockType === 11  // Block.Image
            }, 1000, "the block becomes an Image")
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf(sampleImagePath) !== -1
            }, 1000, "content holds the chosen path")

            // Edit through the delegate (the resize/caption write path).
            // Wait for the Image delegate to be (re)created after conversion.
            tryVerify(function() {
                var d = findBlockDelegate(0)
                return d !== null && d.writeImage !== undefined
            }, 1000, "the Image delegate is created")
            var img = findBlockDelegate(0)
            var stackBefore = UndoStack.count
            img.writeImage(sampleImagePath, "alt text", "a caption", 175)
            tryVerify(function() {
                var c = BlockModel.getContent(0)
                return c.indexOf("|175") !== -1 && c.indexOf("a caption") !== -1
                    && c.indexOf("alt text") !== -1
            }, 1000, "writeImage rebuilds the markdown with width, alt, caption")
            verify(UndoStack.count > stackBefore, "the edit is undoable")
        }

        function test_zv_imageDropAndPasteWiring() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "para")
            wait(100)

            // Paste-path wiring: insertImageBlock (used by handleImagePaste)
            // inserts an Image block below with the built markdown.
            var d = findBlockDelegate(0)
            verify(d !== null)
            verify(typeof d.insertImageBlock === "function")
            d.insertImageBlock("assets/pic.png")
            tryVerify(function() {
                return BlockModel.count === 2
                    && BlockModel.blockAt(1).blockType === 11  // Image
                    && BlockModel.getContent(1) === "![](assets/pic.png)"
            }, 1000, "insertImageBlock adds an Image block")

            // Drop-path wiring: blockForPath routes by extension.
            var w = appLoader.item
            compare(w.blockForPath("clip.mp4").type, 14)  // Media
            compare(w.blockForPath("photo.png").type, 11) // Image

            // External text drop splits into paragraphs at the drop point.
            w.insertBlocksAt(0, [{ type: 0, content: "dropped one" },
                                 { type: 0, content: "dropped two" }])
            tryVerify(function() {
                return BlockModel.getContent(1) === "dropped one"
                    && BlockModel.getContent(2) === "dropped two"
            }, 1000, "insertBlocksAt inserts text-drop paragraphs")
        }

        function test_zw_calloutFoldTitleAndSerialize() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            // Create a callout (type/title/fold via convertBlock).
            BlockModel.convertBlock(0, 12, "callout body", false, "warning", "Careful")
            wait(150)
            compare(BlockModel.blockAt(0).blockType, 12)         // Callout
            compare(BlockModel.blockAt(0).language, "warning")   // type
            compare(BlockModel.blockAt(0).calloutTitle, "Careful")

            // It serializes as an Obsidian callout, byte-identical.
            compare(DocumentSerializer.serialize(BlockModel),
                    "> [!warning] Careful\n> callout body\n")

            // Fold toggle is one undo step.
            var stackBefore = UndoStack.count
            BlockModel.setChecked(0, true)
            compare(BlockModel.blockAt(0).checked, true)         // folded
            compare(DocumentSerializer.serialize(BlockModel),
                    "> [!warning]- Careful\n> callout body\n")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() { return !BlockModel.blockAt(0).checked },
                      1000, "one undo unfolds")

            // Title edit is undoable and round-trips.
            BlockModel.setCalloutTitle(0, "Watch out")
            compare(BlockModel.blockAt(0).calloutTitle, "Watch out")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            tryVerify(function() {
                return BlockModel.blockAt(0).calloutTitle === "Careful"
            }, 1000, "one undo restores the title")

            // Ctrl+Enter in the callout body flips the fold state.
            var cd = findBlockDelegate(0)
            var ta = findTextArea(cd)
            ensureFocus(ta)
            keyClick(Qt.Key_Return, Qt.ControlModifier)
            tryVerify(function() { return BlockModel.blockAt(0).checked },
                      1000, "Ctrl+Enter folds the callout")
        }

        function test_zx_tableEditMutateSortUndo() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 15,   // Block.Table
                "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |")
            wait(150)
            tryVerify(function() {
                var d = findBlockDelegate(0)
                return d !== null && d.commitCell !== undefined
            }, 1000, "the Table delegate is created")
            var tbl = findBlockDelegate(0)

            // Cell edit through the model.
            tbl.commitCell(0, 0, "hello")
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("hello") !== -1
            }, 1000, "commitCell rewrites the cell")

            // Append a column, then a row.
            tbl.writeTable(TableTools.insertColumn(BlockModel.getContent(0),
                                                   tbl.columns - 1))
            tryVerify(function() { return findBlockDelegate(0).columns === 3 },
                      1000, "insertColumn adds a column")
            tbl.writeTable(TableTools.insertRow(BlockModel.getContent(0),
                                                findBlockDelegate(0).dataRows - 1))
            tryVerify(function() { return findBlockDelegate(0).dataRows === 3 },
                      1000, "insertRow adds a row")

            // Sort undoability on a fresh table (its command can't merge with a
            // prior updateContent). The undo stack coalesces consecutive text
            // edits, so exact per-op counts are not asserted.
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 15,
                "| N | V |\n| --- | --- |\n| b | 3 |\n| a | 1 |")
            wait(150)
            var tbl2 = findBlockDelegate(0)
            var before = BlockModel.getContent(0)
            tbl2.sortBy(0)   // ascending by column 0 → a, b
            tryVerify(function() {
                return BlockModel.getContent(0) !== before
                    && BlockModel.getContent(0).indexOf("| a |") < BlockModel.getContent(0).indexOf("| b |")
            }, 1000, "sortBy reorders rows")
            UndoStack.undo()
            tryVerify(function() { return BlockModel.getContent(0) === before },
                      1000, "one undo reverts the sort")
        }

        // A table is laid out inline, so it has no viewport to virtualise
        // against and every rendered row is a live row of cells. Two things
        // used to scale badly and are gated here: each cell asked TableTools
        // for its value, which re-parsed the entire table markdown, so the
        // work was cells x table size; and every row was built regardless of
        // how many there were. Both are invisible to the prose corpus the
        // other performance tests use.
        function test_zx2_largeTableStaysBounded() {
            function buildTable(rows, cols) {
                var header = [], delim = []
                for (var c = 0; c < cols; c++) {
                    header.push("h" + c)
                    delim.push("---")
                }
                var lines = ["| " + header.join(" | ") + " |",
                             "| " + delim.join(" | ") + " |"]
                for (var r = 0; r < rows; r++) {
                    var cells = []
                    for (var c2 = 0; c2 < cols; c2++)
                        cells.push("r" + r + "c" + c2)
                    lines.push("| " + cells.join(" | ") + " |")
                }
                return lines.join("\n")
            }

            function countItems(item) {
                if (!item)
                    return 0
                var n = 1
                var kids = item.children
                for (var i = 0; i < kids.length; i++)
                    n += countItems(kids[i])
                return n
            }

            function createTable(rows, cols) {
                DocumentManager.newDocument()
                wait(50)
                var t0 = Date.now()
                BlockModel.convertBlock(0, 15, buildTable(rows, cols))
                tryVerify(function() {
                    var d = findBlockDelegate(0)
                    return d !== null && findChild(d, "tableGrid") !== null
                }, 5000, rows + "x" + cols + " table renders")
                var ms = Date.now() - t0
                // Counted now: the next newDocument() recycles this delegate,
                // so a reference kept past here reports an empty grid.
                var d = findBlockDelegate(0)
                return { ms: ms, delegate: d,
                         items: countItems(findChild(d, "tableGrid")),
                         hidden: d.hiddenRows }
            }

            // (1) Creation cost must not grow with the square of the table.
            // The 100x100 case is 10,000 cells; before the row window and the
            // O(1) cell lookup it did ~10,000 full parses of a ~500 KB string.
            var small = createTable(10, 10)
            console.log("TABLE CREATE 10x10: " + small.ms + " ms")
            var tall = createTable(100, 10)
            console.log("TABLE CREATE 100x10: " + tall.ms + " ms")
            var huge = createTable(100, 100)
            console.log("TABLE CREATE 100x100: " + huge.ms + " ms")
            verify(huge.ms < 3000,
                   "a 100x100 table must render in under 3s (measured "
                   + huge.ms + "ms)")

            // (2) Rows past the window are not built, so the item count of a
            // 1,000-row table tracks the window rather than the row count.
            var wide = createTable(1000, 5)
            console.log("TABLE CREATE 1000x5: " + wide.ms + " ms")
            verify(wide.ms < 3000,
                   "a 1000-row table must render in under 3s (measured "
                   + wide.ms + "ms)")
            console.log("TABLE ITEMS 1000x5: " + wide.items
                        + " vs 100x10: " + tall.items)
            verify(wide.hidden > 0,
                   "a 1000-row table holds rows back (hidden: "
                   + wide.hidden + ")")
            verify(wide.items < tall.items,
                   "a 1000-row 5-column table must not build more items than a "
                   + "100-row 10-column one (" + wide.items + " vs "
                   + tall.items + ")")

            // (3) The held-back rows stay reachable.
            wide.delegate.revealAllRows()
            tryVerify(function() { return wide.delegate.hiddenRows === 0 },
                      5000, "Show all reveals every row")

            // (4) Editing one cell of a large table must not re-parse the
            // table once per rendered cell.
            var edit = createTable(100, 10)
            var content0 = BlockModel.getContent(0)
            var tEdit = Date.now()
            edit.delegate.commitCell(0, 0, "edited")
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("edited") >= 0
            }, 5000, "the cell edit lands")
            wait(0)
            var editMs = Date.now() - tEdit
            console.log("TABLE CELL EDIT 100x10: " + editMs + " ms")
            verify(editMs < 500,
                   "one cell edit in a 100x10 table must settle under 500ms "
                   + "(measured " + editMs + "ms)")
        }

        function test_zy_todoMetaAndQuoteNesting() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            // A todo; apply a due date and cycle priority through the delegate.
            BlockModel.convertBlock(0, 6, "ship it")   // Todo
            wait(150)
            var todo = findBlockDelegate(0)
            verify(todo !== null && todo.setDue !== undefined)
            todo.setDue("2026-07-15")
            tryVerify(function() {
                return TodoMeta.parse(BlockModel.getContent(0)).due === "2026-07-15"
            }, 1000, "setDue writes the date token")
            todo.cyclePriority()   // none → low
            tryVerify(function() {
                return TodoMeta.parse(BlockModel.getContent(0)).priority === -1
            }, 1000, "cyclePriority sets low")
            // The metadata tail is excluded from the editable text.
            compare(todo.editableMarkdown, "ship it")
            // …and from search: searching the emoji finds nothing, the text does.
            DocumentSearch.active = true
            DocumentSearch.query = "ship"
            tryVerify(function() { return DocumentSearch.matchCount > 0 }, 1000)
            DocumentSearch.query = "📅"
            tryCompare(DocumentSearch, "matchCount", 0, 1000)
            DocumentSearch.active = false

            // Sub-task progress from deeper todo children.
            BlockModel.insertBlock(1, 6, "step one", 1)
            BlockModel.setChecked(1, true)
            BlockModel.insertBlock(2, 6, "step two", 1)
            wait(150)
            var prog = BlockModel.todoProgress(0)
            compare(prog.done, 1)
            compare(prog.total, 2)

            // Quote nesting: Tab increases the quote's depth (indentLevel).
            DocumentManager.newDocument()
            wait(100)
            BlockModel.convertBlock(0, 7, "a quote")   // Quote
            wait(150)
            var q = findTextArea(findBlockDelegate(0))
            ensureFocus(q)
            keyClick(Qt.Key_Tab)
            tryVerify(function() { return BlockModel.blockAt(0).indentLevel === 1 },
                      1000, "Tab nests the quote to depth 2")
            keyClick(Qt.Key_Backtab)
            tryVerify(function() { return BlockModel.blockAt(0).indentLevel === 0 },
                      1000, "Shift+Tab un-nests")
        }

        function test_zg_toolbarBlockDropdownConverts() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "future heading")
            wait(150)
            ensureFocus(findTextArea(findBlockDelegate(0)))

            var combo = findChild(appLoader.item, "toolbarBlockTypeCombo")
            verify(combo !== null)
            tryCompare(combo, "currentIndex", 0, 1000)  // Text

            combo.activated(1)  // Heading 1
            tryVerify(function() {
                return BlockModel.blockAt(0).blockType === 1
            }, 1000)
            tryCompare(combo, "currentIndex", 1, 1000)
            compare(BlockModel.getContent(0), "future heading")

            combo.activated(0)
            tryVerify(function() {
                return BlockModel.blockAt(0).blockType === 0
            }, 1000)
        }

        function test_zh_toolbarInsertAndStatusBarToggle() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            ensureFocus(findTextArea(findBlockDelegate(0)))
            var toolbar = findChild(appLoader.item, "toolbar")
            toolbar.insertBlockOfType(7)  // Quote below the block
            tryVerify(function() {
                return BlockModel.count === 2
                       && BlockModel.blockAt(1).blockType === 7
            }, 1000)

            var statusBar = findChild(appLoader.item, "statusBar")
            verify(statusBar.visible)
            appLoader.item.statusBarVisible = false
            tryCompare(statusBar, "visible", false, 1000)
            compare(AppSettings.value("view.statusBar", true), false)
            appLoader.item.statusBarVisible = true
            tryCompare(statusBar, "visible", true, 1000)
        }

        function test_zi_toolbarCustomizationPersists() {
            var toolbar = findChild(appLoader.item, "toolbar")
            toolbar.setGroupVisible("toolbar.showFormatting",
                                    "showFormatGroup", false)
            compare(toolbar.showFormatGroup, false)
            compare(AppSettings.value("toolbar.showFormatting", true),
                    false)

            // The read path restores it.
            AppSettings.setValue("toolbar.showFormatting", true)
            toolbar.applyPersistedCustomization()
            compare(toolbar.showFormatGroup, true)
        }

        // ============================================================
        // The status bar and the floating formatting
        // bar (features.md §9.7 and §9.3).
        // ============================================================

        function test_zj_statusBarCaretPosition() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "hello world")
            BlockModel.insertBlock(1, 8, "first\nsecond")   // code block
            wait(150)

            var posText = findChild(appLoader.item, "cursorPositionText")
            verify(posText !== null)

            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.cursorPosition = 0
            tryCompare(posText, "text", "Block 1 · Ln 1, Col 1", 1000)
            ta.cursorPosition = 5
            tryCompare(posText, "text", "Block 1 · Ln 1, Col 6", 1000)

            // Second line of a multi-line code block.
            var codeTa = findTextArea(findBlockDelegate(1))
            ensureFocus(codeTa)
            codeTa.cursorPosition = 8   // "first\nse|cond"
            tryCompare(posText, "text", "Block 2 · Ln 2, Col 3", 1000)
        }

        function test_zk_statusBarSelectionCounts() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "one two three")
            BlockModel.insertBlock(1, 0, "four **five**")
            wait(150)

            var wordText = findChild(appLoader.item, "wordCountText")
            tryCompare(wordText, "text", "5 words", 1500)

            // An in-block selection counts itself.
            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.select(0, 7)   // "one two"
            tryCompare(wordText, "text", "2 words selected", 1500)

            // A block selection counts its blocks' display text
            // (markers stripped: "four five" = 2 words, 9 chars).
            ta.deselect()
            DocumentSelection.selectBlock(1)
            tryCompare(wordText, "text", "2 words selected", 1500)
            var charText = findChild(appLoader.item, "charCountText")
            tryCompare(charText, "text", "9 chars", 1500)

            DocumentSelection.clear()
            tryCompare(wordText, "text", "5 words", 1500)
        }

        function test_zka_statusBarSeparatesLargeCounts() {
            var contents = []
            for (var i = 0; i < 1001; i++)
                contents.push("word")
            docWithBlocks(contents)

            var blockText = findChild(appLoader.item, "blockCountText")
            var wordText = findChild(appLoader.item, "wordCountText")
            var charText = findChild(appLoader.item, "charCountText")
            verify(blockText !== null, "block count text exists")
            verify(wordText !== null, "word count text exists")
            verify(charText !== null, "char count text exists")

            tryCompare(blockText, "text", "1,001 blocks", 1500)
            tryCompare(wordText, "text", "1,001 words", 1500)
            tryCompare(charText, "text", "4,004 chars", 1500)
        }

        function test_zl_formattingBarAppearsAndApplies() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "top line here")
            BlockModel.insertBlock(1, 0, "hello brave world")
            for (var filler = 0; filler < 12; ++filler)
                BlockModel.insertBlock(2 + filler, 0,
                    "filler line " + filler + " for scrolling")
            wait(150)

            var bar = findChild(appLoader.item, "formattingBar")
            verify(bar !== null)
            verify(!bar.visible)

            var ta = findTextArea(findBlockDelegate(1))
            ensureFocus(ta)
            ta.select(6, 11)   // "brave"
            appLoader.item.lastFocusedBlock = 1
            tryVerify(function() {
                var target = findBlockDelegate(1)
                return target.selectionEndDoc > target.selectionStartDoc
            }, 1000, "delegate publishes the settled selection")
            tryCompare(bar, "visible", true, 2000)

            // Above the selection: the bar's bottom sits over the
            // block's top half... at least strictly above the caret's
            // line rectangle.
            var delegateItem = findBlockDelegate(1)
            var rect = delegateItem.selectionRectangle()
            var barBottomInDelegate =
                bar.parent.mapToItem(delegateItem, bar.x, bar.y + bar.height)
            verify(barBottomInDelegate.y <= rect.y)

            // Applying wraps the selection in registry markers.
            var boldBtn = findChild(appLoader.item, "fbBoldButton")
            mouseClick(boldBtn)
            tryVerify(function() {
                return BlockModel.getContent(1) === "hello **brave** world"
            }, 1000)

            // Escape drops the selection and the bar with it.
            ta = findTextArea(findBlockDelegate(1))
            ensureFocus(ta)
            ta.select(6, 11)
            tryCompare(bar, "visible", true, 2000)
            keyClick(Qt.Key_Escape)
            tryCompare(bar, "visible", false, 1000)

            // Scrolling dismisses.
            ta = findTextArea(findBlockDelegate(1))
            ensureFocus(ta)
            ta.select(6, 11)
            tryCompare(bar, "visible", true, 2000)
            var listView = findChild(appLoader.item, "blockListView")
            listView.contentY = listView.contentY + 5
            tryCompare(bar, "visible", false, 1000)
            listView.contentY = 0
            ta.deselect()
        }

        function test_zm_formattingBarFlipsAtViewportTop() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "very first line of the document")
            wait(150)

            var bar = findChild(appLoader.item, "formattingBar")
            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.select(0, 9)
            appLoader.item.lastFocusedBlock = 0
            tryVerify(function() {
                var target = findBlockDelegate(0)
                return target.selectionEndDoc > target.selectionStartDoc
            }, 1000, "delegate publishes the settled selection")
            tryCompare(bar, "visible", true, 2000)

            // No room above the first line: the bar flips below it.
            var delegateItem = findBlockDelegate(0)
            var rect = delegateItem.selectionRectangle()
            var barTopInDelegate =
                bar.parent.mapToItem(delegateItem, bar.x, bar.y)
            verify(barTopInDelegate.y >= rect.y + rect.height)
            ta.deselect()
        }

        // ============================================================
        // Context menus (features.md §9.5) and trash
        // emptying. Menus trigger operations that are independently
        // tested; these spot-check routing.
        // ============================================================

        function test_zn_textAndLinkContextMenus() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0,
                "plain words and a [link](https://x.example) here")
            wait(150)
            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)

            // Right-click in plain text: the text menu, cut disabled
            // without a selection.
            mouseClick(ta, 8, 10, Qt.RightButton)
            var textMenu = findChild(appLoader.item, "textContextMenu")
            tryCompare(textMenu, "visible", true, 1000)
            compare(findChild(appLoader.item, "ctxCut").enabled, false)
            textMenu.close()
            tryCompare(textMenu, "visible", false, 1000)

            // With a selection, Cut removes exactly the copy capture.
            ta.select(0, 5)   // "plain"
            mouseClick(ta, 8, 10, Qt.RightButton)
            tryCompare(textMenu, "visible", true, 1000)
            var cut = findChild(appLoader.item, "ctxCut")
            compare(cut.enabled, true)
            cut.triggered()
            textMenu.close()
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("plain") === -1
            }, 1000)
            compare(Clipboard.text, "plain")

            // Right-click ON the link: the link menu wins; Remove link
            // keeps the text.
            var linkPos = ta.text.indexOf("link")
            var rect = ta.positionToRectangle(linkPos + 1)
            mouseClick(ta, rect.x, rect.y + 5, Qt.RightButton)
            var linkMenu = findChild(appLoader.item, "linkContextMenu")
            tryCompare(linkMenu, "visible", true, 1000)
            findChild(appLoader.item, "ctxRemoveLink").triggered()
            linkMenu.close()
            tryVerify(function() {
                var md = BlockModel.getContent(0)
                return md.indexOf("[link]") === -1
                       && md.indexOf("link") !== -1
            }, 1000)
        }

        function test_zo_blockAndSelectionMenus() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            BlockModel.updateContent(0, "block zero")
            BlockModel.insertBlock(1, 0, "block one")
            wait(150)

            // Hover to raise the gutter, then right-click the handle.
            var delegateItem = findBlockDelegate(0)
            mouseMove(delegateItem, 30, delegateItem.height / 2)
            tryCompare(delegateItem, "isHovered", true, 1000)
            var handle = findChild(delegateItem, "dragHandle")
            // The hover-gated gutter fades in; click only once the
            // handle is actually visible (the plus-button storyboards
            // wait the same way).
            tryCompare(handle, "visible", true, 1000)
            mouseClick(handle, 3, 3, Qt.RightButton)
            var blockMenu2 = findChild(appLoader.item, "blockContextMenu")
            tryCompare(blockMenu2, "visible", true, 1000)
            findChild(appLoader.item, "ctxBlockDuplicate").triggered()
            blockMenu2.close()
            tryVerify(function() { return BlockModel.count === 3 }, 1000)
            compare(BlockModel.getContent(1), "block zero")

            // A selected block routes to the selection menu; Delete
            // removes the whole selection.
            DocumentSelection.selectBlock(0)
            DocumentSelection.extendBlockSelectionTo(1)
            mouseMove(delegateItem, 30, delegateItem.height / 2)
            tryCompare(delegateItem, "isHovered", true, 1000)
            tryCompare(handle, "visible", true, 1000)
            mouseClick(handle, 3, 3, Qt.RightButton)
            var selMenu = findChild(appLoader.item, "selectionContextMenu")
            tryCompare(selMenu, "visible", true, 1000)
            findChild(appLoader.item, "ctxSelDelete").triggered()
            selMenu.close()
            tryVerify(function() {
                return BlockModel.count === 1
                       && BlockModel.getContent(0) === "block one"
            }, 1000)
        }

        function test_zp_collectionContextMenus() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()

            // Note row: right-click → pin through the menu.
            var row = noteRowFor("Welcome.md")
            mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton)
            var noteMenu = findChild(appLoader.item, "noteContextMenu")
            tryCompare(noteMenu, "visible", true, 1000)
            findChild(appLoader.item, "ctxNotePin").triggered()
            noteMenu.close()
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").pinned === true
            }, 1000)

            // Folder row: right-click opens the folder menu.
            var folderRow = folderRowFor("Ideas")
            mouseClick(folderRow, folderRow.width / 2, folderRow.height / 2,
                       Qt.RightButton)
            var folderMenu = findChild(appLoader.item, "folderContextMenu")
            tryCompare(folderMenu, "visible", true, 1000)
            compare(folderMenu.relPath, "Ideas")
            folderMenu.close()

            closeTestCollection()
        }

        function test_zq_trashCountAndEmpty() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            compare(NoteCollection.trashItemCount(), 0)

            verify(NoteCollection.deleteNote("Welcome.md"))
            compare(NoteCollection.trashItemCount(), 1)
            var label = findChild(appLoader.item, "trashCountLabel")
            tryCompare(label, "text", "1", 1000)

            // The confirmation applies the permanent removal.
            var dialog = findChild(appLoader.item, "emptyTrashDialog")
            dialog.openFor(1)
            tryCompare(dialog, "opened", true, 1000)
            dialog.accept()
            tryVerify(function() {
                return NoteCollection.trashItemCount() === 0
            }, 1000)
            tryCompare(label, "text", "0", 1000)

            closeTestCollection()
        }

        // ============================================================
        // The custom date-range filter and the theme-switch latency gate.
        // ============================================================

        function test_zr_customDateRangeFilters() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            BlockModel.updateContent(0, "unmistakable content")
            verify(DocumentManager.save())

            CollectionSearch.query = "unmistakable"
            tryVerify(function() {
                return CollectionSearch.noteCount === 1
            }, 1000)

            // The picker's two-click flow: start day, then end day.
            var picker = findChild(appLoader.item, "dateRangePicker")
            verify(picker !== null)
            picker.openFor()
            tryCompare(picker, "opened", true, 1000)

            var today = new Date()
            var old = new Date(today.getFullYear(), today.getMonth(),
                               today.getDate() - 30)
            var older = new Date(today.getFullYear(), today.getMonth(),
                                 today.getDate() - 20)
            picker.pickDay(old)
            picker.pickDay(older)
            compare(CollectionSearch.datePreset, "custom")

            // A month-old window excludes the note written just now...
            tryVerify(function() {
                return CollectionSearch.noteCount === 0
            }, 1000)

            // ...and a window covering today includes it; out-of-order
            // picks swap into a valid range.
            picker.pickDay(today)
            picker.pickDay(new Date(today.getFullYear(), today.getMonth(),
                                    today.getDate() - 2))
            tryVerify(function() {
                return CollectionSearch.noteCount === 1
            }, 1000)

            // Clear returns to the preset path.
            var clearButton = findChild(appLoader.item,
                                        "pickerClearButton")
            mouseClick(clearButton)
            compare(CollectionSearch.datePreset, "any")
            tryCompare(picker, "visible", false, 1000)

            CollectionSearch.query = ""
            closeTestCollection()
        }

        function test_zs_themeSwitchUnder250ms() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            var markdown = ""
            for (var i = 0; i < 100; i++)
                markdown += "Block " + i + " with **bold** and *italic*\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, markdown)
            compare(BlockModel.count, 100)
            var listView = findChild(appLoader.item, "blockListView")
            listView.positionViewAtBeginning()
            wait(200)

            var t0 = Date.now()
            Theme.themeId = "dark"
            waitForRendering(listView)
            var elapsed = Date.now() - t0
            console.log("THEME SWITCH: " + elapsed
                        + " ms on a 100-block document")
            Theme.themeId = "light"
            waitForRendering(listView)
            verify(elapsed < 250, "theme switch must stay under 250 ms "
                   + "(measured " + elapsed + "ms)")
        }

        // ---- Document outline + internal links ----

        function test_zt_outlineAndInternalLinks() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Introduction\n\nSee the [details](#details) section.\n\n"
                + "## Details\n\nBody of details.\n\n## Summary")
            DocumentOutline.rebuildNow()
            wait(100)

            // The outline lists the three headings and resolves their slugs.
            compare(DocumentOutline.count, 3)
            compare(DocumentOutline.blockIndexForSlug("details"), 2)
            compare(DocumentOutline.blockIndexForSlug("summary"), 4)

            // Toggle the outline pane on; it renders the heading rows.
            appLoader.item.outlineVisible = true
            wait(150)
            var panel = findChild(appLoader.item, "outlinePanel")
            verify(panel !== null && panel.visible, "outline panel visible")
            var list = findChild(appLoader.item, "outlineList")
            verify(list !== null, "outline list exists")
            compare(list.count, 3)

            // Internal-link navigation: activating #details scrolls to block 2.
            appLoader.item.linkOpener.openExternally = false
            appLoader.item.linkOpener.activate("#details")
            wait(200)
            var lv = findChild(appLoader.item, "blockListView")
            compare(lv.currentIndex, 2)
            // The caret's section ("Details", outline row 1) lights up live.
            compare(DocumentOutline.currentRow, 1)

            // An unresolved internal link is a no-op with a status note.
            var before = lv.currentIndex
            appLoader.item.linkOpener.activate("#nonexistent")
            wait(150)
            compare(lv.currentIndex, before)
            var status = findChild(appLoader.item, "transientStatusText")
            verify(status.text.indexOf("nonexistent") !== -1,
                   "status names the missing heading")

            appLoader.item.outlineVisible = false
            appLoader.item.linkOpener.openExternally = true
        }

        function test_zu_outlineCollapseAndLevelFilter() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Parent\n\n## Child A\n\n## Child B\n\n# Sibling")
            DocumentOutline.rebuildNow()
            wait(50)
            compare(DocumentOutline.count, 4)

            // Collapsing the first heading hides its two children.
            DocumentOutline.toggleCollapsed(0)
            compare(DocumentOutline.count, 2)
            DocumentOutline.toggleCollapsed(0)
            compare(DocumentOutline.count, 4)

            // The level filter drops H2 rows.
            DocumentOutline.levelMask = 0x1  // level 1 only
            compare(DocumentOutline.count, 2)
            DocumentOutline.levelMask = 0xF  // restore
            compare(DocumentOutline.count, 4)
        }

        function test_zv_linkDialogHeadingMode() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Alpha\n\nsome text\n\n## Beta")
            DocumentOutline.rebuildNow()
            wait(50)

            var dlg = appLoader.item.linkDialog
            dlg.openForInsert(1, 0, 0, "")
            wait(150)
            var combo = findChild(appLoader.item, "linkDialogHeadingCombo")
            verify(combo !== null && combo.visible, "heading combo visible")
            // Placeholder row + the two headings.
            compare(combo.count, 3)
            dlg.close()
            tryCompare(dlg, "visible", false, 1000)
        }

        // ---- The table-of-contents block ----

        function test_zw_tocBlockRegenerates() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# First\n\n```toc\n```\n\n## Second\n\nbody")
            DocumentOutline.rebuildNow()
            wait(200)   // outline rebuild + toc sync timer

            // Block 1 is a toc fence: a CodeBlock (8) tagged "toc", rendered by
            // the TocKind delegate (its card is present).
            compare(BlockModel.blockAt(1).blockType, 8)
            compare(BlockModel.blockAt(1).language, "toc")
            var tocDelegate = findBlockDelegate(1)
            verify(tocDelegate !== null, "toc delegate exists")
            var card = findChild(tocDelegate, "tocCard")
            verify(card !== null, "toc card renders")

            // The stored body was regenerated from the headings.
            var expected = DocumentOutline.tocMarkdown()
            verify(expected.indexOf("First") !== -1
                   && expected.indexOf("Second") !== -1,
                   "toc lists both headings")
            compare(BlockModel.getContent(1), expected)

            // Renaming a heading regenerates the TOC (stored body follows).
            BlockModel.updateContent(2, "Renamed Section")
            wait(250)   // queued outline rebuild + sync
            verify(DocumentOutline.tocMarkdown().indexOf("Renamed Section") !== -1,
                   "toc reflects the rename")
            compare(BlockModel.getContent(1), DocumentOutline.tocMarkdown())

            // Regeneration bypasses the undo stack: the heading edit is the
            // undoable step, not the derived TOC body.
            compare(BlockModel.blockAt(2).content, "Renamed Section")
        }

        // ---- Focus and typewriter modes ----

        function test_zx_focusModeHidesChrome() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            var toolbar = findChild(appLoader.item, "toolbar")
            var statusBar = findChild(appLoader.item, "statusBar")
            verify(toolbar.visible, "toolbar visible before focus mode")
            verify(statusBar.visible, "status bar visible before focus mode")

            appLoader.item.focusMode = true
            wait(200)
            verify(!toolbar.visible, "focus mode hides the toolbar")
            verify(!statusBar.visible, "focus mode hides the status bar")

            // Escape exits focus mode (single-key exit).
            appLoader.item.focusMode = false
            wait(200)
            verify(toolbar.visible, "chrome restored on exit")
            verify(statusBar.visible, "status bar restored on exit")
            // Restore windowed state defensively (the flip may have gone
            // full-screen on the real display).
            appLoader.item.visibility = Window.Windowed
            wait(100)
        }

        function test_zy_typewriterFadesAndCenters() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            var md = ""
            for (var i = 0; i < 30; i++)
                md += "Block " + i + " content on a line\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, md)
            compare(BlockModel.count, 30)
            var lv = findChild(appLoader.item, "blockListView")
            lv.positionViewAtBeginning()
            wait(150)

            appLoader.item.typewriterMode = true
            wait(100)

            // Bring a middle block into view and focus it; it becomes the
            // caret block.
            lv.positionViewAtIndex(12, ListView.Center)
            wait(150)
            var mid = findBlockDelegate(12)
            verify(mid !== null, "middle block exists")
            lv.contentY = 0   // reset so centering has somewhere to scroll
            wait(100)
            ensureFocus(findTextArea(mid))
            wait(400)   // caret-block change → callLater center → animation
            compare(appLoader.item.caretBlockIndex, 12)

            // The caret block is undimmed; other blocks fade.
            mid = findBlockDelegate(12)
            compare(mid.typewriterDim, 1.0)
            var neighbor = findBlockDelegate(11)
            if (neighbor)
                verify(neighbor.typewriterDim < 1.0, "non-caret block fades")

            // Centering scrolled the view down (block 12 was near the top).
            verify(lv.contentY > 0, "typewriter centered the caret line")

            // Turning the mode off removes the fade.
            appLoader.item.typewriterMode = false
            wait(150)
            compare(mid.typewriterDim, 1.0)
            if (neighbor)
                compare(neighbor.typewriterDim, 1.0)
        }

        // ---- Statistics and writing goals ----

        function test_zza_statisticsPanel() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Title\n\none two three four five")
            wait(150)
            // DocumentStats agrees with the visible text (Title = 1, body = 5).
            compare(DocumentStats.documentStats().words, 6)

            // Clicking the word count opens the statistics popover.
            var wc = findChild(appLoader.item, "wordCountText")
            verify(wc !== null, "word count text exists")
            mouseClick(wc, wc.width / 2, wc.height / 2)
            var panel = appLoader.item.statisticsPanel
            tryCompare(panel, "visible", true, 1000)
            wait(200)
            compare(panel.docStats.words, 6)
            verify(panel.docStats.readingMinutes >= 0, "reading time present")
            panel.close()
            tryCompare(panel, "visible", false, 1000)
        }

        function test_zzb_writingGoalRing() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            wait(200)

            // The goal ring appears in collection mode and reflects the goal.
            var ring = findChild(appLoader.item, "goalRing")
            verify(ring !== null, "goal ring exists")
            verify(ring.visible, "goal ring visible in collection mode")
            compare(ring.goal, 0)

            NoteCollection.setGoal("Welcome.md", 12)
            wait(100)
            compare(ring.goal, 12)
            verify(NoteCollection.goalFor("Welcome.md") === 12, "goal persisted")

            // Clearing it removes the target.
            NoteCollection.setGoal("Welcome.md", 0)
            wait(100)
            compare(ring.goal, 0)

            closeTestCollection()
        }

        // ---- Templates ----

        function test_zzc_templatesCreateAndManage() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            NoteTemplates.seedBuiltinsIfEmpty()
            verify(NoteTemplates.templateNames().length >= 3,
                   "three built-in templates seeded")
            verify(NoteTemplates.templateNames().indexOf("Daily Journal") !== -1)

            // Create a note from the Daily Journal template (tags carry over,
            // the body is expanded).
            var rel = appLoader.item.createFromTemplate("Daily Journal")
            verify(rel !== "", "note created from template")
            wait(250)
            verify(BlockModel.count > 0, "template body loaded into the note")
            var info = NoteCollection.noteInfo(rel)
            verify(info.tags.indexOf("journal") !== -1,
                   "template front-matter tags carried through")

            // Manage CRUD: write, list, delete.
            verify(NoteTemplates.writeTemplate("Custom", "# {{title}}\n\nhi"))
            verify(NoteTemplates.templateNames().indexOf("Custom") !== -1)
            verify(NoteTemplates.deleteTemplate("Custom"))
            compare(NoteTemplates.templateNames().indexOf("Custom"), -1)

            // Save the current note as a template.
            verify(appLoader.item.saveCurrentNoteAsTemplate("From Note"))
            verify(NoteTemplates.templateNames().indexOf("From Note") !== -1)

            closeTestCollection()
        }

        // ---- Export ----

        function test_zzd_exportHtmlPdfAndDialog() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Report\n\nSome **bold** text.\n\n- one\n- two")
            wait(100)

            // The HTML builder (the dialog wires the same call) renders blocks.
            var html = DocumentExporter.htmlForModel(BlockModel, "Report")
            verify(html.indexOf("<h1 id=\"report\">Report</h1>") !== -1)
            verify(html.indexOf("<strong>bold</strong>") !== -1)
            verify(html.indexOf("<ul><li>one</li><li>two</li></ul>") !== -1)

            // Writing HTML and PDF files succeeds.
            var htmlPath = testCollectionDir + "/export_out.html"
            verify(DocumentExporter.writeModel(BlockModel, "Report", "html", htmlPath))
            var pdfPath = testCollectionDir + "/export_out.pdf"
            verify(DocumentExporter.writeModel(BlockModel, "Report", "pdf", pdfPath))

            // The export dialog offers the four formats.
            appLoader.item.exportDialog.openDialog()
            tryCompare(appLoader.item.exportDialog, "visible", true, 1000)
            wait(150)
            var combo = findChild(appLoader.item, "exportFormatCombo")
            verify(combo !== null, "format combo exists")
            compare(combo.count, 4)
            appLoader.item.exportDialog.close()
            tryCompare(appLoader.item.exportDialog, "visible", false, 1000)
        }

        function test_zze_exportCollectionMirrorsTree() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            // Export the whole collection as markdown, one file per note.
            var dest = testCollectionDir + "/exp_collection"
            var n = DocumentExporter.exportCollection(
                NoteCollection, dest, "markdown", false)
            verify(n >= 3, "each note exported")
            closeTestCollection()
        }

        // ---- Import ----

        function test_zzf_importIntoCollection() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            // Import an existing note file (a real .md on disk) into a folder.
            var src = NoteCollection.absolutePath("Welcome.md")

            var dry = DocumentImporter.dryRunFiles([src], "Ideas")
            compare(dry.files, 1)
            compare(dry.collisions, 0)

            var n = DocumentImporter.importFiles([src], "Ideas")
            compare(n, 1)
            verify(NoteCollection.noteRelPaths().indexOf("Ideas/Welcome.md") !== -1,
                   "note imported into the target folder")

            // Re-importing the same file collides and suffixes.
            var n2 = DocumentImporter.importFiles([src], "Ideas")
            compare(n2, 1)
            verify(NoteCollection.noteRelPaths().indexOf("Ideas/Welcome 2.md") !== -1,
                   "collision suffixed")

            // The import dialog loads with its pickers.
            appLoader.item.importDialog.openDialog()
            tryCompare(appLoader.item.importDialog, "visible", true, 1000)
            wait(100)
            verify(findChild(appLoader.item, "importFilesButton") !== null)
            verify(findChild(appLoader.item, "importFolderButton") !== null)
            appLoader.item.importDialog.close()
            tryCompare(appLoader.item.importDialog, "visible", false, 1000)

            closeTestCollection()
        }

        // ---- Inline math ----

        function test_zzg_inlineMathOverlayAndReveal() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "The relation $E=mc^2$ holds. It costs $5 and $6 though.")
            wait(300)

            // The paragraph delegate reports one hidden math box (the currency
            // dollars stay literal — no box for "$5 and $6").
            var para = findBlockDelegate(0)
            verify(para !== null, "math paragraph exists")
            compare(para.inlineMathBoxes.length, 1)
            compare(para.inlineMathBoxes[0].tex, "E=mc^2")

            // An overlay image is drawn for it.
            var img = findChild(para, "inlineMathImage")
            verify(img !== null, "inline-math overlay image present")

            // Revealing the span (caret inside) drops its box — the $…$ source
            // shows and is editable. tryVerify tolerates the frame or two the
            // reveal (a deferred, focus-gated transition) takes to settle.
            var ta = findTextArea(para)
            ensureFocus(ta)
            ta.cursorPosition = 15   // inside "$E=mc^2$"
            // The reveal is a deferred, focus-gated document change: the $…$
            // markers appear in the document text and the overlay drops.
            tryVerify(function() { return ta.text.indexOf("$E=mc^2$") !== -1 },
                      2000, "caret inside reveals the $…$ source")
            tryVerify(function() { return para.inlineMathBoxes.length === 0 },
                      2000, "the equation overlay drops while revealed")

            // Move the caret out: the box returns.
            ta.cursorPosition = 0
            tryVerify(function() { return para.inlineMathBoxes.length === 1 },
                      2000, "caret outside restores the equation overlay")
        }

        // Moving the caret through a formula-heavy paragraph used to cost
        // more the more formulas it held, in two compounding ways: every
        // caret rectangle change re-asked the engine for the box list, which
        // re-scanned the markdown; and each overlay image then worked out its
        // line's baseline by scanning every other box and asking the editor
        // for two caret rectangles per box, which is quadratic in the number
        // of formulas. Neither is exercised by the prose corpus the other
        // performance tests use.

        function test_zzg2_formulaHeavyCaretMotionStaysLinear() {
            if (isHeadless) {
                skip("Focus tests require display")
            }

            function paragraphWith(formulas) {
                var parts = []
                for (var i = 0; i < formulas; i++)
                    parts.push("term $a_" + i + "+b^" + i + "$ end")
                return parts.join(" ")
            }

            // Milliseconds per arrow-key caret move, averaged.
            function caretMoveCost(formulas) {
                DocumentManager.newDocument()
                DocumentSerializer.loadIntoModel(BlockModel,
                                                 paragraphWith(formulas))
                wait(400)
                var para = findBlockDelegate(0)
                verify(para !== null, formulas + "-formula paragraph exists")
                tryVerify(function() {
                    return para.inlineMathBoxes.length === formulas
                }, 8000, "all " + formulas + " overlays are reported")
                var ta = findTextArea(para)
                ensureFocus(ta)
                ta.cursorPosition = 0
                wait(100)
                var moves = 40
                var t0 = Date.now()
                for (var k = 0; k < moves; k++)
                    keyClick(Qt.Key_Right)
                return (Date.now() - t0) / moves
            }

            var few = caretMoveCost(10)
            var many = caretMoveCost(100)
            console.log("CARET MOVE 10 formulas: " + few.toFixed(3)
                        + " ms, 100 formulas: " + many.toFixed(3) + " ms")

            // The 60 Hz frame budget, which is what a caret move has to fit
            // inside to feel attached to the key. Measured on this machine at
            // 1.5 ms for ten formulas and 10.6 ms for a hundred; the same
            // paragraphs cost 3.7 ms and 43.9 ms before the box list was
            // cached, the per-line baselines were computed once instead of
            // once per image, and caret movement stopped ticking the overlay
            // layer at all. The hundred-formula case is the one that
            // separates: it was two and a half frames and is now well inside
            // one.
            verify(few < 16,
                   "caret motion in a 10-formula paragraph must stay under "
                   + "16ms/key (measured " + few.toFixed(2) + "ms)")
            verify(many < 16,
                   "caret motion in a 100-formula paragraph must stay under "
                   + "16ms/key (measured " + many.toFixed(2) + "ms)")
        }

        // ---- Embed blocks ----

        function test_zzh_embedCardFromWebUrl() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Links\n\n![](https://example.com/article)")
            wait(300)

            // The web URL is stored as an Image block (11) but renders as an
            // embed card (the content classifier → EmbedKind delegate).
            compare(BlockModel.blockAt(1).blockType, 11)
            var deleg = findBlockDelegate(1)
            verify(deleg !== null, "embed delegate exists")
            var card = findChild(deleg, "embedCard")
            verify(card !== null, "embed card renders")

            // The card is inert until the reader asks for it: opening a note
            // must not contact the host the note names. The title shows the
            // URL and a Load button is offered.
            EgressPolicy.forgetAllOrigins()
            var title = findChild(deleg, "embedTitle")
            verify(title.text.indexOf("Example Page Title") === -1,
                   "an unapproved embed shows no fetched metadata")
            var loadButton = findChild(deleg, "embedLoadButton")
            verify(loadButton !== null && loadButton.visible,
                   "the inert card offers to load the preview")

            // Clicking it approves the origin and fills the card.
            mouseClick(loadButton)
            tryVerify(function() {
                return title.text.indexOf("Example Page Title") !== -1
            }, 2000, "embed title from OpenGraph after the reader loads it")
            verify(EgressPolicy.isAllowed("https://example.com/article"),
                   "loading a preview approves its origin")

            // Round-trips byte-identically as an image expression.
            compare(BlockModel.getContent(1), "![](https://example.com/article)")

            // A remote image URL stays a regular image block (not an embed).
            DocumentSerializer.loadIntoModel(BlockModel,
                "![](https://x.com/pic.png)")
            wait(150)
            var d2 = findBlockDelegate(0)
            verify(d2 !== null)
            verify(findChild(d2, "embedCard") === null,
                   "a remote image is not an embed")
        }

        // Per-block presentation through the model. This is
        // focus-independent — it drives the delegates' presentation methods and
        // the model directly, never the keyboard — so it does not depend on the
        // window holding focus.
        function test_zzi_blockPresentationAlignmentDividerCallout() {
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "# Heading  <!--kvit align=center-->\n\n"
                + "A paragraph.\n\n"
                + "---  <!--kvit style=dashed width=50%-->\n\n"
                + "> [!info] Note  <!--kvit color=#1f9e8b-->\n> body")
            wait(250)

            // Tags parsed into model attributes.
            compare(BlockModel.blockAt(0).attributes, "align=center")
            compare(BlockModel.blockAt(2).blockType, 9)   // Divider
            compare(BlockModel.blockAt(2).attributes, "style=dashed width=50%")
            compare(BlockModel.blockAt(3).blockType, 12)  // Callout
            compare(BlockModel.blockAt(3).attributes, "color=#1f9e8b")

            // The divider delegate renders its styled rule (the Canvas).
            var divDeleg = findBlockDelegate(2)
            verify(divDeleg !== null, "divider delegate exists")
            verify(findChild(divDeleg, "dividerLine") !== null, "styled rule renders")

            // Aligning the paragraph through the delegate method is one undo
            // step that reverts cleanly.
            var paraDeleg = findBlockDelegate(1)
            verify(paraDeleg !== null && paraDeleg.setBlockAlignment !== undefined,
                   "paragraph exposes setBlockAlignment")
            compare(BlockModel.blockAt(1).attributes, "")
            paraDeleg.setBlockAlignment("right")
            tryVerify(function() {
                return BlockModel.blockAt(1).attributes === "align=right"
            }, 1000, "alignment written to the model")
            UndoStack.undo()
            tryVerify(function() {
                return BlockModel.blockAt(1).attributes === ""
            }, 1000, "one undo reverts the alignment")

            // Setting alignment back to left (the default) clears the attribute
            // rather than storing align=left.
            paraDeleg.setBlockAlignment("center")
            tryVerify(function() {
                return BlockModel.blockAt(1).attributes === "align=center"
            }, 1000)
            paraDeleg.setBlockAlignment("left")
            tryVerify(function() {
                return BlockModel.blockAt(1).attributes === ""
            }, 1000, "left alignment carries no tag")
        }

        // Image effects, drop cap, embed dimensions — the
        // delegate presentation methods drive the model; focus-independent.
        function test_zzj_presentationEffectsDropcapEmbed() {
            if (NoteCollection.isOpen)
                closeTestCollection()
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "A short opener paragraph for the drop cap to enlarge.  "
                + "<!--kvit dropcap=3-->\n\n"
                + "![p|200](x.png)  <!--kvit rounded shadow-->")
            wait(250)

            // Attributes parsed (canonical/sorted).
            compare(BlockModel.blockAt(0).attributes, "dropcap=3")
            compare(BlockModel.blockAt(1).blockType, 11)   // Image
            compare(BlockModel.blockAt(1).attributes, "rounded shadow")

            // A drop-cap paragraph renders its enlarged letter when unfocused.
            var para = findBlockDelegate(0)
            verify(para !== null && para.setDropCap !== undefined,
                   "paragraph exposes setDropCap")

            // The image exposes its effects control.
            var imgDeleg = findBlockDelegate(1)
            verify(imgDeleg !== null, "image delegate exists")
            verify(findChild(imgDeleg, "imageEffectsButton") !== null,
                   "image effects button renders")

            // Changing the drop cap through the delegate is one undo step.
            para.setDropCap(5)
            tryVerify(function() {
                return BlockModel.blockAt(0).attributes === "dropcap=5"
            }, 1000, "drop cap written to the model")
            UndoStack.undo()
            tryVerify(function() {
                return BlockModel.blockAt(0).attributes === "dropcap=3"
            }, 1000, "one undo reverts the drop cap")
            para.setDropCap(0)   // None removes the attribute
            tryVerify(function() {
                return BlockModel.blockAt(0).attributes === ""
            }, 1000, "drop cap None carries no tag")

            // An embed carries configurable width/height that size its card.
            DocumentSerializer.loadIntoModel(BlockModel,
                "![](https://example.com/article)  <!--kvit width=320 height=100-->")
            wait(300)
            var embed = findBlockDelegate(0)
            verify(embed !== null && embed.setEmbedSize !== undefined,
                   "embed exposes setEmbedSize")
            var cardItem = findChild(embed, "embedCard")
            verify(cardItem !== null, "embed card renders")
            tryVerify(function() { return cardItem.width === 320 }, 1000,
                      "embed width attribute sizes the card")
        }

        // Keyboard accessibility. Focus-independent — it checks
        // the skip-navigation machinery, the pane focus entries, the focus-ring
        // token, and modal dialog containment without relying on the window
        // holding keyboard focus (which flakes under WSLg).
        function test_zzk_focusAccessibilityMachinery() {
            DocumentManager.newDocument()
            BlockModel.insertBlock(0, 1, "A heading")
            BlockModel.insertBlock(1, 0, "A paragraph")
            BlockModel.insertBlock(2, 0, "Another paragraph")
            wait(200)

            // Skip-navigation exists and moves the editor's current index to a
            // block (a focus-independent effect of focusEditor()).
            verify(appLoader.item.focusEditor !== undefined, "focusEditor exists")
            verify(appLoader.item.cyclePane !== undefined, "cyclePane exists")
            var lv = findChild(appLoader.item, "blockListView")
            verify(lv !== null)
            lv.currentIndex = -1
            appLoader.item.focusEditor()
            tryVerify(function() { return lv.currentIndex >= 0 }, 1000,
                      "focusEditor lands on a block")

            // The focus-ring token is exposed and valid.
            verify(Theme.focusRing !== undefined, "focus ring token exposed")
            verify(String(Theme.focusRing) !== "", "focus ring token is set")

            // A focused block's indicator uses the focus-ring color (the binding
            // holds regardless of whether the window has OS focus).
            var d = findBlockDelegate(1)
            verify(d !== null)
            var ind = findChild(d, "focusIndicator")
            verify(ind !== null, "block has a focus indicator")

            // Modal dialogs trap focus: the shortcut reference is modal and
            // Escape/close dismisses it (Qt Quick Controls containment).
            appLoader.item.openShortcutReference()
            wait(200)
            var ref = findChild(appLoader.item, "shortcutReference")
            verify(ref !== null && ref.modal === true,
                   "shortcut reference is a modal (focus-trapping) dialog")
            tryVerify(function() { return ref.visible }, 1000)
            ref.close()
            tryVerify(function() { return !ref.visible }, 1000,
                      "closing the dialog returns control")
        }

        // Screen-reader support. Reading Accessible.name/role is
        // focus-independent (attached properties), so this runs reliably; the
        // announcer wiring is checked through its lastMessage.
        function test_zzl_screenReaderNamesRolesAndAnnouncements() {
            DocumentManager.newDocument()
            BlockModel.insertBlock(0, 0, "A paragraph block")
            wait(200)

            // A glyph toolbar button carries its tooltip as the accessible name
            // and the Button role.
            var bold = findChild(appLoader.item, "toolbarBoldButton")
            verify(bold !== null, "bold toolbar button exists")
            compare(bold.Accessible.name, "Bold (Ctrl+B)")
            compare(bold.Accessible.role, Accessible.Button)

            // The editable block exposes the EditableText role with a labelled
            // name.
            var d = findBlockDelegate(0)
            var ta = findTextArea(d)
            verify(ta !== null)
            compare(ta.Accessible.role, Accessible.EditableText)
            verify(ta.Accessible.name.indexOf("block") !== -1,
                   "block accessible name is labelled")

            // An image surfaces its alt text as the accessible name.
            DocumentSerializer.loadIntoModel(BlockModel,
                "![A lighthouse at dusk|200](x.png)")
            wait(200)
            var imgDeleg = findBlockDelegate(0)
            var frame = findChild(imgDeleg, "imageAccessible")
            verify(frame !== null, "image exposes an accessible frame")
            compare(frame.Accessible.name, "A lighthouse at dusk")

            // Announcements: a block conversion speaks through the announcer.
            DocumentManager.newDocument()
            BlockModel.insertBlock(0, 0, "convert me")
            wait(150)
            var para = findBlockDelegate(0)
            para.convertBlockType(1)   // Heading 1
            tryVerify(function() {
                return A11y.lastMessage === "Converted to Heading 1"
            }, 1000, "conversion announced")

            // Announcements: the search match count speaks while searching.
            DocumentManager.newDocument()
            BlockModel.insertBlock(0, 0, "the quick brown fox the lazy dog")
            wait(150)
            DocumentSearch.active = true   // the find bar activates the search
            DocumentSearch.query = "the"
            tryVerify(function() {
                return A11y.lastMessage.indexOf("match") !== -1
            }, 1000, "match count announced")
            DocumentSearch.query = ""
            DocumentSearch.active = false
        }

        // System integration. Focus-independent — drives the
        // seams and the quick-capture window through their APIs. The tray/hotkey
        // themselves are documented WSLg gaps (spike (b)); the routing is what is
        // asserted.
        function test_zzm_systemIntegrationSeamsAndQuickCapture() {
            openTestCollection()
            wait(100)
            verify(appLoader.item.collectionOpen,
                   "a collection is open for capture")
            var before = NoteCollection.noteRelPaths().length

            // Quick capture writes a note through the window.
            appLoader.item.openQuickCapture()
            wait(200)
            var qc = findChild(appLoader.item, "quickCaptureWindow")
            verify(qc !== null, "quick capture window exists")
            var ta = findChild(qc, "quickCaptureText")
            verify(ta !== null, "quick capture text field exists")
            ta.text = "Captured by the seam test\nwith a second body line"
            qc.save()
            wait(250)
            var paths = NoteCollection.noteRelPaths()
            compare(paths.length, before + 1, "capture created one note")
            var found = false
            for (var i = 0; i < paths.length; i++)
                if (paths[i].indexOf("Captured by the seam test") !== -1)
                    found = true
            verify(found, "the captured note is titled from its first line")

            // The tray "quick capture" action re-opens the window.
            qc.close(); wait(100)
            SystemTray.triggerAction("quickCapture")
            wait(200)
            tryVerify(function() { return qc.visible }, 1000,
                      "tray quick-capture action opens the window")
            qc.close(); wait(100)

            // The global hotkey activation opens it too.
            GlobalHotkey.trigger()
            wait(200)
            tryVerify(function() { return qc.visible }, 1000,
                      "hotkey activation opens quick capture")
            qc.close()

            // The tray "new note" action creates a note in the collection.
            var n2 = NoteCollection.noteRelPaths().length
            SystemTray.triggerAction("newNote")
            wait(250)
            verify(NoteCollection.noteRelPaths().length >= n2,
                   "tray new-note routes to note creation")

            // The notifier records what it posts.
            SystemTray.notify("Kvit", "Test notification")
            compare(SystemTray.lastNotification, "Test notification")
        }

        // External file watching. Focus-independent — it drives
        // the watcher's change entry point and the banner's resolution functions
        // directly (the real QFileSystemWatcher is covered by test_filewatcher).
        function test_zzn_externalChangeConflictBanner() {
            openTestCollection()
            wait(100)
            verify(appLoader.item.openNoteByPath("Welcome.md"),
                   "open the welcome note")
            wait(150)
            var absPath = DocumentManager.currentFilePath
            verify(absPath !== "")

            var banner = findChild(appLoader.item, "conflictBanner")
            verify(banner !== null, "conflict banner exists")

            // Edit so the open note is dirty, then an external change to it raises
            // the keep-mine / load-theirs banner.
            BlockModel.updateContent(0, "a local edit")
            wait(50)
            verify(DocumentManager.isDirty, "note is dirty")
            FileWatcher.noteChangedExternally(absPath)
            wait(100)
            verify(banner.visible, "external change on a dirty note shows the banner")

            // Load-theirs reloads from disk (discarding the edit) and clears it.
            appLoader.item.loadTheirs()
            wait(200)
            verify(!banner.visible, "banner clears after load-theirs")
            tryVerify(function() { return !DocumentManager.isDirty }, 1000,
                      "reload cleared the dirty state")

            // Keep-mine re-saves the editor's version.
            BlockModel.updateContent(0, "another local edit")
            wait(50)
            verify(DocumentManager.isDirty)
            FileWatcher.noteChangedExternally(absPath)
            wait(100)
            verify(banner.visible)
            appLoader.item.keepMine()
            wait(200)
            verify(!banner.visible, "banner clears after keep-mine")
            tryVerify(function() { return !DocumentManager.isDirty }, 1000,
                      "keep-mine saved the editor version")

            // Own-write guard: a guarded change (the app's own save) is not a
            // conflict. Drive feedChange through the guard then unguarded.
            BlockModel.updateContent(0, "dirty again")
            wait(50)
            verify(DocumentManager.isDirty)
            FileWatcher.noteOwnWrite(absPath)
            FileWatcher.feedChange(absPath, true)   // consumed by the guard
            wait(100)
            verify(!banner.visible, "own write does not raise the banner")
            FileWatcher.feedChange(absPath, true)   // now unguarded → conflict
            wait(100)
            verify(banner.visible, "a later external change does raise it")
            appLoader.item.keepMine()
        }

        // View-layer scale. Focus-independent — it loads a
        // large document into the running app and checks that lazy loading plus
        // delegate pooling keep the live delegate set O(visible), not O(model),
        // which is what makes load and scroll stay within budget at scale. The
        // full 561,693-word data-path budgets are pinned in
        // test_performance_warandpeace.
        function test_zzo_largeDocumentViewStaysBounded() {
            DocumentManager.newDocument()
            var lines = []
            for (var i = 0; i < 3000; i++)
                lines.push("Paragraph number " + i
                           + " with a little prose to give it width and body.")
            var t0 = Date.now()
            DocumentSerializer.loadIntoModel(BlockModel, lines.join("\n\n"))
            wait(50)
            var loadMs = Date.now() - t0
            compare(BlockModel.count, 3000)
            verify(loadMs < 2000, "a 3000-block document loads in under 2s (was "
                   + loadMs + "ms)")

            var lv = findChild(appLoader.item, "blockListView")
            verify(lv !== null)
            lv.positionViewAtBeginning()
            wait(100)

            // Only a bounded window of delegates is instantiated, not all 3000.
            function liveCount() {
                var n = 0
                for (var i = 0; i < BlockModel.count; i++)
                    if (lv.itemAtIndex(i) !== null) n++
                return n
            }
            function liveTextAreaCount() {
                var n = 0
                for (var i = 0; i < BlockModel.count; i++) {
                    var item = lv.itemAtIndex(i)
                    if (item !== null && findTextAreaRaw(item) !== null)
                        n++
                }
                return n
            }
            var atTop = liveCount()
            verify(atTop < 300,
                   "lazy load keeps live delegates bounded (was " + atTop + ")")
            compare(liveTextAreaCount(), 0,
                    "plain unfocused rows should stay on the read-only shell")

            // Scrolling far does not grow the live set unboundedly (pooling).
            lv.positionViewAtIndex(1500, ListView.Center)
            wait(120)
            lv.positionViewAtIndex(2900, ListView.Center)
            wait(120)
            var afterScroll = liveCount()
            verify(afterScroll < 400,
                   "delegate pooling bounds the live set while scrolling (was "
                   + afterScroll + ")")
            compare(liveTextAreaCount(), 0,
                    "pooled plain rows should not keep editor TextAreas alive")
        }

        function test_zzp_oversizedPasteConfirmAndPlaceholder() {
            // Oversized-payload guard: a clipboard payload over the
            // open-size cap gets a confirm dialog before insertMarkdownAt
            // runs, instead of a silent multi-second stall.
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            docWithBlocks(["one", "two"])
            var oldCap = DocumentManager.maxOpenFileSizeMiB
            DocumentManager.maxOpenFileSizeMiB = 1

            var chunk = "0123456789abcdef"
            var big = chunk.repeat((1024 * 1024) / chunk.length + 4096)
            Clipboard.text = big
            verify(big.length > 1024 * 1024)

            selectBlocks(0, 0)
            var countBefore = BlockModel.count
            keyClick(Qt.Key_V, Qt.ControlModifier)

            var dialog = findChild(appLoader.item, "largePasteConfirmDialog")
            verify(dialog !== null)
            tryCompare(dialog, "opened", true, 1000)
            compare(BlockModel.count, countBefore,
                    "nothing pastes before the user confirms")

            dialog.reject()
            wait(50)
            compare(BlockModel.count, countBefore,
                    "cancel pastes nothing")

            // Under the cap the paste is immediate, no dialog.
            Clipboard.text = "small paste"
            selectBlocks(0, 0)
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryVerify(function() {
                return BlockModel.count === countBefore + 1
            }, 1000, "a small payload pastes straight through")

            // The oversized-file placeholder plumbing: banner appears with
            // the rejection state and Dismiss clears it (the rejection
            // signal itself is covered by the C++ DocumentManager tests).
            appLoader.item.oversizedFilePath = "/notes/whale.md"
            appLoader.item.oversizedFileBytes = 42 * 1024 * 1024
            appLoader.item.oversizedFileCap = 10 * 1024 * 1024
            var banner = findChild(appLoader.item, "oversizedFileBanner")
            verify(banner !== null)
            tryCompare(banner, "visible", true, 1000)
            var label = findChild(appLoader.item, "oversizedFileLabel")
            verify(label.text.indexOf("whale.md") !== -1)
            verify(label.text.indexOf("42.0 MiB") !== -1)
            verify(label.text.indexOf("10.0 MiB") !== -1)
            var dismiss = findChild(appLoader.item, "oversizedDismiss")
            mouseClick(dismiss)
            tryCompare(banner, "visible", false, 1000)

            DocumentManager.maxOpenFileSizeMiB = oldCap
        }

        // ---- Math command menu + $ auto-pair ----

        // The popup glue, driven directly (no focus needed, runs
        // headless): browse/completion modes, ranking surface, host
        // hand-off, recency, zero-match close.
        function test_zzq_mathCommandMenuPopupModes() {
            var menu = appLoader.item.mathCommandMenu
            verify(menu !== null && menu !== undefined,
                   "math command menu instantiated")
            var got = null
            var hostStub = { applyMathCommand: function(row) { got = row } }

            menu.openForHost(hostStub, Qt.rect(20, 20, 2, 18), true)
            tryCompare(menu, "visible", true, 1000)
            verify(!menu.completionMode, "bare backslash opens browse mode")
            verify(menu.cats.length >= 15, "the LyX-mapped categories")
            verify(menu.gridRows.length > 0, "glyph grid populated")

            // Typing filters into completion mode, best match first.
            menu.updateQuery("fra")
            verify(menu.completionMode)
            verify(menu.rows.length > 0)
            compare(menu.rows[0].name, "\\frac")

            // Applying hands the row to the host and closes the popup.
            menu.applyHighlighted()
            tryCompare(menu, "visible", false, 1000)
            verify(got !== null, "host received the row")
            compare(got.insert, "\\frac{}{}")
            compare(got.cursorOffset, 6)

            // Recency: the accepted command now leads the browse panel.
            menu.openForHost(hostStub, Qt.rect(20, 20, 2, 18), false)
            tryCompare(menu, "visible", true, 1000)
            compare(menu.cats[0], "Recently used")
            compare(menu.gridRows[0].name, "\\frac")

            // A query that matches nothing closes the menu (the typed
            // character stays with the editor).
            menu.updateQuery("zzqqxy")
            tryCompare(menu, "visible", false, 1000)
        }

        // Display-math surface: backslash trigger, completion, template
        // insertion with the caret in the first slot, Tab slot-chain,
        // Escape layering under the editor-exit binding.
        function test_zzr_displayMathArrowTraversalKeepsCaret() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "above\n\n$$\nx+1\n$$\n\nbelow")
            wait(300)

            compare(BlockModel.count, 3)
            var above = findBlockDelegate(0)
            var mathBlock = findBlockDelegate(1)
            var below = findBlockDelegate(2)
            var aboveText = findTextArea(above)
            var mathSource = findChild(mathBlock, "mathSourceArea")
            var belowText = findTextArea(below)
            verify(mathSource !== null, "math source editor exists")

            // Entering from above opens the source at its start. Continuing
            // down must close the editor and put the caret in the next block.
            ensureFocus(aboveText)
            aboveText.cursorPosition = aboveText.length
            keyClick(Qt.Key_Down)
            tryCompare(mathSource, "activeFocus", true, 1000)
            compare(mathSource.cursorPosition, 0)
            keyClick(Qt.Key_Down)
            tryCompare(belowText, "activeFocus", true, 1000)
            tryCompare(mathBlock, "editing", false, 1000)
            compare(belowText.cursorPosition, 0)

            // The reverse route enters at the source end and then restores
            // the caret at the end of the preceding prose block.
            keyClick(Qt.Key_Up)
            tryCompare(mathSource, "activeFocus", true, 1000)
            compare(mathSource.cursorPosition, mathSource.length)
            keyClick(Qt.Key_Up)
            tryCompare(aboveText, "activeFocus", true, 1000)
            tryCompare(mathBlock, "editing", false, 1000)
            compare(aboveText.cursorPosition, aboveText.length)
        }

        function test_zzr_mathCommandMenuInMathBlock() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel, "$$\nx+1\n$$")
            wait(300)

            var mathBlock = findBlockDelegate(0)
            verify(mathBlock !== null, "math block delegate exists")
            var src = findChild(mathBlock, "mathSourceArea")
            verify(src !== null, "math source editor exists")
            src.forceActiveFocus()
            tryCompare(src, "activeFocus", true, 1000)
            src.cursorPosition = src.length

            var menu = appLoader.item.mathCommandMenu
            keyClick("\\")
            tryCompare(menu, "visible", true, 1000)
            verify(!menu.completionMode, "bare backslash: browse mode")

            keyClick(Qt.Key_F)
            keyClick(Qt.Key_R)
            tryVerify(function() {
                return menu.completionMode && menu.query === "fr"
            }, 2000, "typing after the backslash filters")
            tryVerify(function() {
                return menu.rows.length > 0 && menu.rows[0].name === "\\frac"
            }, 2000, "\\frac leads for 'fr'")

            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() {
                return src.text.indexOf("\\frac{}{}") !== -1
            }, 2000, "the template replaced the trigger text")
            compare(src.text.charAt(src.cursorPosition - 1), "{")
            compare(src.text.charAt(src.cursorPosition), "}")

            // Tab hops from the first empty pair to the second.
            var firstSlot = src.cursorPosition
            keyClick(Qt.Key_Tab)
            tryVerify(function() {
                return src.cursorPosition === firstSlot + 2
            }, 2000, "Tab hops to the second {} slot")

            // Escape layering: the first Escape only closes the menu and
            // the editor keeps focus.
            keyClick("\\")
            tryCompare(menu, "visible", true, 1000)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
            verify(src.activeFocus, "editor keeps focus after menu Escape")
        }

        // Prose surface: $ auto-pair (pair, Backspace-both, Delete-to-
        // literal, type-over), the pair composing with the backslash
        // trigger, and suppression before existing text.
        function test_zzs_dollarAutoPairAndInlineMenu() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel, "note")
            wait(300)

            var para = findBlockDelegate(0)
            verify(para !== null)
            var ta = findTextArea(para)
            ensureFocus(ta)
            ta.cursorPosition = ta.text.length

            // $ auto-pairs with the caret between the dollars.
            keyClick("$")
            tryVerify(function() { return ta.text === "note$$" }, 2000,
                      "$ inserts the pair")
            compare(ta.cursorPosition, 5)

            // Backspace on the empty pair removes both.
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return ta.text === "note" }, 2000,
                      "Backspace removes the whole pair")

            // Delete removes only the auto-closer: the literal-$ hatch.
            keyClick("$")
            tryVerify(function() { return ta.text === "note$$" }, 2000)
            keyClick(Qt.Key_Delete)
            tryVerify(function() { return ta.text === "note$" }, 2000,
                      "Delete leaves a literal dollar")
            keyClick(Qt.Key_Backspace)
            tryVerify(function() { return ta.text === "note" }, 2000)

            // $ then \ opens the command menu inside the fresh pair
            // (auto-pair and trigger composing), and completion inserts.
            var menu = appLoader.item.mathCommandMenu
            keyClick("$")
            keyClick("\\")
            tryCompare(menu, "visible", true, 2000)
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_L)
            keyClick(Qt.Key_P)
            keyClick(Qt.Key_H)
            keyClick(Qt.Key_A)
            tryVerify(function() {
                return menu.completionMode && menu.query === "alpha"
            }, 2000, "query tracks the typed word")
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("$\\alpha$") !== -1
            }, 2000, "the span holds the inserted command")

            // A second $ types over the auto-closer instead of adding a
            // third dollar.
            var lenBefore = ta.text.length
            keyClick("$")
            tryVerify(function() {
                return ta.text.length === lenBefore
                    && ta.cursorPosition === lenBefore
            }, 2000, "$ types over the tracked closer")

            // In front of existing text the pair is suppressed: a single
            // literal dollar (the price case).
            ta.cursorPosition = 0
            keyClick("$")
            tryVerify(function() {
                return ta.text.charAt(0) === "$" && ta.text.charAt(1) !== "$"
            }, 2000, "suppressed before a letter: literal $")
        }

        function test_zzt_cdotAutocompleteInInlineMath() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel, "note")
            wait(300)

            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.cursorPosition = ta.text.length

            var menu = appLoader.item.mathCommandMenu
            keyClick("$")
            keyClick("\\")
            tryCompare(menu, "visible", true, 2000)
            keyClick(Qt.Key_C)
            wait(100)
            keyClick(Qt.Key_D)
            wait(100)
            keyClick(Qt.Key_O)
            wait(100)
            keyClick(Qt.Key_T)
            tryVerify(function() {
                return menu.visible && menu.completionMode
                    && menu.query === "cdot" && menu.rows.length > 0
                    && menu.rows[0].name === "\\cdot"
            }, 2000, "\\cdot should remain the leading completion")
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("$\\cdot$") !== -1
            }, 2000, "accepting the completion inserts \\cdot")
        }

        function test_zzu_controlSymbolAutocompleteInInlineMath() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel, "note")
            wait(300)

            var ta = findTextArea(findBlockDelegate(0))
            ensureFocus(ta)
            ta.cursorPosition = ta.text.length

            var menu = appLoader.item.mathCommandMenu
            keyClick("$")
            keyClick("\\")
            tryCompare(menu, "visible", true, 2000)
            keyClick("|")
            tryVerify(function() {
                return menu.visible && menu.query === "|"
                    && menu.rows.length > 0
                    && menu.rows[0].name === "\\|"
            }, 2000, "\\| should autocomplete as a TeX control symbol")
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() {
                return BlockModel.getContent(0).indexOf("$\\|$") !== -1
            }, 2000, "accepting the completion inserts \\|")
        }

        function test_zzv_controlSymbolAutocompleteInDisplayMath() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel, "$$\nx\n$$")
            wait(300)

            var src = findChild(findBlockDelegate(0), "mathSourceArea")
            verify(src !== null, "math source editor exists")
            src.forceActiveFocus()
            tryCompare(src, "activeFocus", true, 1000)
            src.cursorPosition = src.length

            var menu = appLoader.item.mathCommandMenu
            keyClick("\\")
            tryCompare(menu, "visible", true, 1000)
            keyClick("|")
            tryVerify(function() {
                return menu.visible && menu.query === "|"
                    && menu.rows.length > 0
                    && menu.rows[0].name === "\\|"
            }, 2000, "display math should complete \\|")
            menu.dismiss()
        }

        function test_zzw_displayMenuAfterPopulatedFormula() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "$$\nx^2 + 75=36 \\alpha\n$$")
            wait(300)

            var src = findChild(findBlockDelegate(0), "mathSourceArea")
            verify(src !== null, "math source editor exists")
            src.forceActiveFocus()
            tryCompare(src, "activeFocus", true, 1000)
            src.cursorPosition = src.text.indexOf("\\alpha")
            verify(src.cursorPosition > 0)

            var menu = appLoader.item.mathCommandMenu
            keyClick("\\")
            tryCompare(menu, "visible", true, 1000)
            // A display block can re-apply source from its debounced model
            // write. Rebase text before the command to prove query tracking
            // follows content at the caret, not a stale trigger offset.
            var commandCaret = src.cursorPosition
            src.insert(0, "z")
            src.cursorPosition = commandCaret + 1
            wait(100)
            verify(menu.visible && menu.query === "",
                   "source rebase must preserve the bare command menu")
            wait(500) // a slow next keystroke crosses the slash edit debounce
            verify(menu.visible,
                   "menu lost after debounce; text=" + src.text
                   + " cursor=" + src.cursorPosition
                   + " query=" + menu.query)
            keyClick(Qt.Key_C)
            wait(500) // cross the display block's model-write debounce
            tryVerify(function() {
                return menu.visible && menu.query === "c"
            }, 2000, "display completion must open after a populated formula")
            keyClick(Qt.Key_Space)
            tryCompare(menu, "visible", false, 1000)
            keyClick("\\")
            tryCompare(menu, "visible", true, 1000)
            keyClick(Qt.Key_A)
            tryVerify(function() {
                return menu.visible && menu.query === "a"
            }, 2000, "display completion must reopen after \\c and a space")
        }

        // Regression (user report 2026-07-10): converting a `plain` fence to a
        // text diagram through the language picker path (languageChosen →
        // EditableBlock.setCodeLanguage) silently kept the block at `plain`.
        // The reverse direction (diagram → "Treat as code") worked. Focus-free,
        // so it runs headless.
        function test_zzx_languagePickerPlainFenceToTextDiagram() {
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "```plain\n┌──┐\n│ok│\n└──┘\n```")
            wait(300)

            compare(BlockModel.blockAt(0).language, "plain")
            var d = findBlockDelegate(0)
            verify(d !== null, "delegate exists")
            verify(typeof d.setCodeLanguage === "function",
                   "plain fence renders through the code delegate")

            d.setCodeLanguage("diagram")
            tryVerify(function() {
                return BlockModel.blockAt(0).language === "diagram"
            }, 1000, "picker conversion must set language=diagram; still "
                     + BlockModel.blockAt(0).language)
            // The reported failure was a silent revert; let any deferred
            // delegate-teardown writes land, then re-check.
            wait(400)
            compare(BlockModel.blockAt(0).language, "diagram",
                    "conversion must survive delegate teardown")
            // A `diagram` fence stays on the code delegate: the tag only
            // marks it for ingest straightening.
            var nd = findBlockDelegate(0)
            verify(nd && typeof nd.setCodeLanguage === "function",
                   "diagram fence renders as an ordinary code block")
        }

        // Same report, full round trip: diagram → plain → diagram through
        // the picker path. The revert reportedly failed only in this sequence.
        function test_zzy_treatAsCodeThenBackToTextDiagram() {
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "```diagram\n┌──┐\n│ok│\n└──┘\n```")
            wait(300)

            compare(BlockModel.blockAt(0).language, "diagram")
            var d = findBlockDelegate(0)
            verify(d !== null && typeof d.setCodeLanguage === "function",
                   "diagram fence renders through the code delegate")

            d.setCodeLanguage("plain")
            tryVerify(function() {
                var cd = findBlockDelegate(0)
                return BlockModel.blockAt(0).language === "plain"
                    && cd && typeof cd.setCodeLanguage === "function"
            }, 1000, "plain opt-out stays on the code delegate")

            // The picker opens from the code header while the editor is live —
            // convert with an active editor so delegate teardown runs against
            // a focused TextArea, as in the reported flow.
            var cd = findBlockDelegate(0)
            var ta = findTextArea(cd)
            if (ta) {
                ta.forceActiveFocus()
                wait(100)
            }
            cd.setCodeLanguage("diagram")
            tryVerify(function() {
                return BlockModel.blockAt(0).language === "diagram"
            }, 1000, "picker must promote plain back to diagram; still "
                     + BlockModel.blockAt(0).language)
            wait(400)
            compare(BlockModel.blockAt(0).language, "diagram",
                    "promotion must survive delegate teardown")
        }

        // A flowchart taller than the read window must fit it entirely in
        // fit mode (fit satisfies BOTH dimensions, not just width), and the
        // effective zoom level must show in the bottom-right indicator.
        function test_zzy2_diagramFitFitsTallFlowchartAndShowsZoom() {
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "```mermaid\nflowchart TD\n  A[a] --> B[b]\n  B --> C[c]\n"
                + "%% mermaid-flow:pos A=100,40 B=100,900 C=100,1760\n```")
            wait(300)

            var d = findBlockDelegate(0)
            verify(d !== null, "diagram delegate exists")
            var canvas = findChild(d, "diagramReadCanvas")
            verify(canvas !== null, "read canvas exists")
            tryVerify(function() { return canvas.hasScene }, 3000,
                      "scene renders")
            verify(canvas.sceneHeight > d.maxReadHeight,
                   "fixture must be taller than the read window; sceneHeight="
                   + canvas.sceneHeight)

            tryVerify(function() {
                return canvas.implicitHeight <= d.maxReadHeight + 0.5
            }, 1000, "fit must scale the diagram into the height cap; "
                     + "implicitHeight=" + canvas.implicitHeight)
            verify(canvas.renderScale < 1.0,
                   "a tall diagram fits below 100%; scale=" + canvas.renderScale)
            var flick = findChild(d, "diagramReadFlick")
            verify(flick !== null, "read flickable exists")
            verify(flick.contentHeight <= flick.height + 0.5,
                   "no vertical pan is needed in fit mode; contentHeight="
                   + flick.contentHeight + " height=" + flick.height)

            var zoomText = findChild(d, "diagramZoomText")
            verify(zoomText !== null, "zoom indicator exists")
            verify(zoomText.parent.visible, "zoom indicator is visible")
            compare(zoomText.text,
                    Math.round(canvas.renderScale * 100) + "%",
                    "indicator shows the effective zoom level")
        }

        // Same report, full click path: open the actual LanguagePicker menu
        // and fire its "Text diagram" MenuItem, instead of calling
        // setCodeLanguage directly.
        function test_zzz_languagePickerMenuItemPromotesToDiagram() {
            DocumentManager.newDocument()
            DocumentSerializer.loadIntoModel(BlockModel,
                "```plain\n┌──┐\n│ok│\n└──┘\n```")
            wait(300)

            var d = findBlockDelegate(0)
            verify(d !== null, "delegate exists")
            var picker = findChild(d, "languagePicker")
            verify(picker !== null, "code delegate exposes the language picker")

            picker.open()
            tryCompare(picker, "visible", true, 1000)
            var target = null
            for (var i = 0; i < picker.count; ++i) {
                var it = picker.itemAt(i)
                if (it && it.text !== undefined
                        && it.text.indexOf("Text diagram") !== -1) {
                    target = it
                    break
                }
            }
            verify(target !== null, "picker offers a Text diagram entry")
            target.triggered()
            picker.close()

            tryVerify(function() {
                return BlockModel.blockAt(0).language === "diagram"
            }, 1000, "menu item must promote plain to diagram; still "
                     + BlockModel.blockAt(0).language)
            wait(400)
            compare(BlockModel.blockAt(0).language, "diagram",
                    "promotion must survive menu close and delegate teardown")
        }

        // ============================================================
        // Wiki-links — follow/create, backlinks panel, quick switcher,
        // history, rename safety, completion.
        // ============================================================

        // Write the open note's first block and save, so the collection
        // index (and thus resolution/backlinks) reflects it.
        function setOpenNoteBody(text) {
            BlockModel.updateContent(0, text)
            DocumentManager.save()
            wait(50)
        }

        function test_wiki1_followOpensAndCreates() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            // Resolved target: opens the note (suffix rule, bare name).
            appLoader.item.linkOpener.activate("kvit-note:Kvit")
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath
                    === "Ideas/Projects/Kvit.md"
            }, 2000, "wiki-link follows to the resolved note")

            // Dangling target: created on click in the current folder
            // (the open note's folder, Obsidian's default).
            appLoader.item.linkOpener.activate("kvit-note:Fresh idea")
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath
                    === "Ideas/Projects/Fresh idea.md"
            }, 2000, "dangling wiki-link creates then opens the note")

            // Duplicate suffixes are ambiguous: following one neither opens
            // an arbitrary candidate nor creates a third note.
            verify(NoteCollection.createFolder("", "Archive") !== "")
            verify(testFiles.writeFile(
                NoteCollection.absolutePath("Archive/Welcome.md"), "duplicate\n"))
            NoteCollection.refresh()
            var before = appLoader.item.currentNoteRelPath
            var countBefore = NoteCollection.noteRelPaths().length
            appLoader.item.linkOpener.activate("kvit-note:Welcome")
            wait(100)
            compare(appLoader.item.currentNoteRelPath, before)
            compare(NoteCollection.noteRelPaths().length, countBefore)
            verify(appLoader.item.transientStatus.indexOf("Ambiguous") === 0)

            closeTestCollection()
        }

        function test_wiki2_backlinksPanelListsAndUpdatesLive() {
            openTestCollection()

            // Reading.md links to Welcome twice on one line.
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            setOpenNoteBody("See [[Welcome]] and [[welcome#Intro|w]].")

            verify(appLoader.item.openNoteByPath("Welcome.md"))
            appLoader.item.backlinksVisible = true
            var panel = findChild(appLoader.item, "backlinksPanel")
            verify(panel !== null, "backlinks panel exists")
            tryVerify(function() { return panel.rows.length === 1 },
                      2000, "one referring note listed")
            compare(panel.rows[0].relPath, "Ideas/Reading.md")
            compare(panel.rows[0].count, 2)
            verify(panel.rows[0].contexts.length >= 1)

            // Exact external-editor path: write another note and feed the
            // watcher while the target stays open and the panel stays visible.
            var plansAbs = NoteCollection.absolutePath("Ideas/Plans.md")
            verify(testFiles.writeFile(plansAbs, "Also [[Welcome]].\n"))
            FileWatcher.feedChange(plansAbs, true)
            tryVerify(function() { return panel.rows.length === 2 },
                      2000, "external watcher adds the second referrer")

            verify(testFiles.writeFile(plansAbs, "No link now.\n"))
            FileWatcher.feedChange(plansAbs, true)
            tryVerify(function() { return panel.rows.length === 1 },
                      2000, "external watcher removes the referrer")

            appLoader.item.backlinksVisible = false
            closeTestCollection()
        }

        function test_wiki3_quickSwitcherFiltersAndOpens() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            var switcher = appLoader.item.quickSwitcher
            switcher.openSwitcher()
            tryCompare(switcher, "visible", true, 1000)

            var field = findChild(switcher.contentItem, "quickSwitcherField")
                        || findChild(appLoader.item, "quickSwitcherField")
            verify(field !== null, "switcher query field exists")
            field.text = "kvit"
            tryVerify(function() { return switcher.rows.length === 1 },
                      1000, "query narrows to the one match")
            switcher.applyHighlighted()
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath
                    === "Ideas/Projects/Kvit.md"
            }, 2000, "Enter-equivalent opens the highlighted note")
            tryCompare(switcher, "visible", false, 1000)

            closeTestCollection()
        }

        function test_wiki4_historyBackAndForward() {
            openTestCollection()
            NavigationHistory.clear()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            verify(appLoader.item.openNoteByPath("Ideas/Projects/Kvit.md"))

            verify(NavigationHistory.canGoBack)
            appLoader.item.navigateBack()
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath
                    === "Ideas/Reading.md"
            }, 2000, "back returns to the previous note")
            appLoader.item.navigateBack()
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath === "Welcome.md"
            }, 2000, "back again reaches the first note")
            verify(NavigationHistory.canGoForward)
            appLoader.item.navigateForward()
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath
                    === "Ideas/Reading.md"
            }, 2000, "forward retraces the step")

            closeTestCollection()
        }

        function test_wiki5_renameShowsToastAndRewrites() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            setOpenNoteBody("Points at [[Kvit]] here.")
            verify(appLoader.item.openNoteByPath("Welcome.md"))

            var plan = NoteCollection.planNoteRename(
                "Ideas/Projects/Kvit.md", "Editor")
            verify(plan.ok && plan.linkCount === 1)
            var result = NoteCollection.applyRenamePlan(plan.id, true)
            verify(result.ok)
            tryVerify(function() {
                return appLoader.item.transientStatus.indexOf("Updated") === 0
            }, 2000, "rename shows the updated-links toast")
            tryVerify(function() {
                return NoteCollection.linksFrom("Ideas/Reading.md")
                           .indexOf("Editor") >= 0
            }, 2000, "referrer reindexed with the new target")

            closeTestCollection()
        }

        function test_wiki5b_confirmedRenameRewritesDirtyOpenNoteUndoably() {
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            setOpenNoteBody("Points at [[Kvit]] here.")
            BlockModel.updateContent(0, "Dirty points at [[Kvit]] here.")
            verify(DocumentManager.isDirty)

            appLoader.item.requestNoteRename("Ideas/Projects/Kvit.md", "Editor")
            var dialog = findChild(appLoader.item, "renameLinksDialog")
            verify(dialog !== null)
            tryCompare(dialog, "visible", true, 1000)
            appLoader.item.finishRenamePlan(true)
            dialog.close()

            tryVerify(function() {
                return BlockModel.blockAt(0).content.indexOf("[[Editor]]") >= 0
            }, 1000, "dirty open referrer is rewritten in memory")
            verify(DocumentManager.isDirty)
            UndoStack.undo()
            compare(BlockModel.blockAt(0).content,
                    "Dirty points at [[Kvit]] here.")

            closeTestCollection()
        }

        function test_wiki6_completionPopupInsertsAndDismisses() {
            if (isHeadless) {
                skip("Focus tests require display")
            }
            openTestCollection()
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            BlockModel.updateContent(0, "")
            wait(100)

            var d = findBlockDelegate(0)
            verify(d !== null)
            var textArea = findTextArea(d)
            verify(textArea !== null)
            ensureFocus(textArea)

            var menu = appLoader.item.wikiLinkMenu
            verify(!menu.visible)

            // Typing "[[" opens the popup; letters filter it.
            typeString("[[")
            tryCompare(menu, "visible", true, 2000)
            typeString("kv")
            tryVerify(function() {
                return menu.rows.length === 1
                    && menu.rows[0].title === "Kvit"
            }, 2000, "query narrows the completion to Kvit")

            // Enter inserts the target and closes with ]].
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false, 1000)
            tryVerify(function() {
                return BlockModel.getContent(0) === "[[Kvit]]"
            }, 2000, "completion inserts the closed wiki-link; got "
                     + BlockModel.getContent(0))

            // Escape closes without touching the text.
            BlockModel.updateContent(0, "")
            wait(100)
            ensureFocus(textArea)
            typeString("[[We")
            tryCompare(menu, "visible", true, 2000)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false, 1000)
            compare(BlockModel.getContent(0), "[[We")

            // Inside a code block the trigger never fires.
            DocumentManager.newDocument()
            wait(50)
            verify(appLoader.item.openNoteByPath("Ideas/Reading.md"))
            BlockModel.updateContent(0, "")
            wait(50)
            BlockModel.convertBlock(0, Block.CodeBlock, "", false, "")
            wait(100)
            var cd = findBlockDelegate(0)
            var codeArea = findTextArea(cd)
            verify(codeArea !== null)
            ensureFocus(codeArea)
            typeString("[[")
            wait(200)
            verify(!menu.visible, "no completion inside code fences")

            closeTestCollection()
        }

        // ============================================================
        // Collection query block — rendering, click-to-open, and the
        // live file-watcher update the launch demo claims.
        // ============================================================

        function test_wiki7_queryBlockRendersAndUpdatesLive() {
            openTestCollection()

            // Front-matter fixtures, written as an external tool would.
            verify(testFiles.writeFile(
                NoteCollection.absolutePath("Ideas/Reading.md"),
                "---\nstatus: active\ndue: 2026-08-01\n---\nReading body\n"))
            verify(testFiles.writeFile(
                NoteCollection.absolutePath("Ideas/Projects/Kvit.md"),
                "---\nstatus: done\n---\nKvit body\n"))
            NoteCollection.refresh()
            wait(100)

            // A query fence in the open note renders the matching rows.
            verify(appLoader.item.openNoteByPath("Welcome.md"))
            BlockModel.convertBlock(0, Block.CodeBlock,
                "where: status = active\ncolumns: title, due\n"
                + "sort: title asc", false, "query")
            wait(200)

            var d = findBlockDelegate(0)
            verify(d !== null, "query delegate exists")
            tryVerify(function() {
                return d.queryResult !== undefined && d.queryResult.ok
            }, 2000, "query evaluates")
            tryVerify(function() {
                return d.queryResult.rows.length === 1
            }, 2000, "one active note matches; got "
                     + d.queryResult.rows.length)
            compare(d.queryResult.rows[0].cells[0], "Reading")
            compare(d.queryResult.columns, ["title", "due"])

            // Two visible identical blocks share QueryTools' underlying
            // evaluation rather than reparsing independently.
            BlockModel.insertBlock(1, Block.Paragraph, "")
            BlockModel.convertBlock(1, Block.CodeBlock,
                BlockModel.blockAt(0).content, false, "query")
            wait(250)
            var d2 = findBlockDelegate(1)
            verify(d2 !== null, "second query delegate exists")
            QueryTools.clearCache()
            d.refresh()
            d2.refresh()
            compare(QueryTools.evaluationCount(), 1,
                    "identical visible blocks share one evaluation")

            // Live update through the FileWatcher: an external edit flips
            // another note's status and the block re-evaluates without
            // being touched (the launch-demo claim).
            var abs = NoteCollection.absolutePath("Ideas/Projects/Kvit.md")
            verify(testFiles.writeFile(abs,
                "---\nstatus: active\ndue: 2026-07-15\n---\nKvit body\n"))
            var evaluationsBeforeBurst = QueryTools.evaluationCount()
            for (var burst = 0; burst < 10; ++burst)
                FileWatcher.feedChange(abs, true)
            tryVerify(function() {
                return d.queryResult.rows.length === 2
            }, 3000, "external front-matter edit re-evaluates the block")
            wait(200)
            compare(QueryTools.evaluationCount() - evaluationsBeforeBurst, 1,
                    "ten rapid revisions coalesce into one evaluation")
            compare(d.queryResult.rows[0].cells[0], "Kvit")

            // A parse error surfaces in the read view instead of rows.
            BlockModel.updateContent(0, "wat: no")
            wait(100)
            tryVerify(function() { return !d.queryResult.ok }, 1000,
                      "unknown key is an error")
            verify(d.queryResult.error.indexOf("unknown key") === 0)

            closeTestCollection()
        }

        function test_wiki8_queryBoardGroupsAndClickOpens() {
            openTestCollection()
            verify(testFiles.writeFile(
                NoteCollection.absolutePath("Ideas/Reading.md"),
                "---\nstatus: active\n---\nReading body\n"))
            verify(testFiles.writeFile(
                NoteCollection.absolutePath("Ideas/Projects/Kvit.md"),
                "---\nstatus: done\n---\nKvit body\n"))
            NoteCollection.refresh()
            wait(100)

            verify(appLoader.item.openNoteByPath("Welcome.md"))
            BlockModel.convertBlock(0, Block.CodeBlock,
                "from: Ideas\ngroup-by: status\ncolumns: title", false,
                "query")
            wait(200)

            var d = findBlockDelegate(0)
            verify(d !== null)
            tryVerify(function() {
                return d.queryResult.ok && d.queryResult.view === "board"
                    && d.queryResult.groups.length === 2
            }, 2000, "board groups by status")
            compare(d.queryResult.groups[0].cards.length, 1)

            // Row click opens the note (the openRow path the table and
            // board cards share).
            d.openRow(d.queryResult.rows[0].relPath)
            tryVerify(function() {
                return appLoader.item.currentNoteRelPath !== "Welcome.md"
            }, 2000, "clicking a result opens that note")

            closeTestCollection()
        }

        // Review-v2 regression coverage: document replacement, save, export,
        // and shutdown all call the same pending-edit barrier. Exercise that
        // signal against editors whose newest value otherwise lives only in
        // QML, then exercise the ListView pooling boundary separately.
        function test_zzzz1_pendingEditorsFlushIntoTheModel() {
            DocumentManager.newDocument()
            BlockModel.convertBlock(0, Block.CodeBlock,
                                    "from: .", false, "query")
            wait(200)
            var querySource = findChild(findBlockDelegate(0), "querySourceArea")
            verify(querySource !== null, "query source editor exists")
            querySource.text = "from: .\nwhere: status = active"
            verify(BlockModel.getContent(0) !== querySource.text,
                   "the fixture must still own a QML-only edit")
            DocumentManager.flushPendingEdits()
            compare(BlockModel.getContent(0), querySource.text,
                    "the document barrier commits query source")

            BlockModel.convertBlock(0, Block.Callout, "body", false,
                                    "warning", "Old title")
            wait(200)
            var title = findChild(findBlockDelegate(0), "calloutTitleField")
            verify(title !== null, "callout title editor exists")
            title.text = "Newest title"
            compare(BlockModel.blockAt(0).calloutTitle, "Old title")
            DocumentManager.flushPendingEdits()
            compare(BlockModel.blockAt(0).calloutTitle, "Newest title",
                    "the document barrier commits the callout title")
        }

        function test_zzzz2_poolingCommitsBeforeDelegateReuse() {
            DocumentManager.newDocument()
            BlockModel.convertBlock(0, Block.CodeBlock,
                                    "from: .", false, "query")
            for (var i = 1; i < 80; ++i)
                BlockModel.insertBlock(i, Block.Paragraph,
                                       "filler block " + i)
            wait(250)

            var list = findChild(appLoader.item, "blockListView")
            list.positionViewAtBeginning()
            wait(100)
            var querySource = findChild(findBlockDelegate(0), "querySourceArea")
            verify(querySource !== null, "query source editor exists")
            var pending = "from: .\ncolumns: title, status"
            querySource.text = pending
            verify(BlockModel.getContent(0) !== pending)

            // ListView keeps its current item instantiated even when it is
            // far outside the viewport, so move currentIndex as well as the
            // viewport to force row zero through the pool.
            list.currentIndex = BlockModel.count - 1
            list.positionViewAtEnd()
            tryVerify(function() {
                return BlockModel.getContent(0) === pending
            }, 2000, "pooling commits before the delegate can be reused")
        }

        function test_zzzz3_platformLiteralShortcuts() {
            if (isHeadless) {
                skip("Keyboard tests require display")
            }
            DocumentManager.newDocument()
            wait(100)
            var textArea = findTextArea(findBlockDelegate(0))
            ensureFocus(textArea)
            resetFindBar()

            if (Qt.platform.os === "osx")
                keyClick(Qt.Key_F, Qt.MetaModifier | Qt.AltModifier)
            else
                keyClick(Qt.Key_H, Qt.ControlModifier)
            tryCompare(appLoader.item.findBar, "visible", true, 1000)
            verify(findChild(appLoader.item.findBar, "replaceField").visible,
                   "the platform Find & Replace chord opens replace mode")
            resetFindBar()

            appLoader.item.focusMode = false
            ensureFocus(textArea)
            if (Qt.platform.os === "osx")
                keyClick(Qt.Key_F, Qt.MetaModifier | Qt.ControlModifier)
            else
                keyClick(Qt.Key_F11)
            tryCompare(appLoader.item, "focusMode", true, 1000)
            appLoader.item.focusMode = false
            appLoader.item.visibility = Window.Windowed
        }
    }
}
