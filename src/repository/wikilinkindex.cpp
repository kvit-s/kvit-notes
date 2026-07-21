// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "wikilinkindex.h"

#include "notefileio.h"
#include "notefrontmatter.h"
#include "wikilinkscanner.h"

#include <QCryptographicHash>
#include <QFileInfo>

#include <utility>

namespace {

const QString mdSuffix = QStringLiteral(".md");

QByteArray sha256Of(const QByteArray &content)
{
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256);
}

} // namespace

WikiLinkIndex::WikiLinkIndex(const QHash<QString, NoteEntry> *notes,
                             RevisionProvider revision,
                             AbsolutePathResolver absolutePath,
                             BodyReader readBody)
    : m_notes(notes)
    , m_revision(std::move(revision))
    , m_absolutePath(std::move(absolutePath))
    , m_readBody(std::move(readBody))
{
}

QStringList WikiLinkIndex::extractLinks(const QString &body)
{
    QStringList targets;
    const QList<WikiLinkScanner::Occurrence> occurrences =
        WikiLinkScanner::scan(body);
    for (const WikiLinkScanner::Occurrence &occurrence : occurrences) {
        if (!occurrence.note.isEmpty()) // bare [[#heading]] is same-note only
            targets.append(occurrence.rawTarget);
    }
    return targets;
}

int WikiLinkIndex::rewriteTargetsInText(QString *text,
                                        const QSet<QString> &oldKeys,
                                        const QString &replacement)
{
    if (!text || oldKeys.isEmpty())
        return 0;
    QList<WikiLinkScanner::Occurrence> replacements;
    const QList<WikiLinkScanner::Occurrence> occurrences =
        WikiLinkScanner::scan(*text);
    for (const WikiLinkScanner::Occurrence &occurrence : occurrences) {
        QString key = occurrence.note.toLower();
        if (key.endsWith(mdSuffix))
            key.chop(mdSuffix.size());
        if (!key.isEmpty() && oldKeys.contains(key))
            replacements.append(occurrence);
    }
    for (int i = replacements.size() - 1; i >= 0; --i) {
        const WikiLinkScanner::Occurrence &occurrence = replacements.at(i);
        text->replace(occurrence.noteStart, occurrence.noteLength, replacement);
    }
    return replacements.size();
}

void WikiLinkIndex::ensureIndex() const
{
    const int revision = m_revision();
    if (m_indexRevision == revision
        && m_indexNoteCount == m_notes->size())
        return;
    m_basenames.clear();
    for (auto it = m_notes->constBegin(); it != m_notes->constEnd(); ++it) {
        QString base = nameOfRelPath(it.key());
        if (base.endsWith(mdSuffix, Qt::CaseInsensitive))
            base.chop(mdSuffix.size());
        m_basenames[base.toLower()].append(it.key());
    }
    m_indexRevision = revision;
    m_indexNoteCount = m_notes->size();
}

QString WikiLinkIndex::resolve(const QString &target) const
{
    const QVariantMap result = resolution(target);
    return result.value(QStringLiteral("status")) == QLatin1String("unique")
        ? result.value(QStringLiteral("relPath")).toString() : QString();
}

QVariantMap WikiLinkIndex::resolution(const QString &target) const
{
    QString wanted = target.trimmed();
    const int hash = wanted.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        wanted = wanted.left(hash).trimmed();
    if (wanted.endsWith(mdSuffix, Qt::CaseInsensitive))
        wanted.chop(mdSuffix.size());
    while (wanted.startsWith(QLatin1Char('/')))
        wanted.remove(0, 1);
    if (wanted.isEmpty())
        return {{QStringLiteral("status"), QStringLiteral("missing")},
                {QStringLiteral("relPath"), QString()},
                {QStringLiteral("candidates"), QStringList()}};

    ensureIndex();
    const QString lowered = wanted.toLower();
    const int slash = lowered.lastIndexOf(QLatin1Char('/'));
    const QString base = slash >= 0 ? lowered.mid(slash + 1) : lowered;

    QStringList matches;
    const QStringList candidates = m_basenames.value(base);
    for (const QString &relPath : candidates) {
        QString path = relPath;
        if (path.endsWith(mdSuffix, Qt::CaseInsensitive))
            path.chop(mdSuffix.size());
        const QString lowerPath = path.toLower();
        if (lowerPath != lowered
            && !lowerPath.endsWith(QLatin1Char('/') + lowered))
            continue;
        matches.append(relPath);
    }
    matches.sort(Qt::CaseInsensitive);
    const QString status = matches.isEmpty() ? QStringLiteral("missing")
        : matches.size() == 1 ? QStringLiteral("unique")
                              : QStringLiteral("ambiguous");
    return {{QStringLiteral("status"), status},
            {QStringLiteral("relPath"), matches.size() == 1
                 ? matches.first() : QString()},
            {QStringLiteral("candidates"), matches}};
}

QHash<QString, QSet<QString>> WikiLinkIndex::collectReferrers(
    const QString &relPath) const
{
    QHash<QString, QSet<QString>> referrers;
    for (auto it = m_notes->constBegin(); it != m_notes->constEnd(); ++it) {
        QSet<QString> keys;
        for (const QString &raw : it->links) {
            QString notePart = raw;
            const int hash = notePart.indexOf(QLatin1Char('#'));
            if (hash >= 0)
                notePart = notePart.left(hash);
            notePart = notePart.trimmed();
            if (notePart.isEmpty())
                continue;
            if (resolve(notePart) != relPath)
                continue;
            QString key = notePart.toLower();
            if (key.endsWith(mdSuffix))
                key.chop(mdSuffix.size());
            keys.insert(key);
        }
        if (!keys.isEmpty())
            referrers.insert(it.key(), keys);
    }
    return referrers;
}

