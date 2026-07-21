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
class SearchCancel;

namespace SearchMatching {

// Unicode scalar-value count of an NFC-normalized copy of the query. Routing
// uses this, never QString::length(), so a supplementary-plane
// character is one character and composed/decomposed spellings route the same.
int unicodeScalarCount(const QString &query);

// Word characters are letters, numbers, underscore, and private-use
// characters. This is deliberately the same classification SQLite's unicode61
// tokenizer applies (categories L*, N* and Co, plus the '_' declared in
// tokenchars): the verifier is the final authority on what counts as a match,
// so anything it accepts must also be reachable as an FTS candidate. A
// narrower rule here — Qt's isLetterOrNumber() alone — makes the verifier
// accept "x" as a whole word in "𝐀x" while unicode61 indexes that block under
// the single token "𝐀x" and never offers it as a candidate.
//
// Classification is over Unicode scalar values, never UTF-16 code units, so a
// supplementary-plane letter is a letter rather than two unpaired surrogates.
bool isWordScalar(char32_t scalar);

// Classify the character *containing* `index`: when the code unit there is
// half of a surrogate pair, the pair is decoded and classified as one scalar.
bool isWordCharAt(const QString &text, int index);

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
// display text. `wholeWord` applies the word-character boundary rule for
// one/two-character queries; otherwise it is a plain case-insensitive
// substring scan. Offsets and length are display-text coordinates.
QList<int> verifyOccurrences(const QString &displayText, const QString &query,
                             bool wholeWord);

// The bounded, interruptible form of the same scan, and the one the query path
// uses. It appends at most `collectLimit` offsets to `collected` (which may be
// null) and returns the total number of occurrences, so a block holding a
// million matches costs ten stored offsets plus a counter rather than a
// million-element list. `cancel` is polled while scanning; a cancelled scan
// returns -1 and leaves `collected` unusable.
int scanOccurrences(const QString &displayText, const QString &query,
                    bool wholeWord, int collectLimit, QList<int> *collected,
                    const SearchCancel *cancel = nullptr);

} // namespace SearchMatching

// The cancellation contract for query work.
//
// A token is only ever *read* by the engine. Nothing the engine does can clear
// a cancellation, which is what keeps an older, slower request from
// resurrecting itself over a newer one.
class SearchCancel
{
public:
    virtual ~SearchCancel();
    virtual bool cancelled() const = 0;
};

// Generation-based cancellation. Work tagged `generation` is obsolete the
// moment the shared `target` moves past it; the target only ever increases, so
// submitting or abandoning a request cannot un-cancel work already superseded.
class GenerationCancel final : public SearchCancel
{
public:
    GenerationCancel(const std::atomic<quint64> &target, quint64 generation)
        : m_target(target), m_generation(generation)
    {
    }

    bool cancelled() const override { return m_target.load() > m_generation; }

private:
    const std::atomic<quint64> &m_target;
    quint64 m_generation;
};

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
    // Fingerprint of the exact file text this row was built from. Size and
    // modification time alone cannot see an equal-size rewrite that preserved
    // the timestamp, and the app itself produces those (a same-length tag
    // rename restores the mtime deliberately), so freshness is decided on the
    // fingerprint.
    QString contentHash;
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
    // False when the query could not be answered — a prepare, execution, or
    // connection failure. An empty result set with `ok` false means "the index
    // could not tell you", which is not the same answer as "no matches", and
    // the two were indistinguishable while this flag did not exist.
    bool ok = true;
    // True when the scan stopped early because its generation was superseded.
    // The group list is then partial and must be discarded, not displayed.
    bool cancelled = false;
};

class SearchIndexDb
{
public:
    // `role` names what this instance is for ("write", "read", "probe") and
    // appears in the connection name; it is a label, not an identity. The
    // identity is generated here, once, and kept for the object's whole
    // lifetime — Qt SQL connection names are process-global, so two
    // coordinators sharing a fixed name would silently share, and then
    // destroy, one another's registry entries.
    explicit SearchIndexDb(const QString &role = QStringLiteral("db"));
    ~SearchIndexDb();

    SearchIndexDb(const SearchIndexDb &) = delete;
    SearchIndexDb &operator=(const SearchIndexDb &) = delete;

    // Who may destroy and recreate the database file.
    enum class OpenMode {
        // The single writer. A database that cannot be made usable is deleted
        // and rebuilt empty.
        RebuildIfUnusable,
        // Everyone else. An unusable database is left exactly as it is and the
        // open fails, so a reader can never unlink a file the writer is
        // attached to and leave the two connections on different inodes.
        RequireUsable,
    };

    // The FTS5 trigram capability probe: create a temporary
    // trigram FTS table on a throwaway in-memory connection. A packaged build
    // that fails this has a broken SQLite driver.
    static bool probeCapability();

    // Fingerprint of one note's file text, stored alongside the row so a
    // rewrite that preserved size and modification time is still seen.
    static QString contentFingerprint(const QString &fileText);

    // Open (creating if needed) the database at `dbPath` under this instance's
    // connection name, apply connection PRAGMAs, verify integrity and schema,
    // and create the schema when the file is new. Returns false when no usable
    // database is available, leaving isUsable() false.
    bool open(const QString &dbPath,
              OpenMode mode = OpenMode::RebuildIfUnusable);
    void close();
    bool isUsable() const { return m_usable; }
    QString dbPath() const { return m_dbPath; }
    // Stable for the lifetime of this object, whether open or closed.
    QString connectionName() const { return m_connectionName; }

    // --- Content (write connection) -------------------------------------
    // Replace one note's title, block, tag, and FTS rows in a single
    // transaction and increment its index_revision. Returns the
    // new revision through `outRevision` when non-null.
    bool replaceNote(const IndexedNote &note, qint64 *outRevision = nullptr);
    bool removeNote(const QString &relPath);
    // Update only the freshness columns of an existing note, for a file whose
    // content fingerprint still matches but whose size or timestamp moved.
    // Nothing is reparsed and no FTS posting is touched.
    bool touchNote(const QString &relPath, qint64 fileSize, qint64 modifiedMs,
                   const QString &contentHash);

    // True when the note row exists and is unchanged. A non-empty
    // `contentHash` must match the stored fingerprint, which is the only check
    // that survives an equal-size rewrite with a preserved modification time;
    // passing an empty hash compares metadata only and cannot see one.
    bool hasNoteFresh(const QString &relPath, qint64 fileSize,
                      qint64 modifiedMs,
                      const QString &contentHash = QString()) const;
    QStringList allRelPaths() const;
    qint64 revisionOf(const QString &relPath) const;
    int noteRowCount() const;

    // --- Query (read connection) ----------------------------------------
    SearchResults query(const SearchQuery &request,
                        const SearchCancel *cancel = nullptr) const;

    // --- Diagnostics ----------------------------------------------------
    // Structural integrity (PRAGMA integrity_check) plus FTS5's
    // external-content integrity check for both indexes. The SQLite pragma
    // alone says nothing about whether the FTS postings still agree with
    // search_blocks, so a database with stale or tampered postings passes it
    // and then silently loses or invents matches.
    bool integrityOk() const;
    // Every table, index, and FTS definition the queries rely on is present
    // and declared the way this version expects, and user_version matches.
    bool schemaValid() const;

private:
    bool applyPragmas();
    bool ensureSchema();
    bool schemaObjectsPresent() const;
    bool structuralIntegrityOk() const;
    bool ftsIntegrityOk() const;

    QString m_connectionName;
    QString m_dbPath;
    bool m_open = false;
    bool m_usable = false;
};

#endif // SEARCHINDEXDB_H
