// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockmodel.h"
#include "undostack.h"
#include "textchangecommand.h"
#include "insertblockcommand.h"
#include "removeblockcommand.h"
#include "moveblockcommand.h"
#include "moveblockscommand.h"
#include "changetypecommand.h"
#include "splitblockcommand.h"
#include "mergeblockscommand.h"
#include "setcheckedcommand.h"
#include "changeindentcommand.h"
#include "convertblockcommand.h"
#include "setblockattributescommand.h"
#include "imageassets.h"
#include "perflog.h"

#include <QSet>

#include <algorithm>
#include <utility>

void BlockModel::setBlockKindRegistry(BlockKindRegistry *registry)
{
    m_blockKinds = registry ? registry : &m_ownedBlockKinds;
}

int BlockModel::delegateKindForBlock(Block::BlockType type,
                                     const QString &language) const
{
    if (type == Block::CodeBlock) {
        // The registry holds the built-in fence languages and any a linked
        // module claimed at startup; 0 means "not a fence kind", which falls
        // through to the type's own kind.
        const int kind = m_blockKinds->kindForLanguage(language);
        if (kind != 0)
            return kind;
    }
    return delegateKindFor(type);
}

int BlockModel::delegateKindForContent(Block::BlockType type,
                                       const QString &language,
                                       const QString &content) const
{
    if (type == Block::Image || type == Block::Media) {
        const ImageAssets::Parsed p = ImageAssets::parseLine(content);
        if (p.valid && ImageAssets::isEmbedUrl(p.path))
            return EmbedKind;
    }
    return delegateKindForBlock(type, language);
}

BlockModel::BlockModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

BlockModel::~BlockModel()
{
    qDeleteAll(m_blocks);
}

int BlockModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_blocks.count();
}

QVariant BlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.count())
        return QVariant();

    Block *block = m_blocks.at(index.row());

    switch (role) {
    case BlockIdRole:
        return block->blockId();
    case BlockTypeRole:
        return static_cast<int>(block->blockType());
    case ContentRole:
        return block->content();
    case DisplayTextRole:
        return block->displayText();
    case IndentLevelRole:
        return block->indentLevel();
    case BlockObjectRole:
        return QVariant::fromValue(block);
    case CheckedRole:
        return block->checked();
    case LanguageRole:
        return block->language();
    case OrdinalRole:
        return ordinalAt(index.row());
    case DelegateKindRole:
        return delegateKindForContent(block->blockType(), block->language(),
                                      block->content());
    case CalloutTitleRole:
        return block->calloutTitle();
    case AttributesRole:
        return block->attributes();
    case TodoProgressRole:
        return todoProgress(index.row());
    case MathNumberRole:
        return mathNumber(index.row());
    default:
        return QVariant();
    }
}

// Standard model editing IS supported, and every role it accepts is routed
// through the same undo-aware entry point the rest of the application uses.
// Writing the block directly here (what this used to do) put the edit on a
// clean undo stack: Ctrl+Z could not take it back, and save/close logic that
// asks the stack whether the document is clean would drop it.
bool BlockModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || !isValidIndex(index.row()))
        return false;

    const int row = index.row();
    Block *block = m_blocks.at(row);

    switch (role) {
    case ContentRole:
        updateContent(row, value.toString());
        return true;
    case BlockTypeRole: {
        bool ok = false;
        const int type = value.toInt(&ok);
        if (!ok || !Block::isValidType(type))
            return false;
        updateType(row, type);
        return true;
    }
    case IndentLevelRole: {
        bool ok = false;
        const int level = value.toInt(&ok);
        if (!ok)
            return false;
        // Absolute level, clamped: setData is the generic model API, so it
        // does not carry changeIndent's list-parent rule — but it must not
        // be able to write a depth serialization would clamp on reload.
        const int target = Block::clampIndent(level);
        const int current = block->indentLevel();
        if (target == current)
            return true;
        pushOrRun(std::make_unique<ChangeIndentCommand>(this, row, current,
                                                        target));
        return true;
    }
    case CheckedRole:
        setChecked(row, value.toBool());
        return true;
    case LanguageRole: {
        const QString language = value.toString();
        if (block->language() == language)
            return true;
        // No language-only command exists; the full-state one covers it.
        const Block::State oldState = block->state();
        Block::State newState = oldState;
        newState.language = language;
        pushOrRun(std::make_unique<ConvertBlockCommand>(this, row, oldState,
                                                        newState));
        return true;
    }
    default:
        return false;
    }
}

QHash<int, QByteArray> BlockModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[BlockIdRole] = "blockId";
    roles[BlockTypeRole] = "blockType";
    roles[ContentRole] = "content";
    roles[IndentLevelRole] = "indentLevel";
    roles[BlockObjectRole] = "blockObject";
    roles[CheckedRole] = "checked";
    roles[LanguageRole] = "language";
    roles[OrdinalRole] = "ordinal";
    roles[DelegateKindRole] = "delegateKind";
    roles[CalloutTitleRole] = "calloutTitle";
    roles[AttributesRole] = "attributes";
    roles[DisplayTextRole] = "displayText";
    roles[TodoProgressRole] = "todoProgress";
    roles[MathNumberRole] = "mathNumber";
    return roles;
}

Qt::ItemFlags BlockModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void BlockModel::watchBlock(Block *block)
{
    if (!block)
        return;
    const auto publish = [this, block](int role) {
        return [this, block, role] { onBlockMutated(block, role); };
    };
    connect(block, &Block::contentChanged, this, publish(ContentRole));
    connect(block, &Block::blockTypeChanged, this, publish(BlockTypeRole));
    connect(block, &Block::indentLevelChanged, this, publish(IndentLevelRole));
    connect(block, &Block::checkedChanged, this, publish(CheckedRole));
    connect(block, &Block::languageChanged, this, publish(LanguageRole));
    connect(block, &Block::calloutTitleChanged, this, publish(CalloutTitleRole));
    connect(block, &Block::attributesChanged, this, publish(AttributesRole));
}

