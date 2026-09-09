// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockpositions.h"

#include "block.h"
#include "blockkinddef.h"
#include "blockmodel.h"
#include "inlinemarkdown.h"

#include <QList>
#include <QString>
#include <QtGlobal>

namespace BlockPositions {

int markdownPosition(const BlockModel *model, int blockIndex, int displayPos)
{
    const Block *block = model ? model->blockAt(blockIndex) : nullptr;
    if (!block)
        return 0;
    const QString content = block->content();
    const int contentLength = static_cast<int>(content.length());
    // Clamped low here, high on the result: past the end the mapping
    // extrapolates one markdown character per display character, so
    // clamping the answer to the content length is the same as having
    // clamped the input to the display length, without parsing the
    // markdown a second time to find out what that length is.
    const int pos = qMax(0, displayPos);
    if (block->kind()->isVerbatim())
        return qMin(pos, contentLength);
    return qBound(0,
                  InlineMarkdown::documentToMarkdown(content, QList<int>(), pos),
                  contentLength);
}

int displayPosition(const BlockModel *model, int blockIndex, int mdPos)
{
    const Block *block = model ? model->blockAt(blockIndex) : nullptr;
    if (!block)
        return 0;
    const QString content = block->content();
    const int pos = qBound(0, mdPos, static_cast<int>(content.length()));
    if (block->kind()->isVerbatim())
        return pos;
    // An offset already inside the content maps inside the display text,
    // so no second clamp is needed on the way out.
    return InlineMarkdown::markdownToDocument(content, QList<int>(), pos);
}

} // namespace BlockPositions
