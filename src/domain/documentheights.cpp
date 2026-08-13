// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentheights.h"

#include "block.h"

#include <QtGlobal>

#include <algorithm>

namespace {

// Sub-pixel jitter is not a new measurement. A row re-reports its geometry
// on every relayout, and without a tolerance each of those would invalidate
// the offsets and wake the scrollbar for a height that has not moved.
constexpr qreal kHeightTolerance = 0.01;

// Which roles can change how tall a row is drawn.
//
// Not every data change can. BlockModel renumbers equations across the whole
// suffix of a document on any edit that could have moved one (MathNumberRole)
// and renumbers an ordered list across its run (OrdinalRole), and a todo
// being ticked is a CheckedRole change — none of which changes a row's
// height. Dropping measurements for those would empty the table on every
// keystroke in any note holding an equation, which is exactly the note whose
// rows are unequal enough to need it.
bool affectsHeight(const QList<int> &roles)
{
    // No roles at all means "everything about these rows", which includes
    // their content.
    if (roles.isEmpty())
        return true;
    for (int role : roles) {
        switch (role) {
        case BlockModel::ContentRole:
        case BlockModel::DisplayTextRole:
        case BlockModel::BlockTypeRole:
        case BlockModel::DelegateKindRole:
        case BlockModel::LanguageRole:
        case BlockModel::AttributesRole:
        case BlockModel::CalloutTitleRole:
        case BlockModel::IndentLevelRole:
        case BlockModel::FontRoleRole:
        case BlockModel::BlockObjectRole:
            return true;
        default:
            break;
        }
    }
    return false;
}

// Which doubling band of content length a block falls in. A block's height
// follows how many lines its text wraps onto, which follows how much text it
// holds, so two blocks in one band are within a factor of two of each other's
// length and much closer than that in height, once the parts that do not
// scale with the text are counted.
int lengthBand(int characters)
{
    int band = 0;
    for (int edge = 80; band < 7 && characters > edge; edge *= 2)
        ++band;
    return band;
}

} // namespace

DocumentHeights::DocumentHeights(QObject *parent)
    : QObject(parent)
{
}

void DocumentHeights::setModel(BlockModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        // Auto-disconnection unhooks the signals but leaves the QPointer to
        // do the guarding, and this table answers questions after the model
        // that filled it is gone.
        connect(m_model, &QObject::destroyed,
                this, &DocumentHeights::onModelDestroyed);
        connect(m_model, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex &, int first, int last) {
                    insertRows(first, last - first + 1);
                });
        connect(m_model, &QAbstractItemModel::rowsRemoved, this,
                [this](const QModelIndex &, int first, int last) {
                    removeRows(first, last - first + 1);
                });
        connect(m_model, &QAbstractItemModel::rowsMoved, this,
                [this](const QModelIndex &, int first, int last,
                       const QModelIndex &, int destination) {
                    moveRows(first, last - first + 1, destination);
                });
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles) {
                    if (affectsHeight(roles))
                        forgetRange(topLeft.row(), bottomRight.row());
                });
        // A different document, which is what a note being opened is: the
        // heights go, and so do the kinds, since block 4 is now some other
        // block entirely.
        connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
            syncCount();
            m_shapes.fill(kUnknownShape);
            clear();
        });
    }
    syncCount();
    clear();
}

void DocumentHeights::onModelDestroyed()
{
    m_model = nullptr;
    m_heights.clear();
    m_shapes.clear();
    m_measuredSum = 0;
    m_measuredCount = 0;
    m_byShape.clear();
    m_byKind.clear();
    m_width = -1;
    m_offsetsValid = false;
    bumpRevision();
}

// ---- the sample ----

qreal DocumentHeights::averageHeight() const
{
    if (m_measuredCount <= 0)
        return 0;
    return m_measuredSum / m_measuredCount;
}

qreal DocumentHeights::totalHeight() const
{
    ensureOffsets();
    return m_offsets.isEmpty() ? 0 : m_offsets.last();
}

void DocumentHeights::setSpacing(qreal spacing)
{
    if (qFuzzyCompare(m_spacing + 1.0, spacing + 1.0))
        return;
    m_spacing = spacing;
    m_offsetsValid = false;
    bumpRevision();
}

