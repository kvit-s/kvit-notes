// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mergeblockscommand.h"
#include "blockmodel.h"

MergeBlocksCommand::MergeBlocksCommand(BlockModel *model, int keepIndex, int removeIndex)
    : UndoCommand(model, "Merge Blocks")
    , m_keepIndex(keepIndex)
    , m_removeIndex(removeIndex)
{
    // Capture state of both blocks
    Block *keepBlock = model->blockAt(keepIndex);
    Block *removeBlock = model->blockAt(removeIndex);

    if (keepBlock) {
        m_keepContent = keepBlock->content();
    }
    if (removeBlock) {
        m_removeState = removeBlock->state();
    }
}

void MergeBlocksCommand::execute()
{
    QString mergedContent = m_keepContent + m_removeState.content;

    // Update keep block with merged content
    m_model->updateContentInternal(m_keepIndex, mergedContent);

    // Remove the merged block
    m_model->removeBlockInternal(m_removeIndex);
}

void MergeBlocksCommand::undo()
{
    // Restore keep block to original content
    m_model->updateContentInternal(m_keepIndex, m_keepContent);

    // Re-insert the removed block with its full original state
    m_model->insertBlockInternal(m_removeIndex, m_removeState);
}
