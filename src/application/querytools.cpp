// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "querytools.h"

#include <QVariantList>
#include <QtConcurrent/QtConcurrentRun>

#include "notecollection.h"
#include "querydata.h"

QueryTools::QueryTools(QObject *parent)
    : QObject(parent)
{
}

QueryTools::~QueryTools()
{
    cancelPending();
}

void QueryTools::setCollection(NoteCollection *collection)
{
    if (m_collection == collection)
        return;
    if (m_rootConnection)
        disconnect(m_rootConnection);
    cancelPending();
    m_collection = collection;
    ++m_collectionGeneration;
    m_cache.clear();
    m_snapshotValid = false;
    m_snapshot = QueryData::Snapshot();
    if (m_collection) {
        m_rootConnection = connect(m_collection, &NoteCollection::rootChanged,
                                   this, [this] {
            cancelPending();
            ++m_collectionGeneration;
            m_cache.clear();
            m_snapshotValid = false;
            m_snapshot = QueryData::Snapshot();
        });
    }
}

void QueryTools::clearCache()
{
    m_cache.clear();
    m_evaluationCount = 0;
    m_snapshotValid = false;
    m_snapshot = QueryData::Snapshot();
}

void QueryTools::cancelPending()
{
    const QList<Pending *> pending = m_pending;
    m_pending.clear();
    for (Pending *entry : pending) {
        if (entry->watcher) {
            // The worker holds a copy of the snapshot rather than a pointer
            // into this object, but the watcher does not, so it cannot be
            // left running past here.
            const QSignalBlocker blocker(entry->watcher);
            entry->watcher->waitForFinished();
            entry->watcher->deleteLater();
        }
        delete entry;
    }
}

namespace {

QVariantMap rowMap(const QueryData::Row &row)
{
    return {
        {QStringLiteral("relPath"), row.relPath},
        {QStringLiteral("cells"), QVariant(row.cells)},
    };
}

QVariantMap emptyResult()
{
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("view"), QStringLiteral("table")},
            {QStringLiteral("columns"), QStringList()},
            {QStringLiteral("rows"), QVariantList()},
            {QStringLiteral("groups"), QVariantList()}};
}

// The whole evaluation, with nothing of QueryTools touched: this is what
// runs on the pool thread. Both arguments are values the caller handed over.
QVariantMap evaluateSpec(QueryData::Spec spec, bool board,
                         QueryData::Snapshot snapshot)
{
    const QueryData::Result result = QueryData::evaluate(spec, snapshot);

    QVariantList rows;
    for (const QueryData::Row &row : result.rows)
        rows.append(rowMap(row));
    QVariantList groups;
    for (const QueryData::Group &group : result.groups) {
        QVariantList cards;
        for (const QueryData::Row &row : group.rows)
            cards.append(rowMap(row));
        groups.append(QVariantMap{
            {QStringLiteral("name"), group.name},
            {QStringLiteral("cards"), cards},
        });
    }

    QVariantMap out = emptyResult();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("view"),
               board ? QStringLiteral("board") : QStringLiteral("table"));
    out.insert(QStringLiteral("columns"), result.columns);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("groups"), groups);
    return out;
}

} // namespace

int QueryTools::rowCountOf(const QVariantMap &result)
{
    return result.value(QStringLiteral("rows")).toList().size();
}

const QueryData::Snapshot &QueryTools::snapshotFor(int revision)
{
    if (m_snapshotValid && m_snapshotRevision == revision
        && m_snapshotGeneration == m_collectionGeneration) {
        return m_snapshot;
    }
    m_snapshot = m_collection ? QueryData::snapshotOf(*m_collection)
                              : QueryData::Snapshot();
    m_snapshotRevision = revision;
    m_snapshotGeneration = m_collectionGeneration;
    m_snapshotValid = true;
    return m_snapshot;
}

QVariantMap QueryTools::cachedResult(const QString &body) const
{
    const int revision = m_collection ? m_collection->revision() : -1;
    for (const CacheEntry &entry : m_cache) {
        if (entry.generation == m_collectionGeneration
            && entry.revision == revision && entry.body == body)
            return entry.result;
    }
    return QVariantMap();
}

void QueryTools::cacheResult(const QString &body, int revision,
                             const QVariantMap &result)
{
    CacheEntry entry;
    entry.generation = m_collectionGeneration;
    entry.revision = revision;
    entry.body = body;
    entry.result = result;
    entry.rows = rowCountOf(result);
    m_cache.prepend(entry);
    pruneCache(revision);
}

