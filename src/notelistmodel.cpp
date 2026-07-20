// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notelistmodel.h"
#include "notecollection.h"

#include <algorithm>
#include <QSet>

NoteListModel::NoteListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(20);
    connect(&m_rebuildTimer, &QTimer::timeout,
            this, &NoteListModel::rebuild);
}

void NoteListModel::setCollection(NoteCollection *collection)
{
    if (m_collection)
        disconnect(m_collection, nullptr, this, nullptr);
    m_collection = collection;
    if (m_collection) {
        connect(m_collection, &NoteCollection::revisionChanged,
                this, &NoteListModel::scheduleRebuild);
        connect(m_collection, &NoteCollection::rootChanged,
                this, &NoteListModel::scheduleRebuild);
    }
    rebuild();
}

void NoteListModel::setScope(const QString &scope)
{
    if (m_scope == scope)
        return;
    m_scope = scope;
    emit projectionChanged();
    rebuild();
}

void NoteListModel::setFolderPath(const QString &folderPath)
{
    if (m_folderPath == folderPath)
        return;
    m_folderPath = folderPath;
    emit projectionChanged();
    rebuild();
}

void NoteListModel::setSortMode(const QString &sortMode)
{
    if (m_sortMode == sortMode)
        return;
    m_sortMode = sortMode;
    emit projectionChanged();
    rebuild();
}

void NoteListModel::setAscending(bool ascending)
{
    if (m_ascending == ascending)
        return;
    m_ascending = ascending;
    emit projectionChanged();
    rebuild();
}

void NoteListModel::setTagFilter(const QString &tagFilter)
{
    if (m_tagFilter == tagFilter)
        return;
    m_tagFilter = tagFilter;
    emit projectionChanged();
    rebuild();
}

void NoteListModel::rebuild()
{
    if (m_rebuildTimer.isActive())
        m_rebuildTimer.stop();
    applyRows(projectedRows());
}

void NoteListModel::scheduleRebuild()
{
    if (!m_rebuildTimer.isActive())
        m_rebuildTimer.start();
}

QStringList NoteListModel::projectedRows() const
{
    QStringList rows;

    if (m_collection && m_collection->isOpen()) {
        // Scope.
        QStringList candidates;
        if (m_scope == QLatin1String("folder")) {
            candidates = m_collection->notesInFolder(m_folderPath);
        } else {
            candidates = m_collection->noteRelPaths();
            if (m_scope == QLatin1String("favorites")) {
                QStringList favorites;
                for (const QString &relPath : candidates) {
                    const NoteCollection::NoteEntry *entry =
                        m_collection->note(relPath);
                    if (entry && entry->meta.favorite)
                        favorites.append(relPath);
                }
                candidates = favorites;
            }
        }

        // Tag filter composes with the scope (§8.2).
        if (!m_tagFilter.isEmpty()) {
            QStringList tagged;
            for (const QString &relPath : candidates) {
                const NoteCollection::NoteEntry *entry =
                    m_collection->note(relPath);
                if (entry && entry->meta.tags.contains(m_tagFilter))
                    tagged.append(relPath);
            }
            candidates = tagged;
        }

        // Sort. Manual keeps notesInFolder()'s order and only exists
        // inside a folder scope; elsewhere it degrades to title.
        const bool manual = m_sortMode == QLatin1String("manual")
            && m_scope == QLatin1String("folder");
        if (!manual) {
            const QString mode = m_sortMode == QLatin1String("manual")
                ? QStringLiteral("title")
                : m_sortMode;
            const NoteCollection *collection = m_collection;
            const bool ascending = m_ascending;
            std::sort(candidates.begin(), candidates.end(),
                      [collection, mode, ascending](const QString &pathA,
                                                    const QString &pathB) {
                const NoteCollection::NoteEntry *a = collection->note(pathA);
                const NoteCollection::NoteEntry *b = collection->note(pathB);
                if (!a || !b)
                    return pathA < pathB;
                int cmp = 0;
                if (mode == QLatin1String("created")) {
                    cmp = a->created < b->created
                        ? -1 : (a->created > b->created ? 1 : 0);
                } else if (mode == QLatin1String("title")) {
                    cmp = a->title.compare(b->title, Qt::CaseInsensitive);
                } else { // modified
                    cmp = a->modified < b->modified
                        ? -1 : (a->modified > b->modified ? 1 : 0);
                }
                if (cmp == 0) // stable, deterministic tie-break
                    cmp = pathA.compare(pathB);
                return ascending ? cmp < 0 : cmp > 0;
            });
        } else if (m_ascending == false) {
            // Manual order is one sequence; "descending" reverses it.
            std::reverse(candidates.begin(), candidates.end());
        }

        // Pinned notes float to the top within every sort (§8.3), keeping
        // their relative order.
        QStringList pinned, rest;
        for (const QString &relPath : candidates) {
            const NoteCollection::NoteEntry *entry = m_collection->note(relPath);
            (entry && entry->meta.pinned ? pinned : rest).append(relPath);
        }
        rows = pinned + rest;
    }

    return rows;
}

