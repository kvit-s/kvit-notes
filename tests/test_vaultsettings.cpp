// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "vaultsettings.h"

// The per-vault settings file: how the notes in a vault refer to pictures.
// What is checked is the default each value takes, what the file may and
// may not hold, and that nothing is written where it should not be.
class TestVaultSettings : public QObject
{
    Q_OBJECT

    static void touch(const QString &path, const QByteArray &bytes = "x")
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }

private slots:
    void plainFolderDefaults()
    {
        QTemporaryDir dir;
        VaultSettings settings;
        settings.setRoot(dir.path());
        QCOMPARE(settings.siteFolder(), QString());
        QVERIFY(settings.siteFolderIsDefault());
        QCOMPARE(settings.siteRootPath(), QDir::cleanPath(dir.path()));
        QCOMPARE(settings.imageFolder(), QStringLiteral("assets"));
        // Reading defaults writes nothing.
        QVERIFY(!QDir(dir.path()).exists(".kvit"));
    }

    void detectsHugo_data()
    {
        QTest::addColumn<QStringList>("files");
        QTest::addColumn<bool>("withContent");
        QTest::addColumn<QString>("expected");
        QTest::newRow("hugo.toml") << QStringList{"hugo.toml"} << false << "static";
        QTest::newRow("hugo.yaml") << QStringList{"hugo.yaml"} << false << "static";
        QTest::newRow("config.toml + content")
            << QStringList{"config.toml"} << true << "static";
        // config.toml alone is too common a name to mean Hugo.
        QTest::newRow("config.toml alone")
            << QStringList{"config.toml"} << false << "";
    }

    void detectsHugo()
    {
        QFETCH(QStringList, files);
        QFETCH(bool, withContent);
        QFETCH(QString, expected);
        QTemporaryDir dir;
        for (const QString &f : files)
            touch(QDir(dir.path()).filePath(f));
        if (withContent)
            QDir(dir.path()).mkpath("content");

        VaultSettings settings;
        settings.setRoot(dir.path());
        QCOMPARE(settings.siteFolder(), expected);
        QCOMPARE(settings.detectedFrom(), expected.isEmpty() ? QString() : files.first());
        QCOMPARE(settings.imageFolder(), expected.isEmpty()
                     ? QStringLiteral("assets") : QStringLiteral("static/images"));
        if (!expected.isEmpty())
            QCOMPARE(settings.siteRootPath(),
                     QDir::cleanPath(dir.path()) + "/static");
    }

    void storedChoiceOverridesDetectionAndPersists()
    {
        QTemporaryDir dir;
        touch(QDir(dir.path()).filePath("hugo.toml"));
        {
            VaultSettings settings;
            settings.setRoot(dir.path());
            QSignalSpy changed(&settings, &VaultSettings::changed);
            // An explicit "" (the vault folder) is a choice, not a reset.
            QVERIFY(settings.setSiteFolder(QString()));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(settings.siteFolder(), QString());
            QVERIFY(!settings.siteFolderIsDefault());
            QVERIFY(settings.setImageFolder("./pics/new/"));
            QCOMPARE(settings.imageFolder(), QStringLiteral("pics/new"));
        }
        VaultSettings reopened;
        reopened.setRoot(dir.path());
        QCOMPARE(reopened.siteFolder(), QString());
        QVERIFY(!reopened.siteFolderIsDefault());
        QCOMPARE(reopened.imageFolder(), QStringLiteral("pics/new"));

        // Resetting returns to the detected value, and the file forgets it.
        QVERIFY(reopened.resetSiteFolder());
        QCOMPARE(reopened.siteFolder(), QStringLiteral("static"));
        QVERIFY(reopened.setImageFolder(QString()));
        QVERIFY(reopened.imageFolderIsDefault());
        QCOMPARE(reopened.imageFolder(), QStringLiteral("static/images"));
    }

    void refusesFoldersOutsideTheVault_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("parent") << "../elsewhere";
        QTest::newRow("absolute") << "/etc";
        QTest::newRow("dot-dot inside") << "static/../../x";
        QTest::newRow("repository folder") << ".kvit/pics";
    }

    void refusesFoldersOutsideTheVault()
    {
        QFETCH(QString, value);
        QTemporaryDir dir;
        VaultSettings settings;
        settings.setRoot(dir.path());
        QVERIFY(!settings.isValidFolder(value));
        QVERIFY(!settings.setSiteFolder(value));
        QVERIFY(!settings.setImageFolder(value));
        QVERIFY(settings.siteFolderIsDefault());
        QVERIFY(!QDir(dir.path()).exists(".kvit"));
    }

    // The file is shared with other tools, so a value it holds is checked
    // exactly as a typed one is.
    void ignoresUnsafeValuesInTheFile()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath(".kvit");
        touch(QDir(dir.path()).filePath(".kvit/settings.json"),
              R"({"siteFolder": "../../etc", "imageFolder": 5})");
        VaultSettings settings;
        settings.setRoot(dir.path());
        QVERIFY(settings.siteFolderIsDefault());
        QCOMPARE(settings.siteFolder(), QString());
        QCOMPARE(settings.imageFolder(), QStringLiteral("assets"));
    }

    void readOnlyVaultIsNotWritten()
    {
        QTemporaryDir dir;
        VaultSettings settings;
        settings.setRoot(dir.path());
        settings.setWritableCheck([] { return false; });
        QVERIFY(!settings.setSiteFolder("static"));
        QVERIFY(settings.siteFolderIsDefault());
        QVERIFY(!QDir(dir.path()).exists(".kvit"));
    }
};

QTEST_MAIN(TestVaultSettings)
#include "test_vaultsettings.moc"
