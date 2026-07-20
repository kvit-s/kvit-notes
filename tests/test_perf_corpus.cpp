// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "notecollection.h"
#include "perf/corpus.h"

#include <QTemporaryDir>

class TestPerfCorpus : public QObject
{
    Q_OBJECT

private slots:
    void warAndPeaceShape();
    void otherDocumentShapes();
    void vault10KShape();
};

void TestPerfCorpus::warAndPeaceShape()
{
    const PerfCorpus::DocumentFixture wp = PerfCorpus::warAndPeace();
    QCOMPARE(PerfCorpus::countedBlocks(wp.markdown), 6241);
    QCOMPARE(PerfCorpus::countedHeadings(wp.markdown), 382);
    QCOMPARE(PerfCorpus::countedWords(wp.markdown), 562900);

    const PerfCorpus::DocumentFixture live = PerfCorpus::warAndPeaceLiveSized();
    QCOMPARE(PerfCorpus::countedBlocks(live.markdown), 11752);
    QCOMPARE(PerfCorpus::countedHeadings(live.markdown), 383);
    QCOMPARE(PerfCorpus::countedWords(live.markdown), 562365);

    const PerfCorpus::DocumentFixture synth = PerfCorpus::warAndPeaceSynth();
    QCOMPARE(PerfCorpus::countedBlocks(synth.markdown), 6241);
    QCOMPARE(PerfCorpus::countedHeadings(synth.markdown), 0);
    QCOMPARE(PerfCorpus::countedWords(synth.markdown), 561690);
}

void TestPerfCorpus::otherDocumentShapes()
{
    const PerfCorpus::DocumentFixture headings = PerfCorpus::headings2K();
    QCOMPARE(PerfCorpus::countedBlocks(headings.markdown), 4000);
    QCOMPARE(PerfCorpus::countedHeadings(headings.markdown), 2000);

    const PerfCorpus::DocumentFixture list = PerfCorpus::list5K();
    QCOMPARE(PerfCorpus::countedBlocks(list.markdown), 5000);
    QCOMPARE(PerfCorpus::countedHeadings(list.markdown), 0);

    const PerfCorpus::DocumentFixture mixed = PerfCorpus::mixed100();
    QCOMPARE(PerfCorpus::countedBlocks(mixed.markdown), 100);
    QCOMPARE(PerfCorpus::countedHeadings(mixed.markdown), 10);
}

void TestPerfCorpus::vault10KShape()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PerfCorpus::writeVault10K(dir.path()), 10003);

    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));
    QCOMPARE(collection.noteCount(), 10003);
}

QTEST_MAIN(TestPerfCorpus)
#include "test_perf_corpus.moc"
