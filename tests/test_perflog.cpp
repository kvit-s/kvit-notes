// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "perflog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTemporaryDir>

class TestPerfLog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void disabledScopeRecordsNothingAndConstructsNoTimer();
    void enabledScopeRecordsSample();
    void thresholdDropsBelowBudgetSamples();
    void beginEndMergesContext();
    void environmentLogPathReceivesJsonlOutput();
    void jsonLineParses();
};

void TestPerfLog::init()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLogFilePath(QString());
    log.setLevel(PerfLog::Off);
    log.setHumanOutput(false);
    PerfLog::resetTimerConstructionCount();
}

void TestPerfLog::disabledScopeRecordsNothingAndConstructsNoTimer()
{
    {
        PerfLog::ScopedTimer scope(QStringLiteral("unit.disabled"));
        QTest::qWait(1);
    }

    QCOMPARE(PerfLog::instance().samples().size(), 0);
    QCOMPARE(PerfLog::timerConstructionCount(), 0);
}

void TestPerfLog::enabledScopeRecordsSample()
{
    PerfLog::instance().setLevel(PerfLog::Major);
    {
        PerfLog::ScopedTimer scope(
            QStringLiteral("unit.scope"),
            QVariantMap{{QStringLiteral("answer"), 42}});
        QTest::qWait(1);
    }

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("unit.scope"));
    QCOMPARE(samples.size(), 1);
    QVERIFY(samples.first().durationMs >= 0.0);
    QCOMPARE(samples.first().context.value(QStringLiteral("answer")).toInt(), 42);
    QCOMPARE(PerfLog::timerConstructionCount(), 1);
}

void TestPerfLog::thresholdDropsBelowBudgetSamples()
{
    PerfLog::instance().setLevel(PerfLog::Major);
    {
        PerfLog::ScopedTimer scope(QStringLiteral("unit.threshold"),
                                   QVariantMap(),
                                   PerfLog::Major,
                                   1000.0);
        QTest::qWait(1);
    }

    QCOMPARE(PerfLog::instance().samples(QStringLiteral("unit.threshold")).size(), 0);
    QCOMPARE(PerfLog::timerConstructionCount(), 1);
}

void TestPerfLog::beginEndMergesContext()
{
    PerfLog &log = PerfLog::instance();
    log.setLevel(PerfLog::Major);

    log.begin(QStringLiteral("qml.operation"),
              QVariantMap{{QStringLiteral("start"), 1}});
    QTest::qWait(1);
    log.end(QStringLiteral("qml.operation"),
            QVariantMap{{QStringLiteral("end"), 2}});

    const QList<PerfLog::Sample> samples =
        log.samples(QStringLiteral("qml.operation"));
    QCOMPARE(samples.size(), 1);
    QCOMPARE(samples.first().context.value(QStringLiteral("start")).toInt(), 1);
    QCOMPARE(samples.first().context.value(QStringLiteral("end")).toInt(), 2);
}

void TestPerfLog::environmentLogPathReceivesJsonlOutput()
{
    const QByteArray oldPerf = qgetenv("KVIT_PERF");
    const QByteArray oldPerfLog = qgetenv("KVIT_PERF_LOG");
    const bool hadPerf = qEnvironmentVariableIsSet("KVIT_PERF");
    const bool hadPerfLog = qEnvironmentVariableIsSet("KVIT_PERF_LOG");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString logPath = QDir(tempDir.path()).filePath(QStringLiteral("perf.jsonl"));

    qputenv("KVIT_PERF", "1");
    qputenv("KVIT_PERF_LOG", logPath.toLocal8Bit());

    PerfLog &log = PerfLog::instance();
    log.configureFromEnvironment();
    QVERIFY(log.enabled());
    QVERIFY(log.hasLogFilePath());
    log.mark(QStringLiteral("env.path"), 1.5);

    QFile file(logPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine();
    QVERIFY(line.contains("\"op\":\"env.path\""));

    if (hadPerf)
        qputenv("KVIT_PERF", oldPerf);
    else
        qunsetenv("KVIT_PERF");
    if (hadPerfLog)
        qputenv("KVIT_PERF_LOG", oldPerfLog);
    else
        qunsetenv("KVIT_PERF_LOG");
}

void TestPerfLog::jsonLineParses()
{
    PerfLog &log = PerfLog::instance();
    log.setLevel(PerfLog::Major);
    log.mark(QStringLiteral("json.sample"),
             3.25,
             QVariantMap{{QStringLiteral("blocks"), 7}});

    const QList<PerfLog::Sample> samples =
        log.samples(QStringLiteral("json.sample"));
    QCOMPARE(samples.size(), 1);

    QJsonParseError error;
    const QJsonDocument doc =
        QJsonDocument::fromJson(PerfLog::sampleToJsonLine(samples.first()),
                                &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("op")).toString(),
             QStringLiteral("json.sample"));
    QCOMPARE(doc.object().value(QStringLiteral("ctx")).toObject()
                 .value(QStringLiteral("blocks")).toInt(),
             7);
}

QTEST_MAIN(TestPerfLog)
#include "test_perflog.moc"
