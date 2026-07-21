// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SEARCHINDEXDB_H
#define SEARCHINDEXDB_H

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

// The synchronous SQLite FTS5 engine behind global search.
//
// This class is deliberately GUI-free and thread-affine: one instance owns one
// QSqlDatabase connection and must be used only on the thread that opened it.
// CollectionSearchIndex wraps two instances (a write connection on the index
// worker thread, a read connection on the query worker thread); tests drive one
// instance directly and synchronously, which is why the matching primitives are
// exposed as static functions — the differential oracle checks them without a
// database.
//
// Design: search_blocks holds one disk-backed copy of the
// display text per block (kind 0 = title at block_index -1, kind 1 = body). Two
// external-content FTS5 indexes over the folded shadow of that text serve the
// two query classes: a unicode61 word index for one/two-character whole-word
// queries and a trigram index for literal substrings of three or more
// characters. FTS is only a candidate generator; every candidate row is refined
// with Qt so counts, offsets, and snippets stay exact.
namespace SearchMatching {

// Unicode scalar-value count of an NFC-normalized copy of the query. Routing
// uses this, never QString::length(), so a supplementary-plane
// character is one character and composed/decomposed spellings route the same.
int unicodeScalarCount(const QString &query);

// Word characters are letters, numbers, and underscore — the editor's
// whole-word boundary rule.
bool isWordChar(QChar ch);

// Case-folded shadow used for both indexing and candidate lookup.
// Folding can change length, so folded offsets are never exposed.
QString fold(const QString &text);

// True when a folded query carries at least one word character; a punctuation-
// only short query (e.g. "::") has none and produces no word candidates.
bool hasWordChar(const QString &folded);

// Quote a folded value as one literal FTS5 phrase: wrap in double quotes and
// double any interior double quote. The query is never interpreted as FTS
// syntax.
QString ftsPhrase(const QString &folded);

// Non-overlapping start offsets of the query inside one block's original
// display text. `wholeWord` applies the letter/number/underscore boundary rule
// for one/two-character queries; otherwise it is a plain case-insensitive
// substring scan. Offsets and length are display-text coordinates.
QList<int> verifyOccurrences(const QString &displayText, const QString &query,
                             bool wholeWord);

} // namespace SearchMatching

// One indexable block: its position in the note, the exact display text the
// editor shows, and whether it is verbatim (a code block — identity mapping for
// navigation).
struct IndexedBlock {
    int blockIndex = 0;
    QString displayText;
    bool verbatim = false;
};

// A complete parsed note ready to replace its rows atomically.
// `title` is indexed as the kind-0 block and stored on the note row so a
// body-only candidate can still report titleMatched.
struct IndexedNote {
    QString relPath;
    QString folder;
    QString title;
    qint64 modifiedMs = 0;
    qint64 fileSize = 0;
    QStringList tags;
    QList<IndexedBlock> blocks;
};

// A resolved query and its filters. `nowMs` is injected so date presets are
// deterministic in tests.
struct SearchQuery {
    QString query;
    QString folderScope;
    QString tagFilter;
    QString datePreset = QStringLiteral("any");
    QDate customFrom;
    QDate customTo;
    qint64 nowMs = 0;
};

// One verified occurrence row (a result row that reached the model).
struct SearchMatch {
    int blockIndex = 0;
    int start = 0;
    int length = 0;
    QString snippet;
    int snippetStart = 0;
    int snippetLength = 0;
};

// One matching note, in relative-path order.
struct SearchGroup {
    QString relPath;
    QString title;
    bool titleMatched = false;
    int matchCount = 0;   // exact, every verified body occurrence
    int moreMatches = 0;  // surplus beyond the 10-row cap, never silent
    qint64 indexRevision = 0;
    QList<SearchMatch> matches;
};

struct SearchResults {
    int noteCount = 0;
    int matchCount = 0;
    QList<SearchGroup> groups;
};

class SearchIndexDb
{
public:
    SearchIndexDb() = default;
    ~SearchIndexDb();

    SearchIndexDb(const SearchIndexDb &) = delete;
    SearchIndexDb &operator=(const SearchIndexDb &) = delete;

    // The FTS5 trigram capability probe: create a temporary
    // trigram FTS table on a throwaway in-memory connection. A packaged build
    // that fails this has a broken SQLite driver.
    static bool probeCapability();

    // Open (creating if needed) the database at `dbPath` under a unique
    // connection name, apply connection PRAGMAs, verify integrity, and create
    // or migrate the schema. A corrupt or wrong-version database is removed and
    // rebuilt empty. Returns false only when no usable database can be made
    // (e.g. missing FTS5), leaving isUsable() false.
    bool open(const QString &dbPath, const QString &connectionName);
    void close();
    bool isUsable() const { return m_usable; }
    QString dbPath() const { return m_dbPath; }

    // --- Content (write connection) -------------------------------------
    // Replace one note's title, block, tag, and FTS rows in a single
    // transaction and increment its index_revision. Returns the
    // new revision through `outRevision` when non-null.
    bool replaceNote(const IndexedNote &note, qint64 *outRevision = nullptr);
    bool removeNote(const QString &relPath);

    // True when the note row exists with the same size and modification time —
    // the warm-startup unchanged test.
    bool hasNoteFresh(const QString &relPath, qint64 fileSize,
                      qint64 modifiedMs) const;
    QStringList allRelPaths() const;
    qint64 revisionOf(const QString &relPath) const;
    int noteRowCount() const;

    // --- Query (read connection) ----------------------------------------
    SearchResults query(const SearchQuery &request,
                        const std::atomic_bool *cancel = nullptr) const;

    // --- Diagnostics ----------------------------------------------------
    // FTS integrity check across both indexes.
    bool integrityOk() const;

private:
    bool applyPragmas();
    bool ensureSchema();
    bool schemaValid() const;

    QString m_connectionName;
    QString m_dbPath;
    bool m_open = false;
    bool m_usable = false;
};

#endif // SEARCHINDEXDB_H
