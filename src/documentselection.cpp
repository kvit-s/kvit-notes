// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentselection.h"
#include "blockmodel.h"
#include "block.h"
#include "blockeditorengine.h"
#include "documentserializer.h"
#include "perflog.h"

#include <QAbstractItemModel>

DocumentSelection::DocumentSelection(QObject *parent)
    : QObject(parent)
{
}

void DocumentSelection::setModel(BlockModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        // Removals invalidate ids (an undo re-inserts a NEW block with a
        // new id, so stale ids simply never match again); moves and
        // inserts leave ids valid — selection follows moved blocks.
        // The about-to-remove hook captures the removed range's ids so
        // pruning never rescans the whole model.
        connect(m_model, &QAbstractItemModel::rowsAboutToBeRemoved,
                this, &DocumentSelection::captureRemovedIds);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &DocumentSelection::pruneRemovedBlocks);
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &DocumentSelection::clear);
    }
    m_pendingRemovedIds.clear();
    clear();
}

bool DocumentSelection::hasBlockSelection() const
{
    // Pruning keeps m_baseIds valid, so membership alone answers this
    // without materializing the effective set.
    if (!m_baseIds.isEmpty())
        return true;
    return m_rangeActive && indexOfId(m_anchorId) >= 0 && indexOfId(m_headId) >= 0;
}

bool DocumentSelection::hasTextSelection() const
{
    Endpoint start, end;
    if (!orderedEndpoints(start, end))
        return false;
    // A collapsed range (same block, same position) is no selection.
    return start.index != end.index || start.pos != end.pos;
}

// ---- helpers ----

QString DocumentSelection::idAt(int index) const
{
    if (!m_model)
        return QString();
    Block *block = m_model->blockAt(index);
    return block ? block->blockId() : QString();
}

int DocumentSelection::indexOfId(const QString &id) const
{
    if (!m_model || id.isEmpty())
        return -1;
    return m_model->indexOfBlockId(id);
}

QString DocumentSelection::contentAt(int index) const
{
    if (!m_model)
        return QString();
    Block *block = m_model->blockAt(index);
    return block ? block->content() : QString();
}

QSet<QString> DocumentSelection::effectiveIds() const
{
    QSet<QString> ids = m_baseIds;
    if (m_rangeActive) {
        const int a = indexOfId(m_anchorId);
        const int h = indexOfId(m_headId);
        if (a >= 0 && h >= 0) {
            for (int i = qMin(a, h); i <= qMax(a, h); ++i)
                ids.insert(idAt(i));
        }
    }
    return ids;
}

// Every mutator funnels through here so the revision bumps exactly when
// the observable state changed. The snapshot compares the raw members:
// the model order is fixed inside one mutation, so the derived
// effective set differs exactly when the members do, at O(selection)
// rather than O(document) cost.
DocumentSelection::Snapshot DocumentSelection::snapshot() const
{
    Snapshot s;
    s.baseIds = m_baseIds;
    s.anchorId = m_anchorId;
    s.headId = m_headId;
    s.rangeActive = m_rangeActive;
    s.lastActiveId = m_lastActiveId;
    s.textAnchorId = m_textAnchorId;
    s.textHeadId = m_textHeadId;
    s.textAnchorRaw = m_textAnchorRaw;
    s.textAnchorStart = m_textAnchorStart;
    s.textAnchorEnd = m_textAnchorEnd;
    s.textHeadRaw = m_textHeadRaw;
    s.granularity = static_cast<int>(m_granularity);
    return s;
}

void DocumentSelection::bumpIfChanged(const std::function<void()> &mutate)
{
    const Snapshot before = snapshot();
    mutate();
    if (!(snapshot() == before)) {
        ++m_revision;
        emit revisionChanged();
    }
}

// ---- block selection ----

