// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTCOMPARE_H
#define DOCUMENTCOMPARE_H

#include <QObject>
#include <QString>
#include <QVariantList>

// What differs between two markdown documents, as ranges of the first one.
//
// The application shows a reader more than one version of the same note: the
// backup dialog lists the copies the collection rotated before each
// overwrite, and draws the one under the cursor. A timestamp and a first line
// do not tell two edits of the same afternoon apart, so the question a reader
// actually has — which words of this stored version are not the words in front
// of me — needs answering, and answering it is a comparison of two markdown
// strings rather than anything to do with the dialog that asks.
//
// The answer is shaped for DocumentBlockMarks: {block, start, length} with
// start and length in the block's DISPLAY text, so a drawn document can mark
// the run directly.
//
// Exposed as the `DocumentCompare` QML singleton. Stateless — every answer is
// a pure function of the two strings — so the one instance serves every
// caller, exactly as the shared DocumentSerializer does.
class DocumentCompare : public QObject
{
    Q_OBJECT

public:
    explicit DocumentCompare(QObject *parent = nullptr);

    // The runs of `markdown` that `baseline` does not have.
    //
    // Both strings are parsed into blocks and the two block sequences are
    // aligned, so a paragraph inserted into one of them shifts nothing after
    // it. Then, per block:
    //
    //   - A block present in both, unchanged, contributes nothing.
    //   - A block that corresponds to one in `baseline` but differs
    //     contributes the run between their common prefix and their common
    //     suffix: for "the second draft" against "the final draft", the word
    //     "second".
    //   - A block whose words are the same but whose kind, indent, to-do tick
    //     or fence language changed contributes its whole display text, since
    //     no run of characters can say "this line is not the line you have".
    //   - A block with no counterpart at all contributes its whole display
    //     text.
    //
    // Two cases contribute nothing and are worth knowing about. A block whose
    // display text is a prefix or a suffix of its counterpart's — the reader
    // added a sentence and changed nothing else — has no character of its own
    // that differs, so there is nothing in it to mark. And a block with no
    // text a range can address (a divider, a picture) cannot carry a
    // character range at all, so a picture that changed is not reported here.
    //
    // Ranges come back in block order, at most one per block.
    Q_INVOKABLE static QVariantList changedRanges(const QString &markdown,
                                                  const QString &baseline);
};

#endif // DOCUMENTCOMPARE_H
