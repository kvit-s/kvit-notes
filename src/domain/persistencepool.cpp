// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "persistencepool.h"

#include <QThreadPool>

QThreadPool *persistenceThreadPool()
{
    // Function-local static: constructed on first use, after QCoreApplication
    // exists, and destroyed at exit. QThreadPool's destructor waits for its
    // threads, so a save in flight at shutdown completes rather than being
    // torn out from under QSaveFile.
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        // Two threads: a save and a journal write can overlap, and nothing
        // else runs here. More would add contention for the same files
        // without adding throughput.
        created->setMaxThreadCount(2);
        // These are user data reaching disk; they should not be the tasks
        // that wait.
        created->setThreadPriority(QThread::HighPriority);
        return created;
    }();
    return pool;
}
