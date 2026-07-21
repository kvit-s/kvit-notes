// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef VAULTLOCK_H
#define VAULTLOCK_H

#include <QDateTime>
#include <QString>

// One writer per vault.
//
// Notes, sidecars, collection.json and the search index are all read into
// memory when a vault opens and written back whole. QSaveFile makes each of
// those writes atomic, so no file is ever half written, but atomicity says
// nothing about two processes: both load the same state, both change
// something different, and whichever saves second silently discards the
// other's work. tests/test_vaultlock.cpp demonstrates exactly that with two
// processes and a tag colour.
//
// The fix is an advisory lock on the vault rather than a revision or
// compare-and-swap protocol per artifact. A merge policy would have to be
// designed and maintained separately for note bodies, the JSON sidecar, the
// collection state blob and the SQLite index, and a notes app has no real
// multi-writer story to justify that. Refusing the second session is a
// smaller and more honest guarantee than four merge implementations that
// have to stay correct forever.
//
// What holds it is a kernel advisory lock (flock on POSIX, LockFileEx on
// Windows) on `<vault>/.kvit/vault.lock`, held open for the session. That
// choice is what makes a stale lock impossible rather than merely detectable:
// the kernel drops the lock when the owning process dies, whether it exited
// cleanly, crashed, was SIGKILLed, or was OOM-killed. Nothing has to guess
// whether a recorded process id is still alive, which also means nothing goes
// wrong when the recorded id belongs to another PID namespace -- a Flatpak
// session and a host session see different pid numbers but the same inode,
// so they contend correctly. A lock file copied onto another machine by a
// file sync carries no lock with it, so it cannot wedge a vault either.
//
// The file's contents are advisory only, used to tell the user who holds the
// vault. Correctness never depends on them.
class VaultLock
{
public:
    VaultLock() = default;
    ~VaultLock();
    VaultLock(const VaultLock &) = delete;
    VaultLock &operator=(const VaultLock &) = delete;

    enum class Result {
        Acquired,        // this session owns the vault
        HeldByAnother,   // another live process owns it; refuse to open
        Unavailable,     // locking could not be attempted; proceed unlocked
    };

    // What the acquirer intends to do with the vault. A writer contends for
    // the lock; a reader states that it will not write and therefore takes no
    // lock and is never refused. The distinction exists because "several
    // collections on one root in one process" covers two different things: a
    // second window that would save over the first, and a tool or a preview
    // that only reads. Only the first is a correctness problem, and only an
    // explicit mode can tell them apart.
    enum class Access {
        Write,
        Read,
    };

    // Who holds the vault, read from the lock file for the refusal message.
    // Advisory: the fields are whatever the other process wrote, and a
    // hostile or truncated file only degrades the wording.
    struct Holder {
        QString host;
        QString application;
        qint64 pid = 0;
        QDateTime since;
        // A sentence naming the holder, for the UI.
        QString describe() const;
    };

    // Take the lock for `vaultRoot`, creating `.kvit/vault.lock`.
    //
    // Unavailable is returned when the lock cannot be attempted at all: the
    // directory is read-only, the filesystem does not implement locking (some
    // network mounts), or the kernel refused for a reason other than
    // contention. That case proceeds unlocked rather than refusing, because a
    // vault that cannot be opened is a worse failure than one that is not
    // protected from a second session that probably does not exist.
    //
    // Acquiring twice within one process succeeds: the lock is against other
    // processes, and one process legitimately holds several NoteCollection
    // objects on a root. Ownership is reference counted per canonical path,
    // so a POSIX flock on a second descriptor cannot make a process refuse
    // itself.
    //
    // A lock this object already holds is kept when the new vault cannot be
    // taken. Releasing first and then failing left the previous vault open in
    // the application but unlocked on disk, which is the one state the lock
    // exists to prevent: another process could open it and start writing
    // whole-file snapshots underneath a session that is still showing it.
    Result acquire(const QString &vaultRoot, Access access = Access::Write);

    void release();
    bool isHeld() const { return !m_root.isEmpty(); }
    QString root() const { return m_root; }
    Access access() const { return m_access; }
    // Whether a kernel lock is actually held. False for a read-only
    // acquisition, and false when the platform could not lock at all — the
    // deliberate fail-open case the user has to be told about, since the
    // vault is then open with none of the protection the lock provides.
    bool holdsNativeLock() const { return m_holdsNativeLock; }

    // Valid after acquire() returned HeldByAnother.
    Holder blockingHolder() const { return m_blockingHolder; }

    // Test seam: pretend the platform cannot lock, to exercise the
    // proceed-unlocked path without needing an exotic filesystem.
    static void setForcedUnavailableForTests(bool forced);

private:
    // Assumes the ownership table's mutex is already held.
    void releaseLocked();

    QString m_root;                 // canonical, empty when not held
    Access m_access = Access::Write;
    bool m_holdsNativeLock = false;
    Holder m_blockingHolder;
};

#endif // VAULTLOCK_H
