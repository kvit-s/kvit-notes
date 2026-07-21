// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NAVIGATIONHISTORY_H
#define NAVIGATIONHISTORY_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>

#include <optional>

// Back/forward navigation over notes: a GUI-free
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
    //
    // The move happens here, before the caller has opened the note, and that
    // open can still fail — the departing note would not save, the target was
    // deleted or is unreadable. So a successful move leaves a rollback token
    // behind: rollbackNavigation() puts both stacks and the current entry back
    // exactly as they were, and the reader's Back button still points where
    // the document on screen says it should. A visit() (which only happens
    // once a note has actually opened) confirms the move instead.
    Q_INVOKABLE QVariantMap goBack(qreal departingPosition = 0);
    Q_INVOKABLE QVariantMap goForward(qreal departingPosition = 0);

    // Confirm/undo the last Back or Forward move. Both are no-ops when there
    // is nothing outstanding, so a caller may commit defensively.
    Q_INVOKABLE void commitNavigation();
    Q_INVOKABLE void rollbackNavigation();
    // True while a Back/Forward move is still waiting for its open to report.
    Q_INVOKABLE bool navigationPending() const { return m_pending.has_value(); }

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
    // Everything a Back/Forward move changes, so undoing one is a restore
    // rather than a replay in the opposite direction (which would have to
    // reason about trimming and about the position stamped on departure).
    struct Snapshot {
        QList<Entry> back;
        QList<Entry> forward;
        Entry current;
        bool hasCurrent = false;
    };
    Snapshot snapshot() const;

    static QVariantMap toMap(const Entry &entry, bool ok);
    void trim();
    // Removes repeats that sit next to each other in either stack, or next
    // to the current entry, keeping the older of each pair.
    void collapseAdjacentDuplicates();

    QList<Entry> m_back;
    QList<Entry> m_forward;
    Entry m_current;          // relPath empty = nothing visited yet
    bool m_hasCurrent = false;
    // Set while a Back/Forward move is unconfirmed; holds the state to
    // restore if the note never opened.
    std::optional<Snapshot> m_pending;
};

#endif // NAVIGATIONHISTORY_H
