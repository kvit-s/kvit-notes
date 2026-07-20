// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "convertblockcommand.h"
#include "blockmodel.h"

ConvertBlockCommand::ConvertBlockCommand(BlockModel *model, int index,
                                         const Block::State &oldState,
                                         const Block::State &newState)
    : UndoCommand(model, "Convert Block")
    , m_index(index)
    , m_oldState(oldState)
    , m_newState(newState)
{
}

void ConvertBlockCommand::execute()
{
    m_model->applyStateInternal(m_index, m_newState);
}

void ConvertBlockCommand::undo()
{
    m_model->applyStateInternal(m_index, m_oldState);
}
