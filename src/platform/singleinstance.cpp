// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "singleinstance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

#include <memory>

SingleInstance::SingleInstance(const QString &serverName, QObject *parent)
    : QObject(parent)
    , m_serverName(serverName)
{
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::defaultServerName()
{
    // Per-user and per-install: two users on one machine, or a development
    // build and an installed one, must not share an instance. Hashing the home
    // directory and the executable path keeps the name stable across launches
    // of the same install by the same user, and distinct otherwise.
    const QByteArray seed =
        (QDir::homePath() + QLatin1Char('|')
         + QCoreApplication::applicationFilePath()).toUtf8();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha1)
            .toHex()
            .left(16));
    return QStringLiteral("kvit-notes-") + digest;
}

bool SingleInstance::tryBecomePrimary()
{
    // Probe for a live primary first. A successful connection is the only
    // reliable proof one exists: on Unix a stale socket file survives a crash
    // and would otherwise make listen() fail forever, while removing the file
    // blindly would evict a live primary.
    {
        QLocalSocket probe;
        probe.connectToServer(m_serverName);
        if (probe.waitForConnected(200)) {
            probe.abort();
            return false;   // a primary is already running
        }
    }

    // No live primary: clear any stale endpoint a crashed one left behind, then
    // claim it.
    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstance::onNewConnection);
    if (!m_server->listen(m_serverName)) {
        // Could not listen — a rare race with another launch claiming the name
        // between the probe and here. Proceed as our own instance rather than
        // fail to start; the vault lock still prevents two writers on one vault.
        delete m_server;
        m_server = nullptr;
    }
    return true;
}

bool SingleInstance::forwardToPrimary(const QString &request)
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(500))
        return false;
    QByteArray payload = request.toUtf8();
    payload.append('\n');   // frames the request; the reader waits for the newline
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(500);
    return true;
}

void SingleInstance::onNewConnection()
{
    QLocalSocket *client = m_server->nextPendingConnection();
    if (!client)
        return;
    // Accumulate until the framing newline, then act once. The payload is a
    // single short path, so this almost always arrives in one read.
    auto buffer = std::make_shared<QByteArray>();
    connect(client, &QLocalSocket::readyRead, this,
            [this, client, buffer]() {
                buffer->append(client->readAll());
                const int nl = buffer->indexOf('\n');
                if (nl < 0)
                    return;
                const QString request =
                    QString::fromUtf8(buffer->left(nl)).trimmed();
                emit requestReceived(request);
                client->disconnectFromServer();
            });
    connect(client, &QLocalSocket::disconnected,
            client, &QObject::deleteLater);
}
