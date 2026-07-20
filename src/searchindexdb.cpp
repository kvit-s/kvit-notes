// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "searchindexdb.h"

#include "perflog.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

// Current on-disk schema version (search.md §5: every schema change bumps
// PRAGMA user_version; an unsupported version is rebuilt).
static constexpr int kSchemaVersion = 1;

namespace SearchMatching {

int unicodeScalarCount(const QString &query)
{
    // Count Unicode scalar values of an NFC-normalized copy so a supplementary-
    // plane character is one character and composed/decomposed spellings route
    // the same (search.md §4.1). Normalization affects routing only.
    const QString nfc = query.normalized(QString::NormalizationForm_C);
    int count = 0;
    for (int i = 0; i < nfc.size(); ++i) {
        if (nfc.at(i).isHighSurrogate() && i + 1 < nfc.size()
            && nfc.at(i + 1).isLowSurrogate())
            ++i; // a surrogate pair is one scalar value
        ++count;
    }
    return count;
}

bool isWordChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

QString fold(const QString &text)
{
    return text.toCaseFolded();
}

bool hasWordChar(const QString &folded)
{
    for (const QChar ch : folded) {
        if (isWordChar(ch))
            return true;
    }
    return false;
}

QString ftsPhrase(const QString &folded)
{
    QString escaped = folded;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QList<int> verifyOccurrences(const QString &displayText, const QString &query,
                             bool wholeWord)
{
    QList<int> offsets;
    if (query.isEmpty())
        return offsets;
    const int step = qMax(1, query.size());
    int from = 0;
    while (true) {
        const int at = displayText.indexOf(query, from, Qt::CaseInsensitive);
        if (at < 0)
            break;
        const int end = at + query.size();
        bool accept = true;
        if (wholeWord) {
            // A whole-word match is bounded by non-word characters on both
            // sides (search.md §4.2). The Qt refinement is the final authority.
            if (at > 0 && isWordChar(displayText.at(at - 1)))
                accept = false;
            if (accept && end < displayText.size()
                && isWordChar(displayText.at(end)))
                accept = false;
        }
        if (accept)
            offsets.append(at);
        // Non-overlapping: advance past this occurrence even when rejected, so
        // a single scan is O(n) (search.md §4.6). CaseInsensitive keeps display
        // offsets exact because folding is never applied to the display text.
        from = at + step;
    }
    return offsets;
}

} // namespace SearchMatching

namespace {

// A readable window around a match inside one block's display text (search.md
// §8): the match's line, trimmed to 32 leading and 120 total characters with
// ellipses. This is the sole snippet source; it never reopens the Markdown file
// nor uses FTS5 snippet().
SearchMatch buildMatch(const QString &text, int blockIndex, int start,
                       int length)
{
    const int lineStart =
        text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), start);
    if (lineEnd < 0)
        lineEnd = text.size();

    static const int leadMax = 32;
    static const int totalMax = 120;
    int from = lineStart;
    bool leadCut = false;
    if (start - lineStart > leadMax) {
        from = start - leadMax;
        leadCut = true;
    }
    const int to = qMin(lineEnd, from + totalMax);
    const bool tailCut = to < lineEnd;

    QString snippet = text.mid(from, to - from);
    int snippetStart = start - from;
    if (leadCut) {
        snippet = QStringLiteral("…") + snippet;
        snippetStart += 1;
    }
    if (tailCut)
        snippet += QStringLiteral("…");

    SearchMatch match;
    match.blockIndex = blockIndex;
    match.start = start;
    match.length = length;
    match.snippet = snippet;
    match.snippetStart = snippetStart;
    match.snippetLength = length;
    return match;
}

// Escape LIKE wildcards so a folder named "a_b" never scopes into "axb"
// (search.md §7 step 3). Paired with an explicit ESCAPE clause.
QString escapeLike(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('%')
            || ch == QLatin1Char('_'))
            out += QLatin1Char('\\');
        out += ch;
    }
    return out;
}

qint64 startOfDayMs(const QDate &day)
{
    return day.startOfDay().toMSecsSinceEpoch();
}

qint64 endOfDayMs(const QDate &day)
{
    return day.endOfDay().toMSecsSinceEpoch();
}

} // namespace

