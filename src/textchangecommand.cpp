// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "textchangecommand.h"
#include "blockmodel.h"
#include <QtMath>

TextChangeCommand::TextChangeCommand(BlockModel *model, int blockIndex,
                                     const QString &oldContent, const QString &newContent,
                                     int oldCursorPos, int newCursorPos)
    : UndoCommand(model, "Text Change")
    , m_blockIndex(blockIndex)
    , m_oldLength(oldContent.length())
    , m_newLength(newContent.length())
    , m_oldCursorPos(oldCursorPos)
    , m_newCursorPos(newCursorPos)
{
    // Trim the shared prefix and suffix so only the splice is stored.
    int prefix = 0;
    const int maxPrefix = qMin(m_oldLength, m_newLength);
    while (prefix < maxPrefix && oldContent.at(prefix) == newContent.at(prefix))
        ++prefix;
    int suffix = 0;
    const int maxSuffix = maxPrefix - prefix;
    while (suffix < maxSuffix
           && oldContent.at(m_oldLength - 1 - suffix)
                  == newContent.at(m_newLength - 1 - suffix))
        ++suffix;
    m_offset = prefix;
    m_removed = oldContent.mid(prefix, m_oldLength - prefix - suffix);
    m_inserted = newContent.mid(prefix, m_newLength - prefix - suffix);
}

QString TextChangeCommand::reconstructOld() const
{
    // Current content is the command's "after" state.
    const QString current = m_model->getContent(m_blockIndex);
    return current.left(m_offset) + m_removed
        + current.mid(m_offset + m_inserted.length());
}

QString TextChangeCommand::reconstructNew() const
{
    // Current content is the command's "before" state.
    const QString current = m_model->getContent(m_blockIndex);
    return current.left(m_offset) + m_inserted
        + current.mid(m_offset + m_removed.length());
}

void TextChangeCommand::execute()
{
    // First execution: content was already applied by BlockModel::updateContent()
    // We just mark it as executed for redo
    if (!m_executed) {
        m_executed = true;
        return;
    }

    // Redo: apply the new content
    m_model->updateContentInternal(m_blockIndex, reconstructNew());
}

void TextChangeCommand::undo()
{
    m_model->updateContentInternal(m_blockIndex, reconstructOld());
}

void TextChangeCommand::redo()
{
    m_model->updateContentInternal(m_blockIndex, reconstructNew());
}

bool TextChangeCommand::canMergeWith(const UndoCommand *other) const
{
    if (other->type() != Type::TextChange) return false;

    const auto *textCmd = static_cast<const TextChangeCommand*>(other);

    // Can only merge changes to same block
    if (textCmd->m_blockIndex != m_blockIndex) return false;

    // Don't merge if it would create a large change
    // (User might want granular undo for large pastes)
    int charDiff = qAbs(textCmd->m_newLength - textCmd->m_oldLength);
    if (charDiff > 20) {
        return false;
    }

    // Don't merge if the other edit doesn't start from our result
    // (This catches cases where something else modified the block)
    if (textCmd->m_oldLength != m_newLength) return false;

    // Diff composition needs the two splices to overlap or abut in the
    // intermediate content; disjoint edits keep their own undo steps.
    const int ourStart = m_offset;
    const int ourEnd = m_offset + m_inserted.length();
    const int theirStart = textCmd->m_offset;
    const int theirEnd = textCmd->m_offset + textCmd->m_removed.length();
    return theirStart <= ourEnd && theirEnd >= ourStart;
}

void TextChangeCommand::mergeWith(const UndoCommand *other)
{
    const auto *textCmd = static_cast<const TextChangeCommand*>(other);

    // Compose the two splices. In the intermediate content our insert
    // spans [o1, o1+|i1|) and their removal spans [o2, o2+|r2|); the
    // merge precondition guarantees the spans overlap or abut, so every
    // piece of the combined region comes from a stored fragment.
    const int o1 = m_offset;
    const int i1len = m_inserted.length();
    const int r1len = m_removed.length();
    const int o2 = textCmd->m_offset;
    const int r2len = textCmd->m_removed.length();

    const int start = qMin(o1, o2);
    const int end = qMax(o1 + i1len, o2 + r2len);

    // Combined removed text, in OLD coordinates: old[start, end - i1len
    // + r1len). Left of o1 the intermediate equals old, and that stretch
    // lies inside their removal span; right of our insert, old text maps
    // through the r1/i1 length shift into their removal span.
    QString removed;
    if (start < o1)
        removed += textCmd->m_removed.left(o1 - start);
    removed += m_removed;
    if (end > o1 + i1len)
        removed += textCmd->m_removed.mid(o1 + i1len - o2);

    // Combined inserted text: the intermediate span [start, end) with
    // their splice applied. Outside their removal span the intermediate
    // text comes from our insert.
    QString inserted;
    if (start < o2)
        inserted += m_inserted.left(o2 - o1);
    inserted += textCmd->m_inserted;
    if (end > o2 + r2len)
        inserted += m_inserted.mid(o2 + r2len - o1);

    m_offset = start;
    m_removed = removed;
    m_inserted = inserted;
    m_newLength = textCmd->m_newLength;
    m_newCursorPos = textCmd->m_newCursorPos;

    // Re-trim: composed splices can regain a shared prefix/suffix (type
    // then delete the same character).
    int prefix = 0;
    const int maxPrefix = qMin(m_removed.length(), m_inserted.length());
    while (prefix < maxPrefix && m_removed.at(prefix) == m_inserted.at(prefix))
        ++prefix;
    int suffix = 0;
    const int maxSuffix = maxPrefix - prefix;
    while (suffix < maxSuffix
           && m_removed.at(m_removed.length() - 1 - suffix)
                  == m_inserted.at(m_inserted.length() - 1 - suffix))
        ++suffix;
    if (prefix > 0 || suffix > 0) {
        m_offset += prefix;
        m_removed = m_removed.mid(prefix, m_removed.length() - prefix - suffix);
        m_inserted = m_inserted.mid(prefix, m_inserted.length() - prefix - suffix);
    }

    // Update timestamp to extend merge window
    m_timestamp = textCmd->timestamp();
}
