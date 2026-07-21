// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef LINKREDIRECTS_H
#define LINKREDIRECTS_H

#include "notefileio.h"
#include "vaultpaths.h"
#include "wikilinkindex.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

// Where a note used to live.
//
// Renaming a note breaks every `[[wiki link]]` that names it, and the obvious
// repair — rewrite all of them before the rename returns — makes one
// keystroke into a write across the vault, holds the interface while it runs,
// and leaves half the links pointing at the old name if the process stops
// part way. This table is the alternative: the rename records that
// `old/path.md` is now `new/path.md` and returns, link resolution consults
// the table whenever a target does not name a real note, and the linking
// notes are rewritten afterwards, a few at a time, between event-loop turns.
// No link is ever broken, not even for the moment between the rename and the
// rewrite reaching a particular file.
//
// The table is the durable record of work still owed: an entry exists exactly
// while some note still names the old path. Once the rewrite has reached them
// all, the entry is dropped, so a table found at startup means an
// interrupted rename and the rewrite simply runs again. It lives in
// <root>/.kvit/redirects.json, reached through the containment primitives in
// vaultpaths.h like every other repository-owned file, rather than in the
// renamed note's own front matter: the whole point of the design is that a
// rename touches no note's bytes until the background pass rewrites them
// properly.
//
// Two rules keep the table from ever disagreeing with the filesystem. A
// rename of an already-redirected note collapses the chain (A to B then B to
// C is stored as A to C, never followed twice), and a note created at a
// redirected path drops the entry, because a redirect must never shadow a
// file that exists.
//
// This is vault-local state. Another tool opening the same vault knows
// nothing about it and will show the old links as broken until the rewrite
// finishes, which is one reason the rewrite runs at once rather than on some
// idle timer.
class LinkRedirects
{
public:
    struct Entry {
        QString from;   // the note's relPath before the rename
        QString to;     // where it lives now
    };

    // Point at a vault and load whatever it holds. An empty path detaches.
    // A missing or unreadable file is simply an empty table: the file is
    // optional on read, so a vault written by a version without it opens
    // unchanged.
    void setRootPath(const QString &rootPath)
    {
        m_rootPath = rootPath;
        m_entries.clear();
        m_byBaseName.clear();
        if (!m_rootPath.isEmpty())
            load();
    }

    bool isEmpty() const { return m_entries.isEmpty(); }
    int count() const { return int(m_entries.size()); }
    const QList<Entry> &entries() const { return m_entries; }

