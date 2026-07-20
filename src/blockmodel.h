// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKMODEL_H
#define BLOCKMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include "block.h"
#include "blockkindregistry.h"

class UndoStack;
class UndoCommand;

class BlockModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int documentWordCount READ documentWordCount NOTIFY documentCountsChanged)
    Q_PROPERTY(int documentCharCount READ documentCharCount NOTIFY documentCountsChanged)
    Q_PROPERTY(int documentCharsNoSpaces READ documentCharsNoSpaces NOTIFY documentCountsChanged)
    Q_PROPERTY(int documentParagraphCount READ documentParagraphCount NOTIFY documentCountsChanged)
    Q_PROPERTY(int tocBlockCount READ tocBlockCount NOTIFY tocBlockIndexesChanged)

public:
    enum BlockRoles {
        BlockIdRole = Qt::UserRole + 1,
        BlockTypeRole,
        ContentRole,
        IndentLevelRole,
        BlockObjectRole,
        CheckedRole,
        LanguageRole,
        OrdinalRole,
        DelegateKindRole,
        CalloutTitleRole,
        AttributesRole,
        DisplayTextRole
    };

    // The DelegateChooser watches this role instead of the raw type:
    // paragraphs and headings share one delegate, and the chooser
    // recreates a row's delegate (dropping focus and pooled state)
    // whenever its watched role changes — so the role must be stable
    // across same-delegate type changes. Emitted only when the kind
    // actually changes.
    // A kanban board is a code fence tagged `kanban`: no new stored type, just
    // this derived kind so the chooser renders the board instead of a plain
    // code block. Chosen well above the enum range so it never collides with a
    // type value.
    //
    // The fence-language kinds are numbered in BlockKinds and looked up
    // through BlockKindRegistry, which is what lets a linked module add a
    // fence kind of its own without touching this file; the constants below
    // stay as the names the rest of the code and the tests already use.
    static constexpr int KanbanKind = BlockKinds::Kanban;
    // A table of contents is a `toc`-tagged code fence: no new stored type,
    // just this derived kind so the chooser renders the read-only linked TOC
    // instead of a plain code block.
    static constexpr int TocKind = BlockKinds::Toc;
    // An embed is an image expression ![](url) whose URL is a web page or
    // video host: no new stored type, a derived kind from the CONTENT so the
    // chooser renders a preview card. Because it depends on content (not just
    // type), the kind is computed by delegateKindForContent, used wherever the
    // role is derived.
    static constexpr int EmbedKind = BlockKinds::Embed;
    // A Mermaid diagram is a `mermaid`-tagged code fence: no new stored type, a
    // derived kind so the chooser renders the native diagram instead of a plain
    // code block, exactly like `kanban`/`toc`. Character diagrams (`diagram`
    // fences) carry no kind of their own: the tag marks the fence for ingest
    // straightening and the block renders as an ordinary code block.
    static constexpr int MermaidKind = BlockKinds::Mermaid;
    // A collection query is a `query`-tagged code fence: no new stored type, a
    // derived kind so the chooser renders the live table/board over the
    // collection's front-matter.
    static constexpr int QueryKind = BlockKinds::Query;
    // Content-aware delegate kind: an Image/Media block whose URL is an embed
    // becomes EmbedKind; everything else falls back to the type/language kind.
    // The fence registry this model resolves kinds against. AppContext wires
    // its shared one so a linked module's kinds are visible; a model with
    // none of its own falls back to a private registry holding the built-ins,
    // which is what keeps a bare BlockModel in a unit test rendering `kanban`
    // fences as boards without any global state.
    void setBlockKindRegistry(BlockKindRegistry *registry);
    BlockKindRegistry *blockKindRegistry() const { return m_blockKinds; }

    // Content-aware delegate kind: an Image/Media block whose URL is an embed
    // becomes EmbedKind; everything else falls back to the type/language kind.
    int delegateKindForContent(Block::BlockType type,
                               const QString &language,
                               const QString &content) const;

    static int delegateKindFor(Block::BlockType type)
    {
        // Explicit range, not ">= BulletList": Heading4 is appended
        // after Divider yet shares the text delegate with the other
        // headings and paragraphs (kind 0). The wave-2 types (Image and
        // later) each render through their own delegate, so they keep
        // their own kind — appended after Heading4, they fall past the
        // wave-1 range and are admitted explicitly.
        if (type >= Block::BulletList && type <= Block::Divider)
            return static_cast<int>(type);
        if (type >= Block::Image)
            return static_cast<int>(type);
        return 0;
    }

    // The delegate kind considering the language: a `kanban` code fence gets
    // KanbanKind so the DelegateChooser recreates the delegate when a fence's
    // language becomes (or stops being) `kanban`.
    int delegateKindForBlock(Block::BlockType type, const QString &language) const;

    // Indentation depth limit (features.md §3.3)
    static constexpr int MaxIndentLevel = 4;

    explicit BlockModel(QObject *parent = nullptr);
    ~BlockModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Custom methods exposed to QML
    Q_INVOKABLE void insertBlock(int index, int type, const QString &content,
                                 int indentLevel = 0);
    Q_INVOKABLE void removeBlock(int index);
    Q_INVOKABLE void updateContent(int index, const QString &content);

    // Commit content to the block named by its stable id rather than by row.
    //
    // A delegate's index is mutable: it changes when rows are inserted or
    // removed, and a pooled delegate is rebound to a different row entirely.
    // Anything that writes back after a delay - a debounce timer, an
    // editingFinished handler - is holding an index that may no longer mean
    // what it did when the edit started, so writing by index can land the text
    // in the wrong block or in a different document. Returns false when the
    // block is gone, which is the signal to drop the edit rather than guess.
    Q_INVOKABLE bool updateContentById(const QString &blockId,
                                       const QString &content);
    // Update content WITHOUT pushing an undo step: the table-of-contents
    // fence keeps its stored body in sync with the live outline as headings
    // change, and that regeneration is derived state, not a user edit — so it
    // must not spawn undo entries or interfere with the heading edit's own
    // undo. Emits dataChanged like the internal path.
    Q_INVOKABLE void updateContentSilently(int index, const QString &content);
    Q_INVOKABLE void updateType(int index, int type);
    Q_INVOKABLE void moveBlock(int fromIndex, int toIndex);
    Q_INVOKABLE Block* blockAt(int index) const;

    // Block-type operations
    Q_INVOKABLE void setChecked(int index, bool checked);
    Q_INVOKABLE void changeIndent(int index, int delta);
    Q_INVOKABLE void convertBlock(int index, int type, const QString &content,
                                  bool checked = false,
                                  const QString &language = QString(),
                                  const QString &calloutTitle = QString());
    // Set a callout's title as one undo step; reuses the full-state
    // ConvertBlockCommand so the fold/type/body are captured too.
    Q_INVOKABLE void setCalloutTitle(int index, const QString &title);
    // Set a block's presentation attributes as one undo step. Pushes
    // SetBlockAttributesCommand; a no-op when unchanged.
    Q_INVOKABLE void setBlockAttributes(int index, const QString &attributes);
    // Display number of a numbered-list block (1-based); 0 for any other
    // type. Computed, never stored.
    Q_INVOKABLE int ordinalAt(int index) const;

    // Equation number of a MathBlock: its 1-based position among all
    // MathBlocks in the document, or 0 for any other type.
    // Computed by position, never stored; the delegate shows it only when the
    // equation-numbering setting is on.
    Q_INVOKABLE int mathNumber(int index) const;

    // Sub-task progress of a parent todo: {done, total} over the
    // deeper-indented todo children that follow it, derived (never stored).
    // total is 0 when the todo has no children.
    Q_INVOKABLE QVariantMap todoProgress(int index) const;

    // Multi-block operations. Every one is a single undo step,
    // composed from the existing commands through the stack's macro.
    // Index lists arrive from DocumentSelection::selectedIndexes()
    // (possibly non-contiguous).
    Q_INVOKABLE void removeBlocks(const QVariantList &indexes);
    // Inserts full-state clones directly below the last selected block,
    // in document order; returns the clones' indexes (§3.6).
    Q_INVOKABLE QVariantList duplicateBlocks(const QVariantList &indexes);
    // Moves each contiguous run of selected blocks one step (VSCode
    // line-move semantics); a run against the document edge stops the
    // whole operation. delta is +1 or -1.
    Q_INVOKABLE void moveBlocksBy(const QVariantList &indexes, int delta);
    // Indents/outdents every list-family block in the selection under
    // the existing per-block clamps (§3.3).
    Q_INVOKABLE void changeIndentForBlocks(const QVariantList &indexes, int delta);
    // Drag-and-drop primitives. previewMoveBlock is the drag's live
    // make-room feedback: an undo-bypassing internal move, valid ONLY
    // inside a drag gesture whose drop pushes commitDragMove — one
    // pre-applied command for the whole gesture (or nothing, when the
    // drag cancels or ends where it started).
    Q_INVOKABLE void previewMoveBlock(int fromIndex, int toIndex);
    Q_INVOKABLE void commitDragMove(int originalFrom, int finalTo);
    // Multi-block drop: moves the given blocks, in document order, to
    // sit contiguously at the gap BEFORE block index targetGap
    // (0..count, in the pre-drop arrangement). One undo step.
    Q_INVOKABLE void moveBlocksTo(const QVariantList &indexes, int targetGap);

    // Removes a cross-block text range: the first block keeps its type
    // and fields with content before+after; the blocks between and the
    // last are removed. A divider first block holds no text, so there
    // the remainder keeps the LAST block's identity. Returns
    // {"index", "cursor"} for refocusing, or an empty map if the range
    // is invalid.
    Q_INVOKABLE QVariantMap removeTextRange(int startIndex, int startMd,
                                            int endIndex, int endMd);

    // One range move in ListView coordinates: rows [first..last] move to
    // sit before row dest (pre-move coordinates, beginMoveRows contract).
    // moveBlocksTo plans one of these per contiguous selected run instead
    // of deriving O(N) single moves through repeated linear scans.
    struct RangeMove {
        int first = -1;
        int last = -1;
        int dest = -1;
    };

    // Milestone 2 helper methods
    Q_INVOKABLE QString getContent(int index) const;
    Q_INVOKABLE QString getAttributes(int index) const;
    Q_INVOKABLE QString displayTextAt(int index) const;
    Q_INVOKABLE int wordCountAt(int index) const;
    Q_INVOKABLE int charCountAt(int index, bool withSpaces = true) const;
    Q_INVOKABLE QVariantList tocBlockIndexes() const;
    // O(1) id lookup backed by a lazily rebuilt id->index hash. Structural
    // changes mark the hash dirty; the next lookup rebuilds it once.
    // Returns -1 for unknown ids.
    Q_INVOKABLE int indexOfBlockId(const QString &blockId) const;
    Q_INVOKABLE void splitBlock(int index, int position);
    Q_INVOKABLE void mergeBlocks(int keepIndex, int removeIndex);

    // Milestone 6: Clear all blocks
    Q_INVOKABLE void clear();

    int count() const;
    int documentWordCount() const { return m_documentWordCount; }
    int documentCharCount() const { return m_documentCharCount; }
    int documentCharsNoSpaces() const { return m_documentCharsNoSpaces; }
    int documentParagraphCount() const { return m_documentParagraphCount; }
    int tocBlockCount() const { return m_tocBlockIndexes.size(); }

    void initializeWithSampleData();

    // UndoStack integration
    void setUndoStack(UndoStack *stack) { m_undoStack = stack; }
    UndoStack* undoStack() const { return m_undoStack; }

    // Internal methods (for undo commands to use - bypass undo stack)
    void insertBlockInternal(int index, int type, const QString &content);
    void insertBlockInternal(int index, const Block::State &state);
    void removeBlockInternal(int index);
    void updateContentInternal(int index, const QString &content);
    void updateTypeInternal(int index, int type);
    void moveBlockInternal(int fromIndex, int toIndex);
    void splitBlockInternal(int index, int position);
    void mergeBlocksInternal(int keepIndex, int removeIndex);
    void setCheckedInternal(int index, bool checked);
    void setAttributesInternal(int index, const QString &attributes);
    void setIndentInternal(int index, int level);
    void applyStateInternal(int index, const Block::State &state);
    void replaceAllBlocksInternal(const QList<Block::State> &states);
    // Moves rows [first..last] to before row dest in one
    // beginMoveRows/endMoveRows pair. dest uses pre-move coordinates and
    // must lie outside [first, last+1].
    void moveBlocksRangeInternal(int first, int last, int dest);

