// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "removeblockcommand.h"
#include "blockmodel.h"

RemoveBlockCommand::RemoveBlockCommand(BlockModel *model, int index)
    : UndoCommand(model, "Remove Block")
    , m_index(index)
{
    // Capture full block state before removal
    Block *block = model->blockAt(index);
    if (block) {
        m_state = block->state();
        m_blockId = block->blockId();
    }
}

void RemoveBlockCommand::execute()
{
    m_model->removeBlockInternal(m_index);
}

void RemoveBlockCommand::undo()
{
    m_model->insertBlockInternal(m_index, m_state);
}