void QueryTools::pruneCache(int revision)
{
    // A result computed against a revision that has been superseded can
    // never be served again, so it is dropped now rather than waiting to
    // fall off the end of a least-recently-used list.
    for (int i = m_cache.size() - 1; i >= 0; --i) {
        const CacheEntry &entry = m_cache.at(i);
        if (entry.generation != m_collectionGeneration
            || entry.revision != revision)
            m_cache.removeAt(i);
    }

    int rows = 0;
    for (int i = 0; i < m_cache.size(); ++i) {
        rows += m_cache.at(i).rows;
        // Keep the most recent entry whatever it costs — evicting the result
        // that was just asked for would mean never serving it.
        if (i > 0 && (i >= MaxCacheEntries || rows > MaxCacheRows)) {
            while (m_cache.size() > i)
                m_cache.removeLast();
            break;
        }
    }
}

QVariantMap QueryTools::evaluateNow(const QString &body, int revision)
{
    ++m_evaluationCount;

    const QueryData::ParseResult parsed = QueryData::parse(body);
    if (!parsed.ok) {
        QVariantMap out = emptyResult();
        out.insert(QStringLiteral("error"), parsed.error);
        return out;
    }
    if (!m_collection) {
        QVariantMap out = emptyResult();
        out.insert(QStringLiteral("error"),
                   QStringLiteral("no collection is open"));
        return out;
    }
    return evaluateSpec(parsed.spec,
                        parsed.spec.view == QueryData::View::Board,
                        snapshotFor(revision));
}

QVariantMap QueryTools::run(const QString &body)
{
    const int revision = m_collection ? m_collection->revision() : -1;
    for (int i = 0; i < m_cache.size(); ++i) {
        const CacheEntry &entry = m_cache.at(i);
        if (entry.generation != m_collectionGeneration
            || entry.revision != revision || entry.body != body)
            continue;
        const QVariantMap result = entry.result;
        if (i > 0)
            m_cache.move(i, 0);
        return result;
    }

    const QVariantMap out = evaluateNow(body, revision);
    cacheResult(body, revision, out);
    return out;
}

void QueryTools::requestRun(const QString &token, const QString &body)
{
    const int revision = m_collection ? m_collection->revision() : -1;

    // Already computed for this revision: answer at once rather than making
    // the caller wait a turn of the event loop for something it has.
    for (int i = 0; i < m_cache.size(); ++i) {
        const CacheEntry &entry = m_cache.at(i);
        if (entry.generation != m_collectionGeneration
            || entry.revision != revision || entry.body != body)
            continue;
        const QVariantMap result = entry.result;
        if (i > 0)
            m_cache.move(i, 0);
        emit resultReady(token, result);
        return;
    }

    // A parse error needs no scan and no thread; it is also the answer to a
    // body the user is still typing, which is most of them.
    const QueryData::ParseResult parsed = QueryData::parse(body);
    if (!parsed.ok || !m_collection) {
        ++m_evaluationCount;
        QVariantMap out = emptyResult();
        out.insert(QStringLiteral("error"),
                   parsed.ok ? QStringLiteral("no collection is open")
                             : parsed.error);
        cacheResult(body, revision, out);
        emit resultReady(token, out);
        return;
    }

    // The same query already running for this revision: wait on it instead
    // of scanning the vault a second time. This is what keeps N query blocks
    // refreshing after one collection change from costing N scans.
    for (Pending *pending : m_pending) {
        if (pending->generation == m_collectionGeneration
            && pending->revision == revision && pending->body == body) {
            if (!pending->tokens.contains(token))
                pending->tokens.append(token);
            return;
        }
    }

    ++m_evaluationCount;
    auto *pending = new Pending;
    pending->generation = m_collectionGeneration;
    pending->revision = revision;
    pending->body = body;
    pending->tokens.append(token);
    pending->watcher = new QFutureWatcher<QVariantMap>(this);
    m_pending.append(pending);

    connect(pending->watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, pending] { finishPending(pending); });

    pending->watcher->setFuture(QtConcurrent::run(
        evaluateSpec, parsed.spec,
        parsed.spec.view == QueryData::View::Board, snapshotFor(revision)));
}

void QueryTools::finishPending(Pending *pending)
{
    const int index = m_pending.indexOf(pending);
    if (index < 0)
        return;                     // cancelled; nothing to deliver
    m_pending.removeAt(index);

    const bool current = pending->generation == m_collectionGeneration
        && m_collection
        && pending->revision == m_collection->revision();
    const bool haveResult = pending->watcher
        && pending->watcher->future().resultCount() > 0;

    if (current && haveResult) {
        const QVariantMap result = pending->watcher->result();
        cacheResult(pending->body, pending->revision, result);
        for (const QString &token : pending->tokens)
            emit resultReady(token, result);
    }
    // Otherwise the collection moved on while this ran. The result describes
    // a state nothing is showing any more, and every waiting block has
    // already asked again against the new revision, so it is dropped rather
    // than delivered or cached.

    if (pending->watcher)
        pending->watcher->deleteLater();
    delete pending;
}
