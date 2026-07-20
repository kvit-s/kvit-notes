// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "block.h"
#include "blockmodel.h"
#include "extensionregistry.h"

// The shell as the application actually composes it: AppContext wired up and
// qml/main.qml loaded from the shipped resource.
//
// Two things are checked here that unit tests cannot see. First, that the
// composition root really does compose a working application — the case that
// gives the app/library split its meaning, since a second binary is supposed
// to get an editor by constructing this one object. Second, that the delegate
// chooser has a delegate for every block type there is: the chooser matches on
// a numeric kind, so a type whose kind no choice claims would silently render
// as an empty row, and no unit test on the model would notice.
class TestShell : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Nothing to reset: the context owns its registries, so this suite
        // starts from the built-ins whatever else ran in the process.
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));
        m_context->installContextProperties(&m_engine);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    }

    void theComposedContextLoadsTheShell()
    {
        QVERIFY(!m_engine.rootObjects().isEmpty());
    }

    void withNoModuleInstalledEverySlotIsInert()
    {
        // What the open build looks like: the seams are present and empty, so
        // the shell lays out exactly as it did before they existed.
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);

        for (const char *name : {"extensionBanner", "extensionBottomBar",
                                 "extensionSidePanel"}) {
            QObject *slot = window->findChild<QObject *>(name);
            QVERIFY2(slot, name);
            QVERIFY2(slot->property("source").toString().isEmpty(), name);
            QVERIFY2(!slot->property("active").toBool(), name);
        }
        // The two horizontal slots reserve no height and the vertical one no
        // width, which is what keeps the editor pane's margins unchanged.
        QCOMPARE(window->findChild<QObject *>("extensionBanner")
                     ->property("height").toReal(), 0.0);
        QCOMPARE(window->findChild<QObject *>("extensionBottomBar")
                     ->property("height").toReal(), 0.0);
        QCOMPARE(window->findChild<QObject *>("extensionSidePanel")
                     ->property("width").toReal(), 0.0);
    }

    void everyBlockTypeGetsADelegate_data()
    {
        QTest::addColumn<int>("blockType");
        QTest::addColumn<QString>("language");

        QTest::newRow("paragraph") << int(Block::Paragraph) << QString();
        QTest::newRow("heading1") << int(Block::Heading1) << QString();
        QTest::newRow("heading2") << int(Block::Heading2) << QString();
        QTest::newRow("heading3") << int(Block::Heading3) << QString();
        // Heading4 was appended after Divider, so it is the type most likely
        // to fall outside a range-based kind rule.
        QTest::newRow("heading4") << int(Block::Heading4) << QString();
        QTest::newRow("bullet") << int(Block::BulletList) << QString();
        QTest::newRow("numbered") << int(Block::NumberedList) << QString();
        QTest::newRow("todo") << int(Block::Todo) << QString();
        QTest::newRow("quote") << int(Block::Quote) << QString();
        QTest::newRow("divider") << int(Block::Divider) << QString();
        QTest::newRow("callout") << int(Block::Callout) << QString();
        QTest::newRow("math") << int(Block::MathBlock) << QString();
        QTest::newRow("table") << int(Block::Table) << QString();
        QTest::newRow("code-plain") << int(Block::CodeBlock) << QString();
        QTest::newRow("code-python") << int(Block::CodeBlock) << QStringLiteral("python");
        // The fence languages that route by the registry, taken FROM the
        // registry rather than listed here. A fence kind added to the
        // registry gets a case automatically; a hand-written list would let a
        // new kind ship with no coverage, which is the drift this suite
        // exists to catch.
        for (const QString &language : m_context->blockKinds()->languages()) {
            QTest::newRow(qPrintable(QStringLiteral("code-") + language))
                << int(Block::CodeBlock) << language;
        }
        // A fence language nobody registered must still render — as a plain
        // code block, the way an unknown highlight language always has.
        QTest::newRow("code-unregistered")
            << int(Block::CodeBlock) << QStringLiteral("no-such-language");
    }

    void everyBlockTypeGetsADelegate()
    {
        QFETCH(int, blockType);
        QFETCH(QString, language);

        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QObject *listView = window->findChild<QObject *>("blockListView");
        QVERIFY(listView);

        BlockModel *model = m_context->blockModel();
        // Each case works on a document holding just this block: a ListView
        // creates delegates only for rows near the viewport, so a growing
        // document would report a null row for reasons having nothing to do
        // with the chooser.
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);

        const int index = 0;
        model->insertBlock(index, blockType, QString());
        if (!language.isEmpty()) {
            model->convertBlock(index, blockType, QStringLiteral("```\ntext\n```"),
                                false, language);
        }

        QQuickItem *row = nullptr;
        QTRY_VERIFY2(QMetaObject::invokeMethod(
                         listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                         Q_ARG(int, index)) && row,
                     "the delegate chooser produced no delegate for this block");
        QVERIFY(row->height() > 0);
    }

    // The block-kind numbers exist once, in the BlockKinds enum, and the
    // shipped shell names them. This is the guard on that pairing: a kind
    // added to the enum with no DelegateChoice to render it would otherwise
    // produce an empty row at runtime and nothing would say so. The enum is
    // read from the metaobject, so the check cannot fall behind it.
    void everyBuiltinKindIsNamedByTheShell()
    {
        const QMetaObject &meta = BlockKinds::staticMetaObject;
        const QMetaEnum kinds = meta.enumerator(meta.indexOfEnumerator("Kind"));
        QVERIFY(kinds.isValid());
        QVERIFY(kinds.keyCount() > 0);

        QFile shell(QStringLiteral(":/qml/main.qml"));
        QVERIFY2(shell.open(QIODevice::ReadOnly | QIODevice::Text),
                 "the shipped shell is not in the test's resources");
        const QString source = QString::fromUtf8(shell.readAll());

        for (int i = 0; i < kinds.keyCount(); ++i) {
            const QString token =
                QStringLiteral("BlockKinds.") + QString::fromLatin1(kinds.key(i));
            QVERIFY2(source.contains(token),
                     qPrintable(QStringLiteral(
                                    "qml/main.qml has no DelegateChoice for %1; "
                                    "a block of that kind would render as an "
                                    "empty row").arg(token)));
        }

        // The other half of the pairing: the shell must not carry a bare
        // number where a kind belongs, which is how these fell out of step
        // before.
        for (int i = 0; i < kinds.keyCount(); ++i) {
            const QString literal =
                QStringLiteral("roleValue: %1").arg(kinds.value(i));
            QVERIFY2(!source.contains(literal),
                     qPrintable(QStringLiteral(
                                    "qml/main.qml still hard-codes %1; use the "
                                    "BlockKinds enum so the number lives in one "
                                    "place").arg(literal)));
        }
    }

    // Theme and typography snapshot the store when attached, so they must
    // attach after openSettings() has loaded the file — attached in wire(),
    // before the store opens, a saved dark theme silently came back light.
    // Composing a fresh context over a seeded file is the same startup path
    // KvitApplication runs.
    void persistedAppearanceSettingsApplyAtStartup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("settings.json"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ \"theme.id\": \"dark\", \"typography.fontSize\": 19 }");
        }

        AppContext context;
        context.openSettings(path);

        QCOMPARE(context.theme()->themeId(), QStringLiteral("dark"));
        QCOMPARE(context.theme()->resolvedTheme(), QStringLiteral("dark"));
        QCOMPARE(context.typography()->baseSize(), 19);
    }

private:
    QTemporaryDir m_dir;
    // The context outlives the engine, as it does in KvitApplication.
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
};

QTEST_MAIN(TestShell)
#include "test_shell.moc"
