// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "splitblockcommand.h"
#include "blockmodel.h"

SplitBlockCommand::SplitBlockCommand(BlockModel *model, int index, int position)
    : UndoCommand(model, "Split Block")
    , m_index(index)
    , m_position(position)
{
    // Capture original state
    Block *block = model->blockAt(index);
    if (block) {
        m_originalState = block->state();
    }
}

void SplitBlockCommand::execute()
{
    QString before = m_originalState.content.left(m_position);
    QString after = m_originalState.content.mid(m_position);

    // Update original block
    m_model->updateContentInternal(m_index, before);

    // Insert new block. It inherits type, indent, and language; a split
    // todo starts unchecked.
    Block::State state;
    state.type = m_originalState.type;
    state.content = after;
    state.indentLevel = m_originalState.indentLevel;
    state.language = m_originalState.language;
    m_model->insertBlockInternal(m_index + 1, state);
}

void SplitBlockCommand::undo()
{
    // Remove the split-off block
    m_model->removeBlockInternal(m_index + 1);

    // Restore original content
    m_model->updateContentInternal(m_index, m_originalState.content);
}
