// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SETCHECKEDCOMMAND_H
#define SETCHECKEDCOMMAND_H

#include "undocommand.h"

class SetCheckedCommand : public UndoCommand
{
public:
    SetCheckedCommand(BlockModel *model, int index, bool checked);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::SetChecked; }

private:
    int m_index;
    bool m_checked;
};

#endif // SETCHECKEDCOMMAND_H
