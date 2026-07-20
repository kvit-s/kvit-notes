// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTSEARCH_H
#define DOCUMENTSEARCH_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class BlockModel;

// All search state: the query, the match options, the document-ordered
// match list, the current match, and the in-selection domain. Exposed as
// the `documentSearch` context property; the find bar binds to it and
// delegates RENDER matches by querying it through the revision-counter
// pattern — nobody else owns search state, mirroring DocumentSelection.
//
// Matches are computed over DISPLAY text: what the user sees,
// markers stripped — BlockEditorEngine::displayText(content), or the
// content itself for verbatim code blocks. Positions are display
// coordinates. Structural chrome (bullets, ordinals, checkboxes, quote
// bars, fence language tags) is block state, not text, and never matches.
//
// Recompute triggers: query/option/active changes recompute synchronously
// (deterministic for the bar and tests); model changes schedule a queued,
// compressed recompute so a burst — a replace-all macro, a multi-block
// paste — recomputes once. With `active` false or an empty query every
// hook is a no-op, so the typing path pays nothing while the bar is
// closed (§21.7 discipline).
class DocumentSearch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool caseSensitive READ caseSensitive WRITE setCaseSensitive NOTIFY optionsChanged)
    Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY optionsChanged)
    Q_PROPERTY(bool useRegex READ useRegex WRITE setUseRegex NOTIFY optionsChanged)
    Q_PROPERTY(bool preserveCase READ preserveCase WRITE setPreserveCase NOTIFY optionsChanged)
    // Restrict matching to the armed domain. Meaningful only
    // while hasDomain; the bar shows the toggle only then.
    Q_PROPERTY(bool inSelectionOnly READ inSelectionOnly WRITE setInSelectionOnly NOTIFY optionsChanged)
    // Bumped on every observable change of matches/current/error/domain;
    // delegate and bar bindings depend on it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY revisionChanged)
    // 1-based position of the current match for the "3 of 15" label;
    // 0 while there are no matches.
    Q_PROPERTY(int currentNumber READ currentNumber NOTIFY revisionChanged)
    // True while the query is an uncompilable regular expression: an
    // error STATE, never a crash.
    Q_PROPERTY(bool patternError READ patternError NOTIFY revisionChanged)
    Q_PROPERTY(bool hasDomain READ hasDomain NOTIFY revisionChanged)

