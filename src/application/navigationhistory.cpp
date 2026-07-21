// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "navigationhistory.h"

namespace {
// Browser-like bound; beyond this the oldest entries fall off.
constexpr int kMaxEntries = 100;
}

NavigationHistory::NavigationHistory(QObject *parent)
    : QObject(parent)
{
}

void NavigationHistory::visit(const QString &relPath, qreal departingPosition)
{
    if (relPath.isEmpty())
        return;
    // A visit only happens once a note has actually opened, which is exactly
    // the confirmation an outstanding Back/Forward move was waiting for —
    // including the re-entrant visit of the entry that move restored.
    commitNavigation();
    if (m_hasCurrent && m_current.relPath == relPath)
        return;
    if (m_hasCurrent) {
        m_current.position = departingPosition;
        m_back.append(m_current);
        trim();
    }
    m_current = {relPath, 0};
    m_hasCurrent = true;
    m_forward.clear();
    emit changed();
}

QVariantMap NavigationHistory::toMap(const Entry &entry, bool ok)
{
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("relPath"), entry.relPath},
        {QStringLiteral("position"), entry.position},
    };
}

NavigationHistory::Snapshot NavigationHistory::snapshot() const
{
    return Snapshot{m_back, m_forward, m_current, m_hasCurrent};
}

void NavigationHistory::commitNavigation()
{
    m_pending.reset();
}

void NavigationHistory::rollbackNavigation()
{
    if (!m_pending)
        return;
    const Snapshot restored = *m_pending;
    m_pending.reset();
    m_back = restored.back;
    m_forward = restored.forward;
    m_current = restored.current;
    m_hasCurrent = restored.hasCurrent;
    emit changed();
}

QVariantMap NavigationHistory::goBack(qreal departingPosition)
{
    if (m_back.isEmpty())
        return toMap(Entry(), false);
    m_pending = snapshot();
    if (m_hasCurrent) {
        m_current.position = departingPosition;
        m_forward.append(m_current);
    }
    m_current = m_back.takeLast();
    m_hasCurrent = true;
    emit changed();
    return toMap(m_current, true);
}

QVariantMap NavigationHistory::goForward(qreal departingPosition)
{
    if (m_forward.isEmpty())
        return toMap(Entry(), false);
    m_pending = snapshot();
    if (m_hasCurrent) {
        m_back.append(m_current);
        m_back.last().position = departingPosition;
    }
    m_current = m_forward.takeLast();
    m_hasCurrent = true;
    emit changed();
    return toMap(m_current, true);
}

void NavigationHistory::renamePath(const QString &oldRelPath,
                                   const QString &newRelPath)
{
    if (oldRelPath.isEmpty() || newRelPath.isEmpty())
        return;
    // The snapshot behind an unconfirmed move names paths this rename is
    // rewriting, so restoring it would reintroduce the old name. The
    // collection has spoken; the move is no longer reversible.
    commitNavigation();
    for (Entry &entry : m_back) {
        if (entry.relPath == oldRelPath)
            entry.relPath = newRelPath;
    }
    for (Entry &entry : m_forward) {
        if (entry.relPath == oldRelPath)
            entry.relPath = newRelPath;
    }
    if (m_hasCurrent && m_current.relPath == oldRelPath)
        m_current.relPath = newRelPath;
    // Renaming one note onto a path already in the history can leave two
    // identical entries side by side, and Back onto the note already on
    // screen looks like the button is broken. Deletion collapses those the
    // same way.
    collapseAdjacentDuplicates();
    emit changed();
}

// Adjacent repeats carry no navigation value: moving between two entries
// naming the same note is a no-op the reader reads as a broken control. The
// SURVIVING entry is the older one, whose stored scroll position is where
// the reader actually was.
void NavigationHistory::collapseAdjacentDuplicates()
{
    auto dedupe = [](QList<Entry> *stack) {
        for (int i = stack->size() - 1; i > 0; --i) {
            if (stack->at(i).relPath == stack->at(i - 1).relPath)
                stack->removeAt(i);
        }
    };
    dedupe(&m_back);
    dedupe(&m_forward);
    if (!m_hasCurrent)
        return;
    // The stack tops sit next to the current entry, so they can duplicate it
    // too.
    while (!m_back.isEmpty() && m_back.last().relPath == m_current.relPath)
        m_back.removeLast();
    while (!m_forward.isEmpty() && m_forward.last().relPath == m_current.relPath)
        m_forward.removeLast();
}

void NavigationHistory::dropPath(const QString &relPath)
{
    // As with renamePath: the pre-move snapshot may still contain the note
    // that has just been deleted, so it must not be restorable.
    commitNavigation();
    auto scrub = [&relPath](QList<Entry> *stack) {
        for (int i = stack->size() - 1; i >= 0; --i) {
            if (stack->at(i).relPath == relPath)
                stack->removeAt(i);
        }
        // Deduplicate adjacent repeats the removal may have created, so
        // back never appears to do nothing.
        for (int i = stack->size() - 1; i > 0; --i) {
            if (stack->at(i).relPath == stack->at(i - 1).relPath)
                stack->removeAt(i);
        }
    };
    scrub(&m_back);
    scrub(&m_forward);
    if (m_hasCurrent && m_current.relPath == relPath) {
        m_hasCurrent = false;
        m_current = Entry();
    }
    if (m_hasCurrent) {
        while (!m_back.isEmpty()
               && m_back.last().relPath == m_current.relPath)
            m_back.removeLast();
        while (!m_forward.isEmpty()
               && m_forward.last().relPath == m_current.relPath)
            m_forward.removeLast();
    }
    emit changed();
}

void NavigationHistory::clear()
{
    commitNavigation();
    m_back.clear();
    m_forward.clear();
    m_current = Entry();
    m_hasCurrent = false;
    emit changed();
}

void NavigationHistory::trim()
{
    while (m_back.size() > kMaxEntries)
        m_back.removeFirst();
}
