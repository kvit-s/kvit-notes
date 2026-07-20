// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef COLLECTIONSEARCH_H
#define COLLECTIONSEARCH_H

#include <QDate>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "searchindexdb.h"

class NoteCollection;
class CollectionSearchIndex;

// Global search across the collection. The disk-backed SQLite FTS5
// index is a candidate generator; Qt refinement makes counts, snippets, and
// click navigation exact. This object is the QML-facing state: it owns query
// generations and the immutable result snapshot, runs queries off the GUI
// thread through CollectionSearchIndex, and keeps only the latest generation.
//
// One- and two-character queries use whole-word semantics; three or more
// characters use literal-substring semantics. Filters — a recursive folder
// scope, a tag, and a modified-date preset or custom range — compose within
// the query.
class CollectionSearch : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY inputChanged)
    Q_PROPERTY(QString folderScope READ folderScope WRITE setFolderScope
                   NOTIFY inputChanged)
    Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter
                   NOTIFY inputChanged)
    Q_PROPERTY(QString datePreset READ datePreset WRITE setDatePreset
                   NOTIFY inputChanged)
    Q_PROPERTY(QDate customFrom READ customFrom WRITE setCustomFrom
                   NOTIFY inputChanged)
    Q_PROPERTY(QDate customTo READ customTo WRITE setCustomTo
                   NOTIFY inputChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int noteCount READ noteCount NOTIFY revisionChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY revisionChanged)
    // False while the index is still building or the shown snapshot predates the
    // completed index: the view labels results as possibly incomplete.
    Q_PROPERTY(bool complete READ complete NOTIFY revisionChanged)
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged)

public:
    explicit CollectionSearch(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);
    void setSearchIndex(CollectionSearchIndex *index);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString folderScope() const { return m_folderScope; }
    void setFolderScope(const QString &folderScope);
    QString tagFilter() const { return m_tagFilter; }
    void setTagFilter(const QString &tagFilter);
    QString datePreset() const { return m_datePreset; }
    void setDatePreset(const QString &datePreset);
    QDate customFrom() const { return m_customFrom; }
    void setCustomFrom(const QDate &date);
    QDate customTo() const { return m_customTo; }
    void setCustomTo(const QDate &date);

    int revision() const { return m_revision; }
    int noteCount() const { return m_groups.size(); }
    int matchCount() const { return m_matchCount; }
    bool complete() const { return m_complete; }
    bool indexing() const;

    // One entry per matching note, in relPath order:
    //   { relPath, title, titleMatched, matchCount, moreMatches,
    //     matches: [ { blockIndex, start, length,
    //                  snippet, snippetStart, snippetLength } ] }
    Q_INVOKABLE QVariantList results() const { return m_groups; }

    // Run the current query immediately, bypassing the debounce — the
    // Enter-key path.
    Q_INVOKABLE void submitNow();

    // Markdown position of a display-text match start inside a note's block —
    // what DocumentSearch's cursor seeding takes. Reads and parses only the
    // requested note file, then maps display to Markdown coordinates through the
    // engine's static mapping.
    Q_INVOKABLE int markdownPosition(const QString &relPath, int blockIndex,
                                     int displayStart) const;

signals:
    void inputChanged();
    void revisionChanged();
    void indexingChanged();

private slots:
    void scheduleQuery();
    void onQueryFinished(quint64 generation, SearchResults results);

private:
    void submit();
    void publishEmpty();
    void applyResults(const SearchResults &results, bool complete);

    NoteCollection *m_collection = nullptr;
    CollectionSearchIndex *m_index = nullptr;
    QString m_query;
    QString m_folderScope;
    QString m_tagFilter;
    QString m_datePreset = QStringLiteral("any");
    QDate m_customFrom;
    QDate m_customTo;
    int m_revision = 0;
    int m_matchCount = 0;
    bool m_complete = true;
    QVariantList m_groups;

    QTimer m_debounce;
    // The generation of the input currently on screen. Advanced by every
    // input change and by every submission, so a reply is displayable only
    // while it still answers the current input.
    quint64 m_generation = 0;
};

#endif // COLLECTIONSEARCH_H
