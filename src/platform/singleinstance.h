// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

// The single-instance channel: a per-user lock elects the first process, then
// a named local endpoint (a named pipe on Windows, a Unix domain socket
// elsewhere) carries requests to it.
//
// A later launch connects to that endpoint, forwards its request (a folder
// path, a file path, or an empty string for a bare launch), and exits, so the
// running process acts on it — opening or raising the right window — instead of
// a second tray-resident process accumulating. The kernel vault lock stays the
// backstop for a genuinely separate process (a different user, or a shared
// filesystem); this only removes the same-user, same-install pile-up.
//
// The endpoint name folds in a stable per-user location and the executable
// path, so two users, or a development build and an installed one, never share
// an instance.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &serverName, QObject *parent = nullptr);
    ~SingleInstance() override;

    // A stable per-user, per-install endpoint name.
    static QString defaultServerName();

    // Attempt to own the endpoint. Returns true when this process becomes the
    // primary (the caller sets up windows and connects requestReceived); false
    // when a primary is already running (the caller forwards and exits). A
    // stale endpoint left by a crashed primary is reclaimed.
    bool tryBecomePrimary();

    // As a secondary, hand `request` to the primary. Returns true when it was
    // delivered.
    bool forwardToPrimary(const QString &request);

signals:
    // A secondary forwarded a request; the primary opens or raises for it.
    void requestReceived(const QString &request);

private:
    // Whether something is accepting connections on the endpoint right now.
    // The only way to tell a running primary from the socket file a crashed
    // one left behind, since both make listen() fail.
    bool livePrimaryAnswers() const;
    void onNewConnection();

    QString m_serverName;
    // QLocalServer::listen() is not exclusive on Windows: multiple servers may
    // listen on the same named pipe and Windows distributes connections among
    // them. QLockFile is the actual atomic owner election on every platform;
    // the local server is transport only.
    std::unique_ptr<QLockFile> m_ownerLock;
    QLocalServer *m_server = nullptr;
};

#endif // SINGLEINSTANCE_H
