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

    // The user's notes first, then each realm as a section of its own.
    //
    // Files an application manages (reservedsubtrees.h) are findable — that
    // is why they are indexed — but they are not the person's notes, and a
    // vault with a few hundred of them would otherwise bury the note being
    // looked for. Ranking each group separately and drawing the realms after
    // the notes keeps the switcher's first answer a note, whatever else is in
    // the vault.
    const auto rank = [this, &lowered](const QStringList &paths) {
        QList<Ranked> ranked;
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
        return ranked;
    };

    const auto appendRows = [&rows, limit](const QList<Ranked> &ranked) {
        for (const Ranked &r : ranked) {
            if (limit > 0 && rows.size() >= limit)
                return false;
            rows.append(QVariantMap{
                {QStringLiteral("title"), r.entry->title},
                {QStringLiteral("relPath"), r.entry->relPath},
                {QStringLiteral("folder"), r.entry->folder},
                // "" for one of the user's notes, which is what the popup
                // draws its section headings from.
                {QStringLiteral("realm"), r.entry->realm},
            });
        }
        return true;
    };

    if (!appendRows(rank(m_collection->noteRelPaths())))
        return rows;

    const QVariantList realms = m_collection->realmListing();
    for (const QVariant &realm : realms) {
        const QString label = realm.toMap().value(QStringLiteral("label")).toString();
        if (!appendRows(rank(m_collection->realmNoteRelPaths(label))))
            break;
    }
    return rows;
}
