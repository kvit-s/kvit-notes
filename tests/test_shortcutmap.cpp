// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "shortcutcatalog.h"

// The keyboard-shortcut audit. This corpus cross-checks the
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
    void everyChordSurvivesPlatformRendering();
    void literalTriggersRenderAsThemselves();
    void standardKeyEntriesAgreeWithTheSpecChord();
    void onlyStandardKeyDrivenActionsClaimOne();
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

void TestShortcutMap::everyChordSurvivesPlatformRendering()
{
    // The cheat sheet shows displayChord, so a chord the platform renderer
    // cannot express would leave the reference blank where a working key
    // exists. Holds on every platform: only the spelling differs.
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        const QString shown = ShortcutCatalog::displayChord(e.chord);
        if (e.chord.isEmpty()) {
            QVERIFY2(shown.isEmpty(),
                     qPrintable(e.action + " has no chord but renders one"));
            continue;
        }
        QVERIFY2(!shown.isEmpty(),
                 qPrintable(e.action + " has chord \"" + e.chord
                            + "\" but renders blank"));
    }
}

void TestShortcutMap::literalTriggersRenderAsThemselves()
{
    // Two catalog entries are typed characters rather than modifier chords.
    // They must survive rendering unchanged — "\\" opens the math command
    // menu and "$" auto-pairs inline math, and both are shown to the user
    // exactly as they are typed.
    QCOMPARE(ShortcutCatalog::displayChord(QStringLiteral("\\")),
             QStringLiteral("\\"));
    QCOMPARE(ShortcutCatalog::displayChord(QStringLiteral("$")),
             QStringLiteral("$"));
    QCOMPARE(ShortcutCatalog::displayChord(QString()), QString());

    // On Windows and Linux the portable spelling is already native, so the
    // resolution is the identity there; macOS is where it substitutes glyphs.
#ifndef Q_OS_MACOS
    QCOMPARE(ShortcutCatalog::displayChord(QStringLiteral("Ctrl+B")),
             QStringLiteral("Ctrl+B"));
#else
    QVERIFY(!ShortcutCatalog::displayChord(QStringLiteral("Ctrl+B"))
                 .contains(QStringLiteral("Ctrl")));
#endif
}

void TestShortcutMap::standardKeyEntriesAgreeWithTheSpecChord()
{
    // The catalog and the running app are two descriptions of one thing: the
    // catalog stores the §13 chord, while main.qml binds Qt standard keys and
    // the editor tests Ctrl directly. They agree today on Windows and Linux —
    // Qt's primary binding for each of these resolves to exactly the §13
    // chord, Redo's Ctrl+Y included. Nothing enforced that agreement, so this
    // does: if a Qt release or a spec edit moves either side, the reference
    // would start advertising a chord that no longer fires, and this fails
    // instead.
    //
    // macOS is excluded because that is where they are MEANT to differ: the
    // §13 table is the Windows/Linux column, and Qt resolves the same standard
    // keys to Command-based chords there. That divergence is the feature.
#ifdef Q_OS_MACOS
    QSKIP("the §13 table is the Windows/Linux column; macOS resolves elsewhere");
#else
    int checked = 0;
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        if (e.standardKey == QKeySequence::UnknownKey)
            continue;
        const QList<QKeySequence> bound = QKeySequence::keyBindings(e.standardKey);
        QVERIFY2(!bound.isEmpty(),
                 qPrintable(e.action + " claims a standard key Qt does not bind"));
        // Membership, not position: Qt lists several bindings per standard key
        // and their order follows the platform theme, so "first" is not a
        // stable notion. What must hold is that the chord §13 documents is one
        // of the sequences the standard key actually arms.
        const QKeySequence documented =
            QKeySequence::fromString(e.chord, QKeySequence::PortableText);
        QVERIFY2(bound.contains(documented),
                 qPrintable(e.action + ": §13 documents " + e.chord
                            + " but StandardKey binds only "
                            + [&] {
                                  QStringList all;
                                  for (const QKeySequence &b : bound)
                                      all << b.toString(QKeySequence::PortableText);
                                  return all.join(QStringLiteral(", "));
                              }()));
        // And the reference shows that documented chord, not an arbitrary
        // sibling binding.
        QCOMPARE(ShortcutCatalog::displayChord(e.standardKey, e.chord),
                 documented.toString(QKeySequence::NativeText));
        ++checked;
    }
    QVERIFY2(checked >= 9,
             qPrintable(QStringLiteral("only %1 entries carry a standard key; "
                                       "the wiring in main.qml uses more")
                            .arg(checked)));
#endif
}

void TestShortcutMap::onlyStandardKeyDrivenActionsClaimOne()
{
    // The reference must never show a chord grander than what is wired. These
    // four actions are bound to literal sequences on purpose — Find & Replace
    // because the platform theme maps StandardKey.Replace to Ctrl+R or nothing
    // on some Linux desktops, and Back, Forward and Distraction-free because
    // Qt's standard bindings for them include keys an editor cannot spare
    // (Backspace navigates Back). None may claim a standard key, or the
    // reference would advertise a macOS chord nothing listens for.
    const QStringList literalByDesign{
        QStringLiteral("Find & Replace"), QStringLiteral("Back"),
        QStringLiteral("Forward"), QStringLiteral("Distraction-free")};
    for (const ShortcutInfo &e : ShortcutCatalog::entries()) {
        if (!literalByDesign.contains(e.action))
            continue;
        QVERIFY2(e.standardKey == QKeySequence::UnknownKey,
                 qPrintable(e.action + " is wired literally but claims a "
                                       "standard key"));
    }
}

QTEST_MAIN(TestShortcutMap)
#include "test_shortcutmap.moc"