void NoteListModel::applyRows(const QStringList &rows)
{
    const QList<int> roles = {
        RelPathRole, TitleRole, SnippetRole, ModifiedRole,
        CreatedRole, WordCountRole, PinnedRole, FavoriteRole, TagsRole
    };
    auto emitAllRowsChanged = [this, &roles]() {
        if (!m_rows.isEmpty())
            emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), roles);
    };

    if (rows == m_rows) {
        emitAllRowsChanged();
        return;
    }

    const bool countChangedValue = rows.size() != m_rows.size();

    if (m_rows.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, rows.size() - 1);
        m_rows = rows;
        endInsertRows();
        if (countChangedValue)
            emit countChanged();
        emitAllRowsChanged();
        return;
    }

    if (rows.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
        m_rows.clear();
        endRemoveRows();
        if (countChangedValue)
            emit countChanged();
        return;
    }

    QSet<QString> target;
    target.reserve(rows.size());
    for (const QString &row : rows)
        target.insert(row);

    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (target.contains(m_rows.at(i)))
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }

    QSet<QString> current;
    current.reserve(m_rows.size() + rows.size());
    for (const QString &row : std::as_const(m_rows))
        current.insert(row);

    for (int i = 0; i < rows.size(); ++i) {
        const QString &row = rows.at(i);
        if (current.contains(row))
            continue;
        beginInsertRows(QModelIndex(), i, i);
        m_rows.insert(i, row);
        endInsertRows();
        current.insert(row);
    }

    for (int i = 0; i < rows.size(); ++i) {
        if (m_rows.at(i) == rows.at(i))
            continue;
        const int from = m_rows.indexOf(rows.at(i), i + 1);
        Q_ASSERT(from > i);
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
        m_rows.move(from, i);
        endMoveRows();
    }

    if (countChangedValue)
        emit countChanged();
    emitAllRowsChanged();
}

int NoteListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant NoteListModel::data(const QModelIndex &index, int role) const
{
    if (!m_collection || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const NoteCollection::NoteEntry *entry =
        m_collection->note(m_rows.at(index.row()));
    if (!entry)
        return QVariant();

    switch (role) {
    case RelPathRole:
        return entry->relPath;
    case TitleRole:
        return entry->title;
    case SnippetRole:
        return entry->snippet;
    case ModifiedRole:
        return entry->modified;
    case CreatedRole:
        return entry->created;
    case WordCountRole:
        return entry->wordCount;
    case PinnedRole:
        return entry->meta.pinned;
    case FavoriteRole:
        return entry->meta.favorite;
    case TagsRole:
        return entry->meta.tags;
    }
    return QVariant();
}

QHash<int, QByteArray> NoteListModel::roleNames() const
{
    return {
        {RelPathRole, "relPath"},
        {TitleRole, "title"},
        {SnippetRole, "snippet"},
        {ModifiedRole, "modified"},
        {CreatedRole, "created"},
        {WordCountRole, "wordCount"},
        {PinnedRole, "pinned"},
        {FavoriteRole, "favorite"},
        {TagsRole, "tags"},
    };
}

int NoteListModel::rowOf(const QString &relPath) const
{
    return m_rows.indexOf(relPath);
}

QString NoteListModel::relPathAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row) : QString();
}
