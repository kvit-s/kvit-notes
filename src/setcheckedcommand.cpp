// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "setcheckedcommand.h"
#include "blockmodel.h"

SetCheckedCommand::SetCheckedCommand(BlockModel *model, int index, bool checked)
    : UndoCommand(model, "Toggle Todo")
    , m_index(index)
    , m_checked(checked)
{
}

void SetCheckedCommand::execute()
{
    m_model->setCheckedInternal(m_index, m_checked);
}

void SetCheckedCommand::undo()
{
    m_model->setCheckedInternal(m_index, !m_checked);
}
