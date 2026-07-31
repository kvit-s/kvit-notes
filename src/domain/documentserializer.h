// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTSERIALIZER_H
#define DOCUMENTSERIALIZER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include "block.h"

class BlockModel;

class DocumentSerializer : public QObject
{
    Q_OBJECT

public:
    explicit DocumentSerializer(QObject *parent = nullptr);

    // What a parse produces, and what a block holds: one type, not two.
    //
    // This was its own seven-field struct, field-for-field identical to
    // Block::State. Being a copy is what made it lossy — the exporter kept a
    // third copy of the same fields with `attributes` left out, so every
    // alignment, drop cap, divider style and image effect was dropped on the
    // way to HTML and PDF. Nobody decided that; a struct was copied and a
    // field was not. There is now one snapshot type, so the next field added
    // to a block reaches every consumer or none.
    //
    // The name survives as an alias because it reads well at the call sites
    // that mean "one parsed block" rather than "one block's state".
    using BlockData = Block::State;

    // Serialize blocks to Markdown string
    Q_INVOKABLE QString serialize(BlockModel *model) const;

    // Serialize a single block to Markdown. Numbered-list ordinals are
    // computed by the caller (serialize() reads them off the model).
    QString serializeBlock(const Block *block, int ordinal = 1) const;

    // Markdown for a subset of blocks (clipboard text of a block
    // selection, features.md §5.1 "copy multiple blocks preserving
    // structure"): the same prefixes and separators as serialize(), with
    // tightness decided by adjacency in the OUTPUT and ordinals read off
    // the document. No trailing newline — this is clipboard text.
    Q_INVOKABLE QString serializeBlocks(BlockModel *model,
                                        const QVariantList &indexes) const;

    // Parse markdown into typed blocks and insert them before the given
    // index as ONE undo step (block-selection paste, §5.3 "paste after
    // selected block"). Returns the number of blocks inserted.
    Q_INVOKABLE int insertMarkdownAt(BlockModel *model, int index,
                                     const QString &markdown) const;

    // Insert text as plain paragraphs before the given index as ONE undo
    // step — the block-selection half of Ctrl+Shift+V (§5.3 "paste as plain
    // text"). Unlike insertMarkdownAt() this never interprets structure, so
    // a pasted "# Title" stays literal text rather than becoming a heading.
    // Returns the number of blocks inserted.
    Q_INVOKABLE int insertPlainTextAt(BlockModel *model, int index,
                                      const QString &text) const;

    // Parse Markdown string to block data
    Q_INVOKABLE QList<BlockData> parse(const QString &markdown) const;

    // Run the fenced-block ingest pass over one code block's info string and
    // body, returning {"language", "content"}.
    //
    // Opening a file and pasting markdown reach this through parse(), which
    // is where a crooked character diagram gets recognised and straightened.
    // A code block that already exists never goes through parse() again, so
    // text pasted into one — or a body the reader has just declared to be a
    // text diagram — needs the same pass applied on its own. Editing the
    // body by hand deliberately does not: a classifier that ran on every
    // keystroke would retag a block out from under whoever is typing in it.
    Q_INVOKABLE QVariantMap ingestCodeFence(const QString &language,
                                            const QString &content) const;

    // Load directly into model (clears existing blocks)
    Q_INVOKABLE void loadIntoModel(BlockModel *model, const QString &markdown);

    // Configuration
    void setTrailingNewline(bool enable) { m_trailingNewline = enable; }
    bool trailingNewline() const { return m_trailingNewline; }

private:
    bool m_trailingNewline = true;  // Add newline at end of file
};

#endif // DOCUMENTSERIALIZER_H