void DocumentHeights::recordHeight(int block, qreal height, qreal width)
{
    // A row the view has not laid out yet reports a width of zero and a
    // height of nothing, and neither is a measurement of anything.
    if (height < 0 || width <= 0)
        return;
    if (block < 0)
        return;
    if (block >= count())
        syncCount();
    if (block >= count())
        return;

    // A measurement taken at another width belongs to another layout. The
    // table holds one layout's worth of heights or none, never a mixture of
    // two, so the arriving width decides which layout that is.
    if (m_width > 0 && !qFuzzyCompare(m_width, width))
        clear();
    m_width = width;

    const qreal existing = m_heights.at(block);
    if (existing >= 0 && qAbs(existing - height) < kHeightTolerance)
        return;
    takeMeasurement(block);
    m_heights[block] = height;
    m_measuredSum += height;
    ++m_measuredCount;
    const int shape = shapeOf(block);
    if (shape != kUnknownShape) {
        Sample &byShape = m_byShape[shape];
        byShape.sum += height;
        ++byShape.count;
        Sample &byKind = m_byKind[shape / kLengthBands];
        byKind.sum += height;
        ++byKind.count;
    }
    m_offsetsValid = false;
    bumpRevision();
}

qreal DocumentHeights::heightOf(int block) const
{
    if (block < 0 || block >= count())
        return 0;
    const qreal own = m_heights.at(block);
    return own >= 0 ? own : estimateFor(block);
}

qreal DocumentHeights::estimateFor(int block) const
{
    if (block < 0 || block >= count())
        return 0;
    // The blocks measured so far that are shaped like this one; failing
    // that, the ones drawn the way it will be whatever they hold; failing
    // that, every height there is. Each step is a wider and worse-fitting
    // sample, and the last one is the best answer available until some row of
    // this document has been built at all.
    const int shape = shapeOf(block);
    if (shape == kUnknownShape)
        return averageHeight();
    const Sample byShape = m_byShape.value(shape);
    if (byShape.count > 0)
        return byShape.sum / byShape.count;
    const Sample byKind = m_byKind.value(shape / kLengthBands);
    if (byKind.count > 0)
        return byKind.sum / byKind.count;
    return averageHeight();
}

int DocumentHeights::shapeOf(int block) const
{
    if (block < 0 || block >= m_shapes.size())
        return kUnknownShape;
    if (m_shapes.at(block) != kUnknownShape || !m_model)
        return m_shapes.at(block);
    const Block *content = m_model->blockAt(block);
    if (!content)
        return kUnknownShape;
    // The delegate AND the type, because neither alone separates the rows
    // that differ in size: paragraphs and headings share a delegate, and a
    // Mermaid diagram, a kanban board and a plain fence share a type.
    const int delegate = m_model->data(m_model->index(block, 0),
                                       BlockModel::DelegateKindRole).toInt();
    const int kind = delegate * 32 + int(content->blockType());
    m_shapes[block] = kind * kLengthBands + lengthBand(content->content().size());
    return m_shapes.at(block);
}

bool DocumentHeights::isMeasured(int block) const
{
    if (block < 0 || block >= count())
        return false;
    return m_heights.at(block) >= 0;
}

qreal DocumentHeights::offsetOf(int block) const
{
    ensureOffsets();
    if (m_offsets.isEmpty())
        return 0;
    return m_offsets.at(qBound(0, block, count()));
}

int DocumentHeights::blockAt(qreal offset) const
{
    const int rows = count();
    if (rows <= 0)
        return -1;
    ensureOffsets();
    if (offset <= 0)
        return 0;
    // The last block whose top is at or above the offset. upper_bound stops
    // one past it, and the search runs over the blocks alone rather than over
    // the end marker m_offsets carries.
    const auto begin = m_offsets.cbegin();
    const auto it = std::upper_bound(begin, begin + rows, offset);
    return qBound(0, int(it - begin) - 1, rows - 1);
}

void DocumentHeights::clear()
{
    if (m_measuredCount == 0 && m_width < 0)
        return;
    for (qreal &height : m_heights)
        height = kUnmeasured;
    m_measuredSum = 0;
    m_measuredCount = 0;
    m_byShape.clear();
    m_byKind.clear();
    m_width = -1;
    m_offsetsValid = false;
    bumpRevision();
}

// ---- following the model ----

void DocumentHeights::syncCount()
{
    const int rows = m_model ? m_model->count() : 0;
    if (rows == count())
        return;
    if (rows < count())
        removeRows(rows, count() - rows);
    else
        insertRows(count(), rows - count());
}