void DocumentSelection::selectBlock(int index)
{
    const QString id = idAt(index);
    if (id.isEmpty())
        return;
    bumpIfChanged([&] {
        m_textAnchorId.clear();
        m_textHeadId.clear();
        m_baseIds.clear();
        m_anchorId = id;
        m_headId = id;
        m_rangeActive = true;
        m_lastActiveId = id;
    });
}

void DocumentSelection::toggleBlock(int index)
{
    const QString id = idAt(index);
    if (id.isEmpty())
        return;
    bumpIfChanged([&] {
        m_textAnchorId.clear();
        m_textHeadId.clear();
        // Commit the active range, then flip the block's membership.
        // The toggled block becomes the anchor for a future extend
        // (rangeActive stays false so an OFF toggle stays off).
        m_baseIds = effectiveIds();
        m_rangeActive = false;
        if (m_baseIds.contains(id))
            m_baseIds.remove(id);
        else
            m_baseIds.insert(id);
        m_anchorId = id;
        m_headId = id;
        m_lastActiveId = id;
    });
}

void DocumentSelection::extendBlockSelectionTo(int index)
{
    const QString id = idAt(index);
    if (id.isEmpty())
        return;
    bumpIfChanged([&] {
        m_textAnchorId.clear();
        m_textHeadId.clear();
        if (m_anchorId.isEmpty() || indexOfId(m_anchorId) < 0)
            m_anchorId = id;
        m_headId = id;
        m_rangeActive = true;
        m_lastActiveId = id;
    });
}

void DocumentSelection::extendBlockSelection(int delta)
{
    if (!m_model || delta == 0)
        return;
    const int from = m_rangeActive ? indexOfId(m_headId) : indexOfId(m_anchorId);
    if (from < 0)
        return;
    const int target = qBound(0, from + delta, m_model->count() - 1);
    extendBlockSelectionTo(target);
}

void DocumentSelection::selectAllBlocks()
{
    if (!m_model || m_model->count() == 0)
        return;
    bumpIfChanged([&] {
        m_textAnchorId.clear();
        m_textHeadId.clear();
        m_baseIds.clear();
        m_anchorId = idAt(0);
        m_headId = idAt(m_model->count() - 1);
        m_rangeActive = true;
        m_lastActiveId = m_headId;
    });
}

void DocumentSelection::collapseBlockSelection(int direction)
{
    if (!m_model)
        return;
    const QVariantList indexes = selectedIndexes();
    if (indexes.isEmpty())
        return;
    const int edge = direction < 0 ? indexes.first().toInt()
                                   : indexes.last().toInt();
    const int target = qBound(0, edge + (direction < 0 ? -1 : 1),
                              m_model->count() - 1);
    selectBlock(target);
}

bool DocumentSelection::isBlockSelected(int index) const
{
    const QString id = idAt(index);
    if (id.isEmpty())
        return false;
    if (m_baseIds.contains(id))
        return true;
    if (!m_rangeActive)
        return false;
    // Contiguous range: an index comparison answers membership without
    // materializing the effective set.
    const int a = indexOfId(m_anchorId);
    const int h = indexOfId(m_headId);
    return a >= 0 && h >= 0 && index >= qMin(a, h) && index <= qMax(a, h);
}

QVariantList DocumentSelection::selectedIndexes() const
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("selection.recompute"),
        QVariantMap{{QStringLiteral("blocks"), m_model ? m_model->count() : 0}},
        PerfLog::Verbose);
    QVariantList result;
    if (!m_model)
        return result;
    // Resolve each selected id through the O(1) map and sort, instead
    // of testing every document row for membership.
    QList<int> selected;
    int rangeLo = -1;
    int rangeHi = -2;
    if (m_rangeActive) {
        const int a = indexOfId(m_anchorId);
        const int h = indexOfId(m_headId);
        if (a >= 0 && h >= 0) {
            rangeLo = qMin(a, h);
            rangeHi = qMax(a, h);
            for (int i = rangeLo; i <= rangeHi; ++i)
                selected.append(i);
        }
    }
    for (const QString &id : m_baseIds) {
        const int idx = indexOfId(id);
        if (idx >= 0 && (idx < rangeLo || idx > rangeHi))
            selected.append(idx);
    }
    std::sort(selected.begin(), selected.end());
    for (int idx : selected)
        result.append(idx);
    perf.addContext(QStringLiteral("selected"), result.size());
    return result;
}

