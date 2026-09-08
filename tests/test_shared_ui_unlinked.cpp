// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlEngine>

class TestSharedUiUnlinked : public QObject
{
    Q_OBJECT
private slots:
    void aMissingLinkCannotPassThroughADevelopmentImportPath()
    {
        QQmlEngine engine;
        engine.setImportPathList({QStringLiteral("qrc:/qt/qml"),
                                 QLibraryInfo::path(QLibraryInfo::QmlImportsPath)});
        QQmlComponent component(&engine);
        component.setData("import Kvit.Ui as Ui\nUi.KvitButton { text: 'Missing' }",
                          QUrl("qrc:/test/Unlinked.qml"));
        QVERIFY(component.isError());
        QVERIFY2(component.errorString().contains("Kvit.Ui"), qPrintable(component.errorString()));
        QVERIFY(component.errorString().contains("not installed"));
    }
};
QTEST_MAIN(TestSharedUiUnlinked)
#include "test_shared_ui_unlinked.moc"
