// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef PERSISTENCEPOOL_H
#define PERSISTENCEPOOL_H

class QThreadPool;

// The thread pool that saves run on.
//
// Everything else in the app — vault scans, note parsing, directory
// refreshes, index writes, diagram layout — runs on the global pool, and a
// bulk scan can occupy every thread in it. A save submitted at that moment
// waits for one to come free. The wait is short when the work in front of it
// is short, but it scales with whatever the background happens to be doing,
// and the one operation that must not be at the mercy of that is writing the
// user's text to disk.
//
// Persistence therefore gets a pool of its own. It is small on purpose: the
// writes it carries are serialized against each other anyway (one active save,
// one active journal write), so extra threads would buy nothing, and the point
// is separation from bulk work rather than parallelism.
QThreadPool *persistenceThreadPool();

#endif // PERSISTENCEPOOL_H
