// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "quickswitchermodel.h"

#include <QDateTime>
#include <algorithm>

#include "fuzzymatch.h"
#include "notecollection.h"

QuickSwitcherModel::QuickSwitcherModel(QObject *parent)
    : QObject(parent)
{
}

void QuickSwitcherModel::setCollection(NoteCollection *collection)
{
    if (m_collection == collection)
        return;
    m_collection = collection;
    emit collectionChanged();
}

QVariantList QuickSwitcherModel::itemsFor(const QString &query, int limit) const
{
    QVariantList rows;
    if (!m_collection)
        return rows;

    struct Ranked {
        int tier;
        qint64 modifiedMs;
        const NoteCollection::NoteEntry *entry;
    };

    const QString lowered = query.trimmed().toLower();
    QList<Ranked> ranked;
    const QStringList paths = m_collection->noteRelPaths();
    ranked.reserve(paths.size());
    for (const QString &relPath : paths) {
        const NoteCollection::NoteEntry *entry = m_collection->note(relPath);
        if (!entry)
            continue;
        int tier = FuzzyMatch::PrefixMatch;
        if (!lowered.isEmpty()) {
            tier = FuzzyMatch::tierFor(lowered,
                                       {entry->title, entry->relPath});
            if (tier == FuzzyMatch::NoMatch)
                continue;
        }
        ranked.append({tier, entry->modified.toMSecsSinceEpoch(), entry});
    }

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked &a, const Ranked &b) {
        if (a.tier != b.tier)
            return a.tier < b.tier;
        return a.modifiedMs > b.modifiedMs; // recent first within a tier
    });

    for (const Ranked &r : ranked) {
        rows.append(QVariantMap{
            {QStringLiteral("title"), r.entry->title},
            {QStringLiteral("relPath"), r.entry->relPath},
            {QStringLiteral("folder"), r.entry->folder},
        });
        if (limit > 0 && rows.size() >= limit)
            break;
    }
    return rows;
}