SearchIndexDb::~SearchIndexDb()
{
    close();
}

bool SearchIndexDb::probeCapability()
{
    static const QString probeConn = QStringLiteral("kvit_search_probe");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    probeConn);
        db.setDatabaseName(QStringLiteral(":memory:"));
        if (db.open()) {
            QSqlQuery q(db);
            ok = q.exec(QStringLiteral(
                "CREATE VIRTUAL TABLE probe USING fts5(t, tokenize='trigram')"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(probeConn);
    return ok;
}

bool SearchIndexDb::open(const QString &dbPath, const QString &connectionName)
{
    close();
    m_connectionName = connectionName;
    m_dbPath = dbPath;

    auto tryOpen = [&]() -> bool {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    m_connectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open())
            return false;
        m_open = true;
        if (!applyPragmas())
            return false;
        // A failed integrity check means a corrupt database (search.md §6.3).
        if (!integrityOk())
            return false;
        return ensureSchema() && schemaValid();
    };

    if (tryOpen()) {
        m_usable = true;
        return true;
    }

    // A corrupt or obsolete database is closed, removed, and rebuilt. Note
    // files are never touched (search.md §6.3).
    close();
    if (m_dbPath != QStringLiteral(":memory:")) {
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
    }
    if (tryOpen()) {
        m_usable = true;
        return true;
    }
    close();
    return false;
}

void SearchIndexDb::close()
{
    if (!m_connectionName.isEmpty()) {
        if (QSqlDatabase::contains(m_connectionName)) {
            {
                QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
                if (db.isOpen())
                    db.close();
            }
            QSqlDatabase::removeDatabase(m_connectionName);
        }
    }
    m_open = false;
    m_usable = false;
    m_connectionName.clear();
}

bool SearchIndexDb::applyPragmas()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // WAL keeps readers unblocked by the writer; NORMAL trades a sliver of
    // durability the disposable cache does not need for speed; the busy timeout
    // bounds contention between the read and write connections (search.md §5).
    const char *pragmas[] = {
        "PRAGMA foreign_keys=ON",
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA busy_timeout=5000",
    };
    for (const char *p : pragmas) {
        if (!q.exec(QLatin1String(p)))
            return false;
    }
    return true;
}

bool SearchIndexDb::schemaValid() const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return false;
    return q.value(0).toInt() == kSchemaVersion;
}

bool SearchIndexDb::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery probe(db);
    if (probe.exec(QStringLiteral("PRAGMA user_version")) && probe.next()) {
        const int version = probe.value(0).toInt();
        if (version == kSchemaVersion)
            return true;
        if (version != 0)
            return false; // an unsupported version is rebuilt by open()
    }

    if (!db.transaction())
        return false;
    QSqlQuery q(db);
    const char *ddl[] = {
        "CREATE TABLE search_notes ("
        "  id INTEGER PRIMARY KEY,"
        "  rel_path TEXT NOT NULL UNIQUE,"
        "  folder TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  modified_ms INTEGER NOT NULL,"
        "  file_size INTEGER NOT NULL,"
        "  index_revision INTEGER NOT NULL)",
        "CREATE TABLE search_note_tags ("
        "  note_id INTEGER NOT NULL REFERENCES search_notes(id) ON DELETE CASCADE,"
        "  tag TEXT NOT NULL,"
        "  PRIMARY KEY (note_id, tag))",
        "CREATE TABLE search_blocks ("
        "  id INTEGER PRIMARY KEY,"
        "  note_id INTEGER NOT NULL REFERENCES search_notes(id) ON DELETE CASCADE,"
        "  kind INTEGER NOT NULL,"
        "  block_index INTEGER NOT NULL,"
        "  display_text TEXT NOT NULL,"
        "  folded_text TEXT NOT NULL,"
        "  verbatim INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE (note_id, kind, block_index))",
        "CREATE INDEX idx_blocks_note ON search_blocks(note_id)",
        "CREATE INDEX idx_notes_folder ON search_notes(folder)",
        "CREATE VIRTUAL TABLE search_words USING fts5("
        "  folded_text, content='search_blocks', content_rowid='id',"
        "  detail='none', columnsize=0,"
        "  tokenize=\"unicode61 remove_diacritics 0 tokenchars '_'\")",
        "CREATE VIRTUAL TABLE search_trigrams USING fts5("
        "  folded_text, content='search_blocks', content_rowid='id',"
        "  tokenize='trigram case_sensitive 1')",
    };
    for (const char *stmt : ddl) {
        if (!q.exec(QLatin1String(stmt))) {
            db.rollback();
            return false;
        }
    }
    if (!q.exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion))) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool SearchIndexDb::integrityOk() const
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next())
        return false;
    return q.value(0).toString() == QLatin1String("ok");
}

