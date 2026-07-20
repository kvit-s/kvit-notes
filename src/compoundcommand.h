// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef COMPOUNDCOMMAND_H
#define COMPOUNDCOMMAND_H

#include "undocommand.h"
#include <vector>
#include <memory>

class CompoundCommand : public UndoCommand
{
public:
    CompoundCommand(BlockModel *model, const QString &description);

    void execute() override;
    void undo() override;
    void redo() override;

    Type type() const override { return Type::Compound; }

    void addCommand(std::unique_ptr<UndoCommand> command);
    int commandCount() const { return static_cast<int>(m_commands.size()); }

private:
    std::vector<std::unique_ptr<UndoCommand>> m_commands;
};

#endif // COMPOUNDCOMMAND_H
