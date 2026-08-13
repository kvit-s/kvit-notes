// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "block.h"
#include "blockmodel.h"
#include "documentheights.h"

// The document-height table: the arithmetic over the heights the block list
// measured, and what the table does when the document under it changes.
//
// The table exists because a virtualized list forgets a row's height as soon
// as it recycles it, so its estimate of the document's height is recomputed
// from whichever rows are built at that moment and moves on every notch of
// the wheel. What is asserted here is the property that fixes it: the sample
// only grows, so the estimate settles rather than oscillating, and it is
// exact once every block has been measured once. The scrollbar drawn from it
// is measured in the shell suite, where there are rows to measure.
class TestDocumentHeights : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_model = new BlockModel(this);
        m_heights = new DocumentHeights(this);
        m_heights->setModel(m_model);
        for (int i = 0; i < 5; ++i) {
            m_model->insertBlock(i, Block::Paragraph,
                                 QStringLiteral("block %1").arg(i));
        }
    }

    void cleanup()
    {
        delete m_heights;
        m_heights = nullptr;
        delete m_model;
        m_model = nullptr;
    }

    // ---- what an empty table answers ----

    void testAnUnmeasuredDocumentAnswersNothing()
    {
        QCOMPARE(m_heights->count(), 5);
        QCOMPARE(m_heights->measuredCount(), 0);
        QVERIFY(!m_heights->isReady());
        QCOMPARE(m_heights->totalHeight(), 0.0);
        QCOMPARE(m_heights->averageHeight(), 0.0);
        QCOMPARE(m_heights->heightOf(2), 0.0);
        QVERIFY(!m_heights->isMeasured(2));
    }

    void testATableWithNoModelIsSafeToAsk()
    {
        DocumentHeights orphan;
        QCOMPARE(orphan.count(), 0);
        QCOMPARE(orphan.totalHeight(), 0.0);
        QCOMPARE(orphan.offsetOf(3), 0.0);
        QCOMPARE(orphan.blockAt(120), -1);
        orphan.recordHeight(0, 30, 600);
        QCOMPARE(orphan.measuredCount(), 0);
    }

    // ---- the estimate ----

    void testAnUnmeasuredBlockIsWorthTheMeanOfTheMeasuredOnes()
    {
        m_heights->recordHeight(0, 20, 600);
        m_heights->recordHeight(1, 40, 600);
        QCOMPARE(m_heights->averageHeight(), 30.0);
        QCOMPARE(m_heights->heightOf(0), 20.0);
        QCOMPARE(m_heights->heightOf(4), 30.0);
        QVERIFY(m_heights->isMeasured(0));
        QVERIFY(!m_heights->isMeasured(4));
        // Three measured heights and two estimates of 30.
        QCOMPARE(m_heights->totalHeight(), 20.0 + 40.0 + 30.0 * 3);
    }

    // A block is estimated from the measured blocks SHAPED like it — drawn
    // the same way and holding about as much — because a document's rows
    // differ by a factor of eight and the first screenful is a small sample.
    // A plain mean over everything answers 30 for both of these.
    void testABlockIsEstimatedFromTheOnesShapedLikeIt()
    {
        BlockModel *model = m_model;
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Heading1, QStringLiteral("A heading"));
        model->insertBlock(1, Block::Paragraph, QString(2000, QLatin1Char('x')));
        model->insertBlock(2, Block::Heading1, QStringLiteral("Another heading"));
        model->insertBlock(3, Block::Paragraph, QString(2000, QLatin1Char('y')));

        m_heights->recordHeight(0, 40, 600);
        m_heights->recordHeight(1, 400, 600);
        QCOMPARE(m_heights->heightOf(2), 40.0);
        QCOMPARE(m_heights->heightOf(3), 400.0);
        // The plain mean is still there, and still what a block of a kind
        // nothing has been measured for is worth.
        QCOMPARE(m_heights->averageHeight(), 220.0);
    }

    void testAKindNothingHasBeenMeasuredForFallsBackToEverything()
    {
        BlockModel *model = m_model;
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Paragraph, QStringLiteral("one line"));
        model->insertBlock(1, Block::Paragraph, QStringLiteral("one line too"));
        model->insertBlock(2, Block::Table,
                           QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |"));

        m_heights->recordHeight(0, 30, 600);
        m_heights->recordHeight(1, 50, 600);
        // Nothing shaped like a table has been measured, and neither has
        // anything drawn like one, so the whole sample is the answer.
        QCOMPARE(m_heights->heightOf(2), 40.0);
        QCOMPARE(m_heights->estimateFor(2), 40.0);

        m_heights->recordHeight(2, 200, 600);
        QCOMPARE(m_heights->heightOf(2), 200.0);
        // And the paragraphs are unmoved by a table having been measured.
        QCOMPARE(m_heights->estimateFor(0), 40.0);
    }

    // A block whose content changed is estimated as what it is now, not as
    // what it was: the shape goes with the height.
    void testEditingABlockForgetsItsShapeToo()
    {
        BlockModel *model = m_model;
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Paragraph, QStringLiteral("one line"));
        model->insertBlock(1, Block::Paragraph, QString(2000, QLatin1Char('x')));
        model->insertBlock(2, Block::Paragraph, QStringLiteral("short"));
        m_heights->recordHeight(0, 30, 600);
        m_heights->recordHeight(1, 400, 600);
        m_heights->recordHeight(2, 30, 600);
        QCOMPARE(m_heights->measuredCount(), 3);

        model->updateContent(2, QString(2000, QLatin1Char('z')));
        QVERIFY(!m_heights->isMeasured(2));
        QCOMPARE(m_heights->heightOf(2), 400.0);
    }

    void testAMeasuredBlockKeepsItsHeightAsOthersArrive()
    {
        m_heights->recordHeight(0, 18, 600);
        m_heights->recordHeight(3, 210, 600);
        QCOMPARE(m_heights->heightOf(0), 18.0);
        QCOMPARE(m_heights->heightOf(3), 210.0);
        // Only the blocks nobody has measured move when the sample does.
        QCOMPARE(m_heights->heightOf(1), 114.0);
    }

    // The property the whole table is for. A virtualized list estimates from
    // the rows it has built at that moment, so a row leaving the window takes
    // its height back out of the sample and the estimate moves on every notch
    // of the wheel — in both directions, since the walk back up teaches it
    // nothing it kept. Here the sample only grows, so reading the note
    // through makes the total exact and reading back up moves nothing at all.
    void testReadingTheNoteThroughSettlesTheTotal()
    {
        const QList<qreal> real{18, 96, 42, 210, 60};
        qreal exact = 0;
        for (qreal height : real)
            exact += height;

        // Down the note, three rows built at a time.
        for (int top = 0; top + 3 <= real.size(); ++top) {
            for (int block = top; block < top + 3; ++block)
                m_heights->recordHeight(block, real.at(block), 600);
            QCOMPARE(m_heights->measuredCount(), top + 3);
        }
        QCOMPARE(m_heights->totalHeight(), exact);
        QVERIFY(m_heights->isReady());

        // And back up: every height reported on the way is one the table
        // already holds, so nothing moves and no consumer is woken.
        QSignalSpy spy(m_heights, &DocumentHeights::revisionChanged);
        for (int top = int(real.size()) - 3; top >= 0; --top) {
            for (int block = top; block < top + 3; ++block)
                m_heights->recordHeight(block, real.at(block), 600);
            QCOMPARE(m_heights->totalHeight(), exact);
        }
        QCOMPARE(spy.count(), 0);
    }

    void testSpacingIsPartOfTheDocumentButNotOfARow()
    {
        m_heights->setSpacing(8);
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);
        QCOMPARE(m_heights->heightOf(3), 20.0);
        // Four gaps between five rows, and none after the last.
        QCOMPARE(m_heights->totalHeight(), 5 * 20.0 + 4 * 8.0);
        QCOMPARE(m_heights->offsetOf(0), 0.0);
        QCOMPARE(m_heights->offsetOf(1), 28.0);
        QCOMPARE(m_heights->offsetOf(5), m_heights->totalHeight());
    }

    void testChangingTheSpacingKeepsTheMeasurements()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);
        m_heights->setSpacing(10);
        QCOMPARE(m_heights->measuredCount(), 5);
        QCOMPARE(m_heights->totalHeight(), 5 * 20.0 + 4 * 10.0);
    }

    // ---- the two directions of the same map ----

    void testOffsetsAndBlockAtAreInverses()
    {
        m_heights->setSpacing(4);
        const QList<qreal> real{18, 96, 42, 210, 60};
        for (int block = 0; block < real.size(); ++block)
            m_heights->recordHeight(block, real.at(block), 600);

        for (int block = 0; block < real.size(); ++block) {
            const qreal top = m_heights->offsetOf(block);
            QCOMPARE(m_heights->blockAt(top), block);
            QCOMPARE(m_heights->blockAt(top + real.at(block) / 2), block);
        }
        // Clamped at both ends: the scrollbar drags to a fraction of a total
        // the table is itself estimating, and the fraction can land outside.
        QCOMPARE(m_heights->blockAt(-500), 0);
        QCOMPARE(m_heights->blockAt(m_heights->totalHeight() + 500), 4);
    }

    void testOffsetsUseTheEstimateForUnmeasuredBlocks()
    {
        m_heights->recordHeight(0, 50, 600);
        m_heights->recordHeight(4, 30, 600);
        // Blocks 1..3 are worth the mean of 50 and 30.
        QCOMPARE(m_heights->offsetOf(1), 50.0);
        QCOMPARE(m_heights->offsetOf(4), 50.0 + 3 * 40.0);
    }

    // ---- following the model ----

    void testInsertingABlockMovesTheEntriesBelowIt()
    {
        m_heights->recordHeight(0, 10, 600);
        m_heights->recordHeight(1, 20, 600);
        m_heights->recordHeight(2, 30, 600);

        m_model->insertBlock(1, Block::Paragraph, QStringLiteral("new"));
        QCOMPARE(m_heights->count(), 6);
        QCOMPARE(m_heights->heightOf(0), 10.0);
        QVERIFY(!m_heights->isMeasured(1));
        QCOMPARE(m_heights->heightOf(2), 20.0);
        QCOMPARE(m_heights->heightOf(3), 30.0);
    }

    void testRemovingABlockTakesItsEntry()
    {
        m_heights->recordHeight(0, 10, 600);
        m_heights->recordHeight(1, 20, 600);
        m_heights->recordHeight(2, 30, 600);

        m_model->removeBlock(1);
        QCOMPARE(m_heights->count(), 4);
        QCOMPARE(m_heights->measuredCount(), 2);
        QCOMPARE(m_heights->heightOf(0), 10.0);
        QCOMPARE(m_heights->heightOf(1), 30.0);
        // And the removed height is out of the sample the estimate is over.
        QCOMPARE(m_heights->averageHeight(), 20.0);
    }

    void testMovingABlockMovesItsEntry()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 10 * (block + 1), 600);

        m_model->moveBlock(0, 3);
        QCOMPARE(m_heights->count(), 5);
        QCOMPARE(m_heights->heightOf(0), 20.0);
        QCOMPARE(m_heights->heightOf(1), 30.0);
        QCOMPARE(m_heights->heightOf(2), 40.0);
        QCOMPARE(m_heights->heightOf(3), 10.0);
        QCOMPARE(m_heights->heightOf(4), 50.0);
        QCOMPARE(m_heights->measuredCount(), 5);
    }

    void testEditingABlockDropsItsEntry()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);

        m_model->updateContent(2, QStringLiteral("a much longer paragraph"));
        QVERIFY(!m_heights->isMeasured(2));
        QCOMPARE(m_heights->measuredCount(), 4);
        // The other four are untouched, and the dropped one is estimated
        // from them.
        QCOMPARE(m_heights->heightOf(2), 20.0);
    }

    // A change that cannot move a row's height keeps it. BlockModel renumbers
    // equations across the whole suffix of a document on any edit that could
    // have moved one, and an ordered list across its run; dropping those
    // would empty the table on every keystroke in exactly the notes whose
    // rows are unequal enough to need it.
    void testAChangeThatCannotMoveAHeightKeepsIt()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);

        const QModelIndex first = m_model->index(0, 0);
        const QModelIndex last = m_model->index(4, 0);
        emit m_model->dataChanged(first, last, {BlockModel::MathNumberRole});
        emit m_model->dataChanged(first, last, {BlockModel::OrdinalRole});
        emit m_model->dataChanged(first, last, {BlockModel::CheckedRole});
        QCOMPARE(m_heights->measuredCount(), 5);

        // Naming no role at all means everything about those rows, which
        // includes their content.
        emit m_model->dataChanged(first, last, {});
        QCOMPARE(m_heights->measuredCount(), 0);
    }

    void testANewDocumentEmptiesTheTable()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);

        m_model->clear();
        QCOMPARE(m_heights->count(), 0);
        QCOMPARE(m_heights->measuredCount(), 0);
        QCOMPARE(m_heights->totalHeight(), 0.0);

        for (int i = 0; i < 3; ++i)
            m_model->insertBlock(i, Block::Paragraph, QStringLiteral("fresh"));
        QCOMPARE(m_heights->count(), 3);
        QCOMPARE(m_heights->measuredCount(), 0);
    }

    void testTheTableSurvivesTheModelItProjects()
    {
        auto *model = new BlockModel;
        DocumentHeights heights;
        heights.setModel(model);
        model->insertBlock(0, Block::Paragraph, QStringLiteral("one"));
        heights.recordHeight(0, 40, 600);
        delete model;
        QCOMPARE(heights.count(), 0);
        QCOMPARE(heights.totalHeight(), 0.0);
        QCOMPARE(heights.model(), nullptr);
    }

    // ---- one layout's worth of heights, never two ----

    void testAMeasurementAtANewWidthEmptiesTheTable()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);
        QCOMPARE(m_heights->measuredWidth(), 600.0);

        // The window, a panel, the maximum content width or focus mode: all
        // of them arrive here as a row reporting its height at another width.
        m_heights->recordHeight(0, 34, 420);
        QCOMPARE(m_heights->measuredWidth(), 420.0);
        QCOMPARE(m_heights->measuredCount(), 1);
        QCOMPARE(m_heights->heightOf(0), 34.0);
        QCOMPARE(m_heights->heightOf(1), 34.0);
        QCOMPARE(m_heights->count(), 5);
    }

    void testClearForgetsTheMeasurementsAndKeepsTheRows()
    {
        for (int block = 0; block < 5; ++block)
            m_heights->recordHeight(block, 20, 600);
        m_heights->clear();
        QCOMPARE(m_heights->count(), 5);
        QCOMPARE(m_heights->measuredCount(), 0);
        QVERIFY(!m_heights->isReady());
        QCOMPARE(m_heights->totalHeight(), 0.0);
    }

    void testARowThatWasNotLaidOutIsNotAMeasurement()
    {
        m_heights->recordHeight(0, 40, 0);
        m_heights->recordHeight(1, -1, 600);
        m_heights->recordHeight(-1, 40, 600);
        m_heights->recordHeight(99, 40, 600);
        QCOMPARE(m_heights->measuredCount(), 0);
    }

    // ---- what wakes the consumers ----

    void testRecordingTheSameHeightChangesNothing()
    {
        m_heights->recordHeight(0, 20, 600);
        QSignalSpy spy(m_heights, &DocumentHeights::revisionChanged);
        m_heights->recordHeight(0, 20, 600);
        // Sub-pixel jitter is the same height: a row re-reports its geometry
        // on every relayout, and each of those would otherwise wake every
        // consumer for a height that has not moved.
        m_heights->recordHeight(0, 20.001, 600);
        QCOMPARE(spy.count(), 0);

        m_heights->recordHeight(0, 26, 600);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m_heights->measuredCount(), 1);
        QCOMPARE(m_heights->heightOf(0), 26.0);
    }

    void testEveryChangeToTheTableBumpsTheRevision()
    {
        QSignalSpy spy(m_heights, &DocumentHeights::revisionChanged);
        m_heights->recordHeight(0, 20, 600);
        QCOMPARE(spy.count(), 1);
        m_heights->setSpacing(6);
        QCOMPARE(spy.count(), 2);
        m_model->insertBlock(0, Block::Paragraph, QStringLiteral("new"));
        QCOMPARE(spy.count(), 3);
        m_heights->clear();
        QCOMPARE(spy.count(), 4);
        // And nothing to forget is not a change.
        m_heights->clear();
        QCOMPARE(spy.count(), 4);
    }

private:
    BlockModel *m_model = nullptr;
    DocumentHeights *m_heights = nullptr;
};

QTEST_MAIN(TestDocumentHeights)
#include "test_documentheights.moc"
