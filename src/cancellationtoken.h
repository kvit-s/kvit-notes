// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CANCELLATIONTOKEN_H
#define CANCELLATIONTOKEN_H

#include <QAtomicInt>
#include <QSharedPointer>

// Cooperative cancellation for background work.
//
// Qt's own cancellation does not cover the cases this codebase relies on. A
// QFuture returned by QtConcurrent::run() cannot be cancelled at all: calling
// cancel() on it marks the future cancelled for bookkeeping while the function
// keeps running to completion, so the caller either waits out work whose
// result it has already decided to discard, or abandons a task that goes on
// holding a pool thread. QtConcurrent::mapped() does better — cancel() stops
// further items being scheduled — but it still cannot interrupt an item that
// has started.
//
// The token closes that gap the only way it can be closed: the worker agrees
// to look. Whoever starts the work holds one end, the worker holds the other
// through a shared pointer, and the worker checks it at points where stopping
// is safe and cheap — between files, between directories, before a commit.
// Cancellation is therefore never immediate; it is bounded by the distance
// between two checks, which is the property to keep in mind when placing them.
//
// The flag only ever moves from clear to set. A token is not reused: work that
// has been called off stays called off, and the next task gets a new token.
class CancellationToken
{
public:
    void cancel() { m_cancelled.storeRelease(1); }
    bool isCancelled() const { return m_cancelled.loadAcquire() != 0; }

private:
    QAtomicInt m_cancelled{0};
};

using CancellationTokenPtr = QSharedPointer<CancellationToken>;

inline CancellationTokenPtr makeCancellationToken()
{
    return CancellationTokenPtr::create();
}

// True when there is a token and it has been signalled. Workers receive the
// token by value, and a default-constructed (null) one means "nobody can call
// this off", which is the right reading for a task started without one.
inline bool isCancelled(const CancellationTokenPtr &token)
{
    return token && token->isCancelled();
}

#endif // CANCELLATIONTOKEN_H
