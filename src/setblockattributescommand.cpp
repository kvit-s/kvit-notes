// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "setblockattributescommand.h"
#include "blockmodel.h"
#include "block.h"

SetBlockAttributesCommand::SetBlockAttributesCommand(BlockModel *model, int index,
                                                     const QString &attributes)
    : UndoCommand(model, "Style Block")
    , m_index(index)
    , m_newAttributes(attributes)
{
}

void SetBlockAttributesCommand::execute()
{
    // Capture the prior attributes on first execution so undo restores exactly
    // what was there (redo re-uses the captured value).
    if (!m_captured) {
        if (Block *block = m_model->blockAt(m_index))
            m_oldAttributes = block->attributes();
        m_captured = true;
    }
    m_model->setAttributesInternal(m_index, m_newAttributes);
}

void SetBlockAttributesCommand::undo()
{
    m_model->setAttributesInternal(m_index, m_oldAttributes);
}
