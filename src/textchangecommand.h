// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TEXTCHANGECOMMAND_H
#define TEXTCHANGECOMMAND_H

#include "undocommand.h"
#include <QString>

// A single-block text edit stored as an offset+diff splice
// (performance-plan.md Phase 7, finding A6): only the changed span is
// retained, not two full content copies, so long editing sessions on
// large blocks keep undo memory proportional to the edits. Undo/redo
// reconstruct the other side from the block's current content, which the
// undo stack guarantees matches the command's before/after state.
class TextChangeCommand : public UndoCommand
{
public:
    TextChangeCommand(BlockModel *model, int blockIndex,
                      const QString &oldContent, const QString &newContent,
                      int oldCursorPos = -1, int newCursorPos = -1);

    void execute() override;
    void undo() override;
    void redo() override;

    bool canMergeWith(const UndoCommand *other) const override;
    void mergeWith(const UndoCommand *other) override;

    Type type() const override { return Type::TextChange; }

    // Accessors for testing
    int blockIndex() const { return m_blockIndex; }
    int offset() const { return m_offset; }
    QString removedText() const { return m_removed; }
    QString insertedText() const { return m_inserted; }
    int oldLength() const { return m_oldLength; }
    int newLength() const { return m_newLength; }

private:
    QString reconstructOld() const;
    QString reconstructNew() const;

    int m_blockIndex;
    int m_offset = 0;       // splice position (shared prefix length)
    QString m_removed;      // old text within the splice
    QString m_inserted;     // new text within the splice
    int m_oldLength = 0;    // full content lengths, for merge sanity
    int m_newLength = 0;
    int m_oldCursorPos;
    int m_newCursorPos;
    bool m_executed = false;
};

#endif // TEXTCHANGECOMMAND_H
