// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef UNDOCOMMAND_H
#define UNDOCOMMAND_H

#include <QString>
#include <QDateTime>

class BlockModel;

class UndoCommand
{
public:
    explicit UndoCommand(BlockModel *model, const QString &description = QString());
    virtual ~UndoCommand() = default;

    // Execute the command (called on first execution and redo)
    virtual void execute() = 0;

    // Undo the command
    virtual void undo() = 0;

    // Redo the command (default: call execute)
    virtual void redo() { execute(); }

    // Check if this command can merge with another
    virtual bool canMergeWith(const UndoCommand *other) const { Q_UNUSED(other); return false; }

    // Merge another command into this one
    virtual void mergeWith(const UndoCommand *other) { Q_UNUSED(other); }

    // Get human-readable description
    QString description() const { return m_description; }

    // Get timestamp when command was created
    QDateTime timestamp() const { return m_timestamp; }

    // Get model pointer
    BlockModel* model() const { return m_model; }

    // Get merge ID for grouping related operations
    int mergeId() const { return m_mergeId; }
    void setMergeId(int id) { m_mergeId = id; }

    // Command type for identification
    enum class Type {
        TextChange,
        InsertBlock,
        RemoveBlock,
        MoveBlock,
        MoveBlocks,
        ChangeType,
        SplitBlock,
        MergeBlocks,
        Compound,
        SetChecked,
        ChangeIndent,
        ConvertBlock,
        SetBlockAttributes
    };
    virtual Type type() const = 0;

protected:
    BlockModel *m_model;
    QString m_description;
    QDateTime m_timestamp;
    int m_mergeId = -1;
};

#endif // UNDOCOMMAND_H
