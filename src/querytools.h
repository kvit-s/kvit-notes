// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QUERYTOOLS_H
#define QUERYTOOLS_H

#include <QObject>
#include <QList>
#include <QMetaObject>
#include <QString>
#include <QVariantMap>

class NoteCollection;

// QML seam for the collection query block,
// following the TableTools/KanbanTools pattern: pure QueryData stays
// independently testable; this object is glue only.
class QueryTools : public QObject
{
    Q_OBJECT

public:
    explicit QueryTools(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);

    // Parse + evaluate the fence body against the collection. Returns
    //   { ok, error, view, columns,
    //     rows:   [{relPath, cells: [...]}, ...],
    //     groups: [{name, cards: [{relPath, cells}]}, ...] }
    // ok=false carries the parse error the block shows in read mode.
    Q_INVOKABLE QVariantMap run(const QString &body);

    // Deterministic cache seams used by the C++/QML release-gate tests.
    Q_INVOKABLE int evaluationCount() const { return m_evaluationCount; }
    Q_INVOKABLE int cacheSize() const { return m_cache.size(); }
    Q_INVOKABLE void clearCache();

private:
    struct CacheEntry {
        quint64 generation = 0;
        int revision = 0;
        QString body;
        QVariantMap result;
    };

    NoteCollection *m_collection = nullptr;
    QMetaObject::Connection m_rootConnection;
    QList<CacheEntry> m_cache; // MRU first; deliberately bounded and tiny
    quint64 m_collectionGeneration = 0;
    int m_evaluationCount = 0;
    static constexpr int MaxCacheEntries = 64;
};

#endif // QUERYTOOLS_H
