// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "documentcompare.h"

namespace {

// A comparison as a readable list of "block start length" triples, so a case
// states the answer it expects rather than unpacking maps three times.
QStringList triples(const QVariantList &ranges)
{
    QStringList out;
    for (const QVariant &value : ranges) {
        const QVariantMap range = value.toMap();
        out << QStringLiteral("%1 %2 %3")
                   .arg(range.value(QStringLiteral("block")).toInt())
                   .arg(range.value(QStringLiteral("start")).toInt())
                   .arg(range.value(QStringLiteral("length")).toInt());
    }
    return out;
}

} // namespace

// What differs between two markdown documents.
//
// The consumer is the backup dialog: it draws a stored version of the open note
// and washes the part of it the note no longer has, so a reader can tell two
// edits of the same afternoon apart. The answer is in the coordinates a marked
// range is addressed in — block index, and offset in that block's DISPLAY text,
// the text with the inline markers taken out — which is what the cases below
// pin down alongside the alignment itself.
class TestDocumentCompare : public QObject
{
    Q_OBJECT

private slots:
    void twoIdenticalDocumentsDifferNowhere()
    {
        const QString doc = QStringLiteral("# Title\n\nA paragraph.\n");
        QVERIFY(DocumentCompare::changedRanges(doc, doc).isEmpty());
    }

    void anEmptyBaselineMakesEveryBlockNew()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("First.\n\nSecond one.\n"), QString());
        QCOMPARE(triples(ranges),
                 (QStringList{QStringLiteral("0 0 6"),
                              QStringLiteral("1 0 11")}));
    }

    // The reason the answer is in display coordinates: the same word sits at a
    // different offset in the markdown, so a range measured there would wash
    // the wrong characters. "This is **bold** text" draws as "This is bold
    // text", putting "bold" at display 8 and at markdown 10.
    void aChangedRunIsMeasuredInDisplayText()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("This is **bold** text\n"),
            QStringLiteral("This is **wide** text\n"));
        QCOMPARE(triples(ranges), (QStringList{QStringLiteral("0 8 4")}));
    }

    // The common prefix and the common suffix are taken off, so the mark lands
    // on the words that changed rather than on the whole paragraph.
    void onlyTheRunBetweenTheCommonEndsIsReported()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("the second draft of the report\n"),
            QStringLiteral("the final draft of the report\n"));
        QCOMPARE(ranges.size(), 1);
        const QVariantMap range = ranges.first().toMap();
        QCOMPARE(range.value(QStringLiteral("block")).toInt(), 0);
        const QString text = QStringLiteral("the second draft of the report");
        QCOMPARE(text.mid(range.value(QStringLiteral("start")).toInt(),
                          range.value(QStringLiteral("length")).toInt()),
                 QStringLiteral("second"));
    }

    // A block whose text the baseline extended has no character of its own
    // that differs, so there is nothing in it to mark. Stated as a case
    // because it is the one place a reader might expect a mark and get none.
    void textTheBaselineOnlyAddedToIsNotMarked()
    {
        QVERIFY(DocumentCompare::changedRanges(
                    QStringLiteral("A short line\n"),
                    QStringLiteral("A short line with more on the end\n"))
                    .isEmpty());
    }

    // The alignment, and the reason for it: a paragraph inserted into the
    // baseline shifts every block after it, and a positional comparison would
    // report the whole rest of the document as changed.
    void anInsertedBlockShiftsNothingAfterIt()
    {
        const QString stored =
            QStringLiteral("First.\n\nSecond.\n\nThird.\n");
        const QString current =
            QStringLiteral("First.\n\nInserted.\n\nSecond.\n\nThird.\n");
        QVERIFY(DocumentCompare::changedRanges(stored, current).isEmpty());

        // And the other direction: the block the baseline does not have is the
        // only one reported.
        QCOMPARE(triples(DocumentCompare::changedRanges(current, stored)),
                 QStringList{QStringLiteral("1 0 9")});
    }

    // A structural change leaves the content string alone, so comparing text
    // alone would call these blocks equal. A reader restoring an old version
    // cares that the line used to be a heading.
    void aChangeOfKindOrStateCountsAsAChange()
    {
        QCOMPARE(triples(DocumentCompare::changedRanges(
                     QStringLiteral("# Overview\n"),
                     QStringLiteral("Overview\n"))),
                 QStringList{QStringLiteral("0 0 8")});
        QCOMPARE(triples(DocumentCompare::changedRanges(
                     QStringLiteral("- [ ] buy milk\n"),
                     QStringLiteral("- [x] buy milk\n"))),
                 QStringList{QStringLiteral("0 0 8")});
    }

    // A fence's content is its own text, markers and all, so a change inside
    // one is measured against the source rather than against a rendering of
    // it.
    void aFenceIsComparedAsItsSource()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("```txt\nkeep **this** literal\n```\n"),
            QStringLiteral("```txt\nkeep **that** literal\n```\n"));
        QCOMPARE(ranges.size(), 1);
        const QVariantMap range = ranges.first().toMap();
        const QString source = QStringLiteral("keep **this** literal");
        QCOMPARE(source.mid(range.value(QStringLiteral("start")).toInt(),
                            range.value(QStringLiteral("length")).toInt()),
                 QStringLiteral("is"));
    }

    // A divider and a picture hold no text a character range can address, so a
    // change to one is not reported here. The blocks around it still are.
    void aBlockWithNoTextIsNotReported()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("![Chart](old.png)\n\n---\n\nAfter the rule.\n"),
            QStringLiteral("![Chart](new.png)\n\n***\n\nAfter the rule.\n"));
        QVERIFY(triples(ranges).isEmpty());
    }

    // Two documents with nothing in common: every block of the first is new,
    // paired off against the second's in order so the comparison degrades to
    // per-block text differences rather than reporting nothing.
    void twoUnrelatedDocumentsReportEveryBlock()
    {
        const QVariantList ranges = DocumentCompare::changedRanges(
            QStringLiteral("alpha\n\nbeta\n"),
            QStringLiteral("gamma\n\ndelta\n"));
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges.at(0).toMap().value(QStringLiteral("block")).toInt(), 0);
        QCOMPARE(ranges.at(1).toMap().value(QStringLiteral("block")).toInt(), 1);
    }

    // A long note with one edited paragraph. The comparison has to stay cheap
    // for that case, which is the one it is actually asked for: the common
    // prefix and suffix come off before any alignment work happens.
    void aLongNoteWithOneEditReportsOneRange()
    {
        QString stored;
        for (int i = 0; i < 400; ++i)
            stored += QStringLiteral("Paragraph %1 of the note.\n\n").arg(i);
        QString current = stored;
        current.replace(QStringLiteral("Paragraph 200 of the note."),
                        QStringLiteral("Paragraph 200 of the report."));

        QElapsedTimer timer;
        timer.start();
        const QVariantList ranges =
            DocumentCompare::changedRanges(stored, current);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges.first().toMap().value(QStringLiteral("block")).toInt(),
                 200);
        // Generous by two orders of magnitude: this is here to fail if the
        // trimming stops happening and the whole document goes through the
        // alignment table, not to measure anything.
        QVERIFY2(elapsed < 2000, qPrintable(QStringLiteral("took %1 ms")
                                                .arg(elapsed)));
    }
};

QTEST_APPLESS_MAIN(TestDocumentCompare)
#include "test_documentcompare.moc"
