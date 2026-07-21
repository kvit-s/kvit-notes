// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "searchindexdb.h"

#include "perflog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <limits>

// Current on-disk schema version: every schema change bumps
// PRAGMA user_version; an unsupported version is rebuilt.
// v2 added search_notes.content_hash, the freshness fingerprint.
// v3 added search_notes.change_token, which lets a reconcile decide freshness
// from a stat instead of reading and hashing the file.
static constexpr int kSchemaVersion = 3;

namespace SearchMatching {

int unicodeScalarCount(const QString &query)
{
    // Count Unicode scalar values of an NFC-normalized copy so a supplementary-
    // plane character is one character and composed/decomposed spellings route
    // the same. Normalization affects routing only.
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

bool isWordScalar(char32_t scalar)
{
    if (scalar == U'_')
        return true;
    const QChar::Category category = QChar::category(char32_t(scalar));
    switch (category) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Number_Other:
    // unicode61 counts private-use characters as token characters, so a block
    // containing one indexes it as part of the surrounding token. The verifier
    // has to agree or it accepts matches the index cannot offer.
    case QChar::Other_PrivateUse:
    // Combining marks and modifier symbols continue a token rather than
    // ending it: with remove_diacritics 0, unicode61 indexes decomposed
    // "naïve" as the single token "naïve", so "ve" is not a whole word
    // inside it and the verifier must not say otherwise. Marks that unicode61
    // does treat as separators are included too — that only makes the verifier
    // stricter than the candidate index, which costs an unusual match rather
    // than producing one the index can never supply.
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
    case QChar::Symbol_Modifier:
        return true;
    default:
        return false;
    }
}

bool isWordCharAt(const QString &text, int index)
{
    if (index < 0 || index >= text.size())
        return false;
    const QChar ch = text.at(index);
    if (ch.isHighSurrogate() && index + 1 < text.size()
        && text.at(index + 1).isLowSurrogate()) {
        return isWordScalar(QChar::surrogateToUcs4(ch, text.at(index + 1)));
    }
    if (ch.isLowSurrogate() && index > 0
        && text.at(index - 1).isHighSurrogate()) {
        return isWordScalar(QChar::surrogateToUcs4(text.at(index - 1), ch));
    }
    return isWordScalar(ch.unicode());
}

QString fold(const QString &text)
{
    return text.toCaseFolded();
}

bool hasWordChar(const QString &folded)
{
    // Over scalar values: a supplementary-plane letter is a letter, where
    // inspecting UTF-16 code units sees two unpaired surrogates, classifies
    // neither as a word character, and routes a perfectly ordinary
    // single-letter query to the punctuation-only dead end.
    for (int i = 0; i < folded.size(); ++i) {
        const QChar ch = folded.at(i);
        if (ch.isHighSurrogate() && i + 1 < folded.size()
            && folded.at(i + 1).isLowSurrogate()) {
            if (isWordScalar(QChar::surrogateToUcs4(ch, folded.at(i + 1))))
                return true;
            ++i;
            continue;
        }
        if (isWordScalar(ch.unicode()))
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

int scanOccurrences(const QString &displayText, const QString &query,
                    bool wholeWord, int collectLimit, QList<int> *collected,
                    const SearchCancel *cancel)
{
    if (query.isEmpty())
        return 0;
    // One check per this many occurrences. A single pathological block — a
    // pasted log with a million hits — is otherwise scanned to the end with no
    // way out, which is exactly the work an abandoned query most needs to stop
    // doing.
    static const int cancelInterval = 64;
    int total = 0;
    int probes = 0;
    const int step = qMax(1, query.size());
    int from = 0;
    while (true) {
        if (cancel && (probes++ % cancelInterval) == 0 && cancel->cancelled())
            return -1;
        const int at = displayText.indexOf(query, from, Qt::CaseInsensitive);
        if (at < 0)
            break;
        const int end = at + query.size();
        bool accept = true;
        if (wholeWord) {
            // A whole-word match is bounded by non-word characters on both
            // sides. The Qt refinement is the final authority.
            if (at > 0 && isWordCharAt(displayText, at - 1))
                accept = false;
            if (accept && end < displayText.size()
                && isWordCharAt(displayText, end))
                accept = false;
        }
        if (accept) {
            if (collected && collected->size() < collectLimit)
                collected->append(at);
            ++total;
        }
        // Non-overlapping: advance past this occurrence even when rejected, so
        // a single scan is O(n). CaseInsensitive keeps display
        // offsets exact because folding is never applied to the display text.
        from = at + step;
    }
    return total;
}

QList<int> verifyOccurrences(const QString &displayText, const QString &query,
                             bool wholeWord)
{
    QList<int> offsets;
    scanOccurrences(displayText, query, wholeWord,
                    std::numeric_limits<int>::max(), &offsets, nullptr);
    return offsets;
}

} // namespace SearchMatching

namespace {

// A readable window around a match inside one block's display text: the
// match's line, trimmed to 32 leading and 120 total characters with
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

// Escape LIKE wildcards so a folder named "a_b" never scopes into "axb".
// Paired with an explicit ESCAPE clause.
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

// Document order for two match positions in the same note.
bool sortsBefore(int blockA, int startA, int blockB, int startB)
{
    return blockA != blockB ? blockA < blockB : startA < startB;
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

namespace {

// One name per object, for the life of the process. Qt's connection registry
// is global and keyed by name alone, so a fixed name means the second vault
// opened silently takes over the first one's registry entry — and the first
// one's close() then unregisters a connection the second is still using.
QString uniqueConnectionName(const QString &role)
{
    static std::atomic<quint64> counter{0};
    return QStringLiteral("kvit_search_%1_%2")
        .arg(role)
        .arg(counter.fetch_add(1));
}

} // namespace

namespace SearchIndexOps {
namespace {
std::atomic<quint64> g_fileReads{0};
std::atomic<quint64> g_fingerprints{0};
std::atomic<quint64> g_deepChecks{0};
} // namespace

quint64 fileReads() { return g_fileReads.load(); }
quint64 fingerprints() { return g_fingerprints.load(); }
quint64 deepIntegrityChecks() { return g_deepChecks.load(); }
void recordFileRead() { g_fileReads.fetch_add(1); }

void reset()
{
    g_fileReads.store(0);
    g_fingerprints.store(0);
    g_deepChecks.store(0);
}

} // namespace SearchIndexOps

SearchCancel::~SearchCancel() = default;

SearchIndexDb::SearchIndexDb(const QString &role)
    : m_connectionName(uniqueConnectionName(role))
{
}

SearchIndexDb::~SearchIndexDb()
{
    close();
}

QString SearchIndexDb::contentFingerprint(const QString &fileText)
{
    SearchIndexOps::g_fingerprints.fetch_add(1);
    return QString::fromLatin1(
        QCryptographicHash::hash(fileText.toUtf8(), QCryptographicHash::Sha1)
            .toHex());
}

bool SearchIndexDb::probeCapability()
{
    const QString probeConn = uniqueConnectionName(QStringLiteral("probe"));
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

bool SearchIndexDb::open(const QString &dbPath, OpenMode mode, DeepCheck deep)
{
    close(); // still under the previous path: a clean close is stamped there
    m_dbPath = dbPath;
    m_mode = mode;

    // Consume the marker before anything else looks at the file. Reading and
    // deleting it in one step is what stops it lying: from here until an
    // orderly close there is no marker on disk, so a process that dies in
    // between leaves the next open with nothing to trust. Only the writer
    // takes part — the reader never runs the deep check, and must not be able
    // to spend or issue a certificate the writer relies on.
    const bool marked =
        mode == OpenMode::RebuildIfUnusable && consumeCleanMarker();
    const bool verified = marked && deep == DeepCheck::WhenUnverified;

    auto tryOpen = [&]() -> bool {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    m_connectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open())
            return false;
        m_open = true;
        if (!applyPragmas())
            return false;
        // Structural corruption, a stale FTS index, or a schema this build
        // does not recognise all disqualify the file. The FTS check
        // re-tokenizes the whole content table, so only the writer — which is
        // also the only side that can act on the answer by rebuilding — pays
        // for it; the reader opens after the writer has already vetted the
        // file. The cheap checks run every time; the deep one is skipped when
        // the marker showed this process left the database verified and
        // closed.
        if (!structuralIntegrityOk())
            return false;
        if (mode == OpenMode::RebuildIfUnusable && !verified && !ftsIntegrityOk())
            return false;
        return ensureSchema() && schemaValid();
    };

    if (tryOpen()) {
        m_usable = true;
        return true;
    }

    if (mode == OpenMode::RequireUsable) {
        // Only the writer may destroy the file. A reader that rebuilt here
        // would unlink the database out from under a writer that is still
        // attached, leaving the two connections on different inodes with no
        // sign that anything went wrong.
        close();
        return false;
    }

    // A corrupt or obsolete database is closed, removed, and rebuilt. Note
    // files are never touched.
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
    // Only a writer that reached a usable state has anything to certify, and
    // only this line ever creates a marker.
    const bool certify = m_usable && m_mode == OpenMode::RebuildIfUnusable;
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_open = false;
    m_usable = false;
    // m_connectionName deliberately survives: it is this object's identity,
    // and the rebuild retry inside open() calls close() first. Clearing it
    // there sent the retry to Qt's *default* connection — shared with anything
    // else in the process, and never removed afterwards, because the following
    // close() no longer had a name to remove.
    if (certify)
        writeCleanMarker();
}

QString SearchIndexDb::cleanMarkerPath(const QString &dbPath)
{
    if (dbPath.isEmpty() || dbPath == QStringLiteral(":memory:"))
        return QString();
    return dbPath + QStringLiteral(".clean");
}

// What a marker says. The schema version is part of it because a marker left
// by a build with a different on-disk layout certifies nothing about this one,
// and a database that has been rebuilt under a new version must be checked
// once before it is trusted again.
static QByteArray cleanMarkerContent()
{
    return QByteArrayLiteral("kvit-search-clean v")
        + QByteArray::number(kSchemaVersion) + '\n';
}

bool SearchIndexDb::consumeCleanMarker()
{
    const QString path = cleanMarkerPath(m_dbPath);
    if (path.isEmpty())
        return false; // an in-memory database is never pre-verified
    QFile marker(path);
    if (!marker.exists())
        return false;
    bool valid = false;
    if (marker.open(QIODevice::ReadOnly)) {
        valid = marker.read(64) == cleanMarkerContent();
        marker.close();
    }
    // Removed whether or not it was readable: the point of consuming it is
    // that the next open of this file cannot inherit this one's verdict.
    QFile::remove(path);
    return valid;
}

void SearchIndexDb::writeCleanMarker()
{
    const QString path = cleanMarkerPath(m_dbPath);
    if (path.isEmpty())
        return;
    QFile marker(path);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    marker.write(cleanMarkerContent());
    marker.close();
}

bool SearchIndexDb::applyPragmas()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // WAL keeps readers unblocked by the writer; NORMAL trades a sliver of
    // durability the disposable cache does not need for speed; the busy timeout
    // bounds contention between the read and write connections.
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
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return false;
    if (q.value(0).toInt() != kSchemaVersion)
        return false;
    return schemaObjectsPresent();
}

bool SearchIndexDb::schemaObjectsPresent() const
{
    // user_version is a number anyone can write; it says nothing about what is
    // actually in the file. A database whose search_words table was dropped
    // still reports version 2, opens cleanly, and then answers every word
    // query with an error the caller used to read as "no matches".
    struct Required {
        const char *type;
        const char *name;
        const char *mustContain; // a fragment of the CREATE statement, or null
    };
    static const Required required[] = {
        {"table", "search_notes", "content_hash"},
        {"table", "search_notes", "change_token"},
        {"table", "search_note_tags", nullptr},
        {"table", "search_blocks", "folded_text"},
        {"index", "idx_blocks_note", nullptr},
        {"index", "idx_notes_folder", nullptr},
        {"table", "search_words", "content='search_blocks'"},
        {"table", "search_trigrams", "content='search_blocks'"},
    };

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT type, sql FROM sqlite_master WHERE name = ?"));
    for (const Required &object : required) {
        q.addBindValue(QLatin1String(object.name));
        if (!q.exec() || !q.next())
            return false;
        if (q.value(0).toString() != QLatin1String(object.type))
            return false;
        if (object.mustContain
            && !q.value(1).toString().contains(
                QLatin1String(object.mustContain))) {
            return false;
        }
    }
    return true;
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
        "  content_hash TEXT NOT NULL DEFAULT '',"
        "  change_token INTEGER NOT NULL DEFAULT 0,"
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
    return structuralIntegrityOk() && ftsIntegrityOk();
}

bool SearchIndexDb::structuralIntegrityOk() const
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next())
        return false;
    return q.value(0).toString() == QLatin1String("ok");
}

bool SearchIndexDb::ftsIntegrityOk() const
{
    if (!m_open)
        return false;
    SearchIndexOps::g_deepChecks.fetch_add(1);
    // PRAGMA integrity_check verifies SQLite's own b-trees. It cannot see that
    // an external-content FTS index disagrees with search_blocks, which is the
    // corruption that matters here: postings that no longer match the stored
    // text return notes whose display text does not contain the query, and
    // withdraw notes that do. FTS5's own integrity-check command compares the
    // index against the content table and fails the statement when they part.
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    for (const char *table : {"search_words", "search_trigrams"}) {
        // The tables only exist once the schema has been created; a brand new
        // file legitimately has neither.
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE name = ? AND type = 'table'"));
        exists.addBindValue(QLatin1String(table));
        if (!exists.exec() || !exists.next())
            continue;
        // The argument is what makes this compare the index against the
        // content table; without it FTS5 only checks the index against itself,
        // which a stale external-content index passes.
        QSqlQuery check(db);
        if (check.exec(QStringLiteral(
                "INSERT INTO %1(%1, rank) VALUES('integrity-check', 1)")
                           .arg(QLatin1String(table)))) {
            continue;
        }
        // SQLITE_CORRUPT (11) and SQLITE_CORRUPT_VTAB (267) are the verdict.
        // Anything else means this SQLite does not understand the argument, so
        // fall back to the weaker check rather than condemning a healthy file.
        const int code = check.lastError().nativeErrorCode().toInt();
        if (code == 11 || code == 267)
            return false;
        QSqlQuery legacy(db);
        if (!legacy.exec(QStringLiteral(
                "INSERT INTO %1(%1) VALUES('integrity-check')")
                             .arg(QLatin1String(table)))) {
            return false;
        }
    }
    return true;
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
    // withdrawn with its stored folded_text, or the index corrupts silently.
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
    if (!db.commit()) {
        db.rollback(); // see replaceNote: a failed commit stays open otherwise
        return false;
    }
    return true;
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
    // locations can detect staleness.
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
    // block rows.
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
    const QString contentHash =
        note.contentHash.isNull() ? QString::fromLatin1("") : note.contentHash;
    if (noteId >= 0) {
        QSqlQuery upd(db);
        upd.prepare(QStringLiteral(
            "UPDATE search_notes SET folder=?, title=?, modified_ms=?, "
            "file_size=?, content_hash=?, change_token=?, index_revision=? "
            "WHERE id=?"));
        upd.addBindValue(note.folder);
        upd.addBindValue(note.title);
        upd.addBindValue(note.modifiedMs);
        upd.addBindValue(note.fileSize);
        upd.addBindValue(contentHash);
        upd.addBindValue(note.changeToken);
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
            "file_size, content_hash, change_token, index_revision) "
            "VALUES(?,?,?,?,?,?,?,?)"));
        ins.addBindValue(note.relPath);
        ins.addBindValue(note.folder);
        ins.addBindValue(note.title);
        ins.addBindValue(note.modifiedMs);
        ins.addBindValue(note.fileSize);
        ins.addBindValue(contentHash);
        ins.addBindValue(note.changeToken);
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