public:
    // One match: a display-coordinate range in one block. captures holds
    // the regex captured texts ([0] = whole match) for capture-group
    // substitution in replacements.
    struct Match {
        int blockIndex = -1;
        int start = 0;
        int length = 0;
        QStringList captures;

        bool operator==(const Match &other) const
        {
            return blockIndex == other.blockIndex && start == other.start
                   && length == other.length;
        }
    };

    // Replacing a display range inside block content:
    // select-and-type semantics through the engine's cut contract.
    struct ReplaceResult {
        QString content;
        int mdEnd = 0; // markdown position just after the replacement
    };

    explicit DocumentSearch(QObject *parent = nullptr);

    void setModel(BlockModel *model);
    BlockModel *model() const { return m_model; }

    bool active() const { return m_active; }
    void setActive(bool active);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool caseSensitive() const { return m_caseSensitive; }
    void setCaseSensitive(bool on);
    bool wholeWord() const { return m_wholeWord; }
    void setWholeWord(bool on);
    bool useRegex() const { return m_useRegex; }
    void setUseRegex(bool on);
    bool preserveCase() const { return m_preserveCase; }
    void setPreserveCase(bool on);
    bool inSelectionOnly() const { return m_inSelectionOnly; }
    void setInSelectionOnly(bool on);

    int revision() const { return m_revision; }
    int matchCount() const { return m_matches.size(); }
    int currentNumber() const { return m_current >= 0 ? m_current + 1 : 0; }
    bool patternError() const { return m_patternError; }
    bool hasDomain() const { return m_domainMode != NoDomain; }

    // Where find starts: the last active cursor, markdown coordinates.
    // Query/option recomputes seed the current match to the first match
    // at/after it, wrapping to the document's first match.
    Q_INVOKABLE void setActiveCursor(int blockIndex, int mdPos);

    // Step with wrap. Both keep the seed on the new current
    // match so later recomputes stay nearby.
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

    // What a block's delegate should render: a list of
    // {"start", "length", "current"} in display coordinates.
    Q_INVOKABLE QVariantList matchesForBlock(int blockIndex) const;
    // {"found", "blockIndex", "start", "length"} for scroll-to-match.
    Q_INVOKABLE QVariantMap currentMatchInfo() const;

    // ---- In-selection domain. The QML layer snapshots the
    // live DocumentSelection into these at arm time; ids keep the domain
    // valid across moves, and a vanished id prunes (block domain) or
    // clears the domain (text domain edge). Text positions are MARKDOWN
    // coordinates, as DocumentSelection reports them. ----
    Q_INVOKABLE void setBlockDomain(const QVariantList &indexes);
    Q_INVOKABLE void setTextDomain(int startIndex, int startPos,
                                   int endIndex, int endPos);
    Q_INVOKABLE void clearDomain();

    // ---- Replace. Each is one undo step, isolated from the typing
    // merge window. ----
    // Replaces the current match and advances to the next remaining one.
    Q_INVOKABLE bool replaceCurrent(const QString &replacement);
    // Replaces every match in the domain; returns how many were replaced.
    Q_INVOKABLE int replaceAll(const QString &replacement);
    // Preview rows for the replace-all panel, from the same snapshot the
    // apply uses: {"blockIndex", "prefix", "matched", "replacement",
    // "suffix"} with prefix/suffix clipped to the match's line.
    Q_INVOKABLE QVariantList previewReplacements(const QString &replacement) const;

    // Synchronous recompute; model signals schedule this compressed
    // through a queued call. Public for tests.
    Q_INVOKABLE void recomputeNow();

    // ---- Pure helpers (unit-tested without a GUI) ----

    // Scan one block's searchable text. Zero-length matches are skipped;
    // matches never overlap. patternError reports an uncompilable regex
    // (matches empty then).
    static QList<Match> scanText(const QString &text, const QString &query,
                                 bool caseSensitive, bool wholeWord,
                                 bool useRegex, bool *patternError = nullptr);
    // $1–$9, $& (whole match), $$ (literal dollar); anything else is
    // literal. captures[0] is the whole match.
    static QString substituteCaptures(const QString &replacement,
                                      const QStringList &captures);
    // Adapt replacement casing to that of the matched text: ALL-UPPER
    // → upper, all-lower → lower, Capitalized → capitalized, mixed or
    // letterless → as typed.
    static QString applyPreserveCase(const QString &replacement,
                                     const QString &matched);
    // Select-and-type replacement of a display range: cutRangeResult
    // (no reveals) + insert at the resulting markdown cursor.
    // Verbatim (code) content splices directly — display IS markdown
    // there.
    static ReplaceResult replaceRange(const QString &content, bool verbatim,
                                      int displayStart, int displayEnd,
                                      const QString &replacement);

signals:
    void activeChanged();
    void queryChanged();
    void optionsChanged();
    void revisionChanged();

private:
    enum DomainMode { NoDomain, BlockDomain, TextDomain };

    QString idAt(int index) const;
    int indexOfId(const QString &id) const;
    // displayText(content), or content itself for code blocks.
    QString searchableText(int blockIndex) const;
    bool isVerbatimBlock(int blockIndex) const;
    // Markdown position -> display position within a block, and back.
    int displayPosition(int blockIndex, int mdPos) const;
    int markdownPosition(int blockIndex, int displayPos) const;
    bool matchInDomain(const Match &match) const;
    // The replacement text one match gets (captures + preserve case).
    QString finalReplacement(const QString &replacement, const Match &match) const;
    void scheduleRecompute();
    // Recompute matches, re-seed the current match, bump the revision
    // if anything observable changed.
    void recompute();
    void setCurrent(int flatIndex);
    void bumpRevision();
    QVariantList observableState() const;
    // One undo step per replace, isolated from the typing merge window.
    void applyContentUpdates(const QString &macroName,
                             const QList<QPair<int, QString>> &updates);

    BlockModel *m_model = nullptr;
    int m_revision = 0;

    bool m_active = false;
    QString m_query;
    bool m_caseSensitive = false;
    bool m_wholeWord = false;
    bool m_useRegex = false;
    bool m_preserveCase = false;
    bool m_inSelectionOnly = false;

    QList<Match> m_matches; // document order: (blockIndex, start)
    QHash<int, QList<int>> m_byBlock; // blockIndex -> flat match indexes
    int m_current = -1;
    bool m_patternError = false;

    // Seed: block id + display position.
    QString m_seedId;
    int m_seedDisplayPos = 0;

    DomainMode m_domainMode = NoDomain;
    QSet<QString> m_domainIds;
    QString m_domainStartId;
    QString m_domainEndId;
    int m_domainStartMd = 0;
    int m_domainEndMd = 0;

    bool m_recomputeQueued = false;
};

#endif // DOCUMENTSEARCH_H