void BlockModel::onBlockMutated(Block *block, int role)
{
    // The model's own mutators already publish their change with exactly the
    // roles it touched; this handler exists only for a write that arrived
    // straight at the Block.
    if (m_applyingInternalChange)
        return;
    const int row = indexOfBlockId(block->blockId());
    if (row < 0)
        return;

    // No before-image is available here, so the cheap incremental paths do
    // not apply: recount, and assume the delegate kind may have moved.
    recomputeDocumentCounts();
    invalidateDerivedOrder();
    const QList<int> tocBefore = m_tocBlockIndexes;
    refreshTocBlockIndex(row);

    QVector<int> roles{role};
    if (role == ContentRole || role == BlockTypeRole)
        roles.append(DisplayTextRole);
    if (role == ContentRole || role == BlockTypeRole || role == LanguageRole)
        roles.append(DelegateKindRole);

    const QModelIndex modelIndex = createIndex(row, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(row);
    notifyDerivedState(row);
}

void BlockModel::recomputeDocumentCounts()
{
    resetDocumentCounts();
    for (const Block *block : std::as_const(m_blocks))
        addBlockCounts(block);
}

void BlockModel::notifyDerivedState(int changeIndex)
{
    if (m_blocks.isEmpty() || changeIndex < 0)
        return;
    changeIndex = qBound(0, changeIndex, m_blocks.count() - 1);

    // Sub-task progress: a todo shows the run of deeper-indented todos that
    // follows it, so the row itself and every ancestor whose run reaches it
    // can have changed. Walking back stops at the first non-todo, which is
    // where every such run ends.
    const Block *changed = m_blocks.at(changeIndex);
    if (changed->blockType() == Block::Todo) {
        const QModelIndex self = createIndex(changeIndex, 0);
        emit dataChanged(self, self, {TodoProgressRole});
    }
    // Deliberately NOT bounded by the changed row's current indent: a row
    // that just became shallower (or stopped being a todo) has left the runs
    // of parents its OLD depth belonged to, and only they can tell that their
    // total dropped. Walking every preceding todo down to depth 0 notifies a
    // superset; a row that turns out to have no children just reports
    // {0, 0} again.
    int minIndent = MaxIndentLevel + 1;
    for (int p = changeIndex - 1; p >= 0 && minIndent > 0; --p) {
        const Block *candidate = m_blocks.at(p);
        if (candidate->blockType() != Block::Todo)
            break;
        const int level = candidate->indentLevel();
        if (level >= minIndent)
            continue;
        minIndent = level;
        const QModelIndex parent = createIndex(p, 0);
        emit dataChanged(parent, parent, {TodoProgressRole});
    }

    // Equation numbers count MathBlocks from the top, so only the suffix
    // after the change can renumber — and only if the document has any.
    if (m_mathBlockCount > 0 && changeIndex < m_blocks.count()) {
        emit dataChanged(createIndex(changeIndex, 0),
                         createIndex(m_blocks.count() - 1, 0),
                         {MathNumberRole});
    }

    ++m_derivedRevision;
    emit derivedRevisionChanged();
}

// Public methods that create undo commands

void BlockModel::insertBlock(int index, int type, const QString &content, int indentLevel)
{
    if (!Block::isValidType(type))
        return;

    Block::State state;
    state.type = static_cast<Block::BlockType>(type);
    state.content = content;
    state.indentLevel = Block::clampIndent(indentLevel);

    // Clamp here rather than inside insertBlockInternal alone: the command
    // has to remember the position it actually inserted at, or undo removes
    // the wrong block and redo inserts a second copy.
    const int at = qBound(0, index, m_blocks.count());

    if (m_undoStack) {
        auto cmd = std::make_unique<InsertBlockCommand>(this, at, state);
        m_undoStack->push(std::move(cmd));
    } else {
        insertBlockInternal(at, state);
    }
}

void BlockModel::removeBlock(int index)
{
    if (!isValidIndex(index))
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<RemoveBlockCommand>(this, index);
        m_undoStack->push(std::move(cmd));
    } else {
        removeBlockInternal(index);
    }
}

bool BlockModel::updateContentById(const QString &blockId,
                                   const QString &content)
{
    const int index = indexOfBlockId(blockId);
    if (index < 0)
        return false;   // the block left the model; the edit has nowhere to go
    updateContent(index, content);
    return true;
}

void BlockModel::updateContent(int index, const QString &content)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    QString oldContent = m_blocks.at(index)->content();

    // Skip if content hasn't changed
    if (oldContent == content)
        return;

    // Apply the change immediately
    updateContentInternal(index, content);

    // Create undo command (content is already applied, so execute() will be a no-op)
    if (m_undoStack) {
        auto cmd = std::make_unique<TextChangeCommand>(this, index, oldContent, content);
        m_undoStack->push(std::move(cmd));
    }
}

void BlockModel::updateType(int index, int type)
{
    if (!isValidIndex(index) || !Block::isValidType(type))
        return;

    Block::BlockType oldType = m_blocks.at(index)->blockType();
    // A no-op conversion must not push a command: it would dirty a clean
    // document and add an undo step that undoes nothing visible.
    if (oldType == static_cast<Block::BlockType>(type))
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<ChangeTypeCommand>(
            this, index, oldType, static_cast<Block::BlockType>(type));
        m_undoStack->push(std::move(cmd));
    } else {
        updateTypeInternal(index, type);
    }
}

void BlockModel::moveBlock(int fromIndex, int toIndex)
{
    // Screened before anything else: an out-of-range or same-position move is
    // a no-op, and pushing it would dirty a clean document and add an undo
    // step that undoes nothing. Nothing happened, so nothing is timed either.
    if (!isValidIndex(fromIndex) || !isValidIndex(toIndex)
        || fromIndex == toIndex)
        return;

    PerfLog::ScopedTimer perf(
        QStringLiteral("block.move"),
        QVariantMap{
            {QStringLiteral("from"), fromIndex},
            {QStringLiteral("to"), toIndex},
            {QStringLiteral("blocks"), m_blocks.count()},
        });
    if (m_undoStack) {
        auto cmd = std::make_unique<MoveBlockCommand>(this, fromIndex, toIndex);
        m_undoStack->push(std::move(cmd));
    } else {
        moveBlockInternal(fromIndex, toIndex);
    }
}

void BlockModel::setChecked(int index, bool checked)
{
    if (index < 0 || index >= m_blocks.count())
        return;
    if (m_blocks.at(index)->checked() == checked)
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<SetCheckedCommand>(this, index, checked);
        m_undoStack->push(std::move(cmd));
    } else {
        setCheckedInternal(index, checked);
    }
}

