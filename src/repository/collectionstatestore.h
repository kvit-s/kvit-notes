// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef COLLECTIONSTATESTORE_H
#define COLLECTIONSTATESTORE_H

#include <QFutureWatcher>
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
    //
    // The snapshot is taken here, on the caller's thread, because it reads
    // live collection state. Serialising it and writing the file happen on a
    // pool thread: the manual note order alone carries every note in every
    // folder, so both grow with the vault, and this used to run on the GUI
    // thread inside actions as small as expanding a folder.
    void save();

    // Same, but coalesced behind a short timer. Note switches, folder
    // expand/collapse and manual reordering all arrive in bursts where only
    // the final state is worth writing, and each one used to write the whole
    // file. Callers that need the state on disk before continuing use
    // flushIfDirty() instead.
    void saveDeferred();

    // Whether a write is still owed. Exposed for the I/O-failure tests, and
    // consulted by the owner before it closes or reopens a root.
    bool isDirty() const { return m_dirty; }

    // Write now if anything is owed, cancelling any pending debounce or
    // retry and waiting for a write already in flight. Called on close and
    // from the destructor, so an orderly exit does not lose state a timer was
    // still waiting to write. This one is synchronous by contract: closing a
    // root and reopening another both depend on the state being on disk, in
    // the vault it belongs to, before they continue.
    void flushIfDirty();

    // Waits for any write still in flight, so a store is never destroyed
    // while a pool thread is still reading the snapshot it was handed.
    ~CollectionStateStore() override;

signals:
    // Wired to the collection's operationFailed, so the message reaches the
    // same place every other repository failure does.
    void saveFailed(const QString &message);

private:
    // What the pool thread does with a snapshot, and what it reports back.
    struct WriteResult {
        bool ok = false;
        QString path;
    };
    static WriteResult writeSnapshot(Snapshot snapshot, QString path);

    QString filePath() const;
    // The shared body of save() and flushIfDirty(). Blocking runs the write
    // on the calling thread; the caller is then guaranteed it has happened.
    enum class Mode { Deferred, Blocking };
    void writeNow(Mode mode);
    void applyWriteResult(const WriteResult &result);
    void awaitInFlightWrite();

    QString m_rootPath;
    QString m_canonicalRoot;
    std::function<Snapshot()> m_provider;
    bool m_dirty = false;
    QTimer m_retryTimer;
    QTimer m_debounceTimer;
    QFutureWatcher<WriteResult> m_writeWatcher;
};

#endif // COLLECTIONSTATESTORE_H
