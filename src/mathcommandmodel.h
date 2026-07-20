// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MATHCOMMANDMODEL_H
#define MATHCOMMANDMODEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QList>

// The math-command catalog and its matcher (tex-editing.md): one GUI-free
// object behind the backslash command menu on both math-editing surfaces,
// mirroring BlockMenuModel's role for the slash menu. Browse mode renders
// categories() + itemsForCategory() — a curated catalog organized the way
// LyX's math toolbar is, each entry carrying an insertion template and a
// preview TeX string the menu renders through image://math. Completion
// mode renders itemsFor(query) — the curated entries merged with every
// command MicroTeX can render (MathRenderer::availableCommands), so the
// NewTX additions complete without the curated table chasing them.
//
// Ranking (tex-editing.md "Two modes, one popup"): case-exact matches
// before case-insensitive ones (TeX is case-sensitive — \omega and \Omega
// both surface for "ome", exact case first); within equal case quality,
// name prefix, then substring, then subsequence; curated entries before
// raw enumerated names; alphabetical order breaks ties.
class MathCommandModel : public QObject
{
    Q_OBJECT

public:
    // Recently-used commands lead the browse panel, persisted through the
    // settings store like the block menu's recent types (setting key
    // math.recentCommands).
    static constexpr int MaxRecent = 12;

    explicit MathCommandModel(QObject *parent = nullptr);

    // Browse-mode category names in canonical order, led by "Recently
    // used" once noteUsed() has recorded any command.
    Q_INVOKABLE QStringList categories() const;

    // Entry rows of one category, catalog order. Each row:
    //   { kind: "entry", name, description, category, insert,
    //     insertDisplay, cursorOffset, cursorOffsetDisplay, preview,
    //     standalone, curated }
    // `insert` is the template text ("\frac{}{}"); `insertDisplay` a
    // multi-line variant for display-math blocks (empty = use insert);
    // the cursor offsets locate the caret inside the inserted text
    // (-1 = at its end); `preview` is the TeX the menu renders as the
    // entry's glyph (empty = show the name as text). `standalone` marks
    // templates that render as a self-contained expression once their
    // slots are filled (false for fragments like "&" or "^{}").
    Q_INVOKABLE QVariantList itemsForCategory(const QString &category) const;

    // Completion mode: a flat ranked entry list for a query (the text the
    // user typed after the backslash — no leading backslash). Empty query
    // returns the full corpus, curated first.
    Q_INVOKABLE QVariantList itemsFor(const QString &query) const;

    // Record an accepted command (by its display name, e.g. "\frac") for
    // the recently-used group.
    Q_INVOKABLE void noteUsed(const QString &name);

    // Recency persistence, mirroring BlockMenuModel::recentTypes():
    // display names, most recent first. setRecentCommands() coerces,
    // dedups, and caps without emitting recentChanged.
    Q_INVOKABLE QVariantList recentCommands() const;
    Q_INVOKABLE void setRecentCommands(const QVariantList &names);

signals:
    // Recency changed through noteUsed() (not through setRecentCommands():
    // loading persisted state must not immediately re-save it).
    void recentChanged();

private:
    struct Entry {
        QString name;           // display name as typed: "\frac", "^{}"
        QString description;
        QString category;
        QString insert;         // inserted template (single-line form)
        QString insertDisplay;  // multi-line form for display blocks ("")
        QString preview;        // TeX for the menu glyph ("" = name text)
        QStringList aliases;    // matcher-only, never displayed
        QString command;        // bare engine command, for enumeration dedup
        bool standalone = true; // filled insert renders as an expression
    };

    // Match quality; smaller is better. The pair orders case quality
    // above tier per the ranking contract.
    enum MatchTier { PrefixMatch = 0, SubstringMatch, SubsequenceMatch, NoMatch };
    struct MatchQuality {
        int caseQuality = 1;    // 0 exact-case, 1 case-insensitive
        MatchTier tier = NoMatch;
    };

    void addSymbol(const QString &category, const QString &command,
                   const QString &description = QString(),
                   const QStringList &aliases = {});
    void addSymbols(const QString &category, const QStringList &commands);
    void addTemplate(const QString &category, const QString &name,
                     const QString &insert, const QString &preview,
                     const QString &description,
                     const QStringList &aliases = {},
                     const QString &insertDisplay = QString(),
                     bool standalone = true);

    static bool isSubsequence(const QString &needle, const QString &haystack);
    static MatchTier tierOf(const QString &candidate, const QString &query,
                            Qt::CaseSensitivity cs);
    static MatchQuality bestQuality(const QStringList &candidates,
                                    const QString &query);
    // Caret position inside a freshly inserted template: inside the first
    // empty {} / [] pair, else at the first alignment '&', else after the
    // opening half of a \left…\right pair, else -1 (end).
    static int caretOffsetFor(const QString &insert);

    QVariantMap entryRow(const Entry &entry) const;
    QVariantMap enumeratedRow(const QString &command) const;
    QStringList matchCandidates(const Entry &entry) const;

    QList<Entry> m_catalog;
    QStringList m_categories;         // canonical order, no "Recently used"
    QSet<QString> m_curatedCommands;  // bare commands, for enumeration dedup
    QStringList m_recent;             // display names, most recent first
};

#endif // MATHCOMMANDMODEL_H
