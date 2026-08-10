// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "filesystemtreemodel.h"

#include "ignorerules.h"
#include "imageassets.h"
#include "notecollection.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

FileSystemTreeModel::FileSystemTreeModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &path) {
                if (m_rootPath.isEmpty())
                    return;
                QString rel = QDir(m_rootPath).relativeFilePath(path);
                if (rel == QLatin1String("."))
                    rel.clear();
                if (!m_expanded.contains(rel))
                    return;
                loadDirectory(rel);
                rebuildRows();
            });
}

void FileSystemTreeModel::setCollection(NoteCollection *collection)
{
    if (m_collection == collection)
        return;
    if (m_collection)
        disconnect(m_collection, nullptr, this, nullptr);
    m_collection = collection;
    if (m_collection) {
        connect(m_collection, &NoteCollection::rootChanged,
                this, &FileSystemTreeModel::resetRoot);
    }
    resetRoot();
}

void FileSystemTreeModel::setIgnoreRules(IgnoreRules *rules)
{
    if (m_ignoreRules == rules)
        return;
    if (m_ignoreRules)
        disconnect(m_ignoreRules, nullptr, this, nullptr);
    m_ignoreRules = rules;
    if (m_ignoreRules) {
        connect(m_ignoreRules, &IgnoreRules::rulesChanged,
                this, &FileSystemTreeModel::resetRoot);
    }
    resetRoot();
}

int FileSystemTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant FileSystemTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const Entry &entry = m_rows.at(index.row());
    switch (role) {
    case RelativePathRole: return entry.relativePath;
    case AbsolutePathRole: return absolutePath(entry.relativePath);
    case NameRole: return entry.name;
    case DepthRole: return entry.depth;
    case DirectoryRole: return entry.directory;
    case ExpandedRole: return m_expanded.contains(entry.relativePath);
    case HasChildrenRole: return entry.directory && !entry.symlink;
    case KindRole: return entry.kind;
    }
    return QVariant();
}

QHash<int, QByteArray> FileSystemTreeModel::roleNames() const
{
    return {
        {RelativePathRole, "relativePath"},
        {AbsolutePathRole, "absolutePath"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {DirectoryRole, "directory"},
        {ExpandedRole, "expanded"},
        {HasChildrenRole, "hasChildren"},
        {KindRole, "kind"},
    };
}

void FileSystemTreeModel::resetRoot()
{
    const QString next = m_collection && m_collection->isOpen()
        ? m_collection->rootPath() : QString();
    const bool rootChangedValue = next != m_rootPath;

    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    m_children.clear();
    m_expanded.clear();
    m_rootPath = next;
    if (!m_rootPath.isEmpty()) {
        // The root is the one directory open by definition. Nothing below it
        // is touched until its row is expanded.
        m_expanded.insert(QString());
        loadDirectory(QString());
    }
    rebuildRows();
    if (rootChangedValue)
        emit rootPathChanged();
}

void FileSystemTreeModel::loadDirectory(const QString &relativePath)
{
    if (m_rootPath.isEmpty())
        return;
    const QString absDir = absolutePath(relativePath);
    const QFileInfo dirInfo(absDir);
    if (!dirInfo.isDir() || dirInfo.isSymLink()) {
        m_children.remove(relativePath);
        return;
    }

    const IgnoreRules::Snapshot ignoreRules = m_ignoreRules
        ? m_ignoreRules->snapshot().throughDirectory(relativePath)
        : IgnoreRules::Snapshot();
    const QFileInfoList infos = QDir(absDir).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System
            | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QVector<Entry> children;
    children.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        const QString name = info.fileName();
        // Internal implementation trees are not project content. Other dot
        // entries, notably .gitignore, are real files in the file view.
        if (info.isDir()
            && (name == QLatin1String(".git")
                || name == QLatin1String(".kvit"))) {
            continue;
        }
        const QString rel = relativePath.isEmpty()
            ? name : relativePath + QLatin1Char('/') + name;
        if (ignoreRules.isExcluded(rel, info.isDir()))
            continue;

        Entry entry;
        entry.relativePath = rel;
        entry.name = name;
        entry.depth = int(rel.count(QLatin1Char('/')));
        entry.directory = info.isDir();
        entry.symlink = info.isSymLink();
        entry.kind = kindForPath(info.absoluteFilePath(), entry.directory,
                                 entry.symlink);
        children.append(std::move(entry));
    }
    m_children.insert(relativePath, std::move(children));
    if (!m_watcher.directories().contains(absDir))
        m_watcher.addPath(absDir);
}

