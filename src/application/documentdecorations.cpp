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
    return !m_containers.empty() || !m_marginItems.empty() || !m_spans.empty();
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

QString DocumentDecorations::addSpan(const QString &owner, int block, int start,
                                     int length, SpanStyles style,
                                     const QColor &color)
{
    // Neither half of "paint these characters" can be left out: a style with
    // no channel and a color the platform cannot resolve both come to the
    // same thing, which is an entry the view would carry and never draw.
    if (style == NoStyle || !color.isValid() || length <= 0)
        return QString();
    Entry entry;
    entry.id = nextId(QStringLiteral("span-"));
    entry.owner = owner;
    entry.block = block;
    entry.start = std::max(0, start);
    entry.length = length;
    entry.style = style;
    entry.color = color;
    m_spans.push_back(entry);
    bump();
    return entry.id;
}

bool DocumentDecorations::setSpanRange(const QString &id, int block, int start,
                                       int length)
{
    Entry *entry = find(m_spans, id);
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

bool DocumentDecorations::setSpanStyle(const QString &id, SpanStyles style,
                                       const QColor &color)
{
    Entry *entry = find(m_spans, id);
    if (!entry)
        return false;
    if (style == NoStyle || !color.isValid())
        return false;
    if (entry->style == style && entry->color == color)
        return true;
    entry->style = style;
    entry->color = color;
    bump();
    return true;
}

bool DocumentDecorations::removeSpan(const QString &id)
{
    const auto before = m_spans.size();
    std::erase_if(m_spans, [&id](const Entry &e) { return e.id == id; });
    if (m_spans.size() == before)
        return false;
    bump();
    return true;
}

void DocumentDecorations::removeAll(const QString &owner)
{
    const auto owned = [&owner](const Entry &e) { return e.owner == owner; };
    const auto removed = std::erase_if(m_containers, owned)
                         + std::erase_if(m_marginItems, owned)
                         + std::erase_if(m_spans, owned);
    if (removed > 0)
        bump();
}

void DocumentDecorations::clear()
{
    if (m_containers.empty() && m_marginItems.empty() && m_spans.empty())
        return;
    m_containers.clear();
    m_marginItems.clear();
    m_spans.clear();
    bump();
}

QVariantMap DocumentDecorations::describe(const Entry &entry, Kind kind) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("owner"), entry.owner);
    map.insert(QStringLiteral("block"), entry.block);
    if (kind == Kind::Span) {
        map.insert(QStringLiteral("start"), entry.start);
        map.insert(QStringLiteral("length"), entry.length);
        // One color string per channel, empty where the entry does not use
        // the channel, because that is what the drawing end asks: it paints
        // a background if it was handed one and a border if it was handed
        // one, and never has to decode a style flag.
        const QString value = entry.color.name(QColor::HexArgb);
        map.insert(QStringLiteral("wash"),
                   entry.style.testFlag(Wash) ? value : QString());
        map.insert(QStringLiteral("outline"),
                   entry.style.testFlag(Outline) ? value : QString());
        return map;
    }
    map.insert(QStringLiteral("source"), entry.source);
    // A context object whose module already destroyed it reaches QML as null
    // rather than as a dangling pointer, which is what the QPointer buys.
    map.insert(QStringLiteral("context"),
               QVariant::fromValue(entry.context.data()));
    if (kind == Kind::MarginItem)
        map.insert(QStringLiteral("line"), entry.line);
    return map;
}

QVariantList DocumentDecorations::containersAfter(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_containers) {
        if (entry.block == blockIndex)
            result.append(describe(entry, Kind::Container));
    }
    return result;
}

QVariantList DocumentDecorations::marginItemsForBlock(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_marginItems) {
        if (entry.block == blockIndex)
            result.append(describe(entry, Kind::MarginItem));
    }
    return result;
}

QVariantList DocumentDecorations::spansForBlock(int blockIndex) const
{
    QVariantList result;
    for (const Entry &entry : m_spans) {
        if (entry.block == blockIndex)
            result.append(describe(entry, Kind::Span));
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

// The same forwarding for a question whose answer is a list of rectangles.
QVariantList DocumentDecorations::askViewList(const char *method,
                                              const QVariantList &arguments) const
{
    if (!m_view)
        return QVariantList();
    QVariant result;
    if (!QMetaObject::invokeMethod(m_view, method, Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, arguments.value(0)))) {
        return QVariantList();
    }
    return result.toList();
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

QVariantList DocumentDecorations::spanRects(const QString &id) const
{
    return askViewList("decorationSpanRects", {id});
}
