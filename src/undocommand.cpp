// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "undocommand.h"

UndoCommand::UndoCommand(BlockModel *model, const QString &description)
    : m_model(model)
    , m_description(description)
    , m_timestamp(QDateTime::currentDateTime())
    , m_mergeId(-1)
{
}
