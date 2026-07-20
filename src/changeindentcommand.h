// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CHANGEINDENTCOMMAND_H
#define CHANGEINDENTCOMMAND_H

#include "undocommand.h"

class ChangeIndentCommand : public UndoCommand
{
public:
    ChangeIndentCommand(BlockModel *model, int index, int oldLevel, int newLevel);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::ChangeIndent; }

private:
    int m_index;
    int m_oldLevel;
    int m_newLevel;
};

#endif // CHANGEINDENTCOMMAND_H
