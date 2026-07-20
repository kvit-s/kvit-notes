// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERGEBLOCKSCOMMAND_H
#define MERGEBLOCKSCOMMAND_H

#include "undocommand.h"
#include "block.h"

class MergeBlocksCommand : public UndoCommand
{
public:
    MergeBlocksCommand(BlockModel *model, int keepIndex, int removeIndex);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::MergeBlocks; }

private:
    int m_keepIndex;
    int m_removeIndex;
    QString m_keepContent;
    Block::State m_removeState;  // Full state so undo restores the block exactly
};

#endif // MERGEBLOCKSCOMMAND_H
