// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "insertblockcommand.h"
#include "blockmodel.h"

InsertBlockCommand::InsertBlockCommand(BlockModel *model, int index,
                                       Block::BlockType type, const QString &content)
    : UndoCommand(model, "Insert Block")
    , m_index(index)
{
    m_state.type = type;
    m_state.content = content;
}

InsertBlockCommand::InsertBlockCommand(BlockModel *model, int index,
                                       const Block::State &state)
    : UndoCommand(model, "Insert Block")
    , m_index(index)
    , m_state(state)
{
}

void InsertBlockCommand::execute()
{
    m_model->insertBlockInternal(m_index, m_state);

    // Store block ID for consistent restoration
    Block *block = m_model->blockAt(m_index);
    if (block) {
        m_blockId = block->blockId();
    }
}

void InsertBlockCommand::undo()
{
    m_model->removeBlockInternal(m_index);
}
