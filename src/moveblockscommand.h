// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MOVEBLOCKSCOMMAND_H
#define MOVEBLOCKSCOMMAND_H

#include "undocommand.h"
#include "blockmodel.h"
#include <QList>

// One undo step for a multi-block drop: the planned run moves are stored
// once and replayed on redo; undo replays each move's inverse in reverse
// order, restoring the exact prior arrangement without deriving O(N)
// single moves.
class MoveBlocksCommand : public UndoCommand
{
public:
    MoveBlocksCommand(BlockModel *model, const QList<BlockModel::RangeMove> &moves);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::MoveBlocks; }

    // Accessor for testing
    QList<BlockModel::RangeMove> moves() const { return m_moves; }

private:
    QList<BlockModel::RangeMove> m_moves;
};

#endif // MOVEBLOCKSCOMMAND_H