void BlockModel::setBlockAttributes(int index, const QString &attributes)
{
    if (index < 0 || index >= m_blocks.count())
        return;
    if (m_blocks.at(index)->attributes() == attributes)
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<SetBlockAttributesCommand>(this, index,
                                                               attributes);
        m_undoStack->push(std::move(cmd));
    } else {
        setAttributesInternal(index, attributes);
    }
}

void BlockModel::changeIndent(int index, int delta)
{
    Block *block = blockAt(index);
    if (!block || delta == 0)
        return;

    // Wave 1 indents the list family; quote nesting depth has no
    // list-parent constraint — just the 0..Max clamp. Every other type
    // keeps whitespace out of its markdown.
    const bool isQuote = block->blockType() == Block::Quote;
    if (!Block::isListFamily(block->blockType()) && !isQuote)
        return;

    const int current = block->indentLevel();
    int target = current + delta;

    if (delta > 0 && !isQuote) {
        // A list child may nest at most one level below the list block above
        // it (§3.3 parent-child hierarchy), within the absolute limit.
        int maxAllowed = 0;
        if (index > 0) {
            Block *prev = m_blocks.at(index - 1);
            if (Block::isListFamily(prev->blockType()))
                maxAllowed = prev->indentLevel() + 1;
        }
        target = qMin(target, qMin(maxAllowed, MaxIndentLevel));
    }
    target = qBound(0, target, MaxIndentLevel);

    if (target == current)
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<ChangeIndentCommand>(this, index, current, target);
        m_undoStack->push(std::move(cmd));
    } else {
        setIndentInternal(index, target);
    }
}

void BlockModel::convertBlock(int index, int type, const QString &content,
                              bool checked, const QString &language,
                              const QString &calloutTitle)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("block.convert"),
        QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("type"), type},
            {QStringLiteral("blocks"), m_blocks.count()},
        });
    Block *block = blockAt(index);
    if (!block || !Block::isValidType(type))
        return;

    Block::State newState;
    newState.type = static_cast<Block::BlockType>(type);
    newState.content = content;
    // Indentation only means anything on the list family; conversions
    // out of it land at the margin.
    newState.indentLevel = Block::isListFamily(newState.type) ? block->indentLevel() : 0;
    newState.checked = checked;
    newState.language = language;
    newState.calloutTitle = calloutTitle;

    const Block::State oldState = block->state();
    if (oldState.type == newState.type && oldState.content == newState.content &&
        oldState.indentLevel == newState.indentLevel &&
        oldState.checked == newState.checked && oldState.language == newState.language &&
        oldState.calloutTitle == newState.calloutTitle)
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<ConvertBlockCommand>(this, index, oldState, newState);
        m_undoStack->push(std::move(cmd));
    } else {
        applyStateInternal(index, newState);
    }
}

QVariantMap BlockModel::todoProgress(int index) const
{
    QVariantMap result{ { QStringLiteral("done"), 0 },
                        { QStringLiteral("total"), 0 } };
    const Block *parent = blockAt(index);
    if (!parent || parent->blockType() != Block::Todo)
        return result;
    const int parentIndent = parent->indentLevel();
    int done = 0, total = 0;
    for (int j = index + 1; j < m_blocks.size(); ++j) {
        const Block *c = m_blocks.at(j);
        if (c->blockType() != Block::Todo || c->indentLevel() <= parentIndent)
            break;   // a sibling, a shallower item, or a non-todo ends the run
        ++total;
        if (c->checked())
            ++done;
    }
    result.insert(QStringLiteral("done"), done);
    result.insert(QStringLiteral("total"), total);
    return result;
}

void BlockModel::setCalloutTitle(int index, const QString &title)
{
    Block *block = blockAt(index);
    if (!block || block->calloutTitle() == title)
        return;
    const Block::State oldState = block->state();
    Block::State newState = oldState;
    newState.calloutTitle = title;
    if (m_undoStack) {
        auto cmd = std::make_unique<ConvertBlockCommand>(this, index, oldState, newState);
        m_undoStack->push(std::move(cmd));
    } else {
        applyStateInternal(index, newState);
    }
}

// Index validation for the QML-invokable single-block operations. A delegate
// can hand over a stale or -1 index while it is being torn down, so every
// entry point screens its arguments here BEFORE deciding whether to push a
// command — otherwise the checks that live in the *Internal methods protect
// only the stack-less path, and an attached UndoStack turns the same bad
// index into a destructive edit.

bool BlockModel::isValidIndex(int index) const
{
    return index >= 0 && index < m_blocks.count();
}

bool BlockModel::isValidSplit(int index, int position) const
{
    if (!isValidIndex(index))
        return false;
    return position >= 0 && position <= m_blocks.at(index)->content().length();
}

bool BlockModel::isValidMerge(int keepIndex, int removeIndex) const
{
    if (!isValidIndex(keepIndex) || !isValidIndex(removeIndex))
        return false;
    // Forward merges only. Every caller folds a block into an earlier one,
    // and a backward merge cannot be undone correctly: removing the earlier
    // block shifts the kept block down, so the stored keep index no longer
    // names it.
    return removeIndex > keepIndex;
}

// Deduplication runs through a hash set, not a repeated linear scan of the
// result: a select-all copy/delete/duplicate/move hands over one index per
// block, so `contains` on a growing list made every bulk operation quadratic
// in the selection size.
QList<int> BlockModel::validIndexes(const QVariantList &indexes) const
{
    QSet<int> seen;
    seen.reserve(indexes.size());
    QList<int> result;
    result.reserve(indexes.size());
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int idx = value.toInt(&ok);
        if (ok && idx >= 0 && idx < m_blocks.count() && !seen.contains(idx)) {
            seen.insert(idx);
            result.append(idx);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void BlockModel::pushOrRun(std::unique_ptr<UndoCommand> command)
{
    if (m_undoStack)
        m_undoStack->push(std::move(command));
    else
        command->execute();
}

void BlockModel::removeBlocks(const QVariantList &indexes)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("block.bulk_delete"),
        QVariantMap{
            {QStringLiteral("requested"), indexes.size()},
            {QStringLiteral("blocksBefore"), m_blocks.count()},
        });
    const QList<int> sorted = validIndexes(indexes);
    if (sorted.isEmpty())
        return;

    // Removing every block leaves one empty paragraph: a document is
    // never blockless, and delete-all-then-type must just work.
    const bool removesAll = sorted.size() == m_blocks.size();

    if (m_undoStack)
        m_undoStack->beginMacro(QStringLiteral("Remove Blocks"));
    // Descending, so earlier removals never shift later targets; each
    // command captures its block's full state at construction.
    for (int i = sorted.size() - 1; i >= 0; --i)
        pushOrRun(std::make_unique<RemoveBlockCommand>(this, sorted.at(i)));
    if (removesAll)
        pushOrRun(std::make_unique<InsertBlockCommand>(this, 0, Block::Paragraph,
                                                       QString()));
    if (m_undoStack)
        m_undoStack->endMacro();
    perf.addContext(QStringLiteral("blocksAfter"), m_blocks.count());
}

