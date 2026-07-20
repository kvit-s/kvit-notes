// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef UNDOSTACK_H
#define UNDOSTACK_H

#include <QObject>
#include <vector>
#include <memory>

class UndoCommand;
class BlockModel;

class UndoStack : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoTextChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY redoTextChanged)
    Q_PROPERTY(bool isClean READ isClean NOTIFY cleanChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit UndoStack(QObject *parent = nullptr);
    ~UndoStack() override;

    // Push a command onto the stack and execute it
    void push(std::unique_ptr<UndoCommand> command);

    // Undo the last command
    Q_INVOKABLE void undo();

    // Redo the next command
    Q_INVOKABLE void redo();

    // Check if undo/redo is available
    bool canUndo() const;
    bool canRedo() const;

    // Get descriptions for UI
    QString undoText() const;
    QString redoText() const;

    // Clean state (for tracking unsaved changes)
    bool isClean() const;
    Q_INVOKABLE void setClean();
    int cleanIndex() const { return m_cleanIndex; }

    // Stack management
    int count() const { return static_cast<int>(m_commands.size()); }
    int index() const { return m_index; }
    Q_INVOKABLE void clear();

    // Merge window configuration (milliseconds)
    void setMergeWindow(int ms) { m_mergeWindowMs = ms; }
    int mergeWindow() const { return m_mergeWindowMs; }

    // Bounded history (performance-plan.md Phase 7, finding A6): when a
    // push exceeds the limit the oldest command is dropped and the clean
    // index shifts with it; a clean state older than the drop can never
    // be reached again and becomes -1.
    void setLimit(int limit);
    int limit() const { return m_limit; }

    // Begin/end macro for compound operations
    void beginMacro(const QString &description);
    void endMacro();

    // Get command at index (for debugging/testing)
    const UndoCommand* command(int index) const;

    // Break current merge sequence
    Q_INVOKABLE void breakMerge();

signals:
    void canUndoChanged();
    void canRedoChanged();
    void undoTextChanged();
    void redoTextChanged();
    void cleanChanged();
    void countChanged();
    void indexChanged();

private:
    void clearRedoStack();
    bool shouldMerge(const UndoCommand *newCommand) const;
    void enforceLimit();

    std::vector<std::unique_ptr<UndoCommand>> m_commands;
    int m_index = 0;           // Points to next command to execute (or end of undo history)
    int m_cleanIndex = 0;      // Index at last save
    int m_limit = 200;         // Maximum retained commands (A6 cap)
    int m_mergeWindowMs = 500; // Merge window for text changes
    bool m_mergeBroken = false;

    // Macro support
    bool m_inMacro = false;
    QString m_macroDescription;
    std::vector<std::unique_ptr<UndoCommand>> m_macroCommands;
};

#endif // UNDOSTACK_H
