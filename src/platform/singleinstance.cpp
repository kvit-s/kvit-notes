// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "singleinstance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

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
    // build and an installed one, must not share an instance. On Windows,
    // QDir::homePath() can follow environment variables inherited from the
    // launcher; the shell, an installer and a desktop shortcut need not
    // inherit the same ones. AppLocalDataLocation comes from Windows' known
    // folders and remains stable across those launch paths.
#ifdef Q_OS_WIN
    QString userScope = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
#else
    QString userScope = QDir::homePath();
#endif
    QString executable = QFileInfo(
        QCoreApplication::applicationFilePath()).canonicalFilePath();
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    // The filesystem is case-insensitive, so differently cased spellings of
    // the same installed executable must elect the same primary.
    userScope = userScope.toCaseFolded();
    executable = executable.toCaseFolded();
#endif
    const QByteArray seed =
        (userScope + QLatin1Char('|') + executable).toUtf8();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha1)
            .toHex()
            .left(16));
    return QStringLiteral("kvit-notes-") + digest;
}

bool SingleInstance::tryBecomePrimary()
{
    if (m_ownerLock && m_ownerLock->isLocked())
        return true;

    // A successful QLocalServer::listen() is not an ownership test on
    // Windows: the platform explicitly permits multiple listeners for one
    // pipe name. Elect one process with an atomic file lock before creating
    // the transport. TempLocation is per-user on Windows; the server name
    // itself also includes the user scope on every platform.
    const QString lockDigest = QString::fromLatin1(
        QCryptographicHash::hash(m_serverName.toUtf8(),
                                 QCryptographicHash::Sha1).toHex());
    const QString lockPath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation))
        .filePath(QStringLiteral("kvit-notes-%1.lock").arg(lockDigest));
    m_ownerLock = std::make_unique<QLockFile>(lockPath);
    // Never declare a slow but live process stale based only on the age of its
    // lock. QLockFile still reclaims it when the recorded process is dead.
    m_ownerLock->setStaleLockTime(0);
    if (!m_ownerLock->tryLock(0)) {
        m_ownerLock.reset();
        return false;
    }

    m_server = new QLocalServer(this);
    // The endpoint carries file paths. Keep it private to this user even on
    // platforms whose default local-server permissions are broader.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstance::onNewConnection);

    // Claim the name first, and only investigate if the claim fails. The
    // order matters, because removeServer() unlinks the Unix socket whatever
    // is behind it: probing, then removing, then listening let two launches
    // that started together each find no primary, each unlink the other's
    // freshly bound socket, and each come up believing it was the one that
    // won. Two primaries is two tray icons and two windows for one open
    // request, and in loose-file mode — which takes no vault lock — two
    // editors writing one file.
    if (m_server->listen(m_serverName))
        return true;

    // The name is taken, by a running primary or by a socket file a crashed
    // one left behind. Only a connection tells the two apart: a stale file
    // accepts nothing and would otherwise make listen() fail for good.
    if (livePrimaryAnswers()) {
        delete m_server;
        m_server = nullptr;
        m_ownerLock->unlock();
        m_ownerLock.reset();
        return false;
    }

    QLocalServer::removeServer(m_serverName);
    if (m_server->listen(m_serverName))
        return true;

    // Still refused. Either another launch claimed the name in the interval
    // above — ask it again, and become its secondary if it answers — or
    // listening is impossible here for a reason that has nothing to do with
    // another instance (a read-only runtime directory, a name something else
    // owns). Only the second case proceeds without the channel, and it is the
    // same fail-open choice the vault lock makes: an editor that will not
    // start is a worse outcome than one running unguarded, and the vault lock
    // is still there.
    const bool secondary = livePrimaryAnswers();
    delete m_server;
    m_server = nullptr;
    if (secondary) {
        m_ownerLock->unlock();
        m_ownerLock.reset();
    }
    return !secondary;
}

bool SingleInstance::livePrimaryAnswers() const
{
    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (!probe.waitForConnected(200))
        return false;
    probe.abort();
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
