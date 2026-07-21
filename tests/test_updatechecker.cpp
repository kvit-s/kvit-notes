// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "settingsstore.h"
#include "updatechecker.h"

namespace {

// Records every fetch and replies with a canned payload, synchronously.
class FakeUpdateFetcher : public UpdateFetcher
{
public:
    void fetch(const QUrl &url,
               std::function<void(bool, const QByteArray &)> done) override
    {
        ++fetchCount;
        lastUrl = url;
        done(ok, payload);
    }

    int fetchCount = 0;
    QUrl lastUrl;
    bool ok = true;
    QByteArray payload;
};

QByteArray latestPayload(const char *tag,
                         const char *url = "https://example.com/rel")
{
    return QByteArrayLiteral("{\"tag_name\":\"") + tag
         + QByteArrayLiteral("\",\"html_url\":\"") + url
         + QByteArrayLiteral("\"}");
}

} // namespace

class TestUpdateChecker : public QObject
{
    Q_OBJECT

private slots:
    void compareOrdersReleases()
    {
        QVERIFY(UpdateChecker::compareVersions("1.0.1", "1.0.0") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.0.0", "1.0.1") < 0);
        QCOMPARE(UpdateChecker::compareVersions("1.0.0", "1.0.0"), 0);
        // Numeric fields, not string order.
        QVERIFY(UpdateChecker::compareVersions("1.10.0", "1.9.9") > 0);
        // Missing fields read as zero.
        QCOMPARE(UpdateChecker::compareVersions("1.0", "1.0.0"), 0);
        // Leading v is tolerated on either side.
        QVERIFY(UpdateChecker::compareVersions("v1.2.0", "1.1.9") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.2.0", "V1.2.1") < 0);
    }

    void compareOrdersPrereleases()
    {
        // A final release outranks its own prereleases.
        QVERIFY(UpdateChecker::compareVersions("1.0.0", "1.0.0-rc1") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.0.0-rc1", "1.0.0") < 0);
        // rc ladder, including the two-digit step string order gets wrong.
        QVERIFY(UpdateChecker::compareVersions("1.0.0-rc2", "1.0.0-rc1") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.0.0-rc10", "1.0.0-rc2") > 0);
        // Alphabetic ordering across stems (semver: alpha < beta < rc).
        QVERIFY(UpdateChecker::compareVersions("1.0.0-beta", "1.0.0-alpha") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.0.0-rc1", "1.0.0-beta2") > 0);
        // Fewer identifiers order below more (semver §11).
        QVERIFY(UpdateChecker::compareVersions("1.0.0-alpha", "1.0.0-alpha.1") < 0);
        QCOMPARE(UpdateChecker::compareVersions("1.0.0-rc1", "1.0.0-rc1"), 0);
        // A newer release always beats an older prerelease and vice versa.
        QVERIFY(UpdateChecker::compareVersions("1.0.1-rc1", "1.0.0") > 0);
    }

    // Build metadata takes no part in precedence (semver §10), and it has to
    // be stripped before the release fields are read: left in, the "3+linux"
    // field parses as a non-number and becomes 0, so a packaged 1.2.3+linux
    // ordered as 1.2.0 and the app offered its own build as an update.
    void compareIgnoresBuildMetadata()
    {
        QCOMPARE(UpdateChecker::compareVersions("1.2.3+linux", "1.2.3"), 0);
        QCOMPARE(UpdateChecker::compareVersions("1.2.3", "1.2.3+linux"), 0);
        QVERIFY(UpdateChecker::compareVersions("1.2.3+linux", "1.2.0") > 0);
        QVERIFY(UpdateChecker::compareVersions("1.2.3+build.7", "1.2.4") < 0);
        // Metadata after a prerelease is stripped too, and the prerelease
        // still orders below its release.
        QVERIFY(UpdateChecker::compareVersions("1.2.3-rc1+build.7", "1.2.3") < 0);
        QCOMPARE(UpdateChecker::compareVersions("1.2.3-rc1+a", "1.2.3-rc1+b"), 0);
        QVERIFY(UpdateChecker::compareVersions("v1.2.3+linux", "1.2.2") > 0);
    }

    // A packaged release must not report itself as an update.
    void aBuildTaggedReleaseIsNotNewerThanItself()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        FakeUpdateFetcher fetcher;
        fetcher.payload = latestPayload("v1.2.3");

        UpdateChecker checker;
        checker.setSettings(&settings);
        checker.setFetcher(&fetcher);
        checker.setCurrentVersion(QStringLiteral("1.2.3+linux"));
        QSignalSpy spy(&checker, &UpdateChecker::stateChanged);

        checker.maybeCheck();
        QVERIFY2(!checker.updateAvailable(),
                 "the running build was offered as an update to itself");
        QCOMPARE(spy.count(), 0);
    }

    void parsesLatestPayload()
    {
        QString version, url;
        QVERIFY(UpdateChecker::parseLatestPayload(
            latestPayload("v1.2.0"), &version, &url));
        QCOMPARE(version, QStringLiteral("1.2.0"));
        QCOMPARE(url, QStringLiteral("https://example.com/rel"));

        // Tag without the v prefix.
        QVERIFY(UpdateChecker::parseLatestPayload(
            latestPayload("2.0.0-rc1"), &version, &url));
        QCOMPARE(version, QStringLiteral("2.0.0-rc1"));

        // Missing tag and broken JSON both refuse without touching outputs.
        QVERIFY(!UpdateChecker::parseLatestPayload(
            QByteArrayLiteral("{\"html_url\":\"x\"}"), &version, &url));
        QVERIFY(!UpdateChecker::parseLatestPayload(
            QByteArrayLiteral("not json"), &version, &url));
        QVERIFY(!UpdateChecker::parseLatestPayload(
            QByteArrayLiteral("[1,2]"), &version, &url));
    }

    void checksOncePerDay()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        FakeUpdateFetcher fetcher;
        fetcher.payload = latestPayload("v9.9.9");

        UpdateChecker checker;
        checker.setSettings(&settings);
        checker.setFetcher(&fetcher);
        checker.setCurrentVersion(QStringLiteral("1.0.0"));

        checker.maybeCheck();
        QCOMPARE(fetcher.fetchCount, 1);
        QCOMPARE(fetcher.lastUrl, checker.endpoint());

        // Same day: the stamp written before the first request holds.
        checker.maybeCheck();
        checker.maybeCheck();
        QCOMPARE(fetcher.fetchCount, 1);

        // Yesterday's stamp lets a new day's check run.
        settings.setValue(QStringLiteral("updates.lastCheck"),
                          QDate::currentDate().addDays(-1)
                              .toString(Qt::ISODate));
        checker.maybeCheck();
        QCOMPARE(fetcher.fetchCount, 2);
    }

    void disabledAndFetcherlessNeverFetch()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        FakeUpdateFetcher fetcher;
        UpdateChecker checker;
        checker.setSettings(&settings);
        checker.setFetcher(&fetcher);

        // The opt-out setting wins.
        checker.setEnabled(false);
        QVERIFY(!checker.enabled());
        checker.maybeCheck();
        QCOMPARE(fetcher.fetchCount, 0);

        // Enabled but no settings store: refuses (nowhere to gate the day).
        UpdateChecker bare;
        bare.setFetcher(&fetcher);
        bare.maybeCheck();
        QCOMPARE(fetcher.fetchCount, 0);

        // Settings but no fetcher: silently does nothing.
        UpdateChecker unwired;
        unwired.setSettings(&settings);
        unwired.maybeCheck();

        // The UI-driver environment override wins over everything.
        checker.setEnabled(true);
        qputenv("KVIT_DISABLE_UPDATE_CHECK", "1");
        checker.maybeCheck();
        qunsetenv("KVIT_DISABLE_UPDATE_CHECK");
        QCOMPARE(fetcher.fetchCount, 0);
    }

    void surfacesOnlyNewerVersions()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        FakeUpdateFetcher fetcher;
        fetcher.payload = latestPayload("v1.1.0");

        UpdateChecker checker;
        checker.setSettings(&settings);
        checker.setFetcher(&fetcher);
        checker.setCurrentVersion(QStringLiteral("1.0.0"));
        QSignalSpy spy(&checker, &UpdateChecker::stateChanged);

        checker.maybeCheck();
        QVERIFY(checker.updateAvailable());
        QCOMPARE(checker.latestVersion(), QStringLiteral("1.1.0"));
        QCOMPARE(checker.releaseUrl(), QStringLiteral("https://example.com/rel"));
        QCOMPARE(spy.count(), 1);
    }

    void staysQuietOnEqualOlderOrBroken()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        FakeUpdateFetcher fetcher;
        UpdateChecker checker;
        checker.setSettings(&settings);
        checker.setFetcher(&fetcher);
        checker.setCurrentVersion(QStringLiteral("1.2.0"));
        QSignalSpy spy(&checker, &UpdateChecker::stateChanged);

        struct Case { QByteArray payload; bool ok; } cases[] = {
            {latestPayload("v1.2.0"), true},   // equal
            {latestPayload("v1.1.9"), true},   // older
            {latestPayload("v1.2.0-rc3"), true}, // prerelease of current
            {QByteArrayLiteral("garbage"), true},
            {QByteArray(), false},             // transport failure
        };
        for (const Case &c : cases) {
            settings.remove(QStringLiteral("updates.lastCheck"));
            fetcher.payload = c.payload;
            fetcher.ok = c.ok;
            checker.maybeCheck();
        }
        QVERIFY(!checker.updateAvailable());
        QCOMPARE(spy.count(), 0);
    }