QVariantList BlockModel::duplicateBlocks(const QVariantList &indexes)
{
    const QList<int> sorted = validIndexes(indexes);
    if (sorted.isEmpty())
        return QVariantList();

    // Clones land directly below the LAST selected block (§3.6 "directly
    // below original", generalized to a selection), keeping document
    // order. All sources sit at or above the insertion point, so their
    // indexes never shift while inserting.
    const int insertAt = sorted.last() + 1;

    if (m_undoStack)
        m_undoStack->beginMacro(QStringLiteral("Duplicate Blocks"));
    QVariantList newIndexes;
    for (int i = 0; i < sorted.size(); ++i) {
        const Block::State state = m_blocks.at(sorted.at(i))->state();
        pushOrRun(std::make_unique<InsertBlockCommand>(this, insertAt + i, state));
        newIndexes.append(insertAt + i);
    }
    if (m_undoStack)
        m_undoStack->endMacro();
    return newIndexes;
}

void BlockModel::moveBlocksBy(const QVariantList &indexes, int delta)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("block.move"),
        QVariantMap{
            {QStringLiteral("selected"), indexes.size()},
            {QStringLiteral("delta"), delta},
            {QStringLiteral("blocks"), m_blocks.count()},
        });
    if (delta != 1 && delta != -1)
        return;
    const QList<int> sorted = validIndexes(indexes);
    if (sorted.isEmpty())
        return;

    // Contiguous runs of selected blocks, ascending.
    QList<QPair<int, int>> runs; // inclusive [start, end]
    int runStart = sorted.first();
    int prev = sorted.first();
    for (int i = 1; i < sorted.size(); ++i) {
        if (sorted.at(i) == prev + 1) {
            prev = sorted.at(i);
            continue;
        }
        runs.append(qMakePair(runStart, prev));
        runStart = prev = sorted.at(i);
    }
    runs.append(qMakePair(runStart, prev));

    // A run against the document edge stops the whole operation —
    // partial movement would tear the selection apart.
    if (delta < 0 && runs.first().first == 0)
        return;
    if (delta > 0 && runs.last().second == m_blocks.count() - 1)
        return;

    if (m_undoStack)
        m_undoStack->beginMacro(QStringLiteral("Move Blocks"));
    // Moving a run one step == moving its displaced neighbor across the
    // run. Runs are disjoint, so processing up-moves ascending and
    // down-moves descending keeps every other run's indexes valid.
    if (delta < 0) {
        for (const auto &run : runs)
            pushOrRun(std::make_unique<MoveBlockCommand>(this, run.first - 1,
                                                         run.second));
    } else {
        for (int i = runs.size() - 1; i >= 0; --i)
            pushOrRun(std::make_unique<MoveBlockCommand>(this, runs.at(i).second + 1,
                                                         runs.at(i).first));
    }
    if (m_undoStack)
        m_undoStack->endMacro();
}

void BlockModel::changeIndentForBlocks(const QVariantList &indexes, int delta)
{
    const QList<int> sorted = validIndexes(indexes);
    if (sorted.isEmpty() || delta == 0)
        return;

    // changeIndent() itself skips non-list blocks and applies the §3.3
    // clamps per block; sequential ascending application matches what
    // per-block Tab presses would do. An all-no-op macro pushes nothing.
    if (m_undoStack)
        m_undoStack->beginMacro(QStringLiteral("Indent Blocks"));
    for (int idx : sorted)
        changeIndent(idx, delta);
    if (m_undoStack)
        m_undoStack->endMacro();
}

void BlockModel::previewMoveBlock(int fromIndex, int toIndex)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("block.move"),
        QVariantMap{
            {QStringLiteral("preview"), true},
            {QStringLiteral("from"), fromIndex},
            {QStringLiteral("to"), toIndex},
            {QStringLiteral("blocks"), m_blocks.count()},
        },
        PerfLog::Verbose);
    moveBlockInternal(fromIndex, toIndex);
}

void BlockModel::commitDragMove(int originalFrom, int finalTo)
{
    if (originalFrom == finalTo)
        return;
    if (originalFrom < 0 || originalFrom >= m_blocks.count()
        || finalTo < 0 || finalTo >= m_blocks.count())
        return;
    // The preview moves already applied the final order; without a
    // stack there is nothing left to do.
    if (m_undoStack) {
        m_undoStack->push(std::make_unique<MoveBlockCommand>(
            this, originalFrom, finalTo, /*preApplied=*/true));
    }
}

void BlockModel::moveBlocksTo(const QVariantList &indexes, int targetGap)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("block.move"),
        QVariantMap{
            {QStringLiteral("selected"), indexes.size()},
            {QStringLiteral("targetGap"), targetGap},
            {QStringLiteral("blocks"), m_blocks.count()},
        });
    const QList<int> sorted = validIndexes(indexes);
    if (sorted.isEmpty())
        return;
    targetGap = qBound(0, targetGap, static_cast<int>(m_blocks.count()));

    // Plan one range move per contiguous selected run instead of deriving
    // single moves through repeated linear scans. Moves keep block
    // identity, so ids, delegates, and any selection survive the drop;
    // the whole drop is one undo step.
    const QList<RangeMove> moves = planGroupMove(sorted, targetGap);
    if (moves.isEmpty())
        return;

    pushOrRun(std::make_unique<MoveBlocksCommand>(this, moves));
}

