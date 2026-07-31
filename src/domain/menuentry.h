// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MENUENTRY_H
#define MENUENTRY_H

#include "block.h"

#include <QString>
#include <QStringList>

// One row of the slash menu and the turn-into menu.
//
// A kind returns a LIST of these rather than one, because the mapping has
// always been many-to-one and has to stay that way: a paragraph contributes
// both "Text" and "Drop Cap", a callout both "Callout" and "Toggle", and five
// separate rows all insert a code block with a different fence language.
struct MenuEntry {
    // What the block becomes. The insert path hands this to
    // BlockModel::convertBlock as an int, which is why it is the stored type
    // and not the resolved kind: a task board is a CodeBlock whose language
    // is `kanban`, and the conversion has no vocabulary for anything else.
    Block::BlockType type = Block::Paragraph;

    // Shown to the reader: the row's name, the line under it, the glyph in
    // front of it, and the group heading it sits under.
    QString name;
    QString description;
    QString group;
    QString icon;

    // Extra words that find this row. The menu ranks a prefix match above a
    // word-prefix match above a subsequence, over the name and these together.
    QStringList aliases;

    // A qualifier seeded on insert, carried on the block's `language` field
    // so it rides the existing convertBlock(language) path. It means five
    // different things depending on the row, and every one of them is in use:
    //
    //   a fence language      "kanban", "toc", "mermaid", "query"
    //   a callout type        "info"
    //   a UI marker           "toggle", which is a Callout drawn collapsed
    //   an attribute marker   "dropcap", which writes an attribute rather
    //                         than converting anything
    //   an insert-flow marker "embed", which opens a URL prompt
    //
    // The last three never reach a fence-language lookup, because only a
    // CodeBlock consults one. The routing for all five lives in BlockMenu.qml
    // and is keyed on (type, defaultLanguage) — the pair below.
    QString defaultLanguage;

    // Starter content for a newly inserted block, empty when there is none.
    QString seed;

    // Where this row sits inside its group. Rows sharing a value keep the
    // order their kinds gave them, which is nearly all of them.
    //
    // One row sets it. A drop cap is a paragraph with an attribute rather
    // than a kind of its own, so the paragraph contributes it — and the
    // paragraph has to come first, because its other row is the first row of
    // the Basic group. Without this the drop cap would lead the Advanced
    // group and take the "/d" that has always inserted a divider.
    int order = 0;

    // The row's identity in the recency list, which is persisted in the
    // reader's settings under blockMenu.recent. The format is load-bearing:
    // an id that no longer resolves is dropped silently, so a change here
    // wipes everyone's recently-used list with no error to notice.
    QString entryId() const
    {
        return QString::number(static_cast<int>(type)) + QLatin1Char(':')
             + defaultLanguage;
    }
};

#endif // MENUENTRY_H