int DocumentSelection::lastActiveIndex() const
{
    if (!hasBlockSelection())
        return -1;
    const int idx = indexOfId(m_lastActiveId);
    if (idx >= 0 && isBlockSelected(idx))
        return idx;
    const QVariantList indexes = selectedIndexes();
    return indexes.isEmpty() ? -1 : indexes.last().toInt();
}

// ---- text selection ----

void DocumentSelection::beginTextSelection(int blockIndex, int mdPos, int granularity)
{
    const QString id = idAt(blockIndex);
    if (id.isEmpty())
        return;
    bumpIfChanged([&] {
        m_baseIds.clear();
        m_anchorId.clear();
        m_headId.clear();
        m_rangeActive = false;

        const QString text = contentAt(blockIndex);
        const int pos = qBound(0, mdPos, static_cast<int>(text.length()));
        m_granularity = static_cast<Granularity>(granularity);
        m_textAnchorId = id;
        m_textHeadId = id;
        m_textAnchorRaw = pos;
        m_textHeadRaw = pos;
        switch (m_granularity) {
        case CharacterGranularity:
            m_textAnchorStart = pos;
            m_textAnchorEnd = pos;
            break;
        case WordGranularity:
            m_textAnchorStart = wordStart(text, pos);
            m_textAnchorEnd = wordEnd(text, pos);
            break;
        case BlockGranularity:
            m_textAnchorStart = 0;
            m_textAnchorEnd = text.length();
            break;
        }
    });
}

void DocumentSelection::updateTextSelectionHead(int blockIndex, int mdPos)
{
    if (m_textAnchorId.isEmpty())
        return;
    const QString id = idAt(blockIndex);
    if (id.isEmpty())
        return;
    bumpIfChanged([&] {
        const QString text = contentAt(blockIndex);
        m_textHeadId = id;
        m_textHeadRaw = qBound(0, mdPos, static_cast<int>(text.length()));
    });
}

// Endpoints ordered document-forward, with the head snapped per the
// granularity and direction: extending forward snaps the head outward to
// a word/block END, backward to a word/block START; the anchor extent
// was snapped when the selection began.
bool DocumentSelection::orderedEndpoints(Endpoint &start, Endpoint &end) const
{
    const int aIdx = indexOfId(m_textAnchorId);
    const int hIdx = indexOfId(m_textHeadId);
    if (aIdx < 0 || hIdx < 0)
        return false;

    const bool forward = hIdx > aIdx || (hIdx == aIdx && m_textHeadRaw >= m_textAnchorRaw);
    const QString headText = contentAt(hIdx);
    int headSnapped = m_textHeadRaw;
    switch (m_granularity) {
    case CharacterGranularity:
        break;
    case WordGranularity:
        headSnapped = forward ? wordEnd(headText, m_textHeadRaw)
                              : wordStart(headText, m_textHeadRaw);
        break;
    case BlockGranularity:
        headSnapped = forward ? headText.length() : 0;
        break;
    }

    if (forward) {
        start = {aIdx, m_textAnchorStart};
        end = {hIdx, headSnapped};
        // Same-block word/block selection can invert when the head sits
        // inside the anchor extent; keep the extent.
        if (aIdx == hIdx && end.pos < m_textAnchorEnd)
            end.pos = m_textAnchorEnd;
    } else {
        start = {hIdx, headSnapped};
        end = {aIdx, m_textAnchorEnd};
        if (aIdx == hIdx && start.pos > m_textAnchorStart)
            start.pos = m_textAnchorStart;
    }
    return true;
}