QList<BlockModel::RangeMove> BlockModel::planGroupMove(const QList<int> &sorted,
                                                       int gap) const
{
    // Maximal contiguous runs, split at the gap so each run lies wholly
    // on one side of it.
    QList<QPair<int, int>> runs; // inclusive [start, end]
    int runStart = sorted.first();
    int prev = sorted.first();
    for (int i = 1; i <= sorted.size(); ++i) {
        if (i < sorted.size() && sorted.at(i) == prev + 1) {
            prev = sorted.at(i);
            continue;
        }
        if (runStart < gap && prev >= gap) {
            runs.append(qMakePair(runStart, gap - 1));
            runs.append(qMakePair(gap, prev));
        } else {
            runs.append(qMakePair(runStart, prev));
        }
        if (i < sorted.size())
            runStart = prev = sorted.at(i);
    }

    // Left runs collect right-to-left to sit immediately left of the
    // gap; each move touches only rows between the run and the gap, so
    // the remaining (more-left) runs keep their coordinates. Right runs
    // then collect left-to-right from the gap by the mirrored argument.
    QList<RangeMove> moves;
    int leftEdge = gap;
    for (int i = runs.size() - 1; i >= 0; --i) {
        const int s = runs.at(i).first;
        const int e = runs.at(i).second;
        if (e >= gap)
            continue;
        const int len = e - s + 1;
        if (e + 1 != leftEdge)
            moves.append({s, e, leftEdge});
        leftEdge -= len;
    }
    int rightEdge = gap;
    for (const auto &run : runs) {
        const int s = run.first;
        const int e = run.second;
        if (s < gap)
            continue;
        const int len = e - s + 1;
        if (s != rightEdge)
            moves.append({s, e, rightEdge});
        rightEdge += len;
    }
    return moves;
}

QVariantMap BlockModel::removeTextRange(int startIndex, int startMd,
                                        int endIndex, int endMd)
{
    QVariantMap result;
    if (startIndex < 0 || endIndex >= m_blocks.count() || startIndex > endIndex)
        return result;

    Block *first = m_blocks.at(startIndex);
    Block *last = m_blocks.at(endIndex);
    const QString firstContent = first->content();
    const QString lastContent = last->content();
    const int from = qBound(0, startMd, static_cast<int>(firstContent.length()));
    const int to = qBound(0, endMd, static_cast<int>(lastContent.length()));

    if (startIndex == endIndex) {
        if (from >= to)
            return result;
        const QString newContent = firstContent.left(from) + firstContent.mid(to);
        // Content is applied first; TextChangeCommand's first execute is
        // a no-op by design (the pre-applied pattern updateContent uses).
        // The macro wrap keeps it out of the typing merge window.
        if (m_undoStack)
            m_undoStack->beginMacro(QStringLiteral("Delete Selection"));
        updateContentInternal(startIndex, newContent);
        pushOrRun(std::make_unique<TextChangeCommand>(this, startIndex,
                                                      firstContent, newContent));
        if (m_undoStack) {
            m_undoStack->endMacro();
            m_undoStack->breakMerge();
        }
        result[QStringLiteral("index")] = startIndex;
        result[QStringLiteral("cursor")] = from;
        return result;
    }

    if (m_undoStack)
        m_undoStack->beginMacro(QStringLiteral("Delete Selection"));
    if (first->blockType() == Block::Divider) {
        // Decision 7's divider exception: a divider holds no text, so
        // the remainder keeps the LAST block's identity instead.
        const QString newContent = lastContent.mid(to);
        updateContentInternal(endIndex, newContent);
        pushOrRun(std::make_unique<TextChangeCommand>(this, endIndex,
                                                      lastContent, newContent));
        for (int i = endIndex - 1; i >= startIndex; --i)
            pushOrRun(std::make_unique<RemoveBlockCommand>(this, i));
        result[QStringLiteral("cursor")] = 0;
    } else {
        const QString newContent = firstContent.left(from) + lastContent.mid(to);
        updateContentInternal(startIndex, newContent);
        pushOrRun(std::make_unique<TextChangeCommand>(this, startIndex,
                                                      firstContent, newContent));
        for (int i = endIndex; i > startIndex; --i)
            pushOrRun(std::make_unique<RemoveBlockCommand>(this, i));
        result[QStringLiteral("cursor")] = from;
    }
    if (m_undoStack) {
        m_undoStack->endMacro();
        m_undoStack->breakMerge();
    }
    result[QStringLiteral("index")] = startIndex;
    return result;
}

int BlockModel::mathNumber(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return 0;
    if (m_derivedOrderDirty)
        rebuildDerivedOrder();
    return m_mathNumberCache.at(index);
}

int BlockModel::ordinalAt(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return 0;
    if (m_derivedOrderDirty)
        rebuildDerivedOrder();
    return m_ordinalCache.at(index);
}

// One forward pass replaces the per-row backward run scan: per indent
// level a running counter increments through a numbered run and resets
// when the run breaks — a non-list block resets every level, a block at
// level L resets the deeper levels, and a non-numbered list block at L
// resets level L itself.
void BlockModel::rebuildDerivedOrder() const
{
    const int count = m_blocks.count();
    m_ordinalCache.resize(count);
    m_mathNumberCache.resize(count);

    int counters[MaxIndentLevel + 1] = {0};
    int mathCount = 0;
    for (int i = 0; i < count; ++i) {
        const Block *block = m_blocks.at(i);
        const Block::BlockType type = block->blockType();

        m_mathNumberCache[i] = type == Block::MathBlock ? ++mathCount : 0;

        if (!Block::isListFamily(type)) {
            for (int lv = 0; lv <= MaxIndentLevel; ++lv)
                counters[lv] = 0;
            m_ordinalCache[i] = 0;
            continue;
        }
        const int level = qBound(0, block->indentLevel(), MaxIndentLevel);
        for (int lv = level + 1; lv <= MaxIndentLevel; ++lv)
            counters[lv] = 0;
        if (type == Block::NumberedList) {
            m_ordinalCache[i] = ++counters[level];
        } else {
            counters[level] = 0;
            m_ordinalCache[i] = 0;
        }
    }
    m_derivedOrderDirty = false;
}

void BlockModel::splitBlock(int index, int position)
{
    if (!isValidSplit(index, position))
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<SplitBlockCommand>(this, index, position);
        m_undoStack->push(std::move(cmd));
    } else {
        splitBlockInternal(index, position);
    }
}

