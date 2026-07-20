// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "foldertreemodel.h"
#include "notecollection.h"

#include <QSet>

FolderTreeModel::FolderTreeModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(20);
    connect(&m_rebuildTimer, &QTimer::timeout,
            this, &FolderTreeModel::rebuild);
}

void FolderTreeModel::setCollection(NoteCollection *collection)
{
    if (m_collection)
        disconnect(m_collection, nullptr, this, nullptr);
    m_collection = collection;
    if (m_collection) {
        connect(m_collection, &NoteCollection::revisionChanged,
                this, &FolderTreeModel::scheduleRebuild);
        connect(m_collection, &NoteCollection::rootChanged,
                this, &FolderTreeModel::scheduleRebuild);
    }
    rebuild();
}

void FolderTreeModel::rebuild()
{
    if (m_rebuildTimer.isActive())
        m_rebuildTimer.stop();
    applyRows(projectedRows());
}

void FolderTreeModel::scheduleRebuild()
{
    if (!m_rebuildTimer.isActive())
        m_rebuildTimer.start();
}

QStringList FolderTreeModel::projectedRows() const
{
    QStringList rows;
    if (m_collection && m_collection->isOpen()) {
        // folderRelPaths() is sorted, so "Ideas" precedes "Ideas/Projects":
        // walking it in order IS a depth-first tree walk. A folder is a
        // visible row when every ancestor is expanded.
        const QStringList all = m_collection->folderRelPaths();
        for (const QString &relPath : all) {
            const int slash = relPath.lastIndexOf(QLatin1Char('/'));
            bool visible = true;
            if (slash >= 0) {
                const QString parent = relPath.left(slash);
                const NoteCollection::FolderEntry *entry =
                    m_collection->folder(parent);
                // Parent visible AND expanded; parent visibility is
                // equivalent to its presence in rows (built in order).
                visible = entry && entry->expanded && rows.contains(parent);
            }
            if (visible)
                rows.append(relPath);
        }
    }

    return rows;
}

void FolderTreeModel::applyRows(const QStringList &rows)
{
    const QList<int> roles = {
        RelPathRole, NameRole, DepthRole, ExpandedRole,
        HasChildrenRole, ColorRole, NoteCountRole
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

int FolderTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant FolderTreeModel::data(const QModelIndex &index, int role) const
{
    if (!m_collection || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const QString &relPath = m_rows.at(index.row());
    const NoteCollection::FolderEntry *entry = m_collection->folder(relPath);
    if (!entry)
        return QVariant();

    switch (role) {
    case RelPathRole:
        return entry->relPath;
    case NameRole:
        return entry->name;
    case DepthRole:
        return int(entry->relPath.count(QLatin1Char('/')));
    case ExpandedRole:
        return entry->expanded;
    case HasChildrenRole:
        return hasChildFolders(relPath);
    case ColorRole:
        return entry->color;
    case NoteCountRole:
        return m_collection->noteCountInFolder(relPath, true);
    }
    return QVariant();
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const
{
    return {
        {RelPathRole, "relPath"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {ExpandedRole, "expanded"},
        {HasChildrenRole, "hasChildren"},
        {ColorRole, "folderColor"},
        {NoteCountRole, "noteCount"},
    };
}

bool FolderTreeModel::hasChildFolders(const QString &relPath) const
{
    const QString prefix = relPath + QLatin1Char('/');
    const QStringList all = m_collection->folderRelPaths();
    for (const QString &other : all) {
        if (other.startsWith(prefix))
            return true;
    }
    return false;
}

void FolderTreeModel::toggleExpanded(int row)
{
    if (!m_collection || row < 0 || row >= m_rows.size())
        return;
    const NoteCollection::FolderEntry *entry =
        m_collection->folder(m_rows.at(row));
    if (entry)
        m_collection->setFolderExpanded(entry->relPath, !entry->expanded);
    // The collection's revision signal drives the queued rebuild.
}

int FolderTreeModel::rowOf(const QString &relPath) const
{
    return m_rows.indexOf(relPath);
}

QString FolderTreeModel::relPathAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row) : QString();
}
