// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTELISTMODEL_H
#define NOTELISTMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>

class NoteCollection;

// The note list (phase8-plan.md decision 8): a projection of the
// collection index through scope (all / favorites / one folder) and sort,
// with pinned notes floating to the top within every sort. Owns no state
// beyond the projection inputs; rebuilds on the collection's revision.
class NoteListModel : public QAbstractListModel
{
    Q_OBJECT

    // "all", "favorites", or "folder" (with folderPath naming which).
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY projectionChanged)
    Q_PROPERTY(QString folderPath READ folderPath WRITE setFolderPath
                   NOTIFY projectionChanged)
    // "modified", "created", "title", "manual" (manual applies inside a
    // folder scope; elsewhere it falls back to title).
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode
                   NOTIFY projectionChanged)
    Q_PROPERTY(bool ascending READ ascending WRITE setAscending
                   NOTIFY projectionChanged)
    // "" = no tag filter; otherwise only notes carrying the tag remain,
    // composing with the scope (§8.2 "filter notes by tag").
    Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter
                   NOTIFY projectionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        RelPathRole = Qt::UserRole + 1,
        TitleRole,
        SnippetRole,
        ModifiedRole,
        CreatedRole,
        WordCountRole,
        PinnedRole,
        FavoriteRole,
        TagsRole
    };

    explicit NoteListModel(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);

    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    QString folderPath() const { return m_folderPath; }
    void setFolderPath(const QString &folderPath);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &sortMode);
    bool ascending() const { return m_ascending; }
    void setAscending(bool ascending);
    QString tagFilter() const { return m_tagFilter; }
    void setTagFilter(const QString &tagFilter);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOf(const QString &relPath) const;
    Q_INVOKABLE QString relPathAt(int row) const;

signals:
    void projectionChanged();
    void countChanged();

private slots:
    void rebuild();
    void scheduleRebuild();

private:
    QStringList projectedRows() const;
    void applyRows(const QStringList &rows);

    NoteCollection *m_collection = nullptr;
    QString m_scope = QStringLiteral("all");
    QString m_folderPath;
    QString m_sortMode = QStringLiteral("modified");
    bool m_ascending = false; // newest first is the natural default
    QString m_tagFilter;
    QStringList m_rows;       // note relPaths in display order
    QTimer m_rebuildTimer;
};

#endif // NOTELISTMODEL_H
