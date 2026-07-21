// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SPLITBLOCKCOMMAND_H
#define SPLITBLOCKCOMMAND_H

#include "undocommand.h"
#include "block.h"

class SplitBlockCommand : public UndoCommand
{
public:
    SplitBlockCommand(BlockModel *model, int index, int position);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::SplitBlock; }

private:
    int m_index;
    int m_position;
    Block::State m_originalState;
};

#endif // SPLITBLOCKCOMMAND_H
