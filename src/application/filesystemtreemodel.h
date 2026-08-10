// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FILESYSTEMTREEMODEL_H
#define FILESYSTEMTREEMODEL_H

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

class IgnoreRules;
class NoteCollection;

// A lazy, filesystem-backed view of the open root. Only the root and folders
// the reader expands are listed; collapsed folders have neither cached
// children nor a QFileSystemWatcher registration. This is deliberately not a
// projection of NoteCollection: arbitrary source and media files belong here
// without becoming notes.
class FileSystemTreeModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)

public:
    enum Roles {
        RelativePathRole = Qt::UserRole + 1,
        AbsolutePathRole,
        NameRole,
        DepthRole,
        DirectoryRole,
        ExpandedRole,
        HasChildrenRole,
        KindRole
    };

    explicit FileSystemTreeModel(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);
    void setIgnoreRules(IgnoreRules *rules);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const { return m_rootPath; }
    Q_INVOKABLE void toggleExpanded(int row);
    Q_INVOKABLE QVariantMap entryAt(int row) const;
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE int rowOf(const QString &relativePath) const;

    int loadedDirectoryCountForTests() const { return m_children.size(); }
    int watchedDirectoryCountForTests() const
    { return m_watcher.directories().size(); }

signals:
    void countChanged();
    void rootPathChanged();
    void fileActivated(const QString &absolutePath, const QString &kind,
                       const QString &relativePath);

private:
    struct Entry {
        QString relativePath;
        QString name;
        QString kind;
        int depth = 0;
        bool directory = false;
        bool symlink = false;
    };

    void resetRoot();
    void loadDirectory(const QString &relativePath);
    void unloadBelow(const QString &relativePath);
    void rebuildRows();
    void appendRows(const QString &directory, QVector<Entry> *rows) const;
    QString absolutePath(const QString &relativePath) const;
    static QString kindForPath(const QString &path, bool directory,
                               bool symlink);

    NoteCollection *m_collection = nullptr;
    IgnoreRules *m_ignoreRules = nullptr;
    QString m_rootPath;
    QHash<QString, QVector<Entry>> m_children;
    QSet<QString> m_expanded;
    QVector<Entry> m_rows;
    QFileSystemWatcher m_watcher;
};

#endif // FILESYSTEMTREEMODEL_H
