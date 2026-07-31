// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "blockattributes.h"
#include "blockkinddef.h"
#include "imageassets.h"
#include "llmnormalizer.h"
#include "diagrams/diagramclassifier.h"
#include "diagrams/diagramrepair.h"
#include "tabledata.h"
#include "insertblockcommand.h"
#include "undostack.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

// The file format: blocks separated by blank lines, except that consecutive
// list-family blocks (bullet / numbered / todo) are separated by single
// newlines — the natural "tight list" markdown shape. Structural prefixes are
// block state, not content: heading hashes, list markers, todo checkboxes,
// quote angles, code fences, and divider dashes are added on serialize and
// stripped on parse. Code-fence content is taken verbatim, so it may contain
// blank lines; everything else is line-classified.

namespace {

// Two spaces per indent level before a list marker; tabs also count one
// level each on parse.
int parseIndent(const QString &line, int *prefixLen)
{
    int level = 0;
    int i = 0;
    int spaces = 0;
    for (; i < line.size(); ++i) {
        if (line.at(i) == QLatin1Char('\t')) {
            level += 1;
            spaces = 0;
        } else if (line.at(i) == QLatin1Char(' ')) {
            if (++spaces == 2) {
                level += 1;
                spaces = 0;
            }
        } else {
            break;
        }
    }
    *prefixLen = i;
    return qMin(level, BlockModel::MaxIndentLevel);
}

// A fence line: three or more backticks — or tildes — optionally followed
// by an info string (the language). Returns the fence length, or 0.
int fenceLength(const QString &rest, QString *language, QChar *fenceChar = nullptr)
{
    if (rest.isEmpty())
        return 0;
    const QChar c = rest.at(0);
    if (c != QLatin1Char('`') && c != QLatin1Char('~'))
        return 0;
    int ticks = 0;
    while (ticks < rest.size() && rest.at(ticks) == c)
        ++ticks;
    if (ticks < 3)
        return 0;
    QString info = rest.mid(ticks).trimmed();
    // CommonMark: no backticks in a backtick info string; a tilde info
    // string may contain them (that is the markdown-inside-markdown use).
    // Serialize canonicalizes every fence to backticks, whose info string
    // cannot hold a backtick — so it is dropped here, keeping the
    // canonical form reparseable.
    if (info.contains(QLatin1Char('`'))) {
        if (c == QLatin1Char('`'))
            return 0;
        info = info.remove(QLatin1Char('`')).trimmed();
    }
    if (language)
        *language = info;
    if (fenceChar)
        *fenceChar = c;
    return ticks;
}

// The closing fence must use the opener's character, be at least as long,
// and carry no info string.
bool isClosingFence(const QString &line, int openLen, QChar fenceChar)
{
    QString trimmed = line.trimmed();
    if (trimmed.size() < openLen)
        return false;
    for (const QChar &c : trimmed) {
        if (c != fenceChar)
            return false;
    }
    return true;
}

// Fix 2 (indented fences): content lines lose up to the opener's leading
// whitespace — the characters actually present, so a shorter indent strips
// what is there (CommonMark-style).
QString stripFenceIndent(const QString &line, const QString &indent)
{
    int k = 0;
    while (k < indent.size() && k < line.size() && line.at(k) == indent.at(k))
        ++k;
    return line.mid(k);
}

// The ingest character-diagram tagging pass. Only the info strings that make
// no semantic claim about their contents are eligible: untagged, `text`,
// `plaintext`, and `ascii` — the four wrappers LLMs routinely use for
// character diagrams. A high-confidence body has its info string rewritten to
// `diagram`; every other language (including an already
// `diagram`/`mermaid`/`plain` fence) is returned unchanged, so the pass is a
// no-op on canonical output and reparsing tagged text never re-examines it.
QString classifyFenceLanguage(const QString &language, const QString &content)
{
    const QString id = language.trimmed().toLower();
    const bool eligible = id.isEmpty() || id == QLatin1String("text")
        || id == QLatin1String("plaintext") || id == QLatin1String("ascii");
    if (!eligible)
        return language;
    if (DiagramClassifier::looksLikeDiagram(content))
        return QStringLiteral("diagram");
    return language;
}

// The whole ingest pass for one fenced block: an eligible info string is
// classified, and a body that is a character diagram is straightened. Every
// boundary where a fence enters a document runs this — opening a file,
// pasting markdown, pasting into a code block, or declaring a block to be a
// text diagram — so none of them can apply a different policy from the rest.
void ingestFence(QString *language, QString *content)
{
    *language = classifyFenceLanguage(*language, *content);
    const QString id = language->trimmed().toLower();
    if (id == QLatin1String("diagram")
        || id == QLatin1String("text-diagram")
        || id == QLatin1String("ascii-diagram"))
        *content = DiagramRepair::repair(*content);
}

// Attach a tag to the first line of a multi-line serialization.
QString attachTagToFirstLine(const QString &markdown, const QString &payload)
{
    const int nl = markdown.indexOf(QLatin1Char('\n'));
    if (nl < 0)
        return BlockAttributes::attachTag(markdown, payload);
    return BlockAttributes::attachTag(markdown.left(nl), payload)
         + markdown.mid(nl);
}

} // namespace

