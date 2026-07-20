// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CONVERTBLOCKCOMMAND_H
#define CONVERTBLOCKCOMMAND_H

#include "undocommand.h"
#include "block.h"

// One undo step that rewrites a block's full state — used by block-type
// conversions (markdown prefix auto-conversion, exit-on-empty, the
// Backspace ladder) so a single Ctrl+Z restores exactly what the user had.
class ConvertBlockCommand : public UndoCommand
{
public:
    ConvertBlockCommand(BlockModel *model, int index,
                        const Block::State &oldState, const Block::State &newState);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::ConvertBlock; }

private:
    int m_index;
    Block::State m_oldState;
    Block::State m_newState;
};

#endif // CONVERTBLOCKCOMMAND_H
