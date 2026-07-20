// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FOLDERTREEMODEL_H
#define FOLDERTREEMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>

class NoteCollection;

// The sidebar's folder tree (phase8-plan.md decision 6): a flattened
// QAbstractListModel of the VISIBLE folder rows — children of collapsed
// folders are simply not rows — rebuilt from NoteCollection on its
// revision signal. Expand/collapse state lives in the collection (it
// persists in collection.json); this model only projects it.
class FolderTreeModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        RelPathRole = Qt::UserRole + 1,
        NameRole,
        DepthRole,
        ExpandedRole,
        HasChildrenRole,
        ColorRole,
        NoteCountRole
    };

    explicit FolderTreeModel(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void toggleExpanded(int row);
    Q_INVOKABLE int rowOf(const QString &relPath) const;
    Q_INVOKABLE QString relPathAt(int row) const;

signals:
    void countChanged();

private slots:
    void rebuild();
    void scheduleRebuild();

private:
    QStringList projectedRows() const;
    void applyRows(const QStringList &rows);
    bool hasChildFolders(const QString &relPath) const;

    NoteCollection *m_collection = nullptr;
    QStringList m_rows; // visible folder relPaths, tree order
    QTimer m_rebuildTimer;
};

#endif // FOLDERTREEMODEL_H
