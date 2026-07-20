// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTSELECTION_H
#define DOCUMENTSELECTION_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class BlockModel;
class QModelIndex;

// All selection state above the single-block level (phase6-plan.md
// decision 1): which blocks are block-selected, and the anchor/head of a
// cross-block text range. Delegates RENDER selection by querying this
// object — they never own selection state, mirroring how BlockModel owns
// content. Blocks are tracked by blockId, which is stable across moves
// and index shifts; index-based queries resolve ids at call time, and the
// model's structural signals prune ids whose blocks were removed.
//
// The two selection kinds are mutually exclusive (decision 2): starting
// one clears the other.
//
// Block-selection semantics follow the file-manager convention: a
// committed set (built by Ctrl+Click toggles) plus one active anchor-head
// range (Shift+Click / Ctrl+Shift+Arrows). The effective selection is
// their union; toggling re-anchors at the toggled block.
class DocumentSelection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool hasBlockSelection READ hasBlockSelection NOTIFY revisionChanged)
    Q_PROPERTY(bool hasTextSelection READ hasTextSelection NOTIFY revisionChanged)

public:
    // Mouse-press multiplicity maps to selection granularity (§21.3:
    // character drag, double-click word drag, triple-click block drag).
    enum Granularity {
        CharacterGranularity = 0,
        WordGranularity,
        BlockGranularity
    };
    Q_ENUM(Granularity)

    explicit DocumentSelection(QObject *parent = nullptr);

    void setModel(BlockModel *model);
    BlockModel *model() const { return m_model; }

    int revision() const { return m_revision; }
    bool hasBlockSelection() const;
    bool hasTextSelection() const;

    // ---- Block selection (features.md §3.1) ----
    Q_INVOKABLE void selectBlock(int index);            // handle click
    Q_INVOKABLE void toggleBlock(int index);            // Ctrl+Click
    Q_INVOKABLE void extendBlockSelectionTo(int index); // Shift+Click
    Q_INVOKABLE void extendBlockSelection(int delta);   // Ctrl+Shift+Up/Down
    Q_INVOKABLE void selectAllBlocks();                 // Ctrl+A second stage
    // Plain Up/Down in selection mode: collapse to the single block just
    // outside the selection in that direction (clamped to the document).
    Q_INVOKABLE void collapseBlockSelection(int direction);
    Q_INVOKABLE bool isBlockSelected(int index) const;
    Q_INVOKABLE QVariantList selectedIndexes() const;   // ascending
    // Where the selection gesture last was: Enter edits it, Escape
    // refocuses it. -1 when nothing is selected.
    Q_INVOKABLE int lastActiveIndex() const;

    // ---- Cross-block text selection (features.md §2.5, §21.3) ----
    // Positions are MARKDOWN positions (model coordinates); the QML layer
    // maps document positions through each block's engine.
    Q_INVOKABLE void beginTextSelection(int blockIndex, int mdPos, int granularity);
    Q_INVOKABLE void updateTextSelectionHead(int blockIndex, int mdPos);
    // What a block should render: {"selected": bool, "full": bool,
    // "start": int, "end": int} (markdown coordinates; full covers
    // zero-length content, i.e. dividers inside the range).
    Q_INVOKABLE QVariantMap portionForBlock(int index) const;
    // The range with endpoints ordered document-forward:
    // {"startIndex", "startPos", "endIndex", "endPos"}.
    Q_INVOKABLE QVariantMap orderedTextRange() const;
    Q_INVOKABLE int textAnchorIndex() const;
    Q_INVOKABLE int textAnchorPosition() const;
    Q_INVOKABLE int textHeadIndex() const;
    Q_INVOKABLE int textHeadPosition() const;

    // The selected range as clipboard markdown (§5.1 "copy multiple
    // blocks preserving structure"): partially selected edge blocks
    // contribute self-contained inline fragments through the engine's
    // range mapping; fully covered blocks contribute their serialized
    // line (prefixes, fences, ordinals); separators follow the
    // serializer's tight-list rule. Computed from model content, so it
    // works for blocks whose delegates are virtualized away.
    Q_INVOKABLE QString rangeMarkdown() const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearBlockSelection();
    Q_INVOKABLE void clearTextSelection();

    // Pure word-boundary snapping (unit-tested; used for word-granularity
    // endpoints). Characters classify as word ([A-Za-z0-9_] and any
    // letter/digit), whitespace, or other; a boundary position takes the
    // class of the character before (wordStart) / at (wordEnd) it.
    static int wordStart(const QString &text, int pos);
    static int wordEnd(const QString &text, int pos);

signals:
    // Bumped on every observable selection change; delegates depend on it
    // to re-evaluate their bindings.
    void revisionChanged();

private:
    struct Endpoint {
        int index = -1;
        int pos = 0;
    };

    // Raw member snapshot for change detection (performance-plan.md
    // Phase 6, finding A4). Within one mutation the model order is
    // fixed, so the derived effective set changes exactly when these
    // members do — no per-mutation sort/join of thousands of ids.
    struct Snapshot {
        QSet<QString> baseIds;
        QString anchorId;
        QString headId;
        bool rangeActive = false;
        QString lastActiveId;
        QString textAnchorId;
        QString textHeadId;
        int textAnchorRaw = 0;
        int textAnchorStart = 0;
        int textAnchorEnd = 0;
        int textHeadRaw = 0;
        int granularity = 0;

        bool operator==(const Snapshot &other) const
        {
            return baseIds == other.baseIds && anchorId == other.anchorId
                && headId == other.headId && rangeActive == other.rangeActive
                && lastActiveId == other.lastActiveId
                && textAnchorId == other.textAnchorId
                && textHeadId == other.textHeadId
                && textAnchorRaw == other.textAnchorRaw
                && textAnchorStart == other.textAnchorStart
                && textAnchorEnd == other.textAnchorEnd
                && textHeadRaw == other.textHeadRaw
                && granularity == other.granularity;
        }
    };

    QString idAt(int index) const;
    int indexOfId(const QString &id) const;
    QString contentAt(int index) const;
    QSet<QString> effectiveIds() const;
    bool orderedEndpoints(Endpoint &start, Endpoint &end) const;
    void captureRemovedIds(const QModelIndex &parent, int first, int last);
    void pruneRemovedBlocks();
    void bumpIfChanged(const std::function<void()> &mutate);
    Snapshot snapshot() const;

    BlockModel *m_model = nullptr;
    int m_revision = 0;

    // Block selection: committed set + active range gesture.
    QSet<QString> m_baseIds;
    QString m_anchorId;
    QString m_headId;
    bool m_rangeActive = false;
    QString m_lastActiveId;

    // Text selection: anchor extent (snapped at begin) + raw head
    // (snapped per direction at query time).
    QString m_textAnchorId;
    QString m_textHeadId;
    int m_textAnchorRaw = 0;
    int m_textAnchorStart = 0;
    int m_textAnchorEnd = 0;
    int m_textHeadRaw = 0;
    Granularity m_granularity = CharacterGranularity;

    // Ids captured at rowsAboutToBeRemoved so pruning works from the
    // removed range instead of rescanning the whole model (Phase 6,
    // finding A2).
    QSet<QString> m_pendingRemovedIds;
};

#endif // DOCUMENTSELECTION_H
