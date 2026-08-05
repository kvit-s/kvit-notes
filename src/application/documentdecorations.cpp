// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentdecorations.h"

#include <QGenericArgument>
#include <QMetaObject>

#include <algorithm>

DocumentDecorations::DocumentDecorations(QObject *parent)
    : QObject(parent)
{
}

DocumentDecorations::~DocumentDecorations() = default;

bool DocumentDecorations::isActive() const
{
    return !m_containers.empty() || !m_marginItems.empty();
}

void DocumentDecorations::setMarginColumnReserved(bool reserved)
{
    if (m_marginColumnReserved == reserved)
        return;
    m_marginColumnReserved = reserved;
    emit marginColumnReservedChanged();
    // The column changes what width a row has to lay its text out in, so this
    // is a re-render as much as a registration is.
    bump();
}

QString DocumentDecorations::nextId(const QString &prefix)
{
    return prefix + QString::number(m_nextId++);
}

DocumentDecorations::Entry *DocumentDecorations::find(std::vector<Entry> &entries,
                                                      const QString &id)
{
    const auto hit = std::find_if(entries.begin(), entries.end(),
                                  [&id](const Entry &e) { return e.id == id; });
    return hit == entries.end() ? nullptr : &*hit;
}

void DocumentDecorations::bump()
{
    ++m_revision;
    emit changed();
}

QString DocumentDecorations::addContainer(const QString &owner, int afterBlock,
                                          const QUrl &source, QObject *context)
{
    if (source.isEmpty())
        return QString();
    Entry entry;
    entry.id = nextId(QStringLiteral("container-"));
    entry.owner = owner;
    entry.source = source;
    entry.context = context;
    entry.block = afterBlock;
    m_containers.push_back(entry);
    bump();
    return entry.id;
}

bool DocumentDecorations::setContainerBlock(const QString &id, int afterBlock)
{
    Entry *entry = find(m_containers, id);
    if (!entry)
        return false;
    if (entry->block == afterBlock)
        return true;
    entry->block = afterBlock;
    bump();
    return true;
}

bool DocumentDecorations::removeContainer(const QString &id)
{
    const auto before = m_containers.size();
    std::erase_if(m_containers, [&id](const Entry &e) { return e.id == id; });
    if (m_containers.size() == before)
        return false;
    bump();
    return true;
}

QString DocumentDecorations::addMarginItem(const QString &owner, int block, int line,
                                           const QUrl &source, QObject *context)
{
    if (source.isEmpty())
        return QString();
    Entry entry;
    entry.id = nextId(QStringLiteral("margin-"));
    entry.owner = owner;
    entry.source = source;
    entry.context = context;
    entry.block = block;
    entry.line = std::max(0, line);
    m_marginItems.push_back(entry);
    bump();
    return entry.id;
}

bool DocumentDecorations::setMarginItemPosition(const QString &id, int block, int line)
{
    Entry *entry = find(m_marginItems, id);
    if (!entry)
        return false;
    const int clamped = std::max(0, line);
    if (entry->block == block && entry->line == clamped)
        return true;
    entry->block = block;
    entry->line = clamped;
    bump();
    return true;
}

bool DocumentDecorations::removeMarginItem(const QString &id)
{
    const auto before = m_marginItems.size();
    std::erase_if(m_marginItems, [&id](const Entry &e) { return e.id == id; });
    if (m_marginItems.size() == before)
        return false;
    bump();
    return true;
}

void DocumentDecorations::removeAll(const QString &owner)
{
    const auto owned = [&owner](const Entry &e) { return e.owner == owner; };
    const auto removed = std::erase_if(m_containers, owned)
                         + std::erase_if(m_marginItems, owned);
    if (removed > 0)
        bump();
}

void DocumentDecorations::clear()
{
    if (m_containers.empty() && m_marginItems.empty())
        return;
    m_containers.clear();
    m_marginItems.clear();
    bump();
}

QVariantMap DocumentDecorations::describe(const Entry &entry, bool withLine) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("owner"), entry.owner);
    map.insert(QStringLiteral("source"), entry.source);
    map.insert(QStringLiteral("block"), entry.block);
    // A context object whose module already destroyed it reaches QML as null
    // rather than as a dangling pointer, which is what the QPointer buys.
    map.insert(QStringLiteral("context"),
               QVariant::fromValue(entry.context.data()));
    if (withLine)
        map.insert(QStringLiteral("line"), entry.line);
    return map;
}

QVariantList DocumentDecorations::containersAfter(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_containers) {
        if (entry.block == blockIndex)
            result.append(describe(entry, false));
    }
    return result;
}

QVariantList DocumentDecorations::marginItemsForBlock(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_marginItems) {
        if (entry.block == blockIndex)
            result.append(describe(entry, true));
    }
    return result;
}

void DocumentDecorations::setDocumentView(QObject *view)
{
    m_view = view;
}

// The geometry questions are answered by the document view, because the
// positions only exist once Qt Quick has laid the rows out. The view declares
// one JavaScript function per question and this reaches them through the
// meta-object system, which is how a QML-declared function is callable from
// C++ at all: its parameters and its return value are QVariant.
QRectF DocumentDecorations::askView(const char *method,
                                    const QVariantList &arguments) const
{
    if (!m_view)
        return QRectF();
    QVariant result;
    const QVariant first = arguments.value(0);
    const QVariant second = arguments.value(1);
    const bool called =
        arguments.size() > 1
            ? QMetaObject::invokeMethod(m_view, method, Q_RETURN_ARG(QVariant, result),
                                        Q_ARG(QVariant, first), Q_ARG(QVariant, second))
            : QMetaObject::invokeMethod(m_view, method, Q_RETURN_ARG(QVariant, result),
                                        Q_ARG(QVariant, first));
    if (!called)
        return QRectF();
    return result.toRectF();
}

QRectF DocumentDecorations::blockGeometry(int blockIndex) const
{
    return askView("decorationBlockGeometry", {blockIndex});
}

QRectF DocumentDecorations::containerGeometry(const QString &id) const
{
    return askView("decorationContainerGeometry", {id});
}

QRectF DocumentDecorations::lineGeometry(int blockIndex, int line) const
{
    return askView("decorationLineGeometry", {blockIndex, line});
}
