// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "collectionstatestore.h"

#include "notefileio.h"
#include "vaultpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
const QString kvitDirName = QStringLiteral(".kvit");
const QString collectionFileName = QStringLiteral("collection.json");
// Long enough that a full disk or a briefly-locked file has a chance to
// clear, short enough that the state is on disk before the user moves on.
constexpr int retryIntervalMs = 2000;
}

CollectionStateStore::CollectionStateStore(QObject *parent) : QObject(parent)
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(retryIntervalMs);
    connect(&m_retryTimer, &QTimer::timeout, this, &CollectionStateStore::save);
}

void CollectionStateStore::setRoot(const QString &rootPath,
                                   const QString &canonicalRoot)
{
    m_rootPath = rootPath;
    m_canonicalRoot = canonicalRoot;
    if (rootPath.isEmpty()) {
        m_dirty = false;
        m_retryTimer.stop();
    }
}

void CollectionStateStore::setSnapshotProvider(std::function<Snapshot()> provider)
{
    m_provider = std::move(provider);
}

QString CollectionStateStore::filePath() const
{
    // "" when .kvit is not a directory this vault owns. The state file names
    // tag colours and a note to reopen, and following a link out of the vault
    // would both read somebody else's file and write this vault's state into
    // it.
    return VaultPaths::ownedFile(m_rootPath, kvitDirName, collectionFileName);
}

CollectionStateStore::Snapshot CollectionStateStore::load() const
{
    Snapshot snapshot;
    if (m_rootPath.isEmpty())
        return snapshot;

    const QString path = filePath();
    if (path.isEmpty())
        return snapshot; // refused by containment: defaults

    // Tag colours, folder state and one note path. A file past this size is
    // not state this application wrote, and loading it would only be a way to
    // stall the open.
    constexpr qint64 maxStateBytes = 64LL * 1024 * 1024;
    bool ok = false;
    const QString text = NoteFileIo::readTextFile(path, &ok, maxStateBytes);
    if (!ok)
        return snapshot; // absent or unreadable: defaults (never touches notes)

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return snapshot; // corrupt: defaults

    const QJsonObject root = doc.object();

    const QJsonObject tagColors = root.value(QStringLiteral("tagColors")).toObject();
    for (auto it = tagColors.begin(); it != tagColors.end(); ++it)
        snapshot.tagColors.insert(it.key(), it.value().toString());

    const QJsonObject folders = root.value(QStringLiteral("folders")).toObject();
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        const QJsonObject state = it.value().toObject();
        FolderVisual visual;
        visual.color = state.value(QStringLiteral("color")).toString();
        visual.expanded = state.value(QStringLiteral("expanded")).toBool(true);
        snapshot.folders.insert(it.key(), visual);
    }

    const QJsonObject order = root.value(QStringLiteral("manualOrder")).toObject();
    for (auto it = order.begin(); it != order.end(); ++it) {
        QStringList names;
        const QJsonArray array = it.value().toArray();
        for (const QJsonValue &value : array)
            names.append(value.toString());
        snapshot.manualOrder.insert(it.key(), names);
    }

    // Untrusted like every other persisted path: a crafted "../../outside.md"
    // that happens to exist would otherwise be opened automatically at
    // startup, reading a file outside the vault the user chose.
    const QString lastOpen = root.value(QStringLiteral("lastOpenNote")).toString();
    const QString absolute = m_rootPath + QLatin1Char('/') + lastOpen;
    if (!lastOpen.isEmpty()
        && VaultPaths::isPlainRelativePath(lastOpen)
        && VaultPaths::isWithinCanonicalRoot(m_canonicalRoot, absolute)
        && QFileInfo(absolute).isFile()) {
        snapshot.lastOpenNote = lastOpen;
    }
    return snapshot;
}

void CollectionStateStore::save()
{
    if (m_rootPath.isEmpty() || !m_provider)
        return;

    // Set before the attempt, cleared only by a successful commit, so an
    // exception-free early return anywhere below still leaves the write owed.
    m_dirty = true;
    const Snapshot snapshot = m_provider();

    QJsonObject root;

    QJsonObject tagColors;
    for (auto it = snapshot.tagColors.begin(); it != snapshot.tagColors.end(); ++it)
        tagColors.insert(it.key(), it.value());
    root.insert(QStringLiteral("tagColors"), tagColors);

    QJsonObject folders;
    for (auto it = snapshot.folders.begin(); it != snapshot.folders.end(); ++it) {
        if (it.value().color.isEmpty() && it.value().expanded)
            continue; // defaults need no entry
        QJsonObject state;
        if (!it.value().color.isEmpty())
            state.insert(QStringLiteral("color"), it.value().color);
        state.insert(QStringLiteral("expanded"), it.value().expanded);
        folders.insert(it.key(), state);
    }
    root.insert(QStringLiteral("folders"), folders);

    QJsonObject order;
    for (auto it = snapshot.manualOrder.begin(); it != snapshot.manualOrder.end(); ++it) {
        if (it.value().isEmpty())
            continue;
        order.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }
    root.insert(QStringLiteral("manualOrder"), order);

    if (!snapshot.lastOpenNote.isEmpty())
        root.insert(QStringLiteral("lastOpenNote"), snapshot.lastOpenNote);

    const QString path = filePath();
    if (path.isEmpty()) {
        // Refused by containment rather than by the filesystem, so retrying
        // can only fail the same way. The flag is cleared because closing and
        // switching roots both wait for the state to stop being owed, and a
        // write that will never be permitted must not wedge them.
        m_dirty = false;
        emit saveFailed(tr("Cannot save collection settings inside \"%1\"")
                            .arg(m_rootPath));
        return;
    }

    const QString dir = VaultPaths::ensureOwnedDir(m_rootPath, kvitDirName);
    if (dir.isEmpty()) {
        if (!m_retryTimer.isActive())
            m_retryTimer.start();
        emit saveFailed(tr("Cannot create the collection settings folder \"%1\"")
                            .arg(m_rootPath + QLatin1Char('/') + kvitDirName));
        return;
    }

    if (NoteFileIo::writeTextFileAtomic(
            path, QString::fromUtf8(QJsonDocument(root).toJson(
                      QJsonDocument::Indented)))) {
        m_dirty = false;
        m_retryTimer.stop();
        return;
    }

    if (!m_retryTimer.isActive())
        m_retryTimer.start();
    emit saveFailed(tr("Cannot save collection settings to \"%1\"").arg(path));
}

void CollectionStateStore::flushIfDirty()
{
    m_retryTimer.stop();
    if (m_dirty)
        save();
}
