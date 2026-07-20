// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SETBLOCKATTRIBUTESCOMMAND_H
#define SETBLOCKATTRIBUTESCOMMAND_H

#include "undocommand.h"
#include <QString>

// One undoable step that sets a block's presentation attributes
// (phase12-plan.md decision 1). Every styling change — alignment, divider
// style, callout color, image effect, drop cap, embed size — flows through this
// one command, so it reverts in a single undo like any other block mutation.
class SetBlockAttributesCommand : public UndoCommand
{
public:
    SetBlockAttributesCommand(BlockModel *model, int index,
                              const QString &attributes);

    void execute() override;
    void undo() override;

    Type type() const override { return Type::SetBlockAttributes; }

private:
    int m_index;
    QString m_newAttributes;
    QString m_oldAttributes;
    bool m_captured = false;
};

#endif // SETBLOCKATTRIBUTESCOMMAND_H