    // The real fetcher completes on the network reply, long after
    // maybeCheck() returned, and the callback captured a bare `this`. A
    // window closed while a check is in flight destroys the checker, and the
    // reply then calls applyResult on freed memory. The callback must become
    // inert once its checker is gone.
    void deferredReplyAfterDestructionIsInert()
    {
        // Holds the callback instead of running it, the way a real network
        // reply does.
        class DeferringFetcher : public UpdateFetcher
        {
        public:
            void fetch(const QUrl &,
                       std::function<void(bool, const QByteArray &)> done) override
            {
                pending = std::move(done);
            }
            std::function<void(bool, const QByteArray &)> pending;
        };

        QTemporaryDir dir;
        SettingsStore settings;
        QVERIFY(settings.open(dir.filePath("settings.json")));

        DeferringFetcher fetcher;
        {
            UpdateChecker checker;
            checker.setSettings(&settings);
            checker.setFetcher(&fetcher);
            checker.setCurrentVersion(QStringLiteral("1.0.0"));
            checker.maybeCheck();
        }
        QVERIFY(fetcher.pending != nullptr);

        // The reply lands after the checker is gone. This must not touch it.
        fetcher.pending(true, latestPayload("v9.9.9"));

        // Reaching here without a crash or a sanitizer report is the result;
        // the settings store, which outlived the checker, is untouched too.
        QVERIFY(settings.value(QStringLiteral("updates.lastCheck"))
                    .toString()
                    .size() > 0);
    }
};

QTEST_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