QHash<QString, WikiLinkIndex::RewriteSnapshot>
WikiLinkIndex::snapshotNoteReferrers(const QString &relPath) const
{
    QHash<QString, RewriteSnapshot> snapshots;
    const auto referrers = collectReferrers(relPath);
    for (auto it = referrers.constBegin(); it != referrers.constEnd(); ++it) {
        bool ok = false;
        const QByteArray bytes = NoteFileIo::readFileBytes(m_absolutePath(it.key()), &ok);
        if (!ok)
            continue;
        RewriteSnapshot snapshot;
        snapshot.keys = it.value();
        snapshot.hash = sha256Of(bytes);
        snapshot.modified = QFileInfo(m_absolutePath(it.key())).lastModified();
        // Scan the file text just read for content hashing, not a resident body
        // cache.
        const QString referrerBody =
            NoteFrontMatter::split(QString::fromUtf8(bytes)).body;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(referrerBody)) {
            QString key = occurrence.note.toLower();
            if (key.endsWith(mdSuffix))
                key.chop(mdSuffix.size());
            if (snapshot.keys.contains(key))
                ++snapshot.linkCount;
        }
        snapshots.insert(it.key(), snapshot);
    }
    return snapshots;
}

QHash<QString, WikiLinkIndex::RewriteSnapshot>
WikiLinkIndex::snapshotFolderReferrers(const QString &oldPrefix) const
{
    QHash<QString, RewriteSnapshot> snapshots;
    const QString lowered = oldPrefix.toLower() + QLatin1Char('/');
    for (auto it = m_notes->constBegin(); it != m_notes->constEnd(); ++it) {
        // Prefilter with the resident wiki-link targets so only notes that may
        // link under the folder are read from disk.
        bool candidate = false;
        for (const QString &raw : it->links) {
            QString notePart = raw;
            const int hash = notePart.indexOf(QLatin1Char('#'));
            if (hash >= 0)
                notePart = notePart.left(hash);
            while (notePart.startsWith(QLatin1Char('/')))
                notePart.remove(0, 1);
            if (notePart.toLower().startsWith(lowered)) {
                candidate = true;
                break;
            }
        }
        if (!candidate)
            continue;

        // Read the candidate once and take the accurate count from its body.
        bool ok = false;
        const QByteArray bytes = NoteFileIo::readFileBytes(m_absolutePath(it.key()), &ok);
        if (!ok)
            continue;
        const QString body =
            NoteFrontMatter::split(QString::fromUtf8(bytes)).body;
        int count = 0;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(body)) {
            QString note = occurrence.note;
            while (note.startsWith(QLatin1Char('/')))
                note.remove(0, 1);
            if (note.toLower().startsWith(lowered))
                ++count;
        }
        if (count == 0)
            continue;
        RewriteSnapshot snapshot;
        snapshot.hash = sha256Of(bytes);
        snapshot.modified = QFileInfo(m_absolutePath(it.key())).lastModified();
        snapshot.linkCount = count;
        snapshots.insert(it.key(), snapshot);
    }
    return snapshots;
}

QStringList WikiLinkIndex::headingsFor(const QString &relPath) const
{
    if (!m_notes->contains(relPath))
        return {};
    QStringList headings;
    bool inFence = false;
    // Read only this note, not a resident body cache.
    const QStringList lines = m_readBody(relPath).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```"))
            || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence || !trimmed.startsWith(QLatin1Char('#')))
            continue;
        int level = 0;
        while (level < trimmed.size()
               && trimmed.at(level) == QLatin1Char('#'))
            ++level;
        if (level > 6 || level >= trimmed.size()
            || trimmed.at(level) != QLatin1Char(' '))
            continue;
        const QString text = trimmed.mid(level + 1).trimmed();
        if (!text.isEmpty())
            headings.append(text);
    }
    return headings;
}

QVariantList WikiLinkIndex::backlinksTo(const QString &relPath) const
{
    QVariantList out;
    if (relPath.isEmpty())
        return out;

    QStringList paths = m_notes->keys();
    paths.sort();
    for (const QString &referrer : paths) {
        if (referrer == relPath)
            continue;
        const NoteEntry &entry = *m_notes->constFind(referrer);
        int count = 0;
        for (const QString &raw : entry.links) {
            if (resolve(raw) == relPath)
                ++count;
        }
        if (count == 0)
            continue;

        // Context lines: the referrer's raw body lines whose links resolve
        // to the target — the surrounding text the panel shows per match. Only
        // the notes that actually refer here are read, and only after the
        // resident links established count > 0.
        const QString body = m_readBody(referrer);
        QStringList contexts;
        QSet<int> contextLineStarts;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(body)) {
            if (occurrence.note.isEmpty()
                || resolve(occurrence.rawTarget) != relPath)
                continue;
            int start = body.lastIndexOf(QLatin1Char('\n'),
                                         occurrence.start - 1);
            start = start < 0 ? 0 : start + 1;
            if (contextLineStarts.contains(start))
                continue;
            contextLineStarts.insert(start);
            int end = body.indexOf(QLatin1Char('\n'), occurrence.start);
            if (end < 0)
                end = body.size();
            contexts.append(body.mid(start, end - start).trimmed().left(200));
        }

        out.append(QVariantMap{
            {QStringLiteral("relPath"), referrer},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("count"), count},
            {QStringLiteral("contexts"), contexts},
        });
    }
    return out;
}
