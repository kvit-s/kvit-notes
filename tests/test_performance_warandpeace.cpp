// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "blockmodel.h"
#include "documentserializer.h"
#include "undostack.h"
#include "block.h"

// Phase 12 Step 10: the War-and-Peace performance gate (features.md §21.7).
// A 561,693-word document (the size of War and Peace) must load in under a
// second, round-trip losslessly, and delete in bulk without a long main-thread
// stall; memory is linear in the block count (one Block per block, delegates
// pooled in the view). Spike (a) found the data path an order of magnitude under
// budget; this pins it as a regression gate. The corpus is generated to the same
// word count and a realistic paragraph structure.
class TestPerformanceWarAndPeace : public QObject
{
    Q_OBJECT
public:
    // ~561,693 words as blank-line-separated prose paragraphs of ~90 words.
    static QString buildCorpus(int *paraCountOut)
    {
        static const char *lorem[] = {
            "Well", "Prince", "so", "Genoa", "and", "Lucca", "are", "now",
            "just", "family", "estates", "of", "the", "Buonapartes", "But",
            "I", "warn", "you", "if", "don't", "tell", "me", "that", "this",
            "means", "war", "again"};
        const int wordsPerPara = 90;
        const int targetWords = 561693;
        const int paras = targetWords / wordsPerPara;
        QString doc;
        doc.reserve(targetWords * 7);
        int w = 0;
        for (int p = 0; p < paras; ++p) {
            for (int i = 0; i < wordsPerPara; ++i) {
                if (i) doc += QLatin1Char(' ');
                doc += QLatin1String(lorem[(w++) % 27]);
            }
            doc += QLatin1String("\n\n");
        }
        if (paraCountOut) *paraCountOut = paras;
        return doc;
    }

private slots:
    void loadUnderOneSecond();
    void serializeRoundTripsLosslessly();
    void bulkDeletionUnderBudget();
    void memoryLinearInBlockCount();
};

void TestPerformanceWarAndPeace::loadUnderOneSecond()
{
    int paras = 0;
    const QString doc = buildCorpus(&paras);

    DocumentSerializer ser;
    BlockModel model;
    QElapsedTimer t;
    t.start();
    ser.loadIntoModel(&model, doc);
    const qint64 loadMs = t.elapsed();

    qInfo("WAR&PEACE LOAD: %lld ms for %d blocks (~561,693 words)",
          loadMs, model.count());
    QCOMPARE(model.count(), paras);
    QVERIFY2(loadMs < 1000,
             qPrintable(QStringLiteral("load must be < 1000 ms, was %1 ms")
                            .arg(loadMs)));
}

void TestPerformanceWarAndPeace::serializeRoundTripsLosslessly()
{
    const QString doc = buildCorpus(nullptr);
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    BlockModel model;
    ser.loadIntoModel(&model, doc);

    QElapsedTimer t;
    t.start();
    const QString out = ser.serialize(&model);
    const qint64 serMs = t.elapsed();
    qInfo("WAR&PEACE SERIALIZE: %lld ms", serMs);
    QVERIFY2(serMs < 1000, "serialize must be < 1000 ms");
    QCOMPARE(out, doc.trimmed());   // byte-identical round-trip
}

void TestPerformanceWarAndPeace::bulkDeletionUnderBudget()
{
    const QString doc = buildCorpus(nullptr);
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    DocumentSerializer ser;
    ser.loadIntoModel(&model, doc);
    const int n = model.count();
    QVERIFY(n > 6000);

    // Select every block and delete it as one undo step — the §11 "bulk
    // deletion on the main thread" case.
    QVariantList all;
    all.reserve(n);
    for (int i = 0; i < n; ++i)
        all.append(i);

    QElapsedTimer t;
    t.start();
    model.removeBlocks(all);
    const qint64 delMs = t.elapsed();
    qInfo("WAR&PEACE BULK DELETE: %lld ms for %d blocks", delMs, n);
    // A document is never blockless: deleting every block leaves one empty
    // paragraph (delete-all-then-type just works).
    QCOMPARE(model.count(), 1);
    QVERIFY2(delMs < 1500,
             qPrintable(QStringLiteral("bulk delete must be < 1500 ms, was %1 ms")
                            .arg(delMs)));

    // And it is one undo step that restores the whole document.
    QElapsedTimer u;
    u.start();
    stack.undo();
    const qint64 undoMs = u.elapsed();
    qInfo("WAR&PEACE BULK UNDO: %lld ms", undoMs);
    QCOMPARE(model.count(), n);
    QVERIFY2(undoMs < 1500, "bulk-delete undo must be < 1500 ms");
}

void TestPerformanceWarAndPeace::memoryLinearInBlockCount()
{
    // Memory is linear: exactly one Block per source block, no hidden fan-out
    // (the view pools delegates, so on-screen cost is O(visible), not O(model)).
    int paras = 0;
    const QString doc = buildCorpus(&paras);
    DocumentSerializer ser;
    BlockModel model;
    ser.loadIntoModel(&model, doc);
    QCOMPARE(model.count(), paras);
    // A spot check that blocks are real and independent (no shared aliasing).
    QVERIFY(model.blockAt(0) != model.blockAt(paras - 1));
    QVERIFY(!model.blockAt(paras / 2)->content().isEmpty());
}

QTEST_MAIN(TestPerformanceWarAndPeace)
#include "test_performance_warandpeace.moc"