QVariantMap DocumentSelection::portionForBlock(int index) const
{
    QVariantMap map;
    map[QStringLiteral("selected")] = false;
    map[QStringLiteral("full")] = false;
    map[QStringLiteral("start")] = 0;
    map[QStringLiteral("end")] = 0;

    Endpoint start, end;
    if (!orderedEndpoints(start, end))
        return map;
    if (start.index == end.index && start.pos == end.pos)
        return map;
    if (index < start.index || index > end.index)
        return map;

    const int len = contentAt(index).length();
    const int from = index == start.index ? qBound(0, start.pos, len) : 0;
    const int to = index == end.index ? qBound(0, end.pos, len) : len;
    // A zero-width portion on an edge block (a drag that stops exactly at
    // a block boundary) selects nothing there; zero-length CONTENT (a
    // divider inside the range) is a full selection of that block.
    if (from >= to && len > 0)
        return map;
    map[QStringLiteral("selected")] = true;
    map[QStringLiteral("full")] = from == 0 && to == len;
    map[QStringLiteral("start")] = from;
    map[QStringLiteral("end")] = to;
    return map;
}

QVariantMap DocumentSelection::orderedTextRange() const
{
    QVariantMap map;
    Endpoint start, end;
    if (!orderedEndpoints(start, end))
        return map;
    map[QStringLiteral("startIndex")] = start.index;
    map[QStringLiteral("startPos")] = start.pos;
    map[QStringLiteral("endIndex")] = end.index;
    map[QStringLiteral("endPos")] = end.pos;
    return map;
}

int DocumentSelection::textAnchorIndex() const
{
    return indexOfId(m_textAnchorId);
}

int DocumentSelection::textAnchorPosition() const
{
    return m_textAnchorRaw;
}

QString DocumentSelection::rangeMarkdown() const
{
    Endpoint start, end;
    if (!m_model || !orderedEndpoints(start, end))
        return QString();
    if (start.index == end.index && start.pos == end.pos)
        return QString();

    DocumentSerializer serializer;
    QString result;
    bool haveParts = false;
    bool prevWasListLine = false;
    for (int i = start.index; i <= end.index; ++i) {
        Block *block = m_model->blockAt(i);
        if (!block)
            continue;
        const QString md = block->content();
        const int from = i == start.index ? qBound(0, start.pos, static_cast<int>(md.length())) : 0;
        const int to = i == end.index ? qBound(0, end.pos, static_cast<int>(md.length())) : md.length();
        if (from >= to && md.length() > 0)
            continue; // zero-width edge portion

        const bool fullBlock = from == 0 && to == md.length();
        QString text;
        if (fullBlock) {
            text = serializer.serializeBlock(block, m_model->ordinalAt(i));
        } else {
            // A partial edge block contributes a self-contained inline
            // fragment (no structural prefix — it is partial text).
            // Positions map through the display state; the fragment is
            // reveal-independent because it maps back to markdown.
            const int docStart = BlockEditorEngine::markdownToDocument(md, QList<int>(), from);
            const int docEnd = BlockEditorEngine::markdownToDocument(md, QList<int>(), to);
            text = BlockEditorEngine::markdownForRange(md, QList<int>(), docStart, docEnd);
        }

        const bool listLine = fullBlock && Block::isListFamily(block->blockType());
        if (haveParts)
            result.append(prevWasListLine && listLine ? QStringLiteral("\n")
                                                      : QStringLiteral("\n\n"));
        result.append(text);
        haveParts = true;
        prevWasListLine = listLine;
    }
    return result;
}

int DocumentSelection::textHeadIndex() const
{
    return indexOfId(m_textHeadId);
}

int DocumentSelection::textHeadPosition() const
{
    return m_textHeadRaw;
}

// ---- clearing / pruning ----

void DocumentSelection::clear()
{
    bumpIfChanged([&] {
        m_baseIds.clear();
        m_anchorId.clear();
        m_headId.clear();
        m_rangeActive = false;
        m_lastActiveId.clear();
        m_textAnchorId.clear();
        m_textHeadId.clear();
        m_textAnchorRaw = m_textAnchorStart = m_textAnchorEnd = 0;
        m_textHeadRaw = 0;
        m_granularity = CharacterGranularity;
    });
}

