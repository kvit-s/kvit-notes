// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef REMOVEBLOCKCOMMAND_H
#define REMOVEBLOCKCOMMAND_H

#include "undocommand.h"
#include "block.h"

class RemoveBlockCommand : public UndoCommand
{
public:
    RemoveBlockCommand(BlockModel *model, int index);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::RemoveBlock; }

private:
    int m_index;
    Block::State m_state;  // Full state so undo restores the block exactly
    QString m_blockId;
};

#endif // REMOVEBLOCKCOMMAND_H
