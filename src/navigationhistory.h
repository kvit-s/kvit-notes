// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NAVIGATIONHISTORY_H
#define NAVIGATIONHISTORY_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>

// Back/forward navigation over notes (pre-launch-plan.md §3.3): a GUI-free
// history of visited notes with their scroll positions, driven by the
// window's openNoteByPath. The stack discipline is the browser one — a new
// visit clears the forward stack; goBack/goForward move the current entry
// between the stacks and return the entry to reopen.
//
// Re-entrancy contract: goBack()/goForward() already make the returned
// entry current, so the visit() the window fires when it opens that note
// sees relPath == current and is a no-op — no suppression flag needed.
class NavigationHistory : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)

public:
    explicit NavigationHistory(QObject *parent = nullptr);

    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }

    // A note became current. Ignores repeats of the current entry;
    // otherwise stamps the departing note's scroll position onto it,
    // pushes it onto the back stack, and clears the forward stack. Empty
    // relPath (no open note) is ignored. The position rides on the CALL
    // that leaves a note — never on re-entry — so a goBack-driven visit
    // (relPath == current) can never clobber the restored position.
    Q_INVOKABLE void visit(const QString &relPath,
                           qreal departingPosition = 0);

    // {ok, relPath, position}; ok=false when the stack is empty. The
    // departing position is stamped onto the entry being left, so forward
    // returns the reader to where they were.
    Q_INVOKABLE QVariantMap goBack(qreal departingPosition = 0);
    Q_INVOKABLE QVariantMap goForward(qreal departingPosition = 0);

    // Collection lifecycle: renames rebind entries, deletions drop them
    // (deduplicating adjacent repeats), root changes clear everything.
    Q_INVOKABLE void renamePath(const QString &oldRelPath,
                                const QString &newRelPath);
    Q_INVOKABLE void dropPath(const QString &relPath);
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    struct Entry {
        QString relPath;
        qreal position = 0;
    };
    static QVariantMap toMap(const Entry &entry, bool ok);
    void trim();

    QList<Entry> m_back;
    QList<Entry> m_forward;
    Entry m_current;          // relPath empty = nothing visited yet
    bool m_hasCurrent = false;
};

#endif // NAVIGATIONHISTORY_H
