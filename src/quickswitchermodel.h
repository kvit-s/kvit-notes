// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QUICKSWITCHERMODEL_H
#define QUICKSWITCHERMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>

class NoteCollection;

// The quick switcher's filter (pre-launch-plan.md §3.3): a GUI-free object
// ranking the collection's notes against a typed query with the shared
// fuzzy matcher (fuzzymatch.h), so the popup QML owns no matching logic.
// Rows are QVariantMaps ready to render: {title, relPath, folder}.
class QuickSwitcherModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(NoteCollection *collection READ collection WRITE setCollection
                   NOTIFY collectionChanged)

public:
    explicit QuickSwitcherModel(QObject *parent = nullptr);

    NoteCollection *collection() const { return m_collection; }
    void setCollection(NoteCollection *collection);

    // Empty query: every note, most recently modified first (the "switch
    // back" gesture). Non-empty: ranked by tier over title then relPath,
    // ties broken by recency. Capped at `limit` rows (0 = no cap).
    Q_INVOKABLE QVariantList itemsFor(const QString &query,
                                      int limit = 50) const;

signals:
    void collectionChanged();

private:
    NoteCollection *m_collection = nullptr;
};

#endif // QUICKSWITCHERMODEL_H