bool SearchIndexDb::removeNote(const QString &relPath)
{
    if (!m_usable)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction())
        return false;

    QSqlQuery find(db);
    find.prepare(QStringLiteral("SELECT id FROM search_notes WHERE rel_path=?"));
    find.addBindValue(relPath);
    if (!find.exec()) {
        db.rollback();
        return false;
    }
    if (!find.next()) {
        db.rollback();
        return true; // nothing to remove is success
    }
    const qint64 noteId = find.value(0).toLongLong();

    // External-content FTS5 cannot delete by rowid alone: each posting must be
    // withdrawn with its stored folded_text, or the index corrupts silently
    // (search.md §5 step 4).
    QSqlQuery blocks(db);
    blocks.prepare(QStringLiteral(
        "SELECT id, folded_text FROM search_blocks WHERE note_id=?"));
    blocks.addBindValue(noteId);
    if (!blocks.exec()) {
        db.rollback();
        return false;
    }
    QSqlQuery delWord(db);
    delWord.prepare(QStringLiteral(
        "INSERT INTO search_words(search_words, rowid, folded_text) "
        "VALUES('delete', ?, ?)"));
    QSqlQuery delTri(db);
    delTri.prepare(QStringLiteral(
        "INSERT INTO search_trigrams(search_trigrams, rowid, folded_text) "
        "VALUES('delete', ?, ?)"));
    while (blocks.next()) {
        const qint64 blockId = blocks.value(0).toLongLong();
        const QString folded = blocks.value(1).toString();
        delWord.addBindValue(blockId);
        delWord.addBindValue(folded);
        delTri.addBindValue(blockId);
        delTri.addBindValue(folded);
        if (!delWord.exec() || !delTri.exec()) {
            db.rollback();
            return false;
        }
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM search_notes WHERE id=?"));
    del.addBindValue(noteId);
    if (!del.exec()) {
        db.rollback();
        return false;
    }
    // search_blocks and search_note_tags cascade on the note delete.
    return db.commit();
}

