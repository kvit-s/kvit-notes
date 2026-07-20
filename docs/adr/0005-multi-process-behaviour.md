# 0005. Two instances on one notes root

**Status:** Accepted

## Context

[ADR 0002](0002-exclusive-write-ownership.md) establishes that one object owns
writes to the open note, and everything in it holds within a single process.
Nothing extended that guarantee across processes.

The exposure comes from how vault state is written. Notes, the note-list
sidecar, `collection.json` and the search index are read into memory when a
vault opens and written back whole. `QSaveFile` makes each write atomic, which
prevents a half-written file and nothing else. Two processes both load the same
state, each changes something different, and whichever saves second silently
discards the other's work.

This was demonstrated before anything was built. A test launches two real
processes that each set a different tag color on one vault; the run reported the
final `tagColors` as `("beta", "seed")`, with the first process's `"alpha"`
simply gone. `QSaveFile` had done its job on every individual write, and the
data was lost anyway.

## Decision

One writer per vault, enforced by a kernel advisory lock. A second session
refuses the vault and explains who holds it.

**A lock rather than a revision protocol.** A compare-and-swap or revision
scheme would need a merge policy designed and maintained separately for note
bodies, the JSON sidecar, the collection state blob and the SQLite index, and
that policy would have to stay correct on every future write path. A notes
application has no multi-writer story worth that cost. Refusing the second
session is a smaller guarantee that cannot rot.

**The file is not the lock.** `VaultLock` takes a kernel advisory lock on
`<vault>/.kvit/vault.lock` and holds the descriptor open for the session.
`NoteCollection` acquires it in `prepareRootPath()`, before any vault state is
read, and releases it in `closeRoot()`. The file's contents are advisory JSON
naming the host, application, process id and timestamp, used only to tell the
user who holds the vault. Correctness never reads them, so a truncated or
hostile file makes the message vaguer and changes nothing else.

**A kernel lock rather than a PID file.** The kernel releases the lock when the
process dies by any means, so a stale lock is impossible rather than merely
detectable, and a hard kill can never leave a vault that will not open. It is
also the only option that behaves correctly across a PID namespace. `QLockFile`
decides staleness by reading a recorded process id, so it would conclude that a
live owner inside a Flatpak sandbox was dead and steal the lock, because the
sandbox and the host see different process numbers for the same process. A
kernel lock needs no such inference: both sessions see the same inode and
contend correctly. A lock file copied to another machine by a file sync carries
no lock with it, so it cannot wedge a vault either. This was
checked with `bwrap --unshare-pid` in both directions, and with SIGKILL
recovery, rather than assumed.

**Failure is open.** Only genuine contention refuses a vault. A filesystem
without locking, a read-only directory, or any other kernel error opens the
vault unlocked with a warning on the `kvit.vaultlock` logging category. A vault
nobody can open is a worse failure than an unguarded one.

**Ownership is reference counted per canonical path.** One process legitimately
holds several `NoteCollection` objects on one root, and a POSIX `flock` on a
second descriptor fails against the holder's own process.

**Behavior is identical on Windows and Linux.** POSIX uses
`flock(fd, LOCK_EX | LOCK_NB)` and Windows uses
`LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, ...)`.
Both are exclusive, both fail immediately rather than blocking, and both are
released by the operating system on process death. The Windows handle is opened
with `FILE_SHARE_READ | FILE_SHARE_WRITE` specifically so that the blocked
process can still read the holder description for its message.

**The second session refuses and explains.** `NoteCollection::openRoot()`
returns false and emits `vaultInUse(path, detail)`, where the detail names the
holder in a sentence, and the shell shows that alongside an explanation that one
vault can be open in one window. It does not open read-only and does not hand
off. Read-only would mean auditing every mutation path to be certain nothing
slips through, and half-enforced read-only is worse than a refusal. Handing off
to the running instance is a better experience and a deliberately deferred
feature, needing single-instance IPC through `QLocalServer` and a window-raise
protocol. The refused window stays usable, so opening individual files still
works.

**Single-file mode takes no lock** and creates no lock file, because it opens no
collection and none of the shared state above exists. Two editors on one file
remain the file watcher's problem, which it already answers with the
keep-mine/load-theirs banner.

## Consequences

The single-process guarantees in ADR 0002 now describe the system rather than
one process, for vault mode. The lost-update scenario is prevented rather than
merely unlikely, and the failure a user meets is a refusal with an explanation
instead of silently vanishing work.

The costs are the ones a lock always carries. Two windows on one vault is no
longer possible, which some users expect from other editors; the handoff that
would restore that experience is unbuilt and needs single-instance IPC. Fail-open
means the guarantee is absent exactly where locking is unavailable, such as some
network filesystems, and the warning goes to a logging category rather than the
user. That is the deliberate trade: refusing to open a vault because its
filesystem lacks `flock` would be worse than proceeding without the guarantee.

Ten tests in `VaultLockTests` drive real second processes, re-execing through
`QProcess` rather than simulating contention. They cover the prevented lost
update, the refusal and its message, a SIGKILLed owner's lock disappearing,
release on close, several collections in one process sharing the lock,
single-file mode
taking none, an unlockable filesystem still opening, and a corrupt lock file
still yielding a sane message.

## Evidence in the tree

- `src/vaultlock.h`, `src/vaultlock.cpp`: the lock, and the reasoning for a kernel lock over a PID file
- `src/notecollection.h`, `src/notecollection.cpp`: acquisition, reference counting, `vaultInUse`
- `qml/main.qml`: the message shown to the refused session
- `tests/test_vaultlock.cpp`: the two-process lost-update demonstration and the ten behavioral tests
- `devel.md`: the working notes on the lock
- Commit `2e5ce05` "Hold one writer per vault, so a second session cannot overwrite the first"
