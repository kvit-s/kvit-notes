// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "collectionsearch.h"

#include "block.h"
#include "blockeditorengine.h"
#include "collectionsearchindex.h"
#include "documentserializer.h"
#include "notecollection.h"
#include "notefrontmatter.h"

#include <QDateTime>
#include <QFile>
#include <QVariantMap>

namespace {

QVariantList groupsToVariant(const SearchResults &results)
{
    QVariantList groups;
    groups.reserve(results.groups.size());
    for (const SearchGroup &group : results.groups) {
        QVariantList matches;
        matches.reserve(group.matches.size());
        for (const SearchMatch &match : group.matches) {
            matches.append(QVariantMap{
                {QStringLiteral("blockIndex"), match.blockIndex},
                {QStringLiteral("start"), match.start},
                {QStringLiteral("length"), match.length},
                {QStringLiteral("snippet"), match.snippet},
                {QStringLiteral("snippetStart"), match.snippetStart},
                {QStringLiteral("snippetLength"), match.snippetLength},
            });
        }
        groups.append(QVariantMap{
            {QStringLiteral("relPath"), group.relPath},
            {QStringLiteral("title"), group.title},
            {QStringLiteral("titleMatched"), group.titleMatched},
            {QStringLiteral("matchCount"), group.matchCount},
            {QStringLiteral("moreMatches"), group.moreMatches},
            {QStringLiteral("matches"), matches},
        });
    }
    return groups;
}

} // namespace

CollectionSearch::CollectionSearch(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &CollectionSearch::submit);
}

void CollectionSearch::setCollection(NoteCollection *collection)
{
    if (m_collection)
        disconnect(m_collection, nullptr, this, nullptr);
    m_collection = collection;
    if (m_collection) {
        connect(m_collection, &NoteCollection::revisionChanged, this,
                &CollectionSearch::scheduleQuery);
        connect(m_collection, &NoteCollection::rootChanged, this,
                &CollectionSearch::scheduleQuery);
    }
    scheduleQuery();
}

void CollectionSearch::setSearchIndex(CollectionSearchIndex *index)
{
    if (m_index)
        disconnect(m_index, nullptr, this, nullptr);
    m_index = index;
    if (m_index) {
        connect(m_index, &CollectionSearchIndex::queryFinished, this,
                &CollectionSearch::onQueryFinished);
        // A completed rebuild or a per-note replace can change results under a
        // live query; re-run so the snapshot and completeness flag catch up.
        connect(m_index, &CollectionSearchIndex::indexUpdated, this,
                &CollectionSearch::scheduleQuery);
        connect(m_index, &CollectionSearchIndex::indexingChanged, this,
                &CollectionSearch::indexingChanged);
        connect(m_index, &CollectionSearchIndex::indexingChanged, this,
                &CollectionSearch::scheduleQuery);
    }
    scheduleQuery();
}

bool CollectionSearch::indexing() const
{
    return m_index && m_index->isIndexing();
}

void CollectionSearch::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::setFolderScope(const QString &folderScope)
{
    if (m_folderScope == folderScope)
        return;
    m_folderScope = folderScope;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::setTagFilter(const QString &tagFilter)
{
    if (m_tagFilter == tagFilter)
        return;
    m_tagFilter = tagFilter;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::setDatePreset(const QString &datePreset)
{
    if (m_datePreset == datePreset)
        return;
    m_datePreset = datePreset;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::setCustomFrom(const QDate &date)
{
    if (m_customFrom == date)
        return;
    m_customFrom = date;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::setCustomTo(const QDate &date)
{
    if (m_customTo == date)
        return;
    m_customTo = date;
    emit inputChanged();
    scheduleQuery();
}

void CollectionSearch::scheduleQuery()
{
    // An empty query is inert and applies immediately: the ordinary editing
    // path pays nothing (search.md §4.1).
    if (m_query.trimmed().isEmpty()) {
        m_debounce.stop();
        publishEmpty();
        return;
    }
    m_debounce.start();
}

void CollectionSearch::submitNow()
{
    m_debounce.stop();
    submit();
}

void CollectionSearch::submit()
{
    if (m_query.trimmed().isEmpty()) {
        publishEmpty();
        return;
    }
    if (!m_collection || !m_collection->isOpen() || !m_index
        || !m_index->isUsable()) {
        // No usable index (missing FTS5 in a development build, or no open
        // collection): report nothing rather than a stale or partial count.
        publishEmpty();
        return;
    }

    SearchQuery request;
    request.query = m_query;
    request.folderScope = m_folderScope;
    request.tagFilter = m_tagFilter;
    request.datePreset = m_datePreset;
    request.customFrom = m_customFrom;
    request.customTo = m_customTo;
    request.nowMs = QDateTime::currentMSecsSinceEpoch();

    m_generation += 1;
    m_index->submitQuery(m_generation, request);
}

void CollectionSearch::onQueryFinished(quint64 generation,
                                       SearchResults results)
{
    // Keep only the latest generation (search.md §7).
    if (generation < m_accepted)
        return;
    m_accepted = generation;
    applyResults(results, !indexing());
}

void CollectionSearch::publishEmpty()
{
    applyResults(SearchResults(), !indexing());
}

void CollectionSearch::applyResults(const SearchResults &results, bool complete)
{
    const QVariantList before = m_groups;
    const int beforeMatchCount = m_matchCount;
    const bool beforeComplete = m_complete;

    m_groups = groupsToVariant(results);
    m_matchCount = results.matchCount;
    m_complete = complete;

    if (m_groups == before && m_matchCount == beforeMatchCount
        && m_complete == beforeComplete)
        return; // nothing observable changed
    ++m_revision;
    emit revisionChanged();
}

int CollectionSearch::markdownPosition(const QString &relPath, int blockIndex,
                                       int displayStart) const
{
    if (!m_collection)
        return 0;
    const QString absPath = m_collection->absolutePath(relPath);
    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QString fileText = QString::fromUtf8(file.readAll());
    const NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);

    DocumentSerializer serializer;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(split.body);
    if (blockIndex < 0 || blockIndex >= blocks.size())
        return 0;
    const DocumentSerializer::BlockData &block = blocks.at(blockIndex);
    if (block.type == Block::CodeBlock)
        return displayStart; // verbatim: display IS markdown
    return BlockEditorEngine::documentToMarkdown(block.content, QList<int>(),
                                                 displayStart);
}