signals:
    void countChanged();
    void documentCountsChanged();
    void tocBlockIndexesChanged();

private:
    // The registry kinds resolve against, and the private one used until
    // something wires a shared registry in. Declared before it so it is
    // constructed first.
    BlockKindRegistry m_ownedBlockKinds;
    BlockKindRegistry *m_blockKinds = &m_ownedBlockKinds;

    void addBlockCounts(const Block *block);
    void subtractBlockCounts(const Block *block);
    void adjustBlockCountsBeforeChange(const Block *block);
    void adjustBlockCountsAfterChange(const Block *block);
    void resetDocumentCounts();
    // Ordinals derive from local list runs. Signal only from the change point
    // to the end of the affected contiguous list-family run.
    void emitOrdinalsChangedFrom(int changeIndex);
    void emitOrdinalRoleChanged(int first, int last);
    int ordinalChangeEndFrom(int changeIndex) const;
    int listFamilyRunEnd(int index) const;

    bool isTocBlock(const Block *block) const;
    void refreshTocBlockIndex(int index);
    void emitTocBlockIndexesChangedIfNeeded(const QList<int> &before);

    // Argument screening for the single-block operations, applied before a
    // command is constructed so validation does not depend on whether an
    // UndoStack is attached.
    bool isValidIndex(int index) const;
    bool isValidSplit(int index, int position) const;
    // Forward merges only: removeIndex must sit after keepIndex.
    bool isValidMerge(int keepIndex, int removeIndex) const;

    // Valid, unique, ascending indexes out of a QML-provided list.
    QList<int> validIndexes(const QVariantList &indexes) const;
    // Push through the undo stack (which executes) or execute directly
    // when no stack is attached.
    void pushOrRun(std::unique_ptr<UndoCommand> command);

    // Plans the run moves that gather the sorted selected indexes
    // contiguously at the gap: left-side runs collect right-to-left,
    // right-side runs left-to-right, so every planned move uses valid
    // current coordinates.
    QList<RangeMove> planGroupMove(const QList<int> &sorted, int gap) const;

    void invalidateIdIndex() { m_idIndexDirty = true; }
    // Re-points the id hash entries for rows [first..last] after a move,
    // avoiding a full rebuild when the hash is already valid.
    void reindexIdRange(int first, int last);

    // Cached ordinals and equation numbers: one forward pass fills both
    // after a structural or type/indent change, so per-delegate reads are
    // O(1) instead of a backward scan per row on list-heavy documents.
    void invalidateDerivedOrder() { m_derivedOrderDirty = true; }
    void rebuildDerivedOrder() const;

    QList<Block*> m_blocks;
    UndoStack *m_undoStack = nullptr;
    int m_documentWordCount = 0;
    int m_documentCharCount = 0;
    int m_documentCharsNoSpaces = 0;
    int m_documentParagraphCount = 0;
    QList<int> m_tocBlockIndexes;
    mutable QHash<QString, int> m_idIndex;
    mutable bool m_idIndexDirty = true;
    mutable QList<int> m_ordinalCache;
    mutable QList<int> m_mathNumberCache;
    mutable bool m_derivedOrderDirty = true;
};

#endif // BLOCKMODEL_H