    // Record that `from` is now `to`, collapsing chains and dropping
    // anything the move contradicts. False when the pair says nothing worth
    // storing (either path malformed, or the two are the same).
    bool record(const QString &from, const QString &to)
    {
        if (!VaultPaths::isPlainRelativePath(from)
            || !VaultPaths::isPlainRelativePath(to) || from == to) {
            return false;
        }
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            Entry &entry = m_entries[i];
            // A to B followed by B to C is one hop from A: resolution never
            // has to follow a chain, and dropping the last hop can never
            // strand the first.
            if (entry.to == from)
                entry.to = to;
            // Whatever used to stand at `to` has been superseded by the note
            // that just arrived there; an entry naming the file being moved
            // away from is replaced by the one appended below; and a hop
            // collapsed onto its own source says nothing.
            if (entry.from == to || entry.from == from
                || entry.from == entry.to) {
                m_entries.removeAt(i);
            }
        }
        m_entries.append(Entry{from, to});
        reindex();
        return true;
    }

    // A note some redirect already points at has moved again, by a route
    // that records no redirect of its own — a rename the user asked not to
    // update links for, or a folder rename carrying it. Following it keeps
    // the earlier name working rather than stranding it on a path that no
    // longer holds anything.
    bool retarget(const QString &from, const QString &to)
    {
        if (m_entries.isEmpty() || !VaultPaths::isPlainRelativePath(to))
            return false;
        bool changed = false;
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (m_entries.at(i).to != from)
                continue;
            m_entries[i].to = to;
            if (m_entries.at(i).from == to)
                m_entries.removeAt(i); // renamed back to where it started
            changed = true;
        }
        if (changed)
            reindex();
        return changed;
    }

    // A real note now stands at `relPath`, so any redirect claiming that
    // name is dead. True when something was dropped.
    bool dropFrom(const QString &relPath)
    {
        const int before = int(m_entries.size());
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (m_entries.at(i).from == relPath)
                m_entries.removeAt(i);
        }
        if (int(m_entries.size()) == before)
            return false;
        reindex();
        return true;
    }

    // Keep only the entries named in `keep`. Used once a rewrite pass has
    // run: an entry no note refers to any more has nothing left to do.
    bool retainFrom(const QSet<QString> &keep)
    {
        const int before = int(m_entries.size());
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (!keep.contains(m_entries.at(i).from))
                m_entries.removeAt(i);
        }
        if (int(m_entries.size()) == before)
            return false;
        reindex();
        return true;
    }

    // The redirect a normalized target resolves through — see
    // WikiLinkIndex::normalizeTarget for what "normalized" means — or
    // nullptr when no entry or more than one matches it. Several matches are
    // the same ambiguity a bare name matching two notes has, and are
    // answered the same way: nothing.
    const Entry *lookup(const QString &normalizedTarget) const
    {
        if (normalizedTarget.isEmpty() || m_entries.isEmpty())
            return nullptr;
        const int slash = normalizedTarget.lastIndexOf(QLatin1Char('/'));
        const QString base = slash >= 0 ? normalizedTarget.mid(slash + 1)
                                        : normalizedTarget;
        const Entry *found = nullptr;
        for (int index : m_byBaseName.value(base)) {
            const Entry &entry = m_entries.at(index);
            if (!WikiLinkIndex::pathMatchesTarget(entry.from, normalizedTarget))
                continue;
            if (found && found->to != entry.to)
                return nullptr; // ambiguous: two old names, two destinations
            found = &entry;
        }
        return found;
    }

    QString targetFor(const QString &normalizedTarget) const
    {
        const Entry *entry = lookup(normalizedTarget);
        return entry ? entry->to : QString();
    }

    // Write the table out, or remove the file once the table is empty. A
    // failed write costs the next session the resumption, not the links
    // themselves, which are already correct in every file the pass reached.
    bool save() const
    {
        if (m_rootPath.isEmpty())
            return false;
        const QString path = filePath(!m_entries.isEmpty());
        if (path.isEmpty())
            return false;
        if (m_entries.isEmpty()) {
            QFile::remove(path);
            return true;
        }
        QJsonArray array;
        for (const Entry &entry : m_entries) {
            QJsonObject object;
            object.insert(QStringLiteral("from"), entry.from);
            object.insert(QStringLiteral("to"), entry.to);
            array.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("redirects"), array);
        return NoteFileIo::writeFileBytesAtomic(
            path, QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

private:
    void load()
    {
        const QString path = filePath(false);
        if (path.isEmpty())
            return;
        // One line per renamed note; a file past this size was not written
        // here, and reading it would only be a way to stall the open.
        constexpr qint64 maxRedirectBytes = 16LL * 1024 * 1024;
        bool ok = false;
        const QByteArray bytes =
            NoteFileIo::readFileBytes(path, &ok, maxRedirectBytes);
        if (!ok)
            return;
        const QJsonArray array = QJsonDocument::fromJson(bytes)
                                     .object()
                                     .value(QStringLiteral("redirects"))
                                     .toArray();
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            const QString from = object.value(QStringLiteral("from")).toString();
            const QString to = object.value(QStringLiteral("to")).toString();
            // The file is vault content like any other: a crafted entry must
            // not name a path outside the vault.
            if (!VaultPaths::isPlainRelativePath(from)
                || !VaultPaths::isPlainRelativePath(to) || from == to) {
                continue;
            }
            m_entries.append(Entry{from, to});
        }
        reindex();
    }

    QString filePath(bool create) const
    {
        if (create
            && VaultPaths::ensureOwnedDir(m_rootPath, QStringLiteral(".kvit"))
                   .isEmpty()) {
            return QString();
        }
        return VaultPaths::ownedFile(m_rootPath, QStringLiteral(".kvit"),
                                     QStringLiteral("redirects.json"));
    }

    // Old basename -> entry indices, so a lookup costs a hash probe rather
    // than a walk of a folder rename's worth of entries.
    void reindex()
    {
        m_byBaseName.clear();
        for (int i = 0; i < m_entries.size(); ++i) {
            const QString normalized =
                WikiLinkIndex::normalizeTarget(m_entries.at(i).from);
            const int slash = normalized.lastIndexOf(QLatin1Char('/'));
            m_byBaseName[slash >= 0 ? normalized.mid(slash + 1) : normalized]
                .append(i);
        }
    }

    QString m_rootPath;
    QList<Entry> m_entries;
    QHash<QString, QList<int>> m_byBaseName;
};

#endif // LINKREDIRECTS_H
