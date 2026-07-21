// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef INSERTBLOCKCOMMAND_H
#define INSERTBLOCKCOMMAND_H

#include "undocommand.h"
#include "block.h"

class InsertBlockCommand : public UndoCommand
{
public:
    InsertBlockCommand(BlockModel *model, int index, Block::BlockType type,
                       const QString &content);
    InsertBlockCommand(BlockModel *model, int index, const Block::State &state);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::InsertBlock; }

private:
    int m_index;
    Block::State m_state;
    QString m_blockId;  // Stored after first execute for consistent redo
};

#endif // INSERTBLOCKCOMMAND_H
