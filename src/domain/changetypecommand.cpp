// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "changetypecommand.h"
#include "blockmodel.h"

ChangeTypeCommand::ChangeTypeCommand(BlockModel *model, int index,
                                     Block::BlockType oldType, Block::BlockType newType)
    : UndoCommand(model, "Change Block Type")
    , m_index(index)
    , m_oldType(oldType)
    , m_newType(newType)
{
}

void ChangeTypeCommand::execute()
{
    m_model->updateTypeInternal(m_index, static_cast<int>(m_newType));
}

void ChangeTypeCommand::undo()
{
    m_model->updateTypeInternal(m_index, static_cast<int>(m_oldType));
}