DocumentSerializer::DocumentSerializer(QObject *parent)
    : QObject(parent)
{
}

QString DocumentSerializer::serializeBlocks(BlockModel *model,
                                            const QVariantList &indexes) const
{
    if (!model)
        return QString();

    // Hash-set deduplication, not a repeated scan of the growing list: a
    // select-all copy hands over one index per block, and `contains` there
    // made the preparation quadratic in the selection size.
    QSet<int> seen;
    seen.reserve(indexes.size());
    QList<int> sorted;
    sorted.reserve(indexes.size());
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int idx = value.toInt(&ok);
        if (ok && idx >= 0 && idx < model->count() && !seen.contains(idx)) {
            seen.insert(idx);
            sorted.append(idx);
        }
    }
    std::sort(sorted.begin(), sorted.end());

    int reserveChars = 0;
    for (int index : sorted)
        reserveChars += model->charCountAt(index) + 8;

    QString result;
    result.reserve(reserveChars);
    for (int i = 0; i < sorted.size(); ++i) {
        Block *block = model->blockAt(sorted.at(i));
        if (!block)
            continue;
        if (i > 0) {
            Block *prev = model->blockAt(sorted.at(i - 1));
            const bool tight = prev && Block::isListFamily(prev->blockType()) &&
                               Block::isListFamily(block->blockType());
            result.append(tight ? QStringLiteral("\n") : QStringLiteral("\n\n"));
        }
        const int ordinal = block->blockType() == Block::NumberedList
                            ? model->ordinalAt(sorted.at(i))
                            : 1;
        result.append(serializeBlock(block, ordinal));
    }
    return result;
}

int DocumentSerializer::insertMarkdownAt(BlockModel *model, int index,
                                         const QString &markdown) const
{
    if (!model || markdown.isEmpty())
        return 0;
    const QList<BlockData> parsed = parse(markdown);
    if (parsed.isEmpty())
        return 0;

    index = qBound(0, index, model->count());
    UndoStack *stack = model->undoStack();
    if (stack)
        stack->beginMacro(QStringLiteral("Paste Blocks"));
    for (int i = 0; i < parsed.size(); ++i) {
        Block::State state;
        state.type = parsed.at(i).type;
        state.content = parsed.at(i).content;
        state.indentLevel = parsed.at(i).indentLevel;
        state.checked = parsed.at(i).checked;
        state.language = parsed.at(i).language;
        state.calloutTitle = parsed.at(i).calloutTitle;
        state.attributes = parsed.at(i).attributes;
        auto cmd = std::make_unique<InsertBlockCommand>(model, index + i, state);
        if (stack)
            stack->push(std::move(cmd));
        else
            cmd->execute();
    }
    if (stack)
        stack->endMacro();
    return parsed.size();
}

int DocumentSerializer::insertPlainTextAt(BlockModel *model, int index,
                                          const QString &text) const
{
    if (!model || text.isEmpty())
        return 0;
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    index = qBound(0, index, model->count());
    UndoStack *stack = model->undoStack();
    if (stack)
        stack->beginMacro(QStringLiteral("Paste Blocks"));
    int inserted = 0;
    for (const QString &line : lines) {
        Block::State state;
        state.type = Block::Paragraph;
        state.content = line;
        auto cmd = std::make_unique<InsertBlockCommand>(model, index + inserted,
                                                        state);
        if (stack)
            stack->push(std::move(cmd));
        else
            cmd->execute();
        ++inserted;
    }
    if (stack)
        stack->endMacro();
    return inserted;
}