bool SearchIndexDb::replaceNote(const IndexedNote &note, qint64 *outRevision)
{
    if (!m_usable)
        return false;
    PerfLog::ScopedTimer perf(QStringLiteral("search.index.note_replace"),
                              QVariantMap{{QStringLiteral("blocks"),
                                           note.blocks.size()}});
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction())
        return false;

    // Retain the note id and revision across the replace so published
    // locations can detect staleness (search.md §5 steps 3-6).
    qint64 noteId = -1;
    qint64 nextRevision = 1;
    {
        QSqlQuery find(db);
        find.prepare(QStringLiteral(
            "SELECT id, index_revision FROM search_notes WHERE rel_path=?"));
        find.addBindValue(note.relPath);
        if (!find.exec()) {
            db.rollback();
            return false;
        }
        if (find.next()) {
            noteId = find.value(0).toLongLong();
            nextRevision = find.value(1).toLongLong() + 1;
        }
    }

    // Withdraw old postings with their stored folded_text before deleting the
    // block rows (search.md §5 step 4).
    if (noteId >= 0) {
        QSqlQuery blocks(db);
        blocks.prepare(QStringLiteral(
            "SELECT id, folded_text FROM search_blocks WHERE note_id=?"));
        blocks.addBindValue(noteId);
        if (!blocks.exec()) {
            db.rollback();
            return false;
        }
        QSqlQuery delWord(db);
        delWord.prepare(QStringLiteral(
            "INSERT INTO search_words(search_words, rowid, folded_text) "
            "VALUES('delete', ?, ?)"));
        QSqlQuery delTri(db);
        delTri.prepare(QStringLiteral(
            "INSERT INTO search_trigrams(search_trigrams, rowid, folded_text) "
            "VALUES('delete', ?, ?)"));
        while (blocks.next()) {
            const qint64 blockId = blocks.value(0).toLongLong();
            const QString folded = blocks.value(1).toString();
            delWord.addBindValue(blockId);
            delWord.addBindValue(folded);
            delTri.addBindValue(blockId);
            delTri.addBindValue(folded);
            if (!delWord.exec() || !delTri.exec()) {
                db.rollback();
                return false;
            }
        }
        QSqlQuery delBlocks(db);
        delBlocks.prepare(
            QStringLiteral("DELETE FROM search_blocks WHERE note_id=?"));
        delBlocks.addBindValue(noteId);
        QSqlQuery delTags(db);
        delTags.prepare(
            QStringLiteral("DELETE FROM search_note_tags WHERE note_id=?"));
        delTags.addBindValue(noteId);
        if (!delBlocks.exec() || !delTags.exec()) {
            db.rollback();
            return false;
        }
    }

    // Upsert the note row.
    if (noteId >= 0) {
        QSqlQuery upd(db);
        upd.prepare(QStringLiteral(
            "UPDATE search_notes SET folder=?, title=?, modified_ms=?, "
            "file_size=?, index_revision=? WHERE id=?"));
        upd.addBindValue(note.folder);
        upd.addBindValue(note.title);
        upd.addBindValue(note.modifiedMs);
        upd.addBindValue(note.fileSize);
        upd.addBindValue(nextRevision);
        upd.addBindValue(noteId);
        if (!upd.exec()) {
            db.rollback();
            return false;
        }
    } else {
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO search_notes(rel_path, folder, title, modified_ms, "
            "file_size, index_revision) VALUES(?,?,?,?,?,?)"));
        ins.addBindValue(note.relPath);
        ins.addBindValue(note.folder);
        ins.addBindValue(note.title);
        ins.addBindValue(note.modifiedMs);
        ins.addBindValue(note.fileSize);
        ins.addBindValue(nextRevision);
        if (!ins.exec()) {
            db.rollback();
            return false;
        }
        noteId = ins.lastInsertId().toLongLong();
    }

    // Tags.
    if (!note.tags.isEmpty()) {
        QSqlQuery tag(db);
        tag.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO search_note_tags(note_id, tag) VALUES(?,?)"));
        for (const QString &t : note.tags) {
            tag.addBindValue(noteId);
            tag.addBindValue(t);
            if (!tag.exec()) {
                db.rollback();
                return false;
            }
        }
    }

    // Title row (kind 0, block_index -1) plus one row per body block. Each
    // block row also feeds both FTS tables with its folded shadow.
    QSqlQuery insBlock(db);
    insBlock.prepare(QStringLiteral(
        "INSERT INTO search_blocks(note_id, kind, block_index, display_text, "
        "folded_text, verbatim) VALUES(?,?,?,?,?,?)"));
    QSqlQuery insWord(db);
    insWord.prepare(QStringLiteral(
        "INSERT INTO search_words(rowid, folded_text) VALUES(?,?)"));
    QSqlQuery insTri(db);
    insTri.prepare(QStringLiteral(
        "INSERT INTO search_trigrams(rowid, folded_text) VALUES(?,?)"));

    auto nonNull = [](const QString &s) {
        return s.isNull() ? QString::fromLatin1("") : s;
    };
    auto insertBlock = [&](int kind, int blockIndex, const QString &displayIn,
                           bool verbatim) -> bool {
        const QString display = nonNull(displayIn);
        const QString folded = nonNull(SearchMatching::fold(display));
        insBlock.addBindValue(noteId);
        insBlock.addBindValue(kind);
        insBlock.addBindValue(blockIndex);
        insBlock.addBindValue(display);
        insBlock.addBindValue(folded);
        insBlock.addBindValue(verbatim ? 1 : 0);
        if (!insBlock.exec())
            return false;
        const qint64 blockId = insBlock.lastInsertId().toLongLong();
        insWord.addBindValue(blockId);
        insWord.addBindValue(folded);
        insTri.addBindValue(blockId);
        insTri.addBindValue(folded);
        return insWord.exec() && insTri.exec();
    };

    if (!insertBlock(0, -1, note.title, false)) {
        db.rollback();
        return false;
    }
    for (const IndexedBlock &block : note.blocks) {
        if (!insertBlock(1, block.blockIndex, block.displayText,
                         block.verbatim)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit())
        return false;
    if (outRevision)
        *outRevision = nextRevision;
    return true;
}

