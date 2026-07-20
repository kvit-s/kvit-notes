// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "shortcutcatalog.h"

// Phase 12 Step 4: the keyboard-shortcut audit. This corpus cross-checks the
// ShortcutCatalog (the single source of truth the ShortcutReference cheat sheet
// renders) against the features.md §13.1–§13.4 tables: every shortcut must be
// present with its exact chord, and the three deviations must be marked
// intentional with a reason rather than left as silent gaps. The behavioral
// "does the chord fire the action" wiring is exercised by the integration suite
// (Ctrl+B/I/U, Ctrl+0–3, Ctrl+T, Ctrl+S, Ctrl+F, …); this test guards the
// completeness and the deviation record.
class TestShortcutMap : public QObject
{
    Q_OBJECT

private slots:
    void everySpecShortcutPresentWithChord_data();
    void everySpecShortcutPresentWithChord();
    void deviationsAreIntentionalWithAReason();
    void nonDeviationsHaveAChordAndKnownWiring();
    void categoriesCoverTheFourSpecSections();
    void noDuplicateActions();
};

void TestShortcutMap::everySpecShortcutPresentWithChord_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<QString>("chord");

    // features.md §13.1–§13.4, the Windows/Linux column, verbatim.
    // §13.1
    QTest::newRow("Bold") << "Bold" << "Ctrl+B";
    QTest::newRow("Italic") << "Italic" << "Ctrl+I";
    QTest::newRow("Underline") << "Underline" << "Ctrl+U";
    QTest::newRow("Strikethrough") << "Strikethrough" << "Ctrl+Shift+S";
    QTest::newRow("Inline Code") << "Inline Code" << "Ctrl+E";
    QTest::newRow("Link") << "Link" << "Ctrl+K";
    // §13.2
    QTest::newRow("Move up") << "Move block up" << "Alt+Up";
    QTest::newRow("Move down") << "Move block down" << "Alt+Down";
    QTest::newRow("Duplicate") << "Duplicate block" << "Ctrl+D";
    QTest::newRow("Delete") << "Delete block" << "Ctrl+Shift+D";
    QTest::newRow("Indent") << "Indent" << "Tab";
    QTest::newRow("Outdent") << "Outdent" << "Shift+Tab";
    // §13.3
    QTest::newRow("Paragraph") << "Paragraph" << "Ctrl+0";
    QTest::newRow("Heading 1") << "Heading 1" << "Ctrl+1";
    QTest::newRow("Heading 2") << "Heading 2" << "Ctrl+2";
    QTest::newRow("Heading 3") << "Heading 3" << "Ctrl+3";
    QTest::newRow("Todo") << "Todo" << "Ctrl+T";
    QTest::newRow("Quote") << "Quote" << "Ctrl+Shift+T";
    // §13.4
    QTest::newRow("Save") << "Save" << "Ctrl+S";
    QTest::newRow("Undo") << "Undo" << "Ctrl+Z";
    QTest::newRow("Redo") << "Redo" << "Ctrl+Y";
    QTest::newRow("Find") << "Find" << "Ctrl+F";
    QTest::newRow("Find & Replace") << "Find & Replace" << "Ctrl+H";
    QTest::newRow("Select All") << "Select All" << "Ctrl+A";
    QTest::newRow("New Note") << "New Note" << "Ctrl+N";
    QTest::newRow("Toggle Sidebar") << "Toggle Sidebar" << "Ctrl+\\";
    QTest::newRow("Distraction-free") << "Distraction-free" << "F11";
}

void TestShortcutMap::everySpecShortcutPresentWithChord()
{
    QFETCH(QString, action);
    QFETCH(QString, chord);
    QVERIFY2(ShortcutCatalog::contains(action),
             qPrintable("catalog missing §13 action: " + action));
    QCOMPARE(ShortcutCatalog::chordFor(action), chord);
}

void TestShortcutMap::deviationsAreIntentionalWithAReason()
{
    // The three documented §13 deviations: no shortcut, but recorded with a
    // reason rather than silently missing.
    const QStringList deviations = {
        "Superscript", "Subscript", "Heading 4", "Save As"};
    for (const QString &action : deviations) {
        QVERIFY2(ShortcutCatalog::contains(action),
                 qPrintable("deviation not recorded: " + action));
    }
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        if (deviations.contains(e.action)) {
            QVERIFY2(e.intentional,
                     qPrintable(e.action + " must be marked intentional"));
            QVERIFY2(e.chord.isEmpty(),
                     qPrintable(e.action + " deviation must carry no chord"));
            QVERIFY2(!e.note.isEmpty(),
                     qPrintable(e.action + " deviation must state its reason"));
        }
    }
}

void TestShortcutMap::nonDeviationsHaveAChordAndKnownWiring()
{
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        if (e.intentional)
            continue;
        QVERIFY2(!e.chord.isEmpty(),
                 qPrintable(e.action + " should have a chord"));
        QVERIFY2(e.wiredAt == "engine" || e.wiredAt == "window"
                     || e.wiredAt == "menu",
                 qPrintable(e.action + " has an unknown wiring site"));
    }
}

void TestShortcutMap::categoriesCoverTheFourSpecSections()
{
    ShortcutCatalog cat;
    const QStringList cats = cat.categories();
    // The four features.md §13 sections plus the math section.
    QCOMPARE(cats, QStringList({"Text Formatting", "Block Operations",
                                "Block Conversion", "Math Editing",
                                "General"}));
}

void TestShortcutMap::noDuplicateActions()
{
    QStringList seen;
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        QVERIFY2(!seen.contains(e.action),
                 qPrintable("duplicate action in catalog: " + e.action));
        seen.append(e.action);
    }
}

QTEST_MAIN(TestShortcutMap)
#include "test_shortcutmap.moc"
