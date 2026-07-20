// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "settingsstore.h"

// The per-user settings store: one flat JSON object, debounced atomic
// writes, injectable path, unknown keys preserved, absent/corrupt files
// recovering to defaults.
class TestSettingsStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testAbsentFileYieldsDefaults();
    void testTypesRoundTripThroughDisk();
    void testValueFallbacks();
    void testRemoveAndContains();
    void testDebounceCoalescesBursts();
    void testDestructorFlushesPendingWrite();
    void testUnchangedSetIsANoOp();
    void testValueChangedPerKey();
    void testRevisionContract();
    void testUnknownKeysSurvive();
    void testCorruptFileRecoversToDefaults();
    void testNonObjectFileRecoversToDefaults();
    void testOpenCreatesMissingDirectory();
    void testReopenLandsPendingWriteOnOldPath();

private:
    QString path(const QString &name = QStringLiteral("settings.json")) const
    {
        return m_dir->filePath(name);
    }

    QJsonObject readDisk(const QString &filePath) const
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    QTemporaryDir *m_dir = nullptr;
};

void TestSettingsStore::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
}

void TestSettingsStore::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

void TestSettingsStore::testAbsentFileYieldsDefaults()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    QVERIFY(!store.contains("theme"));
    QCOMPARE(store.value("theme", "light").toString(), QString("light"));
    // Opening never creates the file; only a write does.
    QVERIFY(!QFile::exists(path()));
}

void TestSettingsStore::testTypesRoundTripThroughDisk()
{
    {
        SettingsStore store;
        QVERIFY(store.open(path()));
        store.setValue("string", "dark");
        store.setValue("boolean", true);
        store.setValue("integer", 42);
        store.setValue("real", 1.5);
        store.setValue("list", QStringList{"alpha", "beta"});
        store.setValue("map", QVariantMap{{"x", 3}, {"y", "z"}});
        store.flush();
    }

    SettingsStore reopened;
    QVERIFY(reopened.open(path()));
    QCOMPARE(reopened.value("string").toString(), QString("dark"));
    QCOMPARE(reopened.value("boolean").toBool(), true);
    QCOMPARE(reopened.value("integer").toInt(), 42);
    QCOMPARE(reopened.value("real").toDouble(), 1.5);
    QCOMPARE(reopened.value("list").toStringList(),
             (QStringList{"alpha", "beta"}));
    const QVariantMap map = reopened.value("map").toMap();
    QCOMPARE(map.value("x").toInt(), 3);
    QCOMPARE(map.value("y").toString(), QString("z"));
}

void TestSettingsStore::testValueFallbacks()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    QCOMPARE(store.value("missing").isValid(), false);
    QCOMPARE(store.value("missing", 7).toInt(), 7);
    store.setValue("present", 1);
    QCOMPARE(store.value("present", 7).toInt(), 1);
}

void TestSettingsStore::testRemoveAndContains()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    store.setValue("key", "value");
    QVERIFY(store.contains("key"));
    store.remove("key");
    QVERIFY(!store.contains("key"));
    QCOMPARE(store.value("key", "fallback").toString(), QString("fallback"));

    store.flush();
    QVERIFY(!readDisk(path()).contains("key"));

    // Removing an absent key is silent: no signal, no write.
    QSignalSpy spy(&store, &SettingsStore::valueChanged);
    store.remove("never-existed");
    QCOMPARE(spy.count(), 0);
}

void TestSettingsStore::testDebounceCoalescesBursts()
{
    SettingsStore store;
    QVERIFY(store.open(path()));

    // A burst of changes (a slider drag) stays in memory...
    for (int i = 0; i < 20; ++i)
        store.setValue("fontSize", 10 + i);
    QVERIFY(!QFile::exists(path()));

    // ...and lands as one write once the debounce expires.
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path()),
                             SettingsStore::WriteDelayMs + 2000);
    QCOMPARE(readDisk(path()).value("fontSize").toInt(), 29);
}

void TestSettingsStore::testDestructorFlushesPendingWrite()
{
    {
        SettingsStore store;
        QVERIFY(store.open(path()));
        store.setValue("theme", "sepia");
        // Destroyed mid-debounce: quit must not lose the change.
    }
    QCOMPARE(readDisk(path()).value("theme").toString(), QString("sepia"));
}

