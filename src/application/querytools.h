// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QUERYTOOLS_H
#define QUERYTOOLS_H

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QList>
#include <QMetaObject>
#include <QString>
#include <QVariantMap>

#include "querydata.h"

class NoteCollection;

// QML seam for the collection query block,
// following the TableTools/KanbanTools pattern: pure QueryData stays
// independently testable; this object is glue only.
//
// Evaluating a query scans every note in the collection, sorts the matches
// and builds a QVariant tree out of them, so it does not belong on the
// thread that draws frames — a 1,000-note query measured most of a 60 Hz
// frame, and several query blocks refresh together after one collection
// change. requestRun() therefore evaluates on a pool thread against an
// immutable snapshot and answers with resultReady().
//
// Three things keep that from multiplying:
//
//   - one snapshot per collection revision, shared by every query taken at
//     that revision;
//   - requests for the same body and revision are coalesced, so N blocks
//     showing the same query cost one evaluation rather than N;
//   - results from a superseded revision are dropped rather than delivered,
//     because the blocks have already asked again against the new one.
class QueryTools : public QObject
{
    Q_OBJECT

public:
    explicit QueryTools(QObject *parent = nullptr);
    ~QueryTools() override;

    void setCollection(NoteCollection *collection);

    // Parse + evaluate the fence body against the collection. Returns
    //   { ok, error, view, columns,
    //     rows:   [{relPath, cells: [...]}, ...],
    //     groups: [{name, cards: [{relPath, cells}]}, ...] }
    // ok=false carries the parse error the block shows in read mode.
    //
    // Synchronous, and kept for callers that are measuring the evaluator
    // itself or have nowhere to wait. The query block uses requestRun().
    Q_INVOKABLE QVariantMap run(const QString &body);

    // The cached result for this body at the collection's current revision,
    // or an empty map when there is none. Lets a block paint a result it has
    // already paid for without waiting a frame for the signal.
    Q_INVOKABLE QVariantMap cachedResult(const QString &body) const;

    // Evaluate off the GUI thread and answer with resultReady(token, ...).
    // The token is the caller's own identity — the block id — and comes back
    // unchanged so one shared instance can serve every query block.
    Q_INVOKABLE void requestRun(const QString &token, const QString &body);

    // Deterministic cache seams used by the C++/QML release-gate tests.
    Q_INVOKABLE int evaluationCount() const { return m_evaluationCount; }
    Q_INVOKABLE int cacheSize() const { return m_cache.size(); }
    Q_INVOKABLE void clearCache();

signals:
    void resultReady(const QString &token, const QVariantMap &result);

private:
    struct CacheEntry {
        quint64 generation = 0;
        int revision = 0;
        QString body;
        QVariantMap result;
        int rows = 0;    // what this entry costs, for the cache budget
    };

    // One evaluation running on the pool, and everyone waiting for it.
    struct Pending {
        quint64 generation = 0;
        int revision = 0;
        QString body;
        QStringList tokens;
        QFutureWatcher<QVariantMap> *watcher = nullptr;
    };

    QVariantMap evaluateNow(const QString &body, int revision);
    void cacheResult(const QString &body, int revision,
                     const QVariantMap &result);
    // Drops everything from a superseded revision or collection, then trims
    // what is left to the row budget.
    void pruneCache(int revision);
    const QueryData::Snapshot &snapshotFor(int revision);
    void finishPending(Pending *pending);
    void cancelPending();
    static int rowCountOf(const QVariantMap &result);

    NoteCollection *m_collection = nullptr;
    QMetaObject::Connection m_rootConnection;
    QList<CacheEntry> m_cache; // MRU first; deliberately bounded and tiny
    quint64 m_collectionGeneration = 0;
    int m_evaluationCount = 0;
    QList<Pending *> m_pending;

    // The snapshot every query at m_snapshotRevision evaluates against.
    QueryData::Snapshot m_snapshot;
    int m_snapshotRevision = -1;
    quint64 m_snapshotGeneration = 0;
    bool m_snapshotValid = false;

    static constexpr int MaxCacheEntries = 64;
    // Entries were bounded by count alone, so one unlimited query over a
    // large vault could make a single entry enormous and 64 of them
    // enormous times 64. Rows are what a result actually costs.
    static constexpr int MaxCacheRows = 20000;
};

#endif // QUERYTOOLS_H
