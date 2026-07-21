// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "querytools.h"

#include <QVariantList>

#include "notecollection.h"
#include "querydata.h"

QueryTools::QueryTools(QObject *parent)
    : QObject(parent)
{
}

void QueryTools::setCollection(NoteCollection *collection)
{
    if (m_collection == collection)
        return;
    if (m_rootConnection)
        disconnect(m_rootConnection);
    m_collection = collection;
    ++m_collectionGeneration;
    m_cache.clear();
    if (m_collection) {
        m_rootConnection = connect(m_collection, &NoteCollection::rootChanged,
                                   this, [this] {
            ++m_collectionGeneration;
            m_cache.clear();
        });
    }
}

void QueryTools::clearCache()
{
    m_cache.clear();
    m_evaluationCount = 0;
}

namespace {

QVariantMap rowMap(const QueryData::Row &row)
{
    return {
        {QStringLiteral("relPath"), row.relPath},
        {QStringLiteral("cells"), QVariant(row.cells)},
    };
}

} // namespace

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

    ++m_evaluationCount;
    QVariantMap out{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QString()},
                    {QStringLiteral("view"), QStringLiteral("table")},
                    {QStringLiteral("columns"), QStringList()},
                    {QStringLiteral("rows"), QVariantList()},
                    {QStringLiteral("groups"), QVariantList()}};

    const QueryData::ParseResult parsed = QueryData::parse(body);
    if (!parsed.ok) {
        out.insert(QStringLiteral("error"), parsed.error);
        m_cache.prepend({m_collectionGeneration, revision, body, out});
        if (m_cache.size() > MaxCacheEntries)
            m_cache.removeLast();
        return out;
    }
    if (!m_collection) {
        out.insert(QStringLiteral("error"),
                   QStringLiteral("no collection is open"));
        m_cache.prepend({m_collectionGeneration, revision, body, out});
        if (m_cache.size() > MaxCacheEntries)
            m_cache.removeLast();
        return out;
    }

    const QueryData::Result result =
        QueryData::evaluate(parsed.spec, *m_collection);

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

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("view"),
               parsed.spec.view == QueryData::View::Board
                   ? QStringLiteral("board") : QStringLiteral("table"));
    out.insert(QStringLiteral("columns"), result.columns);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("groups"), groups);
    m_cache.prepend({m_collectionGeneration, revision, body, out});
    if (m_cache.size() > MaxCacheEntries)
        m_cache.removeLast();
    return out;
}
