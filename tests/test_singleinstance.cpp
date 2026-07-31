// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The single-instance channel: the first SingleInstance to claim a name is the
// primary, a second one on that name forwards its request to the primary rather
// than becoming a second primary, and the primary receives exactly what was
// forwarded. Exercised in-process with two SingleInstance objects on one local
// endpoint — the same handshake a second process performs, without a display or
// a subprocess.
#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QSignalSpy>
#include <QString>

#include "singleinstance.h"

namespace {
QString uniqueName()
{
    static int counter = 0;
    return QStringLiteral("kvit-si-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(++counter);
}
}

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void secondInstanceForwardsToPrimary()
    {
        const QString name = uniqueName();

        SingleInstance primary(name);
        QVERIFY(primary.tryBecomePrimary());

        QSignalSpy received(&primary, &SingleInstance::requestReceived);

        SingleInstance secondary(name);
        QVERIFY2(!secondary.tryBecomePrimary(),
                 "a second instance on a claimed name must not become primary");
        QVERIFY(secondary.forwardToPrimary(QStringLiteral("/home/user/Vault")));

        QVERIFY(received.wait(2000));
        QCOMPARE(received.count(), 1);
        QCOMPARE(received.first().at(0).toString(),
                 QStringLiteral("/home/user/Vault"));
    }

    void bareLaunchForwardsAnEmptyRequest()
    {
        const QString name = uniqueName();

        SingleInstance primary(name);
        QVERIFY(primary.tryBecomePrimary());
        QSignalSpy received(&primary, &SingleInstance::requestReceived);

        SingleInstance secondary(name);
        QVERIFY(!secondary.tryBecomePrimary());
        QVERIFY(secondary.forwardToPrimary(QString()));

        QVERIFY(received.wait(2000));
        QCOMPARE(received.count(), 1);
        QVERIFY(received.first().at(0).toString().isEmpty());
    }

    void distinctNamesAreIndependentPrimaries()
    {
        SingleInstance a(uniqueName());
        SingleInstance b(uniqueName());
        QVERIFY(a.tryBecomePrimary());
        QVERIFY(b.tryBecomePrimary());
    }

    // A primary that crashed leaves its Unix socket file behind. Nothing
    // answers on it, but it still makes listen() fail, so an endpoint that is
    // only cleared when nothing answers is the difference between the next
    // launch starting and the application being unable to start again until
    // somebody deletes a file they have never heard of.
    void staleEndpointIsReclaimed()
    {
        const QString name = uniqueName();
        QString fullName;
        {
            QLocalServer server;
            QVERIFY(server.listen(name));
            fullName = server.fullServerName();
        }   // closed and unlinked here
        if (fullName.isEmpty() || fullName.startsWith(QStringLiteral("\\\\")))
            QSKIP("named pipes leave nothing behind to go stale");

        QFile stale(fullName);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        QVERIFY(QFileInfo::exists(fullName));

        SingleInstance instance(name);
        QVERIFY2(instance.tryBecomePrimary(),
                 "a socket file left behind by a crashed primary locked the "
                 "endpoint out for good");
    }

    void forwardingWithNoPrimaryFails()
    {
        // No one is listening on this name, so forwarding cannot connect.
        SingleInstance orphan(uniqueName());
        QVERIFY(!orphan.forwardToPrimary(QStringLiteral("/nowhere")));
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
