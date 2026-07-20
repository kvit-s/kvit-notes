// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentoutline.h"
#include "blockmodel.h"
#include "block.h"
#include "perflog.h"

#include <QTimer>

namespace {

// A heading block's level (1..4), or 0 if the block is not a heading. The four
// heading types are Heading1..Heading3 (enum 1..3) and Heading4 (enum 10,
// appended after the wave-1 types), so the mapping is explicit.
int headingLevel(Block::BlockType type)
{
    switch (type) {
    case Block::Heading1: return 1;
    case Block::Heading2: return 2;
    case Block::Heading3: return 3;
    case Block::Heading4: return 4;
    default: return 0;
    }
}

} // namespace

DocumentOutline::DocumentOutline(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DocumentOutline::setModel(BlockModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        // Any structural or content change may add, remove, renumber, or
        // re-slug a heading; a compressed queued rebuild coalesces a burst.
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles) {
                    if (roles.isEmpty()
                        || roles.contains(BlockModel::BlockTypeRole)) {
                        scheduleRebuild();
                        return;
                    }
                    if (!roles.contains(BlockModel::ContentRole) || !m_model)
                        return;
                    for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
                        Block *block = m_model->blockAt(row);
                        if (block && headingLevel(block->blockType()) > 0) {
                            scheduleRebuild();
                            return;
                        }
                    }
                });
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &DocumentOutline::scheduleRebuild);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &DocumentOutline::scheduleRebuild);
        connect(m_model, &QAbstractItemModel::rowsMoved,
                this, &DocumentOutline::scheduleRebuild);
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &DocumentOutline::scheduleRebuild);
    }
    rebuild();
}

// ---- the shared slug function ----

QString DocumentOutline::baseSlug(const QString &text)
{
    // Slug from the heading's DISPLAY text (markers already stripped by the
    // caller): lowercase; letters and digits (Unicode included) kept; spaces,
    // underscores and hyphens become one hyphen; every other character
    // dropped; runs of hyphens collapsed; leading/trailing hyphens trimmed.
    QString out;
    out.reserve(text.size());
    bool pendingHyphen = false;
    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber()) {
            if (pendingHyphen && !out.isEmpty())
                out.append(QLatin1Char('-'));
            pendingHyphen = false;
            out.append(ch.toLower());
        } else if (ch.isSpace() || ch == QLatin1Char('_')
                   || ch == QLatin1Char('-')) {
            pendingHyphen = true;
        }
        // anything else: dropped, without introducing a hyphen
    }
    return out;
}

// ---- rebuild ----

void DocumentOutline::scheduleRebuild()
{
    if (m_rebuildQueued)
        return;
    m_rebuildQueued = true;
    QMetaObject::invokeMethod(this, "rebuildNow", Qt::QueuedConnection);
}

void DocumentOutline::rebuildNow()
{
    m_rebuildQueued = false;
    rebuild();
}