void FileSystemTreeModel::unloadBelow(const QString &relativePath)
{
    const QString prefix = relativePath + QLatin1Char('/');
    QStringList removeWatches;
    const QSet<QString> expanded = m_expanded;
    for (const QString &dir : expanded) {
        if (dir == relativePath || dir.startsWith(prefix)) {
            if (!dir.isEmpty())
                removeWatches.append(absolutePath(dir));
            m_expanded.remove(dir);
            m_children.remove(dir);
        }
    }
    if (!removeWatches.isEmpty())
        m_watcher.removePaths(removeWatches);
}

void FileSystemTreeModel::toggleExpanded(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const Entry entry = m_rows.at(row);
    if (!entry.directory || entry.symlink)
        return;
    if (m_expanded.contains(entry.relativePath)) {
        unloadBelow(entry.relativePath);
    } else {
        m_expanded.insert(entry.relativePath);
        loadDirectory(entry.relativePath);
    }
    rebuildRows();
}

void FileSystemTreeModel::rebuildRows()
{
    QVector<Entry> next;
    appendRows(QString(), &next);
    const bool countChangedValue = next.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(next);
    endResetModel();
    if (countChangedValue)
        emit countChanged();
}

void FileSystemTreeModel::appendRows(const QString &directory,
                                     QVector<Entry> *rows) const
{
    const auto it = m_children.constFind(directory);
    if (it == m_children.constEnd())
        return;
    for (const Entry &entry : it.value()) {
        rows->append(entry);
        if (entry.directory && m_expanded.contains(entry.relativePath))
            appendRows(entry.relativePath, rows);
    }
}

QString FileSystemTreeModel::absolutePath(const QString &relativePath) const
{
    return relativePath.isEmpty() ? m_rootPath
                                  : QDir(m_rootPath).filePath(relativePath);
}

QString FileSystemTreeModel::kindForPath(const QString &path, bool directory,
                                         bool symlink)
{
    if (directory && !symlink)
        return QStringLiteral("directory");
    if (symlink)
        return QStringLiteral("external");
    if (path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        return QStringLiteral("markdown");
    switch (ImageAssets::kindForExtension(path)) {
    case ImageAssets::Kind::Image: return QStringLiteral("image");
    case ImageAssets::Kind::Media: return QStringLiteral("media");
    case ImageAssets::Kind::None: break;
    }

    static const QSet<QString> textSuffixes = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("json"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("xml"), QStringLiteral("csv"), QStringLiteral("ini"),
        QStringLiteral("conf"), QStringLiteral("cfg"), QStringLiteral("env"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("fish"), QStringLiteral("py"), QStringLiteral("js"),
        QStringLiteral("jsx"), QStringLiteral("ts"), QStringLiteral("tsx"),
        QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cc"),
        QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("hpp"),
        QStringLiteral("java"), QStringLiteral("go"), QStringLiteral("rs"),
        QStringLiteral("cs"), QStringLiteral("qml"), QStringLiteral("sql"),
        QStringLiteral("css"), QStringLiteral("scss"), QStringLiteral("html"),
        QStringLiteral("htm"), QStringLiteral("svg"), QStringLiteral("tex"),
        QStringLiteral("cmake"), QStringLiteral("gradle"), QStringLiteral("kt"),
        QStringLiteral("swift"), QStringLiteral("rb"), QStringLiteral("php"),
    };
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();
    if (textSuffixes.contains(suffix)
        || name == QLatin1String("makefile")
        || name == QLatin1String("dockerfile")
        || name == QLatin1String("cmakelists.txt")
        || name == QLatin1String(".gitignore")
        || name == QLatin1String(".gitattributes")) {
        return QStringLiteral("text");
    }
    return QStringLiteral("external");
}

QVariantMap FileSystemTreeModel::entryAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return QVariantMap();
    const Entry &entry = m_rows.at(row);
    return {
        {QStringLiteral("relativePath"), entry.relativePath},
        {QStringLiteral("absolutePath"), absolutePath(entry.relativePath)},
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("directory"), entry.directory},
        {QStringLiteral("expanded"), m_expanded.contains(entry.relativePath)},
        {QStringLiteral("kind"), entry.kind},
    };
}

void FileSystemTreeModel::activate(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const Entry &entry = m_rows.at(row);
    if (entry.directory && !entry.symlink) {
        toggleExpanded(row);
        return;
    }
    emit fileActivated(absolutePath(entry.relativePath), entry.kind,
                       entry.relativePath);
}

int FileSystemTreeModel::rowOf(const QString &relativePath) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).relativePath == relativePath)
            return i;
    }
    return -1;
}
