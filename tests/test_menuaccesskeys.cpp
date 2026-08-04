// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "menuaccesskeys.h"

// The `&` spelling menu labels are written in, read in both directions.
//
// Windows and Linux underline the marked letter and run the command when it is
// typed; macOS has no such convention and gets the markers taken out. What
// makes that worth a test rather than an eyeball is that the macOS half runs
// on no machine any suite here can reach, so it is exercised through
// labelFor(), which takes the platform as an argument rather than reading the
// one it was built for.
//
// The other half is plain(): the escape that keeps a folder called "R&D" from
// losing its ampersand and gaining an underlined D. Its result has to survive
// the trip back through both platforms, which is the round trip below.
class TestMenuAccessKeys : public QObject
{
    Q_OBJECT

private slots:
    void aMarkedLabelIsUnchangedWhereAccessKeysAreShown();
    void markersComeOutWhereAccessKeysAreNotShown();
    void anEscapedAmpersandSurvivesAsOneAmpersand();
    void plainTextRoundTripsThroughBothPlatforms_data();
    void plainTextRoundTripsThroughBothPlatforms();
    void theRunningPlatformFollowsItsConvention();
    void theDeclaredKeyIsTheLetterAfterTheMarker();
};

void TestMenuAccessKeys::aMarkedLabelIsUnchangedWhereAccessKeysAreShown()
{
    // Qt itself draws the underline and binds Alt+<letter> from this text, so
    // where the convention holds the label has to reach it untouched.
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("&Copy"), true),
             QStringLiteral("&Copy"));
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("Save &As…"), true),
             QStringLiteral("Save &As…"));
}

void TestMenuAccessKeys::markersComeOutWhereAccessKeysAreNotShown()
{
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("&Copy"), false),
             QStringLiteral("Copy"));
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("Save &As…"), false),
             QStringLiteral("Save As…"));
    // The File menu's Settings entry is the one where this is load-bearing:
    // macOS moves it into the application menu by matching its text, so the
    // marker has to be gone before Qt sees it.
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("Se&ttings…"), false),
             QStringLiteral("Settings…"));
    // A trailing marker marks nothing and leaves nothing.
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("Odd&"), false),
             QStringLiteral("Odd"));
}

void TestMenuAccessKeys::anEscapedAmpersandSurvivesAsOneAmpersand()
{
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("R&&D"), false),
             QStringLiteral("R&D"));
    QCOMPARE(MenuAccessKeys::labelFor(QStringLiteral("&Save R&&D"), false),
             QStringLiteral("Save R&D"));
}

void TestMenuAccessKeys::plainTextRoundTripsThroughBothPlatforms_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("no ampersand") << "Notes";
    QTest::newRow("one ampersand") << "R&D";
    QTest::newRow("leading ampersand") << "&drafts";
    QTest::newRow("two ampersands") << "R&D & more";
    QTest::newRow("already doubled") << "R&&D";
    QTest::newRow("a path") << "/home/sk/vaults/R&D notes";
    QTest::newRow("empty") << "";
}

void TestMenuAccessKeys::plainTextRoundTripsThroughBothPlatforms()
{
    QFETCH(QString, name);

    // plain() is what a name off the disk goes through on its way into a
    // menu. Whatever the platform then does with the result has to give the
    // name back exactly: on macOS Qt takes the markers out itself, and
    // labelFor(..., false) is that same rule.
    const QString escaped = MenuAccessKeys::plain(name);
    QCOMPARE(MenuAccessKeys::labelFor(escaped, false), name);

    // And nothing in an escaped name is left claiming a key.
    QVERIFY(MenuAccessKeys::accessKeyOf(escaped).isNull());
}

void TestMenuAccessKeys::theRunningPlatformFollowsItsConvention()
{
    const bool showsKeys = MenuAccessKeys::platformShowsAccessKeys();
#ifdef Q_OS_MACOS
    QVERIFY(!showsKeys);
#else
    QVERIFY(showsKeys);
#endif
    // label() is labelFor() applied to the running platform, and that is the
    // whole of what QML calls.
    QCOMPARE(MenuAccessKeys::label(QStringLiteral("&Copy")),
             MenuAccessKeys::labelFor(QStringLiteral("&Copy"), showsKeys));
}

void TestMenuAccessKeys::theDeclaredKeyIsTheLetterAfterTheMarker()
{
    // What tools/check-menu-access-keys.py compares between the commands of
    // one menu; the letter is compared without case, so "&copy" and "&Copy"
    // are the same claim.
    QCOMPARE(MenuAccessKeys::accessKeyOf(QStringLiteral("&Copy")), QChar('C'));
    QCOMPARE(MenuAccessKeys::accessKeyOf(QStringLiteral("Cu&t")), QChar('T'));
    QCOMPARE(MenuAccessKeys::accessKeyOf(QStringLiteral("&copy")), QChar('C'));
    QCOMPARE(MenuAccessKeys::accessKeyOf(QStringLiteral("&2 lines")), QChar('2'));
    // An escaped ampersand is not a marker; the marker after it still is.
    QCOMPARE(MenuAccessKeys::accessKeyOf(QStringLiteral("R&&D &notes")),
             QChar('N'));
    QVERIFY(MenuAccessKeys::accessKeyOf(QStringLiteral("Plain")).isNull());
    QVERIFY(MenuAccessKeys::accessKeyOf(QStringLiteral("R&&D")).isNull());
}

QTEST_APPLESS_MAIN(TestMenuAccessKeys)
#include "test_menuaccesskeys.moc"