void DocumentHeights::insertRows(int first, int rows)
{
    if (rows <= 0)
        return;
    const int at = qBound(0, first, count());
    m_heights.insert(at, rows, kUnmeasured);
    m_shapes.insert(at, rows, kUnknownShape);
    m_offsetsValid = false;
    bumpRevision();
}

void DocumentHeights::removeRows(int first, int rows)
{
    if (rows <= 0 || first < 0 || first >= count())
        return;
    const int last = qMin(first + rows, count());
    for (int block = first; block < last; ++block)
        takeMeasurement(block);
    m_heights.remove(first, last - first);
    m_shapes.remove(first, last - first);
    m_offsetsValid = false;
    bumpRevision();
}

void DocumentHeights::moveRows(int first, int rows, int destination)
{
    if (rows <= 0 || first < 0 || first + rows > count())
        return;
    const QList<qreal> moved = m_heights.mid(first, rows);
    const QList<int> movedShapes = m_shapes.mid(first, rows);
    m_heights.remove(first, rows);
    m_shapes.remove(first, rows);
    // Qt states a move's destination in the coordinates BEFORE the rows left,
    // so a move downward lands `rows` earlier once they have.
    int at = destination > first ? destination - rows : destination;
    at = qBound(0, at, count());
    for (int i = 0; i < rows; ++i) {
        m_heights.insert(at + i, moved.at(i));
        m_shapes.insert(at + i, movedShapes.at(i));
    }
    m_offsetsValid = false;
    bumpRevision();
}

void DocumentHeights::forgetRange(int first, int last)
{
    const int from = qBound(0, first, qMax(0, count() - 1));
    const int to = qBound(0, last, qMax(0, count() - 1));
    bool changed = false;
    for (int block = from; block <= to && block < count(); ++block) {
        // The height first, so it leaves the sample it was filed under, and
        // the kind second, since the change may have been the kind.
        const bool measured = m_heights.at(block) >= 0;
        takeMeasurement(block);
        changed = changed || measured || m_shapes.at(block) != kUnknownShape;
        m_shapes[block] = kUnknownShape;
    }
    if (!changed)
        return;
    m_offsetsValid = false;
    bumpRevision();
}

void DocumentHeights::takeMeasurement(int block)
{
    if (block < 0 || block >= count())
        return;
    const qreal existing = m_heights.at(block);
    if (existing < 0)
        return;
    m_measuredSum -= existing;
    --m_measuredCount;
    // Out of the two samples it went into. The shape of a block whose
    // content changed is forgotten only AFTER its height is, which is what
    // keeps these in step.
    // A height that was never filed under a shape, because the model went
    // away between the measurement and this, leaves both hashes alone rather
    // than taking itself out of whichever sample the unknown key lands in.
    const int shape = shapeOf(block);
    if (shape != kUnknownShape) {
        const auto fromShape = m_byShape.find(shape);
        if (fromShape != m_byShape.end()) {
            fromShape->sum -= existing;
            if (--fromShape->count <= 0)
                m_byShape.erase(fromShape);
        }
        const auto fromKind = m_byKind.find(shape / kLengthBands);
        if (fromKind != m_byKind.end()) {
            fromKind->sum -= existing;
            if (--fromKind->count <= 0)
                m_byKind.erase(fromKind);
        }
    }
    if (m_measuredCount <= 0) {
        // Floating-point residue from adding and subtracting the same
        // heights would otherwise survive an empty sample and bias the next
        // estimate.
        m_measuredCount = 0;
        m_measuredSum = 0;
    }
    m_heights[block] = kUnmeasured;
}

void DocumentHeights::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}

void DocumentHeights::ensureOffsets() const
{
    if (m_offsetsValid)
        return;
    const int rows = count();
    m_offsets.resize(rows + 1);
    qreal accumulated = 0;
    for (int block = 0; block < rows; ++block) {
        m_offsets[block] = accumulated;
        const qreal own = m_heights.at(block);
        accumulated += own >= 0 ? own : estimateFor(block);
        // The spacing sits BETWEEN rows, so the last one is not followed by
        // any: the document ends where its last block does.
        if (block < rows - 1)
            accumulated += m_spacing;
    }
    m_offsets[rows] = accumulated;
    m_offsetsValid = true;
}
