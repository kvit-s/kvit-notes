// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockmenumodel.h"

#include <QVariantMap>

#include <algorithm>

#include "blockkinddef.h"
#include "blockkindregistry.h"
#include "blockkinds.h"
#include "codelanguages.h"
#include "fuzzymatch.h"

namespace {
// Human labels for the canonical language ids, so the /code menu reads
// "Code: Python" rather than "code: python".
QString languageDisplayName(const QString &id)
{
    static const QHash<QString, QString> names = {
        {"python", "Python"}, {"javascript", "JavaScript"}, {"cpp", "C++"},
        {"java", "Java"}, {"html", "HTML"}, {"css", "CSS"}, {"sql", "SQL"},
        {"bash", "Bash"}, {"json", "JSON"}, {"xml", "XML"},
        {"markdown", "Markdown"},
    };
    return names.value(id, id);
}
} // namespace

BlockMenuModel::BlockMenuModel(QObject *parent)
    : QObject(parent)
{
    rebuildCatalog();
}

void BlockMenuModel::setBlockKindRegistry(const BlockKindRegistry *registry)
{
    if (m_blockKinds == registry)
        return;
    m_blockKinds = registry;
    rebuildCatalog();
}

void BlockMenuModel::rebuildCatalog()
{
    // Every row comes from the kind that contributes it, so a kind reaches
    // the menu by existing. This was a twenty-three entry literal here, kept
    // in step with the twelve other places that decide something per kind by
    // hand and by nothing else.
    //
    // A kind may contribute more than one row and that has to stay true: a
    // paragraph gives "Text" and "Drop Cap", a callout gives "Callout" and
    // "Toggle", and five separate rows all insert a code block under a
    // different fence language.
    m_catalog.clear();
    const QList<const BlockKindDef *> kinds =
        m_blockKinds ? m_blockKinds->all() : BlockKindDefs::builtins();
    for (const BlockKindDef *kind : kinds)
        m_catalog += kind->menuEntries();

    // Grouped, in the order the groups are shown. The menu emits a heading
    // whenever a row's group differs from the row before it, so rows of one
    // group have to be adjacent or the heading appears twice; a stable sort
    // keeps each group's rows in the order the kinds gave them.
    //
    // A group a module invents sorts after the four the core defines, in the
    // order it first appears, rather than being dropped or interleaved.
    static const QStringList kGroupOrder = {
        QStringLiteral("Basic"), QStringLiteral("Lists"),
        QStringLiteral("Advanced"), QStringLiteral("Media"),
    };
    QStringList groupOrder = kGroupOrder;
    for (const Entry &entry : std::as_const(m_catalog)) {
        if (!groupOrder.contains(entry.group))
            groupOrder.append(entry.group);
    }
    std::stable_sort(m_catalog.begin(), m_catalog.end(),
                     [&groupOrder](const Entry &a, const Entry &b) {
                         const int ga = groupOrder.indexOf(a.group);
                         const int gb = groupOrder.indexOf(b.group);
                         if (ga != gb)
                             return ga < gb;
                         return a.order < b.order;
                     });
}

bool BlockMenuModel::isSubsequence(const QString &needle, const QString &haystack)
{
    return FuzzyMatch::isSubsequence(needle, haystack);
}

BlockMenuModel::MatchTier BlockMenuModel::matchTier(const Entry &entry,
                                                    const QString &loweredQuery) const
{
    QStringList candidates = entry.aliases;
    candidates.prepend(entry.name);
    // The shared matcher (fuzzymatch.h) so the
    // block menu, quick switcher, and [[ completion rank identically; the
    // tier enums map one to one.
    return MatchTier(FuzzyMatch::tierFor(loweredQuery, candidates));
}

const BlockMenuModel::Entry *BlockMenuModel::entryForId(const QString &id) const
{
    for (const Entry &entry : m_catalog) {
        if (entry.entryId() == id)
            return &entry;
    }
    return nullptr;
}

QVariantMap BlockMenuModel::entryRow(const Entry &entry) const
{
    QVariantMap row{
        { QStringLiteral("kind"), QStringLiteral("entry") },
        { QStringLiteral("entryId"), entry.entryId() },
        { QStringLiteral("name"), entry.name },
        { QStringLiteral("description"), entry.description },
        { QStringLiteral("icon"), entry.icon },
        { QStringLiteral("type"), static_cast<int>(entry.type) },
    };
    if (!entry.defaultLanguage.isEmpty())
        row.insert(QStringLiteral("language"), entry.defaultLanguage);
    // The starter content the block is created with, when its kind has one.
    // The table of contents is the exception and carries none: its starter
    // body is this document's headings, which only the open document knows,
    // so the menu fills that one in.
    if (!entry.seed.isEmpty())
        row.insert(QStringLiteral("seed"), entry.seed);
    return row;
}