void TestSettingsStore::testUnchangedSetIsANoOp()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    store.setValue("theme", "dark");
    store.flush();

    QSignalSpy changed(&store, &SettingsStore::valueChanged);
    QSignalSpy revision(&store, &SettingsStore::revisionChanged);
    store.setValue("theme", "dark");
    QCOMPARE(changed.count(), 0);
    QCOMPARE(revision.count(), 0);

    // And nothing was scheduled: the file stays untouched.
    QFile::remove(path());
    QTest::qWait(SettingsStore::WriteDelayMs + 200);
    QVERIFY(!QFile::exists(path()));
}

void TestSettingsStore::testValueChangedPerKey()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    QSignalSpy spy(&store, &SettingsStore::valueChanged);
    store.setValue("a", 1);
    store.setValue("b", 2);
    store.setValue("a", 3);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toString(), QString("a"));
    QCOMPARE(spy.at(1).at(0).toString(), QString("b"));
    QCOMPARE(spy.at(2).at(0).toString(), QString("a"));
}

void TestSettingsStore::testRevisionContract()
{
    SettingsStore store;
    QVERIFY(store.open(path()));
    const int afterOpen = store.revision();

    store.setValue("a", 1);
    QVERIFY(store.revision() > afterOpen);
    const int afterSet = store.revision();

    store.remove("a");
    QVERIFY(store.revision() > afterSet);
    const int afterRemove = store.revision();

    // Reads and no-ops do not bump.
    store.value("a");
    store.contains("a");
    store.setValue("b", QVariant());
    store.setValue("b", QVariant());
    QCOMPARE(store.revision(), afterRemove + 1);
}

void TestSettingsStore::testUnknownKeysSurvive()
{
    // A future version's (or a hand-edited) key must round-trip
    // verbatim — the collection.json tolerance rule.
    {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\n  \"futureKey\": {\"nested\": [1, 2]},\n"
                   "  \"theme\": \"light\"\n}\n");
    }

    {
        SettingsStore store;
        QVERIFY(store.open(path()));
        QCOMPARE(store.value("theme").toString(), QString("light"));
        store.setValue("theme", "dark");
        store.flush();
    }

    const QJsonObject disk = readDisk(path());
    QCOMPARE(disk.value("theme").toString(), QString("dark"));
    const QJsonObject future = disk.value("futureKey").toObject();
    QCOMPARE(future.value("nested").toArray().size(), 2);
}

void TestSettingsStore::testCorruptFileRecoversToDefaults()
{
    {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json at all");
    }

    SettingsStore store;
    QVERIFY(store.open(path()));
    QVERIFY(!store.contains("theme"));

    // The first write replaces the corrupt file with a valid one.
    store.setValue("theme", "light");
    store.flush();
    QCOMPARE(readDisk(path()).value("theme").toString(), QString("light"));
}

void TestSettingsStore::testNonObjectFileRecoversToDefaults()
{
    {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[1, 2, 3]");
    }

    SettingsStore store;
    QVERIFY(store.open(path()));
    QCOMPARE(store.revision() > 0, true);
    QVERIFY(!store.contains("0"));
}

void TestSettingsStore::testOpenCreatesMissingDirectory()
{
    const QString nested = m_dir->filePath("a/b/settings.json");
    SettingsStore store;
    QVERIFY(store.open(nested));
    store.setValue("k", 1);
    store.flush();
    QCOMPARE(readDisk(nested).value("k").toInt(), 1);
}

void TestSettingsStore::testReopenLandsPendingWriteOnOldPath()
{
    const QString first = path("first.json");
    const QString second = path("second.json");

    SettingsStore store;
    QVERIFY(store.open(first));
    store.setValue("home", "first");
    QVERIFY(store.open(second));  // mid-debounce

    // The pending change landed on the first file, not the second.
    QCOMPARE(readDisk(first).value("home").toString(), QString("first"));
    QVERIFY(!QFile::exists(second));
    QVERIFY(!store.contains("home"));
}

QTEST_MAIN(TestSettingsStore)
#include "test_settingsstore.moc"
