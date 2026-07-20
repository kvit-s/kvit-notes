// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "moveblockcommand.h"
#include "blockmodel.h"

MoveBlockCommand::MoveBlockCommand(BlockModel *model, int fromIndex, int toIndex,
                                   bool preApplied)
    : UndoCommand(model, "Move Block")
    , m_fromIndex(fromIndex)
    , m_toIndex(toIndex)
    , m_preApplied(preApplied)
{
}

void MoveBlockCommand::execute()
{
    if (m_preApplied) {
        // The drag's preview moves already produced this order; the
        // flag clears so redo after an undo performs the real move.
        m_preApplied = false;
        return;
    }
    m_model->moveBlockInternal(m_fromIndex, m_toIndex);
}

void MoveBlockCommand::undo()
{
    // Reverse the move
    m_model->moveBlockInternal(m_toIndex, m_fromIndex);
}
