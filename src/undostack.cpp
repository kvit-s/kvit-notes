// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "undostack.h"
#include "undocommand.h"
#include "compoundcommand.h"

UndoStack::UndoStack(QObject *parent)
    : QObject(parent)
{
}

UndoStack::~UndoStack()
{
    // unique_ptr handles cleanup
}

void UndoStack::push(std::unique_ptr<UndoCommand> command)
{
    if (!command) return;

    // If in macro mode, collect commands
    if (m_inMacro) {
        command->execute();
        m_macroCommands.push_back(std::move(command));
        return;
    }

    // Clear redo stack when pushing new command
    clearRedoStack();

    // Try to merge with previous command
    if (!m_mergeBroken && shouldMerge(command.get()) && !m_commands.empty()) {
        // If we're merging at the clean index, the clean state becomes invalid
        // because the command at that position is being modified
        bool wasClean = isClean();

        m_commands.back()->mergeWith(command.get());
        // Don't execute - merged into existing command
        emit undoTextChanged();

        // Invalidate clean state if we were clean before merge
        if (wasClean) {
            m_cleanIndex = -1;  // Can never get back to the original clean state
            emit cleanChanged();
        }
        return;
    }

    // Reset merge broken flag after using it
    m_mergeBroken = false;

    // Execute and push
    command->execute();
    m_commands.push_back(std::move(command));
    m_index = static_cast<int>(m_commands.size());
    enforceLimit();

    emit canUndoChanged();
    emit canRedoChanged();
    emit undoTextChanged();
    emit redoTextChanged();
    emit countChanged();
    emit cleanChanged();
}

void UndoStack::undo()
{
    if (!canUndo()) return;

    m_index--;
    m_commands[m_index]->undo();

    // Break merge after undo
    m_mergeBroken = true;

    emit canUndoChanged();
    emit canRedoChanged();
    emit undoTextChanged();
    emit redoTextChanged();
    emit indexChanged();
    emit cleanChanged();
}

void UndoStack::redo()
{
    if (!canRedo()) return;

    m_commands[m_index]->redo();
    m_index++;

    // Break merge after redo
    m_mergeBroken = true;

    emit canUndoChanged();
    emit canRedoChanged();
    emit undoTextChanged();
    emit redoTextChanged();
    emit indexChanged();
    emit cleanChanged();
}

bool UndoStack::canUndo() const
{
    return m_index > 0;
}

bool UndoStack::canRedo() const
{
    return m_index < static_cast<int>(m_commands.size());
}

QString UndoStack::undoText() const
{
    if (!canUndo()) return QString();
    return m_commands[m_index - 1]->description();
}

QString UndoStack::redoText() const
{
    if (!canRedo()) return QString();
    return m_commands[m_index]->description();
}

bool UndoStack::isClean() const
{
    return m_index == m_cleanIndex;
}

void UndoStack::setClean()
{
    if (m_cleanIndex != m_index) {
        m_cleanIndex = m_index;
        emit cleanChanged();
    }
}

void UndoStack::clear()
{
    if (m_commands.empty()) return;

    m_commands.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_mergeBroken = false;

    emit canUndoChanged();
    emit canRedoChanged();
    emit undoTextChanged();
    emit redoTextChanged();
    emit countChanged();
    emit indexChanged();
    emit cleanChanged();
}

void UndoStack::clearRedoStack()
{
    if (m_index < static_cast<int>(m_commands.size())) {
        // The clean state lived somewhere in the branch about to be
        // dropped, so no position on the stack will hold the saved
        // content once these commands are gone. Losing it here rather
        // than in the callers matters because the replacing command
        // pushes the index straight back to the clean index, which
        // would otherwise read as clean again.
        const bool cleanDiscarded = m_cleanIndex > m_index;
        if (cleanDiscarded)
            m_cleanIndex = -1;

        // Remove all commands after current index
        while (static_cast<int>(m_commands.size()) > m_index) {
            m_commands.pop_back();
        }
        emit canRedoChanged();
        emit redoTextChanged();
        emit countChanged();
        if (cleanDiscarded)
            emit cleanChanged();
    }
}

bool UndoStack::shouldMerge(const UndoCommand *newCommand) const
{
    if (m_commands.empty()) return false;

    // No merging if window is 0
    if (m_mergeWindowMs <= 0) return false;

    const UndoCommand *lastCommand = m_commands.back().get();

    // Must be same command type and allow merging
    if (!lastCommand->canMergeWith(newCommand)) return false;

    // Check time window
    qint64 msDiff = lastCommand->timestamp().msecsTo(newCommand->timestamp());
    if (msDiff > m_mergeWindowMs) return false;

    // Check merge ID if set
    if (newCommand->mergeId() != -1 && lastCommand->mergeId() != -1) {
        return newCommand->mergeId() == lastCommand->mergeId();
    }

    return true;
}

void UndoStack::beginMacro(const QString &description)
{
    if (m_inMacro) return;  // Don't nest macros

    m_inMacro = true;
    m_macroDescription = description;
    m_macroCommands.clear();
}

void UndoStack::endMacro()
{
    if (!m_inMacro) return;

    m_inMacro = false;

    if (m_macroCommands.empty()) return;

    // Clear redo stack
    clearRedoStack();

    // Create compound command from collected commands
    if (m_macroCommands.size() == 1) {
        // Single command, no need for compound
        // Override description if macro description is set
        m_commands.push_back(std::move(m_macroCommands[0]));
    } else {
        // Multiple commands - wrap in compound
        auto compound = std::make_unique<CompoundCommand>(
            m_macroCommands[0]->model(),
            m_macroDescription
        );
        for (auto &cmd : m_macroCommands) {
            compound->addCommand(std::move(cmd));
        }
        m_commands.push_back(std::move(compound));
    }

    m_macroCommands.clear();
    m_index = static_cast<int>(m_commands.size());
    enforceLimit();

    emit canUndoChanged();
    emit canRedoChanged();
    emit undoTextChanged();
    emit redoTextChanged();
    emit countChanged();
    emit cleanChanged();
}

const UndoCommand* UndoStack::command(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_commands.size())) return nullptr;
    return m_commands[static_cast<size_t>(index)].get();
}

void UndoStack::breakMerge()
{
    m_mergeBroken = true;
}

void UndoStack::setLimit(int limit)
{
    m_limit = qMax(1, limit);
    enforceLimit();
}

void UndoStack::enforceLimit()
{
    bool dropped = false;
    while (static_cast<int>(m_commands.size()) > m_limit) {
        m_commands.erase(m_commands.begin());
        m_index = qMax(0, m_index - 1);
        // The clean state either shifts with the surviving commands or,
        // when it pointed at or below the dropped command, is gone for
        // good.
        if (m_cleanIndex > 0)
            --m_cleanIndex;
        else if (m_cleanIndex == 0)
            m_cleanIndex = -1;
        dropped = true;
    }
    if (dropped) {
        emit countChanged();
        emit indexChanged();
        emit cleanChanged();
    }
}
