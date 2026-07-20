// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "navigationhistory.h"

// Unit suite for the back/forward note history.
// Contracts: browser stack discipline (a new visit clears forward), the
// re-entrancy rule (goBack's own reopen is a no-op visit), scroll
// positions stamped on departure, and collection lifecycle (rename
// rebinds, deletion scrubs, root change clears).
class TestNavigationHistory : public QObject
{
    Q_OBJECT

private slots:
    void testVisitAndBack();
    void testForwardClearsOnNewVisit();
    void testReentrantVisitIsNoOp();
    void testPositionsRestoreOnBackAndForward();
    void testRenameRebindsEntries();
    void testDropScrubsEntries();
    void testClear();
};

void TestNavigationHistory::testVisitAndBack()
{
    NavigationHistory history;
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());
    QVERIFY(!history.goBack().value("ok").toBool());

    history.visit("a.md");
    QVERIFY(!history.canGoBack()); // first visit has nothing to go back to
    history.visit("b.md");
    QVERIFY(history.canGoBack());

    const QVariantMap back = history.goBack();
    QVERIFY(back.value("ok").toBool());
    QCOMPARE(back.value("relPath").toString(), QString("a.md"));
    QVERIFY(history.canGoForward());

    const QVariantMap fwd = history.goForward();
    QCOMPARE(fwd.value("relPath").toString(), QString("b.md"));
    QVERIFY(!history.canGoForward());
}

void TestNavigationHistory::testForwardClearsOnNewVisit()
{
    NavigationHistory history;
    history.visit("a.md");
    history.visit("b.md");
    history.goBack();          // current = a, forward = [b]
    QVERIFY(history.canGoForward());
    history.visit("c.md");     // branches: forward clears
    QVERIFY(!history.canGoForward());
    QCOMPARE(history.goBack().value("relPath").toString(), QString("a.md"));
}

void TestNavigationHistory::testReentrantVisitIsNoOp()
{
    NavigationHistory history;
    history.visit("a.md");
    history.visit("b.md");
    history.goBack();          // current = a
    // The window reopens a.md and fires visit("a.md") — must not disturb
    // the stacks (that is the whole re-entrancy contract).
    history.visit("a.md", 999);
    QVERIFY(history.canGoForward());
    QCOMPARE(history.goForward().value("relPath").toString(), QString("b.md"));
}

void TestNavigationHistory::testPositionsRestoreOnBackAndForward()
{
    NavigationHistory history;
    history.visit("a.md");
    history.visit("b.md", 120);   // leaving a at y=120
    history.visit("c.md", 300);   // leaving b at y=300

    QVariantMap back = history.goBack(50);  // leaving c at y=50
    QCOMPARE(back.value("relPath").toString(), QString("b.md"));
    QCOMPARE(back.value("position").toReal(), 300.0);

    back = history.goBack(310);             // leaving b (again) at y=310
    QCOMPARE(back.value("relPath").toString(), QString("a.md"));
    QCOMPARE(back.value("position").toReal(), 120.0);

    QVariantMap fwd = history.goForward(125);
    QCOMPARE(fwd.value("relPath").toString(), QString("b.md"));
    QCOMPARE(fwd.value("position").toReal(), 310.0);

    fwd = history.goForward(0);
    QCOMPARE(fwd.value("relPath").toString(), QString("c.md"));
    QCOMPARE(fwd.value("position").toReal(), 50.0);
}

void TestNavigationHistory::testRenameRebindsEntries()
{
    NavigationHistory history;
    history.visit("a.md");
    history.visit("b.md");
    history.renamePath("a.md", "renamed.md");
    QCOMPARE(history.goBack().value("relPath").toString(),
             QString("renamed.md"));
}

void TestNavigationHistory::testDropScrubsEntries()
{
    NavigationHistory history;
    history.visit("a.md");
    history.visit("gone.md");
    history.visit("a.md");
    history.visit("b.md");
    // back = [a, gone, a]; dropping "gone" must also collapse the a,a run.
    history.dropPath("gone.md");
    QCOMPARE(history.goBack().value("relPath").toString(), QString("a.md"));
    QVERIFY(!history.canGoBack());
}

void TestNavigationHistory::testClear()
{
    NavigationHistory history;
    QSignalSpy spy(&history, &NavigationHistory::changed);
    history.visit("a.md");
    history.visit("b.md");
    history.clear();
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());
    QVERIFY(spy.count() >= 3);
    // After a clear, the next visit starts a fresh history.
    history.visit("c.md");
    QVERIFY(!history.canGoBack());
}

QTEST_MAIN(TestNavigationHistory)
#include "test_navigationhistory.moc"
