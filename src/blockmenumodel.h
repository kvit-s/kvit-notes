// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKMENUMODEL_H
#define BLOCKMENUMODEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QList>

#include "block.h"

// The block-type menu catalog and its fuzzy filter: one GUI-free
// object holding every implemented block type with its name,
// description, group, icon glyph, and search aliases. The slash menu
// and the gutter plus-button both render whatever itemsFor() returns;
// the QML layer owns no matching logic.
//
// Matching is case-insensitive subsequence over the name and aliases
// ("h1" matches "Heading 1" via its alias, "cd" matches "Code Block" as
// a subsequence — features.md §4.3). Ranking is tiered: a whole-string
// prefix beats a word prefix beats a bare subsequence, with catalog
// order breaking ties.
class BlockMenuModel : public QObject
{
    Q_OBJECT

public:
    // §3.7 "recently used block types shown first": how many lead the
    // empty-query menu. Persisted through the settings store
    // (recentTypes/setRecentTypes below).
    static constexpr int MaxRecent = 3;

    explicit BlockMenuModel(QObject *parent = nullptr);

    // Display rows for the menu, ready to render:
    //   header row: { kind: "header", text: <group name> }
    //   entry row:  { kind: "entry", entryId, name, description, icon, type }
    // Empty (or whitespace) query: the full catalog grouped under
    // headers, preceded by a "Recently used" group when noteUsed() has
    // recorded any. Non-empty query: a flat ranked entry list, best
    // match first, no headers.
    Q_INVOKABLE QVariantList itemsFor(const QString &query) const;

    // Record the exact entry the user chose. Several catalog entries share
    // one block type — five are CodeBlock (Code Block, Task Board, Table of
    // Contents, Mermaid Diagram, Collection Query) — so the type alone
    // cannot name the choice, and recording it by type made every one of
    // them come back as plain Code Block. This is what the menu calls.
    Q_INVOKABLE void noteUsedEntry(const QString &entryId);

    // Record by block type: the catalog's plain entry for that type, i.e.
    // the one carrying no default language. Convenience for callers holding
    // only a type; the menu itself uses noteUsedEntry().
    Q_INVOKABLE void noteUsed(int type);

    // The recency list for the settings store: entry ids, most recent
    // first. setRecentTypes() coerces, drops ids the catalog does not hold,
    // dedups, and caps at MaxRecent, so a stale or hand-edited settings
    // value cannot corrupt the menu. Plain block-type numbers written by
    // earlier versions still load, resolving to that type's plain entry.
    Q_INVOKABLE QVariantList recentTypes() const;
    Q_INVOKABLE void setRecentTypes(const QVariantList &types);

signals:
    // The recency list changed through noteUsed() (deliberately not
    // through setRecentTypes(): loading persisted state must not
    // immediately re-save it).
    void recentChanged();

private:
    struct Entry {
        Block::BlockType type;
        QString name;
        QString description;
        QString group;
        QString icon;
        QStringList aliases;
        // A block-type qualifier seeded on insert: a callout's type or a
        // toggle marker. Reuses the `language` field, so it rides the
        // existing convertBlock(language) path. Empty otherwise.
        QString defaultLanguage;
    };

    // Match quality; smaller is better. NoMatch excludes the entry.
    enum MatchTier { PrefixMatch = 0, WordPrefixMatch, SubsequenceMatch, NoMatch };

    static bool isSubsequence(const QString &needle, const QString &haystack);
    MatchTier matchTier(const Entry &entry, const QString &loweredQuery) const;
    QVariantMap entryRow(const Entry &entry) const;
    QVariantMap headerRow(const QString &text) const;

    // An entry's stable identity: its block type and default language. That
    // pair is unique across the catalog and is exactly what inserting the
    // entry uses, so it survives display-name changes and catalog
    // reordering, both of which a persisted index or name would not.
    static QString entryId(const Entry &entry);
    const Entry *entryForId(const QString &id) const;

    QList<Entry> m_catalog;
    QStringList m_recent;  // entry ids, most recent first
};

#endif // BLOCKMENUMODEL_H