void DocumentSelection::clearBlockSelection()
{
    bumpIfChanged([&] {
        m_baseIds.clear();
        m_anchorId.clear();
        m_headId.clear();
        m_rangeActive = false;
        m_lastActiveId.clear();
    });
}

void DocumentSelection::clearTextSelection()
{
    bumpIfChanged([&] {
        m_textAnchorId.clear();
        m_textHeadId.clear();
        m_textAnchorRaw = m_textAnchorStart = m_textAnchorEnd = 0;
        m_textHeadRaw = 0;
        m_granularity = CharacterGranularity;
    });
}

void DocumentSelection::captureRemovedIds(const QModelIndex &parent, int first,
                                          int last)
{
    Q_UNUSED(parent);
    if (!m_model)
        return;
    // Cheap early-out: with no selection state at all there is nothing
    // to prune, so a bulk delete pays no per-removal capture cost.
    if (m_baseIds.isEmpty() && m_anchorId.isEmpty() && m_headId.isEmpty()
        && m_lastActiveId.isEmpty() && m_textAnchorId.isEmpty()
        && m_textHeadId.isEmpty())
        return;
    for (int i = first; i <= last; ++i) {
        const QString id = idAt(i);
        if (!id.isEmpty())
            m_pendingRemovedIds.insert(id);
    }
}

void DocumentSelection::pruneRemovedBlocks()
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("selection.prune"),
        QVariantMap{
            {QStringLiteral("blocks"), m_model ? m_model->count() : 0},
            {QStringLiteral("baseIds"), m_baseIds.size()},
        });
    // Only the ids captured from the removed range can have become
    // invalid — removals are the only way an id disappears (resets go
    // through clear()), so no whole-model rescan is needed (finding A2).
    const QSet<QString> removed = m_pendingRemovedIds;
    m_pendingRemovedIds.clear();
    if (removed.isEmpty()) {
        perf.addContext(QStringLiteral("remainingBaseIds"), m_baseIds.size());
        return;
    }
    bumpIfChanged([&] {
        m_baseIds.subtract(removed);
        // Either end of the range gesture gone: the range is gone (the
        // committed set survives).
        if (removed.contains(m_anchorId) || removed.contains(m_headId)) {
            m_anchorId.clear();
            m_headId.clear();
            m_rangeActive = false;
        }
        if (removed.contains(m_lastActiveId))
            m_lastActiveId.clear();
        // A text range with a missing endpoint is meaningless.
        if (removed.contains(m_textAnchorId) || removed.contains(m_textHeadId)) {
            m_textAnchorId.clear();
            m_textHeadId.clear();
        }
    });
    perf.addContext(QStringLiteral("remainingBaseIds"), m_baseIds.size());
}

// ---- word boundaries ----

namespace {
enum class CharClass { Word, Space, Other };

CharClass classify(QChar c)
{
    if (c.isLetterOrNumber() || c == QLatin1Char('_'))
        return CharClass::Word;
    if (c.isSpace())
        return CharClass::Space;
    return CharClass::Other;
}
} // namespace

int DocumentSelection::wordStart(const QString &text, int pos)
{
    pos = qBound(0, pos, static_cast<int>(text.length()));
    if (pos == 0)
        return 0;
    const CharClass cls = classify(text.at(pos - 1));
    while (pos > 0 && classify(text.at(pos - 1)) == cls)
        --pos;
    return pos;
}

int DocumentSelection::wordEnd(const QString &text, int pos)
{
    const int len = text.length();
    pos = qBound(0, pos, len);
    if (pos >= len)
        return len;
    const CharClass cls = classify(text.at(pos));
    while (pos < len && classify(text.at(pos)) == cls)
        ++pos;
    return pos;
}