void DocumentOutline::rebuild()
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("outline.rebuild"),
        QVariantMap{{QStringLiteral("blocks"), m_model ? m_model->count() : 0}});

    QList<Node> newNodes;
    QHash<QString, int> newSlugToNode;

    if (m_model) {
        QHash<QString, int> slugCounts;  // base slug -> times seen
        QList<int> ancestorStack;        // node indexes, increasing level

        for (int i = 0; i < m_model->count(); ++i) {
            Block *block = m_model->blockAt(i);
            if (!block)
                continue;
            const int level = headingLevel(block->blockType());
            if (level == 0)
                continue;

            Node node;
            node.level = level;
            node.blockId = block->blockId();
            node.blockIndex = i;
            node.text = block->displayText();

            const QString base = baseSlug(node.text);
            const int seen = slugCounts.value(base, 0);
            slugCounts.insert(base, seen + 1);
            // First occurrence keeps the bare slug; later ones get -1, -2, …
            // in document order (the GitHub-anchor rule).
            node.slug = seen == 0 ? base
                                  : base + QLatin1Char('-') + QString::number(seen);
            // An empty heading (or one whose text is all punctuation) has an
            // empty base; give it a stable positional slug so it is still a
            // valid, unique link target.
            if (node.slug.isEmpty())
                node.slug = QStringLiteral("section-") + QString::number(i);

            // Parent = nearest heading of a shallower level still open.
            while (!ancestorStack.isEmpty()
                   && newNodes.at(ancestorStack.last()).level >= level)
                ancestorStack.removeLast();
            node.parent = ancestorStack.isEmpty() ? -1 : ancestorStack.last();

            const int idx = newNodes.size();
            newNodes.append(node);
            newSlugToNode.insert(node.slug, idx);
            ancestorStack.append(idx);
        }

        // hasChildren: the next heading in document order is deeper.
        for (int i = 0; i < newNodes.size(); ++i)
            newNodes[i].hasChildren =
                i + 1 < newNodes.size()
                && newNodes.at(i + 1).level > newNodes.at(i).level;
    }

    auto sameNodes = [](const QList<Node> &a, const QList<Node> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i) {
            const Node &x = a.at(i);
            const Node &y = b.at(i);
            if (x.level != y.level || x.text != y.text
                || x.blockId != y.blockId || x.blockIndex != y.blockIndex
                || x.slug != y.slug || x.parent != y.parent
                || x.hasChildren != y.hasChildren) {
                return false;
            }
        }
        return true;
    };

    if (sameNodes(m_nodes, newNodes)) {
        perf.addContext(QStringLiteral("headings"), m_nodes.size());
        perf.addContext(QStringLiteral("skipped"), true);
        return;
    }

    beginResetModel();
    m_nodes = newNodes;
    m_slugToNode = newSlugToNode;

    // Drop collapse state for headings that no longer exist, so the set does
    // not grow without bound.
    if (!m_collapsedIds.isEmpty()) {
        QSet<QString> live;
        for (const Node &n : m_nodes)
            if (m_collapsedIds.contains(n.blockId))
                live.insert(n.blockId);
        m_collapsedIds = live;
    }

    recomputeVisibleRows();
    endResetModel();

    ++m_revision;
    emit revisionChanged();

    ++m_slugsRevision;
    emit slugsChanged();

    // Re-derive the current section against the new tree.
    updateCurrentRow();
    perf.addContext(QStringLiteral("headings"), m_nodes.size());
}

bool DocumentOutline::isShown(const Node &node) const
{
    return (m_levelMask & (1 << (node.level - 1))) != 0;
}

void DocumentOutline::recomputeVisibleRows()
{
    m_visibleRows.clear();
    for (int i = 0; i < m_nodes.size(); ++i) {
        Node &n = m_nodes[i];
        // Visible depth = shown ancestors on the parent chain; hidden if any
        // ancestor heading is collapsed.
        int depth = 0;
        bool hiddenByCollapse = false;
        for (int p = n.parent; p != -1; p = m_nodes.at(p).parent) {
            const Node &anc = m_nodes.at(p);
            if (m_collapsedIds.contains(anc.blockId))
                hiddenByCollapse = true;
            if (isShown(anc))
                ++depth;
        }
        n.depth = depth;
        if (isShown(n) && !hiddenByCollapse)
            m_visibleRows.append(i);
    }
}

// ---- model interface ----

int DocumentOutline::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

QVariant DocumentOutline::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_visibleRows.size())
        return QVariant();
    const Node &n = m_nodes.at(m_visibleRows.at(row));
    switch (role) {
    case LevelRole: return n.level;
    case TextRole: return n.text.isEmpty() ? QStringLiteral("(untitled)") : n.text;
    case BlockIndexRole: return n.blockIndex;
    case BlockIdRole: return n.blockId;
    case SlugRole: return n.slug;
    case DepthRole: return n.depth;
    case CollapsedRole: return m_collapsedIds.contains(n.blockId);
    case HasChildrenRole: return n.hasChildren;
    case IsCurrentRole: return row == m_currentRow;
    }
    return QVariant();
}

QHash<int, QByteArray> DocumentOutline::roleNames() const
{
    return {
        {LevelRole, "level"},
        {TextRole, "text"},
        {BlockIndexRole, "blockIndex"},
        {BlockIdRole, "blockId"},
        {SlugRole, "slug"},
        {DepthRole, "depth"},
        {CollapsedRole, "collapsed"},
        {HasChildrenRole, "hasChildren"},
        {IsCurrentRole, "isCurrent"},
    };
}

// ---- level filter ----