QString DocumentSerializer::serialize(BlockModel *model) const
{
    if (!model || model->count() == 0) {
        return QString();
    }

    QString result;

    Block *prev = nullptr;
    for (int i = 0; i < model->count(); ++i) {
        Block *block = model->blockAt(i);
        if (!block)
            continue;

        if (i > 0) {
            const bool tight = prev && Block::isListFamily(prev->blockType()) &&
                               Block::isListFamily(block->blockType());
            result.append(tight ? QStringLiteral("\n") : QStringLiteral("\n\n"));
        }

        const int ordinal = block->blockType() == Block::NumberedList
                            ? model->ordinalAt(i)
                            : 1;
        result.append(serializeBlock(block, ordinal));
        prev = block;
    }

    if (m_trailingNewline && !result.isEmpty()) {
        result.append(QLatin1Char('\n'));
    }

    return result;
}

QString DocumentSerializer::serializeBlock(const Block *block, int ordinal) const
{
    if (!block) return QString();

    // The block's markdown, written by the kind. This was a 110-line switch
    // over the block type with a `default:` label, which is how Image and
    // Media came to have no case at all: both fell through to the paragraph's
    // `return content`, which happens to be right for them, and nothing said
    // whether that was a decision or an oversight.
    const Block::State state = block->state();
    const BlockKindDef *kind = block->kind();
    const QString base = kind->serialize(state, ordinal);
    if (state.attributes.isEmpty())
        return base;

    // Where the <!--kvit …--> tag goes. For most kinds it trails the last
    // line; for a code fence, a math fence, a table and a callout it rides
    // the OPENING line instead, because their last line is a terminator the
    // parser requires to be bare — a tagged closing fence never closes its
    // block, and the rest of the note is read as that block's content on the
    // next load. The kind states which it is; this used to be a second list
    // of block types kept in step with the first by hand.
    return kind->attributeTagRidesOpeningLine()
        ? attachTagToFirstLine(base, state.attributes)
        : BlockAttributes::attachTag(base, state.attributes);
}

QVariantMap DocumentSerializer::ingestCodeFence(const QString &language,
                                                const QString &content) const
{
    QString lang = language;
    QString body = content;
    ingestFence(&lang, &body);
    return { { QStringLiteral("language"), lang },
             { QStringLiteral("content"), body } };
}

