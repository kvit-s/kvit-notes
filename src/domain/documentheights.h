// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTHEIGHTS_H
#define DOCUMENTHEIGHTS_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

// Included, not forward-declared: the guarded model pointer below is a
// QPointer, which needs the complete type wherever this header is used.
#include "blockmodel.h"

// The heights the block list has measured, remembered per open document: a
// GUI-free projection over BlockModel in the shape of DocumentOutline and
// DocumentStats, exposed to QML as the `DocumentHeights` singleton.
//
// Why it exists. The document view is virtualized, so at any moment only the
// rows near the viewport exist and QQuickListView estimates the rest from the
// average height of the ones it has built. It keeps no memory of a row it
// recycled, so that average is recomputed from whichever rows happen to be
// built — which changes on every notch of the wheel. In a note whose rows are
// of very unequal height (a heading, a one-line paragraph, a wrapped
// paragraph, a fenced block, a list) the estimated document height moves with
// it: measured over one such note in a 175 px viewport, the estimate was
// 19,526 px at the head, 16,375 px at the foot and 17,199 px with every row
// built, and the scrollbar handle — whose length is the viewport over that
// estimate — swung 25% about its mean over a single read. Reading the note
// through teaches the list nothing it keeps, so the walk back up is no better
// than the walk down.
//
// What this holds is one height per block, filled as rows are built and KEPT
// when they are recycled, so the sample only grows. A block nobody has
// measured yet is estimated from the heights measured so far rather than from
// the rows currently built, which is the whole of the difference: the
// estimate settles instead of oscillating, and it is exact once the reader
// has been through the note.
//
// The estimate is over the measured blocks SHAPED like the one being
// estimated, falling back to the ones of the same kind, and then to every
// height measured so far. A plain mean over everything is the obvious thing
// and it is not enough: the first screenful is a small sample of a document
// whose rows differ by a factor of eight, so the total it implies is 10-20%
// out and moves by that much again as the next few screens arrive — which is
// the wandering handle again, quieter and only until the note has been read.
// Measured on a note of thirty repetitions of heading, one-line paragraph,
// wrapped paragraph, fenced block and list, read from the head down
// (tests/test_scrollmetrics.cpp): the handle swung 28% of its size with a
// plain mean, 20% with the kind alone and not at all with the shape, against
// 48% for the list's own estimate over the same walk. All three settle, and
// all three are exact by the foot; what the shape buys is the part of the
// read before the reader has been everywhere.
//
// A KIND is the delegate the row is drawn with and the block's type together,
// because a heading and a paragraph share a delegate and not a size, while a
// Mermaid diagram and a plain code fence share a type and not a size either.
// A SHAPE is a kind and a doubling band of content length, because what makes
// one paragraph six times another's height is how much text it holds.
//
// What it is not. It never places a row. The list goes on measuring and
// positioning rows exactly as it did, and `originY` and `contentHeight` keep
// their present meanings; the only consumer is the scrollbar, which bounds
// what a stale entry can cost to a handle drawn at a slightly wrong length.
// The table is in memory only: it is rebuilt by reading the note, costs
// nothing to lose, and so is never written to disk and has no format to
// version.
//
// A row's height is only meaningful at the width it was measured at, so a
// measurement carries the width it was taken at and a measurement at a
// different width empties the table rather than mixing two layouts. That
// covers the window, the panels, the maximum-content-width setting and focus
// mode in one rule. A typography change — a different reading size, line
// height or face — moves heights without moving the width, and the shell
// calls clear() for it.
class DocumentHeights : public QObject
{
    Q_OBJECT
    // Bumped on every observable change of the table: a measurement, a model
    // change that moved or dropped entries, a new spacing. The scrollbar
    // recomputes off this.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    // Rows tracked, which is the model's block count.
    Q_PROPERTY(int count READ count NOTIFY revisionChanged)
    // How many of them carry a measurement rather than an estimate.
    Q_PROPERTY(int measuredCount READ measuredCount NOTIFY revisionChanged)
    // Every block's height, measured or estimated, plus the spacing between
    // them: the document's height as this table sees it.
    Q_PROPERTY(qreal totalHeight READ totalHeight NOTIFY revisionChanged)
    // The mean over every height measured so far, whatever kind of block it
    // was, and what a block of a kind nothing has been measured for is worth.
    Q_PROPERTY(qreal averageHeight READ averageHeight NOTIFY revisionChanged)
    // False until something has been measured, when the table can answer
    // nothing and a consumer falls back to the view's own numbers.
    Q_PROPERTY(bool ready READ isReady NOTIFY revisionChanged)
    // The gap the view leaves between two rows. Part of the document's
    // height and of every offset, and not part of any row's own height.
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY revisionChanged)
    // The width every measurement in the table was taken at, or -1 for an
    // empty table.
    Q_PROPERTY(qreal measuredWidth READ measuredWidth NOTIFY revisionChanged)