void BlockModel::mergeBlocks(int keepIndex, int removeIndex)
{
    if (!isValidMerge(keepIndex, removeIndex))
        return;

    if (m_undoStack) {
        auto cmd = std::make_unique<MergeBlocksCommand>(this, keepIndex, removeIndex);
        m_undoStack->push(std::move(cmd));
    } else {
        mergeBlocksInternal(keepIndex, removeIndex);
    }
}

// Internal methods (for undo commands to use - bypass undo stack)

void BlockModel::insertBlockInternal(int index, int type, const QString &content)
{
    Block::State state;
    state.type = Block::typeFromInt(type);
    state.content = content;
    insertBlockInternal(index, state);
}

void BlockModel::insertBlockInternal(int index, const Block::State &rawState)
{
    InternalChangeScope scope(this);
    const Block::State state = Block::sanitized(rawState);
    index = qBound(0, index, m_blocks.count());
    const QList<int> tocBefore = m_tocBlockIndexes;

    beginInsertRows(QModelIndex(), index, index);
    Block *block = new Block(state.type, state.content, this);
    block->setIndentLevel(state.indentLevel);
    block->setChecked(state.checked);
    block->setLanguage(state.language);
    block->setCalloutTitle(state.calloutTitle);
    block->setAttributes(state.attributes);
    watchBlock(block);
    m_blocks.insert(index, block);
    invalidateIdIndex();
    invalidateDerivedOrder();
    for (int &tocIndex : m_tocBlockIndexes) {
        if (tocIndex >= index)
            ++tocIndex;
    }
    if (isTocBlock(block)) {
        const auto it = std::lower_bound(m_tocBlockIndexes.begin(),
                                         m_tocBlockIndexes.end(), index);
        m_tocBlockIndexes.insert(it, index);
    }
    addBlockCounts(block);
    endInsertRows();

    emit countChanged();
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(index);
    notifyDerivedState(index);
}

void BlockModel::removeBlockInternal(int index)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    const QList<int> tocBefore = m_tocBlockIndexes;
    subtractBlockCounts(m_blocks.at(index));
    beginRemoveRows(QModelIndex(), index, index);
    delete m_blocks.takeAt(index);
    invalidateIdIndex();
    invalidateDerivedOrder();
    m_tocBlockIndexes.removeAll(index);
    for (int &tocIndex : m_tocBlockIndexes) {
        if (tocIndex > index)
            --tocIndex;
    }
    endRemoveRows();

    emit countChanged();
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(qMin(index, m_blocks.count() - 1));
    notifyDerivedState(qMin(index, m_blocks.count() - 1));
}

void BlockModel::updateContentInternal(int index, const QString &content)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    Block *block = m_blocks.at(index);
    const int oldKind = delegateKindForContent(block->blockType(),
                                               block->language(), block->content());
    adjustBlockCountsBeforeChange(block);
    block->setContent(content);
    adjustBlockCountsAfterChange(block);
    QVector<int> roles{ContentRole, DisplayTextRole};
    if (delegateKindForContent(block->blockType(), block->language(), content)
        != oldKind)
        roles.append(DelegateKindRole);
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

void BlockModel::updateContentSilently(int index, const QString &content)
{
    // Derived-state regeneration (the TOC fence): bypass the undo stack.
    updateContentInternal(index, content);
}

void BlockModel::updateTypeInternal(int index, int type)
{
    if (index < 0 || index >= m_blocks.count() || !Block::isValidType(type))
        return;

    InternalChangeScope scope(this);
    Block *block = m_blocks.at(index);
    const QList<int> tocBefore = m_tocBlockIndexes;
    const int oldKind = delegateKindForContent(block->blockType(),
                                               block->language(),
                                               block->content());
    adjustBlockCountsBeforeChange(block);
    block->setBlockType(static_cast<Block::BlockType>(type));
    invalidateDerivedOrder();
    adjustBlockCountsAfterChange(block);
    refreshTocBlockIndex(index);

    QVector<int> roles{BlockTypeRole, DisplayTextRole};
    if (delegateKindForContent(block->blockType(), block->language(),
                               block->content()) != oldKind)
        roles.append(DelegateKindRole);

    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(index);
    notifyDerivedState(index);
}

void BlockModel::moveBlockInternal(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_blocks.count())
        return;
    if (toIndex < 0 || toIndex >= m_blocks.count())
        return;
    if (fromIndex == toIndex)
        return;

    int destIndex = toIndex > fromIndex ? toIndex + 1 : toIndex;
    const QList<int> tocBefore = m_tocBlockIndexes;

    beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(), destIndex);
    m_blocks.move(fromIndex, toIndex);
    reindexIdRange(qMin(fromIndex, toIndex), qMax(fromIndex, toIndex));
    invalidateDerivedOrder();
    for (int &tocIndex : m_tocBlockIndexes) {
        if (tocIndex == fromIndex) {
            tocIndex = toIndex;
        } else if (fromIndex < toIndex) {
            if (tocIndex > fromIndex && tocIndex <= toIndex)
                --tocIndex;
        } else {
            if (tocIndex >= toIndex && tocIndex < fromIndex)
                ++tocIndex;
        }
    }
    std::sort(m_tocBlockIndexes.begin(), m_tocBlockIndexes.end());
    endMoveRows();

    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(qMin(fromIndex, m_blocks.count() - 1));
    emitOrdinalsChangedFrom(qMin(toIndex, m_blocks.count() - 1));
    notifyDerivedState(qMin(fromIndex, toIndex));
}

void BlockModel::moveBlocksRangeInternal(int first, int last, int dest)
{
    const int count = m_blocks.count();
    if (first < 0 || last < first || last >= count)
        return;
    if (dest < 0 || dest > count)
        return;
    // beginMoveRows contract: a destination inside [first, last+1] is
    // invalid (and a no-op arrangement anyway).
    if (dest >= first && dest <= last + 1)
        return;

    const int len = last - first + 1;
    const int insertAt = dest > last ? dest - len : dest;
    const QList<int> tocBefore = m_tocBlockIndexes;

    beginMoveRows(QModelIndex(), first, last, QModelIndex(), dest);
    const QList<Block*> slice = m_blocks.mid(first, len);
    m_blocks.remove(first, len);
    for (int i = 0; i < len; ++i)
        m_blocks.insert(insertAt + i, slice.at(i));
    reindexIdRange(qMin(first, insertAt), qMax(last, insertAt + len - 1));
    invalidateDerivedOrder();
    for (int &tocIndex : m_tocBlockIndexes) {
        if (tocIndex >= first && tocIndex <= last) {
            tocIndex = insertAt + (tocIndex - first);
        } else if (dest > last) {
            if (tocIndex > last && tocIndex < dest)
                tocIndex -= len;
        } else {
            if (tocIndex >= dest && tocIndex < first)
                tocIndex += len;
        }
    }
    std::sort(m_tocBlockIndexes.begin(), m_tocBlockIndexes.end());
    endMoveRows();

    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(qMin(first, insertAt));
    emitOrdinalsChangedFrom(qMax(last, insertAt + len - 1));
    notifyDerivedState(qMin(first, insertAt));
}