QList<DocumentSerializer::BlockData> DocumentSerializer::parse(const QString &markdown) const
{
    QList<BlockData> blocks;

    if (markdown.isEmpty()) {
        return blocks;
    }

    static const QRegularExpression todoRe(
        QStringLiteral("^[-*] \\[( |x|X)\\](?: (.*))?$"));
    static const QRegularExpression bulletRe(QStringLiteral("^[-*] (.*)$"));
    static const QRegularExpression numberedRe(QStringLiteral("^\\d+\\. (.*)$"));

    // Every route LLM text takes into a document — file open and paste —
    // funnels through parse, so the LLM-markdown repairs live here.
    // A no-op on canonical serializer output.
    const QString normalized = LlmNormalizer::normalize(markdown);

    const QStringList lines = normalized.split(QLatin1Char('\n'));

    // Accumulators for multi-line blocks
    QStringList paragraphRun;
    QStringList quoteRun;
    int quoteDepth = 1;   // nested-quote depth of the current run ("> " count)
    // The <!--kvit ...--> attribute payload split off a line in the current
    // run; last non-empty line wins, applied on flush.
    QString paragraphAttrs;
    QString quoteAttrs;

    auto flushParagraph = [&]() {
        if (paragraphRun.isEmpty())
            return;
        BlockData data;
        data.type = Block::Paragraph;
        data.content = paragraphRun.join(QLatin1Char('\n'));
        data.attributes = paragraphAttrs;
        blocks.append(data);
        paragraphRun.clear();
        paragraphAttrs.clear();
    };
    // A quote whose first content line is an Obsidian callout header
    // ([!type], optional fold marker, optional title) becomes a Callout
    // block: the type reuses `language`, the fold state reuses `checked`
    // ('-' collapsed), the title its own field, and the remaining lines are
    // the multi-paragraph body.
    static const QRegularExpression calloutRe(
        QStringLiteral("^\\[!([A-Za-z][A-Za-z0-9_-]*)\\]([+-]?)\\s*(.*)$"));
    auto flushQuote = [&]() {
        if (quoteRun.isEmpty())
            return;
        const QRegularExpressionMatch cm = calloutRe.match(quoteRun.first());
        BlockData data;
        if (cm.hasMatch()) {
            data.type = Block::Callout;
            data.language = cm.captured(1);          // callout type
            data.checked = cm.captured(2) == QLatin1String("-");  // folded
            data.calloutTitle = cm.captured(3);      // title (may be empty)
            data.content = quoteRun.mid(1).join(QLatin1Char('\n'));  // body
        } else {
            data.type = Block::Quote;
            data.content = quoteRun.join(QLatin1Char('\n'));
            // Nested-quote depth rides indentLevel, clamped like
            // list nesting; a callout keeps depth 0.
            data.indentLevel = qBound(0, quoteDepth - 1, 4);
        }
        data.attributes = quoteAttrs;
        blocks.append(data);
        quoteRun.clear();
        quoteAttrs.clear();
    };
    auto flushRuns = [&]() {
        flushParagraph();
        flushQuote();
    };

    // Whether the line just consumed was a list item or one of its
    // continuation lines, which is the only state under which an indented
    // line joins the item above instead of starting a paragraph. Cleared by
    // every other branch simply by not setting it again.
    bool afterListLine = false;

    int i = 0;
    while (i < lines.size()) {
        const bool prevWasListLine = afterListLine;
        afterListLine = false;

        // Split a trailing <!--kvit ...--> attribute tag off this line
        // before classifying. A tag-free line is returned unchanged, so
        // existing documents parse byte-identically. Verbatim regions
        // (code/math fences, table body) read the original `lines` in their
        // inner loops, so their content is never stripped.
        QString lineAttrs;
        const QString line = BlockAttributes::stripTag(lines.at(i), &lineAttrs);

        // Blank lines separate blocks
        if (line.trimmed().isEmpty()) {
            flushRuns();
            ++i;
            continue;
        }

        // Code fences come first: their content is verbatim and may
        // contain blank lines and marker-shaped text. A fence indented
        // under a list item is a fence too (fix 2) — the opener's leading
        // whitespace is recorded and stripped from the content; the block
        // is top-level, since the block model is flat (content fidelity
        // over layout fidelity, decision of record).
        int fenceIndentLen = 0;
        while (fenceIndentLen < line.size()
               && line.at(fenceIndentLen).isSpace())
            ++fenceIndentLen;
        const QString fenceIndent = line.left(fenceIndentLen);
        QString language;
        QChar fenceChar;
        const int openLen =
            fenceLength(line.mid(fenceIndentLen), &language, &fenceChar);
        if (openLen > 0) {
            flushRuns();
            QStringList codeLines;
            QString fenceAttrs = lineAttrs;   // the opener's tag
            ++i;
            bool closed = false;
            while (i < lines.size()) {
                if (isClosingFence(lines.at(i), openLen, fenceChar)) {
                    closed = true;
                    ++i;
                    break;
                }
                // Kvit before this shape existed wrote the tag AFTER the
                // closing fence, which stopped the closer from closing and
                // swallowed the rest of the note. Accept that legacy line so
                // those files still open, and only when stripping the tag
                // leaves a bare closer — a kvit-shaped comment on any other
                // code line stays verbatim content.
                QString legacyAttrs;
                const QString bare =
                    BlockAttributes::stripTag(lines.at(i), &legacyAttrs);
                if (!legacyAttrs.isEmpty()
                    && isClosingFence(bare, openLen, fenceChar)) {
                    if (fenceAttrs.isEmpty())
                        fenceAttrs = legacyAttrs;
                    closed = true;
                    ++i;
                    break;
                }
                codeLines.append(stripFenceIndent(lines.at(i), fenceIndent));
                ++i;
            }
            Q_UNUSED(closed);  // an unclosed fence runs to end of file
            BlockData data;
            data.type = Block::CodeBlock;
            data.content = codeLines.join(QLatin1Char('\n'));
            data.attributes = fenceAttrs;
            // Ingest: an eligible untagged fence is retagged `diagram`, and a
            // diagram body has its LLM alignment flaws conservatively
            // repaired — the same pass family as the LLM markdown
            // normalizations, idempotent, divergence-armed .bak, undoable on
            // paste.
            data.language = language;
            ingestFence(&data.language, &data.content);
            blocks.append(data);
            continue;
        }

        // A $$ … $$ fence becomes a MathBlock: verbatim TeX, like a code
        // fence but with $$ delimiters. Both the multi-line form ($$ on its
        // own line) and a single-line $$x$$ are recognized; the latter
        // normalizes to multi-line on save.
        {
            const QString t = line.trimmed();
            const bool singleLine = t.length() > 4
                && t.startsWith(QLatin1String("$$"))
                && t.endsWith(QLatin1String("$$"));
            if (t == QLatin1String("$$") || singleLine) {
                flushRuns();
                BlockData data;
                data.type = Block::MathBlock;
                data.attributes = lineAttrs;   // the opening $$ carries the tag
                if (singleLine) {
                    data.content = t.mid(2, t.length() - 4).trimmed();
                    ++i;
                } else {
                    QStringList mathLines;
                    ++i;
                    while (i < lines.size()) {
                        if (lines.at(i).trimmed() == QLatin1String("$$")) {
                            ++i;
                            break;
                        }
                        // The legacy tagged closer, as in the code-fence loop.
                        QString legacyAttrs;
                        const QString bare =
                            BlockAttributes::stripTag(lines.at(i), &legacyAttrs);
                        if (!legacyAttrs.isEmpty()
                            && bare.trimmed() == QLatin1String("$$")) {
                            if (data.attributes.isEmpty())
                                data.attributes = legacyAttrs;
                            ++i;
                            break;
                        }
                        mathLines.append(lines.at(i));
                        ++i;
                    }
                    data.content = mathLines.join(QLatin1Char('\n'));
                }
                blocks.append(data);
                continue;
            }
        }

        // A pipe table (a header row + a delimiter row, then contiguous data
        // rows) becomes a Table block. Content is the raw table markdown;
        // serializeBlock canonicalizes it on save.
        if (i + 1 < lines.size()
            && TableData::looksLikeTableStart(line, lines[i + 1])) {
            flushRuns();
            QStringList tableLines;
            tableLines << line << lines[i + 1];
            QString tableAttrs = lineAttrs;   // the header row carries the tag
            int j = i + 2;
            while (j < lines.size() && !lines[j].trimmed().isEmpty()
                   && lines[j].contains('|')) {
                // Kvit once appended the tag to the last data row, where it
                // reparsed as text in the final cell. Split it back off.
                QString rowAttrs;
                const QString row = BlockAttributes::stripTag(lines[j], &rowAttrs);
                if (rowAttrs.isEmpty()) {
                    tableLines << lines[j];
                } else {
                    tableLines << row;
                    if (tableAttrs.isEmpty())
                        tableAttrs = rowAttrs;
                }
                ++j;
            }
            BlockData data;
            data.type = Block::Table;
            data.content = tableLines.join(QLatin1Char('\n'));
            data.attributes = tableAttrs;
            blocks.append(data);
            i = j;
            continue;
        }

        // A lone image/media expression on its own (un-indented) line becomes
        // an Image or Media block: the whole line must be one
        // ![alt|width](path "caption"), so ![…] mid-prose stays literal.
        // The category is by file extension.
        const ImageAssets::Parsed img = ImageAssets::parseLine(line);
        if (img.valid && img.kind != ImageAssets::Kind::None) {
            flushRuns();
            BlockData data;
            data.type = img.kind == ImageAssets::Kind::Media
                ? Block::Media : Block::Image;
            data.content = line;
            data.attributes = lineAttrs;
            blocks.append(data);
            ++i;
            continue;
        }

        // Divider: exactly three dashes or asterisks on their own line
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("---") || trimmed == QStringLiteral("***")) {
            flushRuns();
            BlockData data;
            data.type = Block::Divider;
            data.attributes = lineAttrs;
            blocks.append(data);
            ++i;
            continue;
        }

        // Quote lines: "> content", nested "> > content", or a bare ">"
        // (empty content line). Contiguous SAME-DEPTH quote lines join into
        // one block; a depth change starts a new block (nested quotes — the
        // flat model represents depth, not nesting).
        if (line.startsWith(QStringLiteral("> ")) || line == QStringLiteral(">")
            || line.startsWith(QStringLiteral(">>"))) {
            flushParagraph();
            // Consume the WHOLE marker run, space-separated or not: the
            // entry test above accepts `>>nested`, and a loop that only ate
            // "> " left the remaining markers in the content, so the block
            // came back out as `> >>nested` — two extra levels of marker
            // appearing in the text on every round trip. Each `>` is one
            // level and eats at most one following space.
            int depth = 0;
            QString rest = line;
            while (rest.startsWith(QLatin1Char('>'))) {
                ++depth;
                rest = rest.mid(1);
                if (rest.startsWith(QLatin1Char(' ')))
                    rest = rest.mid(1);
            }
            if (depth == 0)   // unreachable given the entry test; defensive
                depth = 1;
            if (!quoteRun.isEmpty() && depth != quoteDepth)
                flushQuote();
            quoteDepth = depth;
            quoteRun.append(rest);
            if (!lineAttrs.isEmpty())
                quoteAttrs = lineAttrs;
            ++i;
            continue;
        }

        // List family: leading whitespace encodes the nesting level
        int indentChars = 0;
        const int indentLevel = parseIndent(line, &indentChars);
        const QString rest = line.mid(indentChars);

        auto todoMatch = todoRe.match(rest);
        if (todoMatch.hasMatch()) {
            flushRuns();
            BlockData data;
            data.type = Block::Todo;
            data.content = todoMatch.captured(2);
            data.indentLevel = indentLevel;
            data.checked = todoMatch.captured(1) != QStringLiteral(" ");
            data.attributes = lineAttrs;
            blocks.append(data);
            afterListLine = true;
            ++i;
            continue;
        }

        auto bulletMatch = bulletRe.match(rest);
        if (bulletMatch.hasMatch()) {
            flushRuns();
            BlockData data;
            data.type = Block::BulletList;
            data.content = bulletMatch.captured(1);
            data.indentLevel = indentLevel;
            data.attributes = lineAttrs;
            blocks.append(data);
            afterListLine = true;
            ++i;
            continue;
        }

        auto numberedMatch = numberedRe.match(rest);
        if (numberedMatch.hasMatch()) {
            flushRuns();
            BlockData data;
            data.type = Block::NumberedList;
            data.content = numberedMatch.captured(1);
            data.indentLevel = indentLevel;
            data.attributes = lineAttrs;
            blocks.append(data);
            afterListLine = true;
            ++i;
            continue;
        }

        // A continuation line: indented, carrying no marker of its own, and
        // directly under a list item. It belongs to that item — this is how
        // a wrapped item survives a round trip, and how an LLM's wrapped
        // list arrives intact. Every other construct (fence, table, image,
        // quote) is claimed by the branches above, so what reaches here is
        // prose. A blank line ends the item, since prevWasListLine is false
        // on the line after it.
        if (prevWasListLine && indentChars > 0 && !blocks.isEmpty()
            && Block::isListFamily(blocks.last().type)) {
            BlockData &item = blocks.last();
            item.content += QLatin1Char('\n') + rest;
            if (!lineAttrs.isEmpty() && item.attributes.isEmpty())
                item.attributes = lineAttrs;
            afterListLine = true;
            ++i;
            continue;
        }

        // Headings are single lines at the margin, four levels
        // (features.md §1.2.2); "#text" stays literal
        auto appendHeading = [&](Block::BlockType type, const QString &text) {
            flushRuns();
            BlockData data;
            data.type = type;
            data.content = text;
            data.attributes = lineAttrs;
            blocks.append(data);
        };
        // Five and six hashes map to Heading4 — the same squaring-up
        // philosophy as ragged table rows. Lossy (a reload demotes "#####"
        // to "####"), and accepted as such. Seven or more stay literal.
        if (line.startsWith(QStringLiteral("###### "))) {
            appendHeading(Block::Heading4, line.mid(7));
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("##### "))) {
            appendHeading(Block::Heading4, line.mid(6));
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("#### "))) {
            appendHeading(Block::Heading4, line.mid(5));
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("### "))) {
            appendHeading(Block::Heading3, line.mid(4));
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("## "))) {
            appendHeading(Block::Heading2, line.mid(3));
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("# "))) {
            appendHeading(Block::Heading1, line.mid(2));
            ++i;
            continue;
        }

        // Plain text: consecutive lines join into one paragraph block
        // (pinned: "Line one\nLine two" is a single block)
        flushQuote();
        paragraphRun.append(line);
        if (!lineAttrs.isEmpty())
            paragraphAttrs = lineAttrs;
        ++i;
    }

    flushRuns();
    return blocks;
}

void DocumentSerializer::loadIntoModel(BlockModel *model, const QString &markdown)
{
    if (!model) return;

    QList<Block::State> states = parse(markdown);
    if (states.isEmpty())
        states.append(Block::State());

    model->replaceAllBlocksInternal(states);
}
