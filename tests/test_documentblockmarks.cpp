// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QSignalSpy>

#include "documentblockmarks.h"

// The marked ranges of one drawn document: the registry half.
//
// A read-only surface draws a markdown document that is not the open note, and
// a caller often knows something about part of what it drew — which characters
// differ from the note as it stands, which phrase a search hit fell on. This is
// the object it says so through. The cases below cover what a caller can
// register, what is refused, and the one property that separates this from the
// per-window DocumentDecorations: the entries belong to the instance, so two
// surfaces mark their own documents.
//
// The drawing half is in tests/test_readonlydocument.cpp, where a surface
// exists to draw them.
class TestDocumentBlockMarks : public QObject
{
    Q_OBJECT

private slots:
    void anEmptyRegistryIsInactiveAndAnswersNothing()
    {
        DocumentBlockMarks marks;
        QVERIFY(!marks.isActive());
        QCOMPARE(marks.count(), 0);
        QCOMPARE(marks.revision(), 0);
        QVERIFY(marks.marksForBlock(0).isEmpty());
        QVERIFY(marks.mark(QStringLiteral("mark-1")).isEmpty());
    }

    void aMarkIsAddressedByBlockAndDisplayOffset()
    {
        DocumentBlockMarks marks;
        QSignalSpy changed(&marks, &DocumentBlockMarks::changed);

        const QString id = marks.add(2, 8, 4, QColor(QStringLiteral("#5533aa")),
                                     QColor(QStringLiteral("#cc4400")));

        QVERIFY(!id.isEmpty());
        QCOMPARE(changed.count(), 1);
        QVERIFY(marks.isActive());
        QCOMPARE(marks.count(), 1);
        QCOMPARE(marks.revision(), 1);

        // The shape is the one BlockEditorEngine's decorationSpans takes, so a
        // row hands the answer straight to the engine laying its text out: one
        // colour string per channel, empty where the entry does not use it.
        const QVariantList onBlock = marks.marksForBlock(2);
        QCOMPARE(onBlock.size(), 1);
        const QVariantMap entry = onBlock.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(entry.value(QStringLiteral("block")).toInt(), 2);
        QCOMPARE(entry.value(QStringLiteral("start")).toInt(), 8);
        QCOMPARE(entry.value(QStringLiteral("length")).toInt(), 4);
        QCOMPARE(QColor(entry.value(QStringLiteral("wash")).toString()),
                 QColor(QStringLiteral("#5533aa")));
        QCOMPARE(QColor(entry.value(QStringLiteral("outline")).toString()),
                 QColor(QStringLiteral("#cc4400")));

        // And nowhere else.
        QVERIFY(marks.marksForBlock(1).isEmpty());
        QVERIFY(marks.marksForBlock(3).isEmpty());
    }

    void oneChannelAloneIsEnoughAndTheOtherComesBackEmpty()
    {
        DocumentBlockMarks marks;
        const QString washOnly =
            marks.add(0, 0, 3, QColor(QStringLiteral("#5533aa")));
        const QString outlineOnly =
            marks.add(0, 4, 3, QColor(), QColor(QStringLiteral("#cc4400")));
        QVERIFY(!washOnly.isEmpty());
        QVERIFY(!outlineOnly.isEmpty());

        const QVariantList entries = marks.marksForBlock(0);
        QCOMPARE(entries.size(), 2);
        QVERIFY(entries.at(0).toMap().value(QStringLiteral("outline"))
                    .toString().isEmpty());
        QVERIFY(!entries.at(0).toMap().value(QStringLiteral("wash"))
                     .toString().isEmpty());
        QVERIFY(entries.at(1).toMap().value(QStringLiteral("wash"))
                    .toString().isEmpty());
        QVERIFY(!entries.at(1).toMap().value(QStringLiteral("outline"))
                     .toString().isEmpty());
    }

    // An entry with no channel to draw through, and an entry covering no
    // characters, are both a caller's mistake rather than a state to carry.
    void anEntryThatCouldNotBeDrawnIsRefused()
    {
        DocumentBlockMarks marks;
        QSignalSpy changed(&marks, &DocumentBlockMarks::changed);

        QVERIFY(marks.add(0, 0, 4, QColor(), QColor()).isEmpty());
        QVERIFY(marks.add(0, 0, 0, QColor(QStringLiteral("#5533aa"))).isEmpty());
        QVERIFY(marks.add(0, 0, -3, QColor(QStringLiteral("#5533aa"))).isEmpty());

        QCOMPARE(marks.count(), 0);
        QCOMPARE(changed.count(), 0);
    }