void BlockModel::setCheckedInternal(int index, bool checked)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    m_blocks.at(index)->setChecked(checked);
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {CheckedRole});
    notifyDerivedState(index);
}

void BlockModel::setAttributesInternal(int index, const QString &attributes)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    m_blocks.at(index)->setAttributes(attributes);
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {AttributesRole});
}

void BlockModel::setIndentInternal(int index, int level)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    m_blocks.at(index)->setIndentLevel(level);
    invalidateDerivedOrder();
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {IndentLevelRole});
    emitOrdinalsChangedFrom(index);
    notifyDerivedState(index);
}

void BlockModel::applyStateInternal(int index, const Block::State &state)
{
    if (index < 0 || index >= m_blocks.count())
        return;

    InternalChangeScope scope(this);
    Block *block = m_blocks.at(index);
    const QList<int> tocBefore = m_tocBlockIndexes;
    const int oldKind = delegateKindForContent(block->blockType(),
                                               block->language(),
                                               block->content());
    adjustBlockCountsBeforeChange(block);
    block->setState(state);
    invalidateDerivedOrder();
    adjustBlockCountsAfterChange(block);
    refreshTocBlockIndex(index);

    // Explicit role list: an all-roles dataChanged would include
    // DelegateKindRole and needlessly recreate the delegate on
    // same-kind conversions.
    QVector<int> roles{BlockTypeRole, ContentRole, IndentLevelRole,
                       CheckedRole, LanguageRole, CalloutTitleRole,
                       AttributesRole, DisplayTextRole};
    if (delegateKindForContent(block->blockType(), block->language(),
                               block->content()) != oldKind)
        roles.append(DelegateKindRole);

    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalsChangedFrom(index);
    notifyDerivedState(index);
}

// Helper methods for internal split/merge

void BlockModel::splitBlockInternal(int index, int position)
{
    if (!isValidSplit(index, position))
        return;

    InternalChangeScope scope(this);
    Block *block = m_blocks.at(index);
    QString content = block->content();

    QString before = content.left(position);
    QString after = content.mid(position);

    // Update current block with text before cursor
    adjustBlockCountsBeforeChange(block);
    block->setContent(before);
    adjustBlockCountsAfterChange(block);
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {ContentRole, DisplayTextRole});

    // Insert new block with text after cursor. It inherits type, indent,
    // and language; a split todo starts unchecked.
    Block::State state;
    state.type = block->blockType();
    state.content = after;
    state.indentLevel = block->indentLevel();
    state.language = block->language();
    insertBlockInternal(index + 1, state);
}

void BlockModel::mergeBlocksInternal(int keepIndex, int removeIndex)
{
    if (!isValidMerge(keepIndex, removeIndex))
        return;

    InternalChangeScope scope(this);
    Block *keepBlock = m_blocks.at(keepIndex);
    Block *removeBlockPtr = m_blocks.at(removeIndex);

    QString mergedContent = keepBlock->content() + removeBlockPtr->content();

    // Update the keep block with merged content
    adjustBlockCountsBeforeChange(keepBlock);
    keepBlock->setContent(mergedContent);
    adjustBlockCountsAfterChange(keepBlock);
    QModelIndex modelIndex = createIndex(keepIndex, 0);
    emit dataChanged(modelIndex, modelIndex, {ContentRole, DisplayTextRole});

    // Remove the other block
    removeBlockInternal(removeIndex);
}

Block* BlockModel::blockAt(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return nullptr;
    return m_blocks.at(index);
}

int BlockModel::count() const
{
    return m_blocks.count();
}

void BlockModel::initializeWithSampleData()
{
    InternalChangeScope scope(this);
    const QList<int> tocBefore = m_tocBlockIndexes;
    beginResetModel();

    qDeleteAll(m_blocks);
    m_blocks.clear();
    invalidateIdIndex();
    invalidateDerivedOrder();
    m_tocBlockIndexes.clear();
    resetDocumentCounts();

    m_blocks.append(new Block(Block::Heading1, "Welcome to Kvit Notes", this));
    m_blocks.append(new Block(Block::Paragraph, "This is a paragraph block. Try clicking here and typing some text.", this));
    m_blocks.append(new Block(Block::Heading2, "Getting Started", this));
    m_blocks.append(new Block(Block::Paragraph, "Each block is independent. You can edit them separately.", this));
    m_blocks.append(new Block(Block::Paragraph, "More blocks can be added later with the Enter key.", this));
    for (Block *block : std::as_const(m_blocks)) {
        addBlockCounts(block);
        watchBlock(block);
    }

    endResetModel();
    emit countChanged();
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    notifyDerivedState(0);
}

QString BlockModel::getContent(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return QString();
    return m_blocks.at(index)->content();
}

QString BlockModel::getAttributes(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return QString();
    return m_blocks.at(index)->attributes();
}

QString BlockModel::displayTextAt(int index) const
{
    Block *block = blockAt(index);
    return block ? block->displayText() : QString();
}

int BlockModel::wordCountAt(int index) const
{
    Block *block = blockAt(index);
    return block ? block->wordCount() : 0;
}

int BlockModel::charCountAt(int index, bool withSpaces) const
{
    Block *block = blockAt(index);
    return block ? block->charCount(withSpaces) : 0;
}

int BlockModel::indexOfBlockId(const QString &blockId) const
{
    if (blockId.isEmpty())
        return -1;
    if (m_idIndexDirty) {
        m_idIndex.clear();
        m_idIndex.reserve(m_blocks.size());
        for (int i = 0; i < m_blocks.size(); ++i)
            m_idIndex.insert(m_blocks.at(i)->blockId(), i);
        m_idIndexDirty = false;
    }
    return m_idIndex.value(blockId, -1);
}