    if (!db.commit()) {
        // A commit that fails leaves the transaction open on this connection,
        // and every later transaction on it then fails to begin. Rolling back
        // returns the connection to a usable state instead of poisoning it for
        // the rest of the session.
        db.rollback();
        return false;
    }
    if (outRevision)
        *outRevision = nextRevision;
    return true;
}

bool SearchIndexDb::touchNote(const QString &relPath, qint64 fileSize,
                              qint64 modifiedMs, const QString &contentHash,
                              qint64 changeToken)
{
    if (!m_usable)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE search_notes SET file_size=?, modified_ms=?, content_hash=?, "
        "change_token=? WHERE rel_path=?"));
    q.addBindValue(fileSize);
    q.addBindValue(modifiedMs);
    q.addBindValue(contentHash.isNull() ? QString::fromLatin1("")
                                        : contentHash);
    q.addBindValue(changeToken);
    q.addBindValue(relPath);
    return q.exec() && q.numRowsAffected() != 0;
}

bool SearchIndexDb::hasNoteFresh(const QString &relPath, qint64 fileSize,
                                 qint64 modifiedMs,
                                 const QString &contentHash) const
{
    if (!m_usable)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT file_size, modified_ms, content_hash "
                             "FROM search_notes WHERE rel_path=?"));
    q.addBindValue(relPath);
    if (!q.exec() || !q.next())
        return false;
    if (!contentHash.isEmpty())
        return q.value(2).toString() == contentHash;
    return q.value(0).toLongLong() == fileSize
        && q.value(1).toLongLong() == modifiedMs;
}

