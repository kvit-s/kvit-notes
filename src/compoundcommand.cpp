// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "compoundcommand.h"

CompoundCommand::CompoundCommand(BlockModel *model, const QString &description)
    : UndoCommand(model, description)
{
}

void CompoundCommand::execute()
{
    // Execute all child commands in order
    for (auto &cmd : m_commands) {
        cmd->execute();
    }
}

void CompoundCommand::undo()
{
    // Undo child commands in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo();
    }
}

void CompoundCommand::redo()
{
    // Redo all child commands in order
    for (auto &cmd : m_commands) {
        cmd->redo();
    }
}

void CompoundCommand::addCommand(std::unique_ptr<UndoCommand> command)
{
    m_commands.push_back(std::move(command));
}
