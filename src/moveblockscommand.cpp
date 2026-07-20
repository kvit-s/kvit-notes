// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "moveblockscommand.h"

namespace {
// After moving [first..last] to before dest, the range occupies
// [dest-len, dest-1] (right move) or [dest, dest+len-1] (left move);
// the inverse move returns it to [first..last].
BlockModel::RangeMove inverted(const BlockModel::RangeMove &m)
{
    const int len = m.last - m.first + 1;
    if (m.dest > m.last)
        return {m.dest - len, m.dest - 1, m.first};
    return {m.dest, m.dest + len - 1, m.first + len};
}
} // namespace

MoveBlocksCommand::MoveBlocksCommand(BlockModel *model,
                                     const QList<BlockModel::RangeMove> &moves)
    : UndoCommand(model, QStringLiteral("Move Blocks"))
    , m_moves(moves)
{
}

void MoveBlocksCommand::execute()
{
    for (const BlockModel::RangeMove &mv : m_moves)
        m_model->moveBlocksRangeInternal(mv.first, mv.last, mv.dest);
}

void MoveBlocksCommand::undo()
{
    for (int i = m_moves.size() - 1; i >= 0; --i) {
        const BlockModel::RangeMove inv = inverted(m_moves.at(i));
        m_model->moveBlocksRangeInternal(inv.first, inv.last, inv.dest);
    }
}