void DocumentOutline::setLevelMask(int mask)
{
    mask &= 0xF;
    if (mask == m_levelMask)
        return;
    m_levelMask = mask;
    emit levelMaskChanged();
    beginResetModel();
    recomputeVisibleRows();
    endResetModel();
    ++m_revision;
    emit revisionChanged();
    updateCurrentRow();
}

// ---- resolution (shared by outline / TOC / internal links) ----

int DocumentOutline::blockIndexForSlug(const QString &slug) const
{
    const int node = m_slugToNode.value(slug, -1);
    return node < 0 ? -1 : m_nodes.at(node).blockIndex;
}

bool DocumentOutline::hasSlug(const QString &slug) const
{
    return m_slugToNode.contains(slug);
}

QString DocumentOutline::slugForBlockIndex(int blockIndex) const
{
    for (const Node &n : m_nodes)
        if (n.blockIndex == blockIndex)
            return n.slug;
    return QString();
}

int DocumentOutline::blockIndexAt(int row) const
{
    if (row < 0 || row >= m_visibleRows.size())
        return -1;
    return m_nodes.at(m_visibleRows.at(row)).blockIndex;
}

// The section containing a block = the last heading node at or before it. Then
// climb to the nearest node that is an actual visible row (its own row, the
// row of a collapsing ancestor, or the nearest shown ancestor), and return
// that row. -1 before the first heading.
int DocumentOutline::rowForBlock(int blockIndex) const
{
    int section = -1;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).blockIndex <= blockIndex)
            section = i;
        else
            break;
    }
    if (section < 0)
        return -1;
    for (int node = section; node != -1; node = m_nodes.at(node).parent) {
        const int row = m_visibleRows.indexOf(node);
        if (row >= 0)
            return row;
    }
    return -1;
}

// ---- collapse ----

void DocumentOutline::toggleCollapsed(int row)
{
    if (row < 0 || row >= m_visibleRows.size())
        return;
    const Node &n = m_nodes.at(m_visibleRows.at(row));
    if (m_collapsedIds.contains(n.blockId))
        m_collapsedIds.remove(n.blockId);
    else
        m_collapsedIds.insert(n.blockId);
    beginResetModel();
    recomputeVisibleRows();
    endResetModel();
    ++m_revision;
    emit revisionChanged();
    updateCurrentRow();
}

// ---- current section ----

void DocumentOutline::setCurrentBlock(int blockIndex)
{
    if (blockIndex == m_currentBlockIndex)
        return;
    m_currentBlockIndex = blockIndex;
    updateCurrentRow();
}

void DocumentOutline::updateCurrentRow()
{
    const int row = rowForBlock(m_currentBlockIndex);
    if (row == m_currentRow)
        return;
    const int old = m_currentRow;
    m_currentRow = row;
    // Re-mark just the two affected rows.
    if (old >= 0 && old < m_visibleRows.size())
        emit dataChanged(index(old), index(old), {IsCurrentRole});
    if (row >= 0 && row < m_visibleRows.size())
        emit dataChanged(index(row), index(row), {IsCurrentRole});
    emit currentRowChanged();
}

// ---- Ctrl+K heading list + TOC body ----

QVariantList DocumentOutline::headings() const
{
    QVariantList out;
    for (const Node &n : m_nodes) {
        out.append(QVariantMap{
            {QStringLiteral("level"), n.level},
            {QStringLiteral("text"), n.text},
            {QStringLiteral("slug"), n.slug},
            {QStringLiteral("blockIndex"), n.blockIndex},
        });
    }
    return out;
}

QString DocumentOutline::tocMarkdown() const
{
    if (m_nodes.isEmpty())
        return QString();
    int minLevel = 4;
    for (const Node &n : m_nodes)
        minLevel = qMin(minLevel, n.level);
    QStringList lines;
    for (const Node &n : m_nodes) {
        const QString indent(2 * (n.level - minLevel), QLatin1Char(' '));
        // The text is display text; brackets in it would break the link, so
        // escape them (rare, but keeps the generated markdown valid).
        QString label = n.text;
        label.replace(QLatin1Char('['), QStringLiteral("\\["));
        label.replace(QLatin1Char(']'), QStringLiteral("\\]"));
        if (label.isEmpty())
            label = QStringLiteral("(untitled)");
        lines.append(indent + QStringLiteral("- [") + label
                     + QStringLiteral("](#") + n.slug + QStringLiteral(")"));
    }
    return lines.join(QLatin1Char('\n'));
}