bool SearchIndexDb::hasNoteStamp(const QString &relPath, qint64 fileSize,
                                 qint64 modifiedMs, qint64 changeToken) const
{
    if (!m_usable || changeToken == 0)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT file_size, modified_ms, change_token "
                             "FROM search_notes WHERE rel_path=?"));
    q.addBindValue(relPath);
    if (!q.exec() || !q.next())
        return false;
    return q.value(2).toLongLong() == changeToken
        && q.value(0).toLongLong() == fileSize
        && q.value(1).toLongLong() == modifiedMs;
}

qint64 SearchIndexDb::changeTokenOf(const QString &relPath) const
{
    if (!m_usable)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT change_token FROM search_notes WHERE rel_path=?"));
    q.addBindValue(relPath);
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toLongLong();
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
                                   const SearchCancel *cancel) const
{
    SearchResults results;
    if (!m_usable) {
        // Not an answer of "no matches": there is no index to ask.
        results.ok = false;
        return results;
    }

    const QString effective = request.query.trimmed();
    if (effective.isEmpty())
        return results;

    const int scalarCount = SearchMatching::unicodeScalarCount(effective);
    const bool wholeWord = scalarCount <= 2;
    const QString folded = SearchMatching::fold(effective);

    // A punctuation-only short query has no word token and yields nothing
    // in V1.
    if (wholeWord && !SearchMatching::hasWordChar(folded))
        return results;

    const QString ftsTable = wholeWord ? QStringLiteral("search_words")
                                       : QStringLiteral("search_trigrams");
    const QString phrase = SearchMatching::ftsPhrase(folded);

    // Filters are applied in SQL so irrelevant blocks are never loaded.
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

    // No ORDER BY: sorting every candidate row happens inside the first step()
    // and cannot be interrupted, so an abandoned query would still pay for the
    // whole sort before its first cancellation check. Rows are grouped by note
    // here instead, and the groups — bounded by ten stored matches each — are
    // ordered at the end.
    QString sql = QStringLiteral(
        "SELECT n.rel_path, n.title, n.index_revision, b.kind, b.block_index, "
        "b.display_text, b.verbatim "
        "FROM %1 x JOIN search_blocks b ON b.id = x.rowid "
        "JOIN search_notes n ON n.id = b.note_id WHERE ").arg(ftsTable)
        + predicates.join(QStringLiteral(" AND "));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        results.ok = false;
        return results;
    }
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

    // One entry per candidate note. Each holds at most rowsPerNoteCap matches,
    // so the memory a query can reach is bounded by the number of matching
    // notes rather than by the number of occurrences in them.
    QHash<QString, SearchGroup> groups;
    {
        PerfLog::ScopedTimer perf(QStringLiteral("search.verify"));
        if (!q.exec()) {
            results.ok = false;
            return results;
        }

        while (q.next()) {
            // Every row, not every 256th: the 256-row stride meant a query
            // over fewer than 256 candidates was never checked at all after
            // its first row.
            if (cancel && cancel->cancelled()) {
                results.cancelled = true;
                return results;
            }
            ++candidateBlocks;

            const QString relPath = q.value(0).toString();
            auto it = groups.find(relPath);
            if (it == groups.end()) {
                SearchGroup fresh;
                fresh.relPath = relPath;
                fresh.title = q.value(1).toString();
                fresh.indexRevision = q.value(2).toLongLong();
                const int inTitle = SearchMatching::scanOccurrences(
                    fresh.title, effective, wholeWord, 0, nullptr, cancel);
                if (inTitle < 0) {
                    results.cancelled = true;
                    return results;
                }
                fresh.titleMatched = inTitle > 0;
                it = groups.insert(relPath, fresh);
            }

            const int kind = q.value(3).toInt();
            if (kind != 1)
                continue; // the title row only established candidacy

            const int blockIndex = q.value(4).toInt();
            const QString display = q.value(5).toString();
            QList<int> offsets;
            const int found = SearchMatching::scanOccurrences(
                display, effective, wholeWord, rowsPerNoteCap, &offsets,
                cancel);
            if (found < 0) {
                results.cancelled = true;
                return results;
            }
            if (found == 0)
                continue;
            ++verifiedBlocks;
            it->matchCount += found;
            // Rows arrive in whatever order SQLite produced them, so the ten
            // kept matches are maintained as the first ten in document order
            // rather than the first ten to arrive. The list never exceeds the
            // cap, so a note with a million occurrences still costs ten rows.
            for (const int at : offsets) {
                if (it->matches.size() >= rowsPerNoteCap) {
                    const SearchMatch &last = it->matches.constLast();
                    if (sortsBefore(last.blockIndex, last.start, blockIndex, at))
                        break; // later offsets in this block sort later still
                }
                const SearchMatch match =
                    buildMatch(display, blockIndex, at, effective.size());
                int pos = it->matches.size();
                while (pos > 0
                       && sortsBefore(match.blockIndex, match.start,
                                      it->matches.at(pos - 1).blockIndex,
                                      it->matches.at(pos - 1).start)) {
                    --pos;
                }
                it->matches.insert(pos, match);
                if (it->matches.size() > rowsPerNoteCap)
                    it->matches.removeLast();
            }
        }
    }

    results.groups.reserve(groups.size());
    for (SearchGroup &group : groups) {
        if (!group.titleMatched && group.matchCount == 0)
            continue;
        group.moreMatches = group.matchCount - group.matches.size();
        results.matchCount += group.matchCount;
        results.groups.append(group);
    }
    std::sort(results.groups.begin(), results.groups.end(),
              [](const SearchGroup &a, const SearchGroup &b) {
                  return a.relPath < b.relPath;
              });

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
