// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MOVEBLOCKCOMMAND_H
#define MOVEBLOCKCOMMAND_H

#include "undocommand.h"

class MoveBlockCommand : public UndoCommand
{
public:
    // preApplied: the move already happened (a drag's live preview
    // moves applied it) — the first execute is a no-op, like
    // TextChangeCommand's pre-applied pattern; undo/redo do real moves.
    MoveBlockCommand(BlockModel *model, int fromIndex, int toIndex,
                     bool preApplied = false);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::MoveBlock; }

private:
    int m_fromIndex;
    int m_toIndex;
    bool m_preApplied = false;
};

#endif // MOVEBLOCKCOMMAND_H