void BlockModel::reindexIdRange(int first, int last)
{
    if (m_idIndexDirty)
        return;
    first = qMax(0, first);
    last = qMin(last, static_cast<int>(m_blocks.size()) - 1);
    for (int i = first; i <= last; ++i)
        m_idIndex.insert(m_blocks.at(i)->blockId(), i);
}

QVariantList BlockModel::tocBlockIndexes() const
{
    QVariantList result;
    result.reserve(m_tocBlockIndexes.size());
    for (int index : m_tocBlockIndexes)
        result.append(index);
    return result;
}

void BlockModel::clear()
{
    if (m_blocks.isEmpty()) return;

    InternalChangeScope scope(this);
    const QList<int> tocBefore = m_tocBlockIndexes;
    beginResetModel();
    qDeleteAll(m_blocks);
    m_blocks.clear();
    invalidateIdIndex();
    invalidateDerivedOrder();
    m_tocBlockIndexes.clear();
    resetDocumentCounts();
    endResetModel();

    emit countChanged();
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    ++m_derivedRevision;
    emit derivedRevisionChanged();
}

void BlockModel::replaceAllBlocksInternal(const QList<Block::State> &states)
{
    InternalChangeScope scope(this);
    const QList<int> tocBefore = m_tocBlockIndexes;

    beginResetModel();
    qDeleteAll(m_blocks);
    m_blocks.clear();
    invalidateIdIndex();
    invalidateDerivedOrder();
    m_tocBlockIndexes.clear();
    resetDocumentCounts();

    m_blocks.reserve(states.size());
    for (const Block::State &rawState : states) {
        const Block::State state = Block::sanitized(rawState);
        Block *block = new Block(state.type, state.content, this);
        block->setIndentLevel(state.indentLevel);
        block->setChecked(state.checked);
        block->setLanguage(state.language);
        block->setCalloutTitle(state.calloutTitle);
        block->setAttributes(state.attributes);
        watchBlock(block);
        m_blocks.append(block);
        addBlockCounts(block);
        if (isTocBlock(block))
            m_tocBlockIndexes.append(m_blocks.size() - 1);
    }

    endResetModel();

    emit countChanged();
    emit documentCountsChanged();
    emitTocBlockIndexesChangedIfNeeded(tocBefore);
    emitOrdinalRoleChanged(0, m_blocks.count() - 1);
    notifyDerivedState(0);
}

void BlockModel::addBlockCounts(const Block *block)
{
    if (!block)
        return;
    if (block->blockType() == Block::MathBlock)
        ++m_mathBlockCount;
    m_documentWordCount += block->wordCount();
    m_documentCharCount += block->charCount(true);
    m_documentCharsNoSpaces += block->charCount(false);
    if (block->wordCount() > 0)
        ++m_documentParagraphCount;
}

void BlockModel::subtractBlockCounts(const Block *block)
{
    if (!block)
        return;
    if (block->blockType() == Block::MathBlock)
        --m_mathBlockCount;
    m_documentWordCount -= block->wordCount();
    m_documentCharCount -= block->charCount(true);
    m_documentCharsNoSpaces -= block->charCount(false);
    if (block->wordCount() > 0)
        --m_documentParagraphCount;
}

void BlockModel::adjustBlockCountsBeforeChange(const Block *block)
{
    subtractBlockCounts(block);
}

void BlockModel::adjustBlockCountsAfterChange(const Block *block)
{
    addBlockCounts(block);
    emit documentCountsChanged();
}

void BlockModel::resetDocumentCounts()
{
    m_documentWordCount = 0;
    m_documentCharCount = 0;
    m_documentCharsNoSpaces = 0;
    m_documentParagraphCount = 0;
    m_mathBlockCount = 0;
}

void BlockModel::emitOrdinalsChangedFrom(int changeIndex)
{
    if (m_blocks.isEmpty() || changeIndex < 0)
        return;
    changeIndex = qBound(0, changeIndex, m_blocks.count() - 1);
    emitOrdinalRoleChanged(changeIndex, ordinalChangeEndFrom(changeIndex));
}

void BlockModel::emitOrdinalRoleChanged(int first, int last)
{
    if (m_blocks.isEmpty())
        return;
    first = qBound(0, first, m_blocks.count() - 1);
    last = qBound(0, last, m_blocks.count() - 1);
    if (first > last)
        return;
    emit dataChanged(createIndex(first, 0), createIndex(last, 0), {OrdinalRole});
}

int BlockModel::ordinalChangeEndFrom(int changeIndex) const
{
    if (m_blocks.isEmpty())
        return -1;
    changeIndex = qBound(0, changeIndex, m_blocks.count() - 1);
    const Block *block = m_blocks.at(changeIndex);
    if (Block::isListFamily(block->blockType()))
        return listFamilyRunEnd(changeIndex);
    if (changeIndex + 1 < m_blocks.count()
        && Block::isListFamily(m_blocks.at(changeIndex + 1)->blockType()))
        return listFamilyRunEnd(changeIndex + 1);
    return changeIndex;
}

int BlockModel::listFamilyRunEnd(int index) const
{
    if (index < 0 || index >= m_blocks.count())
        return -1;
    int end = index;
    while (end + 1 < m_blocks.count()
           && Block::isListFamily(m_blocks.at(end + 1)->blockType())) {
        ++end;
    }
    return end;
}

bool BlockModel::isTocBlock(const Block *block) const
{
    return block && block->blockType() == Block::CodeBlock
           && block->language() == QLatin1String("toc");
}

void BlockModel::refreshTocBlockIndex(int index)
{
    if (index < 0 || index >= m_blocks.size())
        return;

    const bool contains = m_tocBlockIndexes.contains(index);
    const bool toc = isTocBlock(m_blocks.at(index));
    if (toc == contains)
        return;
    if (toc) {
        const auto it = std::lower_bound(m_tocBlockIndexes.begin(),
                                         m_tocBlockIndexes.end(), index);
        m_tocBlockIndexes.insert(it, index);
    } else {
        m_tocBlockIndexes.removeAll(index);
    }
}

void BlockModel::emitTocBlockIndexesChangedIfNeeded(const QList<int> &before)
{
    if (before != m_tocBlockIndexes)
        emit tocBlockIndexesChanged();
}