QVariantMap BlockMenuModel::headerRow(const QString &text) const
{
    return {
        { QStringLiteral("kind"), QStringLiteral("header") },
        { QStringLiteral("text"), text },
    };
}

QVariantList BlockMenuModel::itemsFor(const QString &query) const
{
    const QString lowered = query.trimmed().toLower();
    QVariantList rows;

    if (lowered.isEmpty()) {
        // Recently used first (§3.7), then the catalog under its group
        // headers in canonical order.
        if (!m_recent.isEmpty()) {
            rows.append(headerRow(QStringLiteral("Recently used")));
            for (const QString &id : m_recent) {
                if (const Entry *entry = entryForId(id))
                    rows.append(entryRow(*entry));
            }
        }
        QString currentGroup;
        for (const Entry &entry : m_catalog) {
            if (entry.group != currentGroup) {
                currentGroup = entry.group;
                rows.append(headerRow(currentGroup));
            }
            rows.append(entryRow(entry));
        }
        return rows;
    }

    // "/code <language>": the query after the "code " prefix matches
    // language names and aliases, and selecting one inserts a code block
    // already tagged with that language. Only the plain query "code" (no
    // remainder) falls through to the ordinary Code Block entry.
    if (lowered.startsWith(QStringLiteral("code")) && lowered.contains(QLatin1Char(' '))) {
        const QString remainder =
            lowered.mid(lowered.indexOf(QLatin1Char(' ')) + 1).trimmed();
        if (!remainder.isEmpty()) {
            QVariantList langRows;
            // An exact alias (e.g. "py", "c++") leads.
            const QString exact = CodeLanguages::canonicalLanguage(remainder);
            const auto codeRow = [](const QString &id) {
                return QVariantMap{
                    { QStringLiteral("kind"), QStringLiteral("entry") },
                    { QStringLiteral("name"),
                      QStringLiteral("Code: ") + languageDisplayName(id) },
                    { QStringLiteral("description"),
                      languageDisplayName(id) + QStringLiteral(" syntax highlighting") },
                    { QStringLiteral("icon"), QStringLiteral("<>") },
                    { QStringLiteral("type"), static_cast<int>(Block::CodeBlock) },
                    { QStringLiteral("language"), id },
                };
            };
            if (!exact.isEmpty())
                langRows.append(codeRow(exact));
            for (const QString &id : CodeLanguages::supportedLanguages()) {
                if (id == exact)
                    continue;
                if (id.startsWith(remainder) || isSubsequence(remainder, id))
                    langRows.append(codeRow(id));
            }
            if (!langRows.isEmpty())
                return langRows;
        }
    }

    // Filtering: a flat ranked list. Stable sort keeps catalog order
    // within a tier, so "Heading 1" precedes "Heading 2" for query "h".
    QList<QPair<MatchTier, const Entry *>> matches;
    for (const Entry &entry : m_catalog) {
        const MatchTier tier = matchTier(entry, lowered);
        if (tier != NoMatch)
            matches.append({ tier, &entry });
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &match : matches)
        rows.append(entryRow(*match.second));
    return rows;
}

void BlockMenuModel::noteUsedEntry(const QString &entryId)
{
    if (!entryForId(entryId))
        return;  // not a catalog entry; nothing to remember
    m_recent.removeAll(entryId);
    m_recent.prepend(entryId);
    while (m_recent.size() > MaxRecent)
        m_recent.removeLast();
    emit recentChanged();
}

void BlockMenuModel::noteUsed(int type)
{
    // The plain entry for a type is the one with no default language; a
    // type whose every entry is specialized falls back to the first.
    const QString plain = QString::number(type) + QLatin1Char(':');
    if (entryForId(plain)) {
        noteUsedEntry(plain);
        return;
    }
    for (const Entry &entry : m_catalog) {
        if (static_cast<int>(entry.type) == type) {
            noteUsedEntry(entry.entryId());
            return;
        }
    }
}

QVariantList BlockMenuModel::recentTypes() const
{
    QVariantList list;
    for (const QString &id : m_recent)
        list.append(id);
    return list;
}

void BlockMenuModel::setRecentTypes(const QVariantList &types)
{
    m_recent.clear();
    for (const QVariant &value : types) {
        QString id;
        // Entry ids are strings. Settings written by earlier versions hold
        // plain block-type numbers (JSON delivers them as doubles), which
        // resolve to that type's plain entry so recency survives the
        // upgrade instead of being silently dropped.
        if (value.typeId() == QMetaType::QString) {
            id = value.toString();
        } else {
            bool ok = false;
            const int type = value.toInt(&ok);
            if (!ok)
                continue;
            id = QString::number(type) + QLatin1Char(':');
        }
        if (id.isEmpty() || m_recent.contains(id))
            continue;
        if (!entryForId(id))
            continue;  // stale entry (or a hand-edited value)
        m_recent.append(id);
        if (m_recent.size() == MaxRecent)
            break;
    }
}
