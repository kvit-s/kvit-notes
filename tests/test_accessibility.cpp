// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "accessibilityannouncer.h"

// The live-region announcer seam. Deterministic unit coverage
// of the announcement logic — that each dynamic-change category produces the
// right message and fires the signal the gate relies on (the WSLg AT-SPI bridge
// is inactive, spike (c), so the signal, not a spoken read, is what is asserted).
class TestAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void announceSetsLastMessageAndEmits();
    void emptyMessageIgnored();
    void saveStateWording();
    void matchCountWording();
    void modeWording();
    void conversionWording();
};

void TestAccessibility::announceSetsLastMessageAndEmits()
{
    AccessibilityAnnouncer a;
    QSignalSpy spy(&a, &AccessibilityAnnouncer::announced);
    a.announce("Hello there");
    QCOMPARE(a.lastMessage(), QString("Hello there"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("Hello there"));
}

void TestAccessibility::emptyMessageIgnored()
{
    AccessibilityAnnouncer a;
    a.announce("first");
    QSignalSpy spy(&a, &AccessibilityAnnouncer::announced);
    a.announce("");
    QCOMPARE(spy.count(), 0);
    QCOMPARE(a.lastMessage(), QString("first"));
}

void TestAccessibility::saveStateWording()
{
    AccessibilityAnnouncer a;
    a.announceSaveState(true);
    QCOMPARE(a.lastMessage(), QString("Unsaved changes"));
    a.announceSaveState(false);
    QCOMPARE(a.lastMessage(), QString("Saved"));
}

void TestAccessibility::matchCountWording()
{
    AccessibilityAnnouncer a;
    a.announceMatchCount(0);
    QCOMPARE(a.lastMessage(), QString("No matches"));
    a.announceMatchCount(3);
    QCOMPARE(a.lastMessage(), QString("3 matches"));
    a.announceMatchCount(1);
    QCOMPARE(a.lastMessage(), QString("1 match"));
}

void TestAccessibility::modeWording()
{
    AccessibilityAnnouncer a;
    a.announceMode("Focus mode", true);
    QCOMPARE(a.lastMessage(), QString("Focus mode on"));
    a.announceMode("Typewriter mode", false);
    QCOMPARE(a.lastMessage(), QString("Typewriter mode off"));
}

void TestAccessibility::conversionWording()
{
    AccessibilityAnnouncer a;
    a.announceConversion("Heading 1");
    QCOMPARE(a.lastMessage(), QString("Converted to Heading 1"));
}

QTEST_MAIN(TestAccessibility)
#include "test_accessibility.moc"