public:
    explicit DocumentHeights(QObject *parent = nullptr);

    void setModel(BlockModel *model);
    BlockModel *model() const { return m_model; }

    int revision() const { return m_revision; }
    int count() const { return int(m_heights.size()); }
    int measuredCount() const { return m_measuredCount; }
    bool isReady() const { return m_measuredCount > 0 && !m_heights.isEmpty(); }
    qreal spacing() const { return m_spacing; }
    void setSpacing(qreal spacing);
    qreal measuredWidth() const { return m_width; }

    qreal averageHeight() const;
    qreal totalHeight() const;

    // Record what the view laid a row out to be, at the width it laid it out
    // at. A measurement at a width the table was not taken at empties the
    // table first, so no two layouts are ever mixed. Recording the height a
    // block already carries changes nothing and bumps no revision, which is
    // what keeps a row that reports its geometry on every relayout from
    // waking every consumer.
    Q_INVOKABLE void recordHeight(int block, qreal height, qreal width);

    // What a block is worth: its measurement, or the estimate for its shape.
    // Zero for a block outside the document.
    Q_INVOKABLE qreal heightOf(int block) const;
    Q_INVOKABLE bool isMeasured(int block) const;
    // What a block of this one's shape is worth, whether or not this one has
    // been measured. Public for the suite that pins the estimator down.
    Q_INVOKABLE qreal estimateFor(int block) const;

    // Where a block's top sits in the document, counting every block above it
    // and the spacing between them. offsetOf(count()) is the document's
    // height, so a caller may ask about the end.
    Q_INVOKABLE qreal offsetOf(int block) const;

    // The inverse: which block covers `offset`. Clamped at both ends, so an
    // offset past the end answers with the last block rather than with -1 —
    // the scrollbar drags to a fraction of a document whose total it is
    // itself estimating, and the fraction can land just past the end.
    Q_INVOKABLE int blockAt(qreal offset) const;

    // Forget every measurement, keeping the row count. What a typography
    // change costs, and what a measurement at a new width does for itself.
    Q_INVOKABLE void clear();

signals:
    void revisionChanged();

private:
    // A row that has not been measured. Negative rather than 0, because a
    // block CAN measure zero (a container-only row a module collapsed), and
    // "measured as nothing" and "not measured" are different answers.
    static constexpr qreal kUnmeasured = -1.0;
    // A block whose shape has not been worked out from the model yet.
    static constexpr int kUnknownShape = -1;
    // How many length bands a kind is split into, and the factor that turns a
    // shape key back into the kind key it belongs to.
    static constexpr int kLengthBands = 8;

    // The measured heights of one kind or shape of block, kept incrementally
    // so an estimate is a hash lookup and a division.
    struct Sample {
        qreal sum = 0;
        int count = 0;
    };

    void onModelDestroyed();
    // Match the table's length to the model's, admitting new rows as
    // unmeasured and dropping the sums of any that went. Used by the reset
    // path and as the guard on an index the structural signals did not
    // account for.
    void syncCount();
    void insertRows(int first, int count);
    void removeRows(int first, int count);
    void moveRows(int first, int count, int destination);
    // Drop the measurements of a run of blocks whose content changed. The
    // shell re-measures the rows it still has built, so a dropped entry that
    // did not actually change height comes straight back.
    void forgetRange(int first, int last);
    void takeMeasurement(int block);
    void bumpRevision();
    void ensureOffsets() const;
    // How this block is drawn and how much it holds, as one key. Worked out
    // from the model the first time it is asked for and kept until something
    // about that block changes, so a document of thousands of blocks is not
    // walked through the model on every rebuild. shape / kLengthBands is the
    // kind key, which is the same answer with the length band dropped.
    int shapeOf(int block) const;

    // Guarded: the table is a long-lived per-window service and outlives the
    // model it projects, so a raw pointer would dangle across a note switch
    // or a shutdown.
    QPointer<BlockModel> m_model;

    // One entry per block, kUnmeasured until the view has built that row.
    QList<qreal> m_heights;
    // One entry per block, in step with m_heights: how that block is drawn
    // and how much it holds. Mutable because it is filled from the model as
    // blocks are asked about, which the const geometry queries do.
    mutable QList<int> m_shapes;
    // The whole sample, and the same sample split two ways. All three are
    // kept incrementally, so an estimate costs a hash lookup and a division.
    qreal m_measuredSum = 0;
    int m_measuredCount = 0;
    QHash<int, Sample> m_byShape;
    QHash<int, Sample> m_byKind;
    qreal m_spacing = 0;
    qreal m_width = -1;
    int m_revision = 0;

    // Prefix sums, rebuilt lazily: m_offsets[i] is the top of block i and
    // m_offsets[count] the end of the document. A measurement invalidates it,
    // and the next question rebuilds it once, so a burst of measurements
    // arriving in one turn costs one walk rather than one per measurement.
    mutable QList<qreal> m_offsets;
    mutable bool m_offsetsValid = false;
};

#endif // DOCUMENTHEIGHTS_H
