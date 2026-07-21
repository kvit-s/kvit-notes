// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "changeindentcommand.h"
#include "blockmodel.h"

ChangeIndentCommand::ChangeIndentCommand(BlockModel *model, int index,
                                         int oldLevel, int newLevel)
    : UndoCommand(model, "Change Indent")
    , m_index(index)
    , m_oldLevel(oldLevel)
    , m_newLevel(newLevel)
{
}

void ChangeIndentCommand::execute()
{
    m_model->setIndentInternal(m_index, m_newLevel);
}

void ChangeIndentCommand::undo()
{
    m_model->setIndentInternal(m_index, m_oldLevel);
}