bool SearchIndexDb::hasNoteFresh(const QString &relPath, qint64 fileSize,
                                 qint64 modifiedMs) const
{
    if (!m_usable)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT file_size, modified_ms FROM search_notes WHERE rel_path=?"));
    q.addBindValue(relPath);
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toLongLong() == fileSize
        && q.value(1).toLongLong() == modifiedMs;
}

QStringList SearchIndexDb::allRelPaths() const
{
    QStringList paths;
    if (!m_usable)
        return paths;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT rel_path FROM search_notes"))) {
        while (q.next())
            paths.append(q.value(0).toString());
    }
    return paths;
}

qint64 SearchIndexDb::revisionOf(const QString &relPath) const
{
    if (!m_usable)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT index_revision FROM search_notes WHERE rel_path=?"));
    q.addBindValue(relPath);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return 0;
}

int SearchIndexDb::noteRowCount() const
{
    if (!m_usable)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM search_notes")) && q.next())
        return q.value(0).toInt();
    return 0;
}

SearchResults SearchIndexDb::query(const SearchQuery &request,
                                   const std::atomic_bool *cancel) const
{
    SearchResults results;
    if (!m_usable)
        return results;

    const QString effective = request.query.trimmed();
    if (effective.isEmpty())
        return results;

    const int scalarCount = SearchMatching::unicodeScalarCount(effective);
    const bool wholeWord = scalarCount <= 2;
    const QString folded = SearchMatching::fold(effective);

    // A punctuation-only short query has no word token and yields nothing in V1
    // (search.md §4.2).
    if (wholeWord && !SearchMatching::hasWordChar(folded))
        return results;

    const QString ftsTable = wholeWord ? QStringLiteral("search_words")
                                       : QStringLiteral("search_trigrams");
    const QString phrase = SearchMatching::ftsPhrase(folded);

    // Filters are applied in SQL so irrelevant blocks are never loaded
    // (search.md §7 step 3).
    QStringList predicates;
    predicates << ftsTable + QStringLiteral(" MATCH :phrase");
    if (!request.folderScope.isEmpty()) {
        predicates << QStringLiteral(
            "(n.folder = :folder OR n.folder LIKE :folderprefix ESCAPE '\\')");
    }
    if (!request.tagFilter.isEmpty()) {
        predicates << QStringLiteral(
            "EXISTS (SELECT 1 FROM search_note_tags t "
            "WHERE t.note_id = n.id AND t.tag = :tag)");
    }
    const bool custom = request.datePreset == QLatin1String("custom");
    qint64 floorMs = 0;
    bool hasFloor = false;
    if (!custom) {
        const QString &p = request.datePreset;
        if (p == QLatin1String("today")) {
            floorMs = QDateTime::fromMSecsSinceEpoch(request.nowMs)
                          .date().startOfDay().toMSecsSinceEpoch();
            hasFloor = true;
        } else if (p == QLatin1String("week")) {
            floorMs = request.nowMs - qint64(7) * 86400000;
            hasFloor = true;
        } else if (p == QLatin1String("month")) {
            floorMs = request.nowMs - qint64(30) * 86400000;
            hasFloor = true;
        } else if (p == QLatin1String("year")) {
            floorMs = request.nowMs - qint64(365) * 86400000;
            hasFloor = true;
        }
        if (hasFloor)
            predicates << QStringLiteral("n.modified_ms >= :floor");
    } else {
        if (request.customFrom.isValid())
            predicates << QStringLiteral("n.modified_ms >= :customfrom");
        if (request.customTo.isValid())
            predicates << QStringLiteral("n.modified_ms <= :customto");
    }

    QString sql = QStringLiteral(
        "SELECT n.rel_path, n.title, n.index_revision, b.kind, b.block_index, "
        "b.display_text, b.verbatim "
        "FROM %1 x JOIN search_blocks b ON b.id = x.rowid "
        "JOIN search_notes n ON n.id = b.note_id WHERE ").arg(ftsTable)
        + predicates.join(QStringLiteral(" AND "))
        + QStringLiteral(" ORDER BY n.rel_path, b.block_index");

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.prepare(sql))
        return results;
    q.bindValue(QStringLiteral(":phrase"), phrase);
    if (!request.folderScope.isEmpty()) {
        q.bindValue(QStringLiteral(":folder"), request.folderScope);
        q.bindValue(QStringLiteral(":folderprefix"),
                    escapeLike(request.folderScope) + QStringLiteral("/%"));
    }
    if (!request.tagFilter.isEmpty())
        q.bindValue(QStringLiteral(":tag"), request.tagFilter);
    if (hasFloor)
        q.bindValue(QStringLiteral(":floor"), floorMs);
    if (custom) {
        if (request.customFrom.isValid())
            q.bindValue(QStringLiteral(":customfrom"),
                        startOfDayMs(request.customFrom));
        if (request.customTo.isValid())
            q.bindValue(QStringLiteral(":customto"),
                        endOfDayMs(request.customTo));
    }

    qint64 candidateBlocks = 0;
    qint64 verifiedBlocks = 0;
    static const int rowsPerNoteCap = 10;
    static const int cancelInterval = 256;

    // Candidates arrive grouped by rel_path (the ORDER BY), so one pass builds
    // groups without buffering the whole corpus.
    {
        PerfLog::ScopedTimer perf(QStringLiteral("search.verify"));
        if (!q.exec())
            return results;

        SearchGroup current;
        bool haveCurrent = false;
        auto flush = [&]() {
            if (!haveCurrent)
                return;
            if (current.titleMatched || current.matchCount > 0) {
                results.matchCount += current.matchCount;
                current.moreMatches = current.matchCount - current.matches.size();
                results.groups.append(current);
            }
            haveCurrent = false;
        };

        while (q.next()) {
            if (cancel && (candidateBlocks % cancelInterval) == 0
                && cancel->load()) {
                // An obsolete generation stops cooperatively; the caller
                // discards a partial result (search.md §7).
                return SearchResults();
            }
            ++candidateBlocks;

            const QString relPath = q.value(0).toString();
            if (!haveCurrent || relPath != current.relPath) {
                flush();
                current = SearchGroup();
                current.relPath = relPath;
                current.title = q.value(1).toString();
                current.indexRevision = q.value(2).toLongLong();
                current.titleMatched = !SearchMatching::verifyOccurrences(
                                            current.title, effective, wholeWord)
                                            .isEmpty();
                haveCurrent = true;
            }

            const int kind = q.value(3).toInt();
            if (kind != 1)
                continue; // the title row only established candidacy

            const int blockIndex = q.value(4).toInt();
            const QString display = q.value(5).toString();
            const QList<int> offsets = SearchMatching::verifyOccurrences(
                display, effective, wholeWord);
            if (offsets.isEmpty())
                continue;
            ++verifiedBlocks;
            for (const int at : offsets) {
                ++current.matchCount;
                if (current.matches.size() < rowsPerNoteCap) {
                    current.matches.append(
                        buildMatch(display, blockIndex, at, effective.size()));
                }
            }
        }
        flush();
    }

    results.noteCount = results.groups.size();

    PerfLog::instance().mark(
        QStringLiteral("search.query.total"), 0.0,
        QVariantMap{
            {QStringLiteral("class"),
             wholeWord ? QStringLiteral("word") : QStringLiteral("trigram")},
            {QStringLiteral("queryLength"), scalarCount},
            {QStringLiteral("candidateBlocks"), candidateBlocks},
            {QStringLiteral("verifiedBlocks"), verifiedBlocks},
            {QStringLiteral("notes"), results.noteCount},
            {QStringLiteral("matches"), results.matchCount},
        });
    return results;
}
