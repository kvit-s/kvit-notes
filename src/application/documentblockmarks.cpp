// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentblockmarks.h"

#include <algorithm>

DocumentBlockMarks::DocumentBlockMarks(QObject *parent)
    : QObject(parent)
{
}

DocumentBlockMarks::~DocumentBlockMarks() = default;

DocumentBlockMarks::Entry *DocumentBlockMarks::find(const QString &id)
{
    const auto hit = std::find_if(m_marks.begin(), m_marks.end(),
                                  [&id](const Entry &e) { return e.id == id; });
    return hit == m_marks.end() ? nullptr : &*hit;
}

const DocumentBlockMarks::Entry *DocumentBlockMarks::find(const QString &id) const
{
    const auto hit = std::find_if(m_marks.cbegin(), m_marks.cend(),
                                  [&id](const Entry &e) { return e.id == id; });
    return hit == m_marks.cend() ? nullptr : &*hit;
}

void DocumentBlockMarks::bump()
{
    ++m_revision;
    emit changed();
}

QString DocumentBlockMarks::add(int block, int start, int length,
                                const QColor &wash, const QColor &outline)
{
    // Both halves of "paint these characters" have to be there. A range of no
    // characters, and two colors the platform cannot resolve, come to the same
    // thing: an entry a row would carry and never draw.
    if (length <= 0 || (!wash.isValid() && !outline.isValid()))
        return QString();
    Entry entry;
    entry.id = QStringLiteral("mark-") + QString::number(m_nextId++);
    entry.block = block;
    entry.start = std::max(0, start);
    entry.length = length;
    entry.wash = wash;
    entry.outline = outline;
    m_marks.push_back(entry);
    bump();
    return entry.id;
}

bool DocumentBlockMarks::move(const QString &id, int block, int start,
                              int length)
{
    Entry *entry = find(id);
    if (!entry)
        return false;
    const int clampedStart = std::max(0, start);
    const int clampedLength = std::max(0, length);
    if (entry->block == block && entry->start == clampedStart
        && entry->length == clampedLength) {
        return true;
    }
    entry->block = block;
    entry->start = clampedStart;
    entry->length = clampedLength;
    bump();
    return true;
}

bool DocumentBlockMarks::setColors(const QString &id, const QColor &wash,
                                   const QColor &outline)
{
    Entry *entry = find(id);
    if (!entry)
        return false;
    if (!wash.isValid() && !outline.isValid())
        return false;
    if (entry->wash == wash && entry->outline == outline)
        return true;
    entry->wash = wash;
    entry->outline = outline;
    bump();
    return true;
}

bool DocumentBlockMarks::remove(const QString &id)
{
    const auto before = m_marks.size();
    std::erase_if(m_marks, [&id](const Entry &e) { return e.id == id; });
    if (m_marks.size() == before)
        return false;
    bump();
    return true;
}

void DocumentBlockMarks::clear()
{
    if (m_marks.empty())
        return;
    m_marks.clear();
    bump();
}

QVariantMap DocumentBlockMarks::describe(const Entry &entry)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("block"), entry.block);
    map.insert(QStringLiteral("start"), entry.start);
    map.insert(QStringLiteral("length"), entry.length);
    // One color string per channel, empty where the entry does not use it,
    // because that is what the drawing end asks: it paints a background if it
    // was handed one and a border if it was handed one, and never decodes a
    // flag to find out which.
    map.insert(QStringLiteral("wash"),
               entry.wash.isValid() ? entry.wash.name(QColor::HexArgb)
                                    : QString());
    map.insert(QStringLiteral("outline"),
               entry.outline.isValid() ? entry.outline.name(QColor::HexArgb)
                                       : QString());
    return map;
}

QVariantList DocumentBlockMarks::marksForBlock(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_marks) {
        if (entry.block == blockIndex)
            result.append(describe(entry));
    }
    return result;
}

QVariantMap DocumentBlockMarks::mark(const QString &id) const
{
    const Entry *entry = find(id);
    return entry ? describe(*entry) : QVariantMap();
}