    // Registration order is the order they paint in, which decides which of
    // two washes on the same characters ends up underneath.
    void marksOnOneBlockKeepRegistrationOrder()
    {
        DocumentBlockMarks marks;
        const QString first =
            marks.add(1, 0, 5, QColor(QStringLiteral("#5533aa")));
        const QString second =
            marks.add(1, 2, 5, QColor(QStringLiteral("#22aa55")));

        const QVariantList entries = marks.marksForBlock(1);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).toMap().value(QStringLiteral("id")).toString(),
                 first);
        QCOMPARE(entries.at(1).toMap().value(QStringLiteral("id")).toString(),
                 second);
    }

    void aMarkKeepsItsIdAcrossAMove()
    {
        DocumentBlockMarks marks;
        const QString id = marks.add(0, 3, 4, QColor(QStringLiteral("#5533aa")));
        const int revisionAfterAdd = marks.revision();

        QVERIFY(marks.move(id, 2, 0, 6));
        QVERIFY(marks.revision() > revisionAfterAdd);
        QVERIFY(marks.marksForBlock(0).isEmpty());
        const QVariantMap moved = marks.mark(id);
        QCOMPARE(moved.value(QStringLiteral("block")).toInt(), 2);
        QCOMPARE(moved.value(QStringLiteral("start")).toInt(), 0);
        QCOMPARE(moved.value(QStringLiteral("length")).toInt(), 6);

        // A move that changes nothing reports success and bumps nothing, so a
        // caller re-placing every mark on every document change does not
        // re-paint the surface for the ones that did not move.
        const int settled = marks.revision();
        QVERIFY(marks.move(id, 2, 0, 6));
        QCOMPARE(marks.revision(), settled);

        // Registering a zero-length mark is refused; moving one to zero is
        // not. A mark whose text the reader has just deleted is an ordinary
        // state for a caller to keep an id through until it re-places it.
        QVERIFY(marks.move(id, 2, 0, 0));
        QCOMPARE(marks.mark(id).value(QStringLiteral("length")).toInt(), 0);
        QCOMPARE(marks.count(), 1);

        // A negative offset clamps rather than being taken literally.
        QVERIFY(marks.move(id, 0, -5, 3));
        QCOMPARE(marks.mark(id).value(QStringLiteral("start")).toInt(), 0);

        QVERIFY(!marks.move(QStringLiteral("mark-nope"), 0, 0, 1));
    }

    void colorsChangeWithoutRebuildingTheEntry()
    {
        DocumentBlockMarks marks;
        const QString id = marks.add(0, 0, 4, QColor(QStringLiteral("#5533aa")));

        QVERIFY(marks.setColors(id, QColor(QStringLiteral("#22aa55")),
                                QColor(QStringLiteral("#cc4400"))));
        const QVariantMap entry = marks.mark(id);
        QCOMPARE(QColor(entry.value(QStringLiteral("wash")).toString()),
                 QColor(QStringLiteral("#22aa55")));
        QCOMPARE(QColor(entry.value(QStringLiteral("outline")).toString()),
                 QColor(QStringLiteral("#cc4400")));

        // Taking both channels away would leave an entry nothing could draw,
        // so it is refused and the entry keeps the colours it had.
        QVERIFY(!marks.setColors(id, QColor(), QColor()));
        QCOMPARE(QColor(marks.mark(id).value(QStringLiteral("wash")).toString()),
                 QColor(QStringLiteral("#22aa55")));

        QVERIFY(!marks.setColors(QStringLiteral("mark-nope"),
                                 QColor(QStringLiteral("#22aa55")), QColor()));
    }

    void removingAndClearingTakeEntriesBack()
    {
        DocumentBlockMarks marks;
        const QString first =
            marks.add(0, 0, 4, QColor(QStringLiteral("#5533aa")));
        marks.add(1, 0, 4, QColor(QStringLiteral("#5533aa")));

        QVERIFY(marks.remove(first));
        QCOMPARE(marks.count(), 1);
        QVERIFY(marks.marksForBlock(0).isEmpty());
        QVERIFY(!marks.remove(first));

        const int before = marks.revision();
        marks.clear();
        QCOMPARE(marks.count(), 0);
        QVERIFY(!marks.isActive());
        QVERIFY(marks.revision() > before);

        // Clearing an empty registry changes nothing, so a caller that clears
        // before every re-place does not make a surface re-paint for it.
        const int settled = marks.revision();
        marks.clear();
        QCOMPARE(marks.revision(), settled);
    }

    // The difference from DocumentDecorations, whose spans are per window
    // because the note is: these belong to the instance, so two surfaces on
    // screen at once mark their own documents.
    void twoRegistriesShareNothing()
    {
        DocumentBlockMarks first;
        DocumentBlockMarks second;

        const QString id = first.add(0, 0, 4, QColor(QStringLiteral("#5533aa")));

        QCOMPARE(first.count(), 1);
        QCOMPARE(second.count(), 0);
        QVERIFY(!second.isActive());
        QVERIFY(second.marksForBlock(0).isEmpty());
        QVERIFY(second.mark(id).isEmpty());
        QVERIFY(!second.remove(id));
    }
};

QTEST_APPLESS_MAIN(TestDocumentBlockMarks)
#include "test_documentblockmarks.moc"
