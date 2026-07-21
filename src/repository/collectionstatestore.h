// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef COLLECTIONSTATESTORE_H
#define COLLECTIONSTATESTORE_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

// Workspace state: <root>/.kvit/collection.json.
//
// Four things that exist in no Markdown file — tag colours, per-folder colour
// and expansion, the manual note order inside each folder, and which note was
// last open. Losing them is not losing notes, but it is losing work the user
// did, and it used to be possible to lose all of it to one failed write that
// nothing reported.
//
// So a failed save is retained rather than dropped: the dirty flag stays set,
// a bounded retry runs on a timer, the next mutation tries again, and closing
// or destroying the collection flushes synchronously. The error stays visible
// until a write succeeds.
//
// The snapshot is built on demand through a provider the owner installs,
// rather than handed in at save time, so a retry writes the state as it is
// when the retry runs rather than as it was when the failure happened.
class CollectionStateStore : public QObject
{
    Q_OBJECT

public:
    // One folder's appearance, as the file carries it. Kept separate from
    // the folder index: an entry here can name a folder that no longer
    // exists, and the caller drops those against the folders it actually has.
    struct FolderVisual {
        QString color;
        bool expanded = true;
    };

    struct Snapshot {
        QHash<QString, QString> tagColors;
        QHash<QString, FolderVisual> folders;
        QHash<QString, QStringList> manualOrder;
        QString lastOpenNote;
    };

    explicit CollectionStateStore(QObject *parent = nullptr);

    // The vault root and its canonical form. The canonical root is what
    // decides whether a persisted last-open note is inside the vault.
    void setRoot(const QString &rootPath, const QString &canonicalRoot);

    // Where the state is built from when a save runs. Must outlive the store.
    void setSnapshotProvider(std::function<Snapshot()> provider);

    // Read the file. Absent, unreadable or corrupt all give defaults and
    // touch nothing: workspace state is never worth failing an open over.
    //
    // lastOpenNote comes back empty unless it is a plain relative path to a
    // regular file inside the canonical root. A vault is shared and editable
    // by other tools, so this value is untrusted input like any other.
    Snapshot load() const;

    // Build a snapshot through the provider and write it. On failure the
    // dirty flag stays set, a retry is scheduled, and saveFailed carries a
    // message the user can act on.
    void save();

    // Whether a write is still owed. Exposed for the I/O-failure tests, and
    // consulted by the owner before it closes or reopens a root.
    bool isDirty() const { return m_dirty; }

    // Write now if anything is owed, cancelling any pending retry. Called on
    // close and from the destructor, so an orderly exit does not lose state
    // a retry timer was still waiting to write.
    void flushIfDirty();

signals:
    // Wired to the collection's operationFailed, so the message reaches the
    // same place every other repository failure does.
    void saveFailed(const QString &message);

private:
    QString filePath() const;

    QString m_rootPath;
    QString m_canonicalRoot;
    std::function<Snapshot()> m_provider;
    bool m_dirty = false;
    QTimer m_retryTimer;
};

#endif // COLLECTIONSTATESTORE_H
