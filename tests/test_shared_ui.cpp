// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QFile>
#include <QLibraryInfo>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include "appcontext.h"

namespace {
constexpr auto scene = R"(
import QtQuick
import Kvit as Core
import Kvit.Ui as Ui
Window {
    width: 360; height: 160
    property bool identical: Core.Theme === Ui.Theme
        && Core.Interface === Ui.Interface && Core.Typography === Ui.Typography
        && Core.AppSettings === Ui.AppSettings
        && Core.SystemAppearance === Ui.SystemAppearance
    property string theme: Ui.Theme.themeId
    property int size: Ui.Interface.fontSize
    property string family: Ui.Typography.fontFamily
    property bool contrast: Ui.SystemAppearance.highContrast
    property string settingsPath: Ui.AppSettings.filePath()
    Ui.KvitButton { objectName: "sharedButton"; text: "Rename"; symbol: "pencil" }
    Ui.KvitIcon { objectName: "sharedIcon"; y: 80; name: "file" }
})";

void prepare(AppContext &context, QQmlEngine &engine)
{
    // Keep Qt's installed modules, but no application source/build paths.
    engine.setImportPathList({QStringLiteral("qrc:/qt/qml"),
                             QLibraryInfo::path(QLibraryInfo::QmlImportsPath)});
    context.installContextProperties(&engine);
}

QStringList names()
{
    return {"Theme", "Interface", "Typography", "AppSettings", "SystemAppearance"};
}
}

class TestSharedUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
    }

    void embeddedControlsUseTheExistingComposition()
    {
        QTemporaryDir dir;
        AppContext context(ProcessServices::Options{false, false});
        context.openSettings(dir.filePath("settings.json"));
        QQmlEngine engine;
        prepare(context, engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(scene, QUrl("qrc:/test/SharedUi.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(root->property("identical").toBool());
        QCOMPARE(root->property("settingsPath").toString(), context.settings()->filePath());
        for (const auto &name : names()) {
            auto *old = engine.singletonInstance<QObject *>("Kvit", name);
            auto *shared = engine.singletonInstance<QObject *>("Kvit.Ui", name);
            QVERIFY(old);
            QCOMPARE(shared, old);
            QCOMPARE(QQmlEngine::objectOwnership(shared), QQmlEngine::CppOwnership);
        }
        QVERIFY(root->findChild<QObject *>("sharedButton"));
        QVERIFY(root->findChild<QObject *>("sharedIcon")->property("recognized").toBool());
        QFile font(":/qt/qml/Kvit/Ui/fonts/Phosphor.ttf");
        QVERIFY(font.open(QIODevice::ReadOnly));
        QVERIFY(font.size() > 0);
        qobject_cast<QQuickWindow *>(root.get())->show();
        QTest::qWait(50);
        QVERIFY(!qobject_cast<QQuickWindow *>(root.get())->grabWindow().isNull());
        QCOMPARE(warnings.size(), 0);
        QQmlComponent missing(&engine, QUrl("qrc:/qt/qml/Kvit/Ui/NotPackaged.qml"));
        QVERIFY(missing.isError());
        QVERIFY(!missing.errors().isEmpty());
    }

    void twoWindowsShareLiveServicesAndDoNotOwnThem()
    {
        QTemporaryDir dir;
        ProcessServices globals(ProcessServices::Options{false, false});
        globals.openSettings(dir.filePath("settings.json"));
        AppContext first(globals), second(globals);
        QPointer<QObject> theme(globals.theme());
        {
            QQmlEngine one, two;
            prepare(first, one);
            prepare(second, two);
            QSignalSpy warningsOne(&one, &QQmlEngine::warnings);
            QSignalSpy warningsTwo(&two, &QQmlEngine::warnings);
            QQmlComponent a(&one), b(&two);
            a.setData(scene, QUrl("qrc:/test/One.qml"));
            b.setData(scene, QUrl("qrc:/test/Two.qml"));
            QVERIFY2(a.isReady(), qPrintable(a.errorString()));
            QVERIFY2(b.isReady(), qPrintable(b.errorString()));
            std::unique_ptr<QObject> left(a.create()), right(b.create());
            QVERIFY(left && right);
            // A second composition must not replace registrations underneath
            // already-created engines or give their singletons new type IDs.
            AppContext::registerQmlTypes();
            qobject_cast<QQuickWindow *>(left.get())->show();
            qobject_cast<QQuickWindow *>(right.get())->show();
            for (const auto &name : names())
                QCOMPARE(one.singletonInstance<QObject *>("Kvit.Ui", name),
                         two.singletonInstance<QObject *>("Kvit.Ui", name));
            globals.theme()->setThemeId("dark");
            globals.interfaceMetrics()->setFontSize(24);
            globals.typography()->setFontFamily("monospace");
            globals.systemAppearance()->setOverride(true, true);
            for (QObject *view : {left.get(), right.get()}) {
                QVERIFY(view->property("identical").toBool());
                QTRY_COMPARE(view->property("theme").toString(), QString("dark"));
                QTRY_COMPARE(view->property("size").toInt(), 24);
                QTRY_COMPARE(view->property("family").toString(), QString("monospace"));
                QTRY_VERIFY(view->property("contrast").toBool());
                auto *button = view->findChild<QObject *>("sharedButton");
                QTRY_VERIFY(button->property("implicitHeight").toReal()
                            >= globals.interfaceMetrics()->controlHeight());
            }
            one.collectGarbage(); two.collectGarbage();
            QVERIFY(theme);
            QCOMPARE(warningsOne.size(), 0);
            QCOMPARE(warningsTwo.size(), 0);
        }
        // Engine destruction must not delete the composition's borrowed objects.
        QVERIFY(theme);
        QQmlEngine recreated;
        prepare(first, recreated);
        QCOMPARE(recreated.singletonInstance<QObject *>("Kvit.Ui", "Theme"), theme.data());
    }

    void isolatedSettingsSurviveAnEngineRestart()
    {
        QTemporaryDir dir;
        const QString savedPath = dir.filePath("saved.json");
        {
            AppContext saved(ProcessServices::Options{false, false});
            saved.openSettings(savedPath);
            saved.theme()->setThemeId("dark");
            saved.processServices()->interfaceMetrics()->setFontSize(24);
            saved.settings()->flush();
        }
        AppContext restored(ProcessServices::Options{false, false});
        AppContext isolated(ProcessServices::Options{false, false});
        restored.openSettings(savedPath);
        isolated.openSettings(dir.filePath("isolated.json"));
        QQmlEngine first, second;
        prepare(restored, first); prepare(isolated, second);
        for (const auto &name : names()) {
            auto *a = first.singletonInstance<QObject *>("Kvit.Ui", name);
            auto *b = second.singletonInstance<QObject *>("Kvit.Ui", name);
            QVERIFY(a && b);
            QVERIFY(a != b);
            QCOMPARE(a, first.singletonInstance<QObject *>("Kvit", name));
        }
        QCOMPARE(first.singletonInstance<QObject *>("Kvit.Ui", "Theme")
                     ->property("themeId").toString(), QString("dark"));
        QCOMPARE(first.singletonInstance<QObject *>("Kvit.Ui", "Interface")
                     ->property("fontSize").toInt(), 24);
        QVERIFY(second.singletonInstance<QObject *>("Kvit.Ui", "Theme")
                    ->property("themeId").toString() != "dark");
    }
};
QTEST_MAIN(TestSharedUi)
#include "test_shared_ui.moc"
