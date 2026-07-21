// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CHANGETYPECOMMAND_H
#define CHANGETYPECOMMAND_H

#include "undocommand.h"
#include "block.h"

class ChangeTypeCommand : public UndoCommand
{
public:
    ChangeTypeCommand(BlockModel *model, int index, Block::BlockType oldType,
                      Block::BlockType newType);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::ChangeType; }

private:
    int m_index;
    Block::BlockType m_oldType;
    Block::BlockType m_newType;
};

#endif // CHANGETYPECOMMAND_H
