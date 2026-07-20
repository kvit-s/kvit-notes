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

QVariantMap NavigationHistory::goBack(qreal departingPosition)
{
    if (m_back.isEmpty())
        return toMap(Entry(), false);
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
    emit changed();
}

void NavigationHistory::dropPath(const QString &relPath)
{
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
