// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"

// Size-cap validation corpora for the oversized-file guard:
// synthetic 10/25/50 MB documents — prose-shaped and one-line-shaped —
// measuring load (parse + model build), full serialize, and a journal-style
// write. The default cap (10 MiB) is validated against felt-latency budgets
// of ~1 s load and ~250 ms serialize; the measured numbers are recorded in
// the plan's guard section. Assertions here are deliberately loose sanity
// bounds (CI machines vary); the value of the suite is the printed numbers.

class TestPerformanceBigFiles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void measureCorpora_data();
    void measureCorpora();

private:
    QTemporaryDir m_dir;
};

void TestPerformanceBigFiles::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TestPerformanceBigFiles::measureCorpora_data()
{
    QTest::addColumn<int>("megabytes");
    QTest::addColumn<bool>("oneLine");
    QTest::newRow("prose-10MB") << 10 << false;
    QTest::newRow("prose-25MB") << 25 << false;
    QTest::newRow("prose-50MB") << 50 << false;
    QTest::newRow("oneline-10MB") << 10 << true;
    QTest::newRow("oneline-25MB") << 25 << true;
    QTest::newRow("oneline-50MB") << 50 << true;
}

void TestPerformanceBigFiles::measureCorpora()
{
    QFETCH(int, megabytes);
    QFETCH(bool, oneLine);

    const qint64 target = qint64(megabytes) * 1024 * 1024;

    // Prose shape: paragraphs with headings and lists sprinkled in, blank
    // lines between blocks. One-line shape: a single enormous line — the
    // JSONL/log pathology, where total size is not the only hazard.
    QString corpus;
    corpus.reserve(int(target) + 512);
    if (oneLine) {
        const QString piece = QStringLiteral(
            "{\"role\":\"assistant\",\"content\":\"a chunk of transcript "
            "prose that keeps the line growing\"} ");
        while (corpus.size() < target)
            corpus += piece;
        corpus += QLatin1Char('\n');
    } else {
        int block = 0;
        const QString para = QStringLiteral(
            "A paragraph of ordinary prose, long enough to look like real "
            "writing and to give the parser and the span scanner something "
            "to walk through on every load.\n\n");
        while (corpus.size() < target) {
            if (block % 20 == 0)
                corpus += QStringLiteral("## Section %1\n\n").arg(block / 20);
            else if (block % 7 == 0)
                corpus += QStringLiteral("- a list item with some text\n\n");
            else
                corpus += para;
            ++block;
        }
    }

    DocumentSerializer serializer;
    BlockModel model;
    QElapsedTimer timer;

    timer.start();
    serializer.loadIntoModel(&model, corpus);
    const qint64 loadMs = timer.elapsed();

    timer.restart();
    const QString serialized = serializer.serialize(&model);
    const qint64 serializeMs = timer.elapsed();
    QVERIFY(!serialized.isEmpty());

    // Journal-style write: the crash journal serializes the whole document
    // and writes it on every dirty 2 s debounce; measure the write alone.
    const QString path = m_dir.filePath(
        QStringLiteral("journal-%1.md").arg(QTest::currentDataTag()));
    timer.restart();
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << serialized;
        stream.flush();
    }
    const qint64 writeMs = timer.elapsed();
    QFile::remove(path);

    qInfo("%s: %d blocks | load %lld ms | serialize %lld ms | journal write %lld ms",
          QTest::currentDataTag(), model.count(), loadMs, serializeMs, writeMs);

    // Loose sanity bounds only — an order of magnitude above the budgets,
    // so a real regression trips but machine variance does not.
    QVERIFY2(loadMs < 30000, "load should stay under 30 s even at 50 MB");
    QVERIFY2(serializeMs < 15000, "serialize should stay under 15 s");
}

QTEST_MAIN(TestPerformanceBigFiles)
#include "test_performance_bigfiles.moc"
