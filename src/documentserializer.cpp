// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "blockattributes.h"
#include "imageassets.h"
#include "llmnormalizer.h"
#include "diagrams/diagramclassifier.h"
#include "diagrams/diagramrepair.h"
#include "tabledata.h"
#include "insertblockcommand.h"
#include "undostack.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

// The file format (basic-features.md §7, phase4-plan.md step 2): blocks
// separated by blank lines, except that consecutive list-family blocks
// (bullet / numbered / todo) are separated by single newlines — the
// natural "tight list" markdown shape. Structural prefixes are block
// state, not content: heading hashes, list markers, todo checkboxes,
// quote angles, code fences, and divider dashes are added on serialize
// and stripped on parse. Code-fence content is taken verbatim, so it may
// contain blank lines; everything else is line-classified.

namespace {

// Two spaces per indent level before a list marker; tabs also count one
// level each on parse (phase4-plan.md design decision 7).
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

QString indentPrefix(const Block *block)
{
    if (!Block::isListFamily(block->blockType()))
        return QString();
    return QString(2 * block->indentLevel(), QLatin1Char(' '));
}

// A fence line: three or more backticks — or tildes (llm-normalization.md
// fix 8) — optionally followed by an info string (the language). Returns
// the fence length, or 0.
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

// Serialized fences must be longer than any backtick-run line inside the
// content, or that line would close the fence early (phase4-plan.md
// design decision 6).
// The ingest character-diagram tagging pass (diagrams-prd.md §7.1, §7.2). Only
// the info strings that make no semantic claim about their contents are
// eligible: untagged, `text`, `plaintext`, and `ascii` — the four wrappers LLMs
// routinely use for character diagrams. A high-confidence body has its info
// string rewritten to `diagram`; every other language (including an already
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

QString fenceFor(const QString &content)
{
    int longest = 2;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        bool allTicks = true;
        for (const QChar &c : trimmed) {
            if (c != QLatin1Char('`')) {
                allTicks = false;
                break;
            }
        }
        if (allTicks)
            longest = qMax(longest, int(trimmed.size()));
    }
    return QString(qMax(3, longest + 1), QLatin1Char('`'));
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

    QList<int> sorted;
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int idx = value.toInt(&ok);
        if (ok && idx >= 0 && idx < model->count() && !sorted.contains(idx))
            sorted.append(idx);
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

    const QString content = block->content();

    // The block's base markdown, then the trailing <!--kvit ...--> tag
    // re-attached (a no-op when the block has no attributes, so an unstyled
    // block is byte-identical — phase12 decision 1).
    const QString base = [&]() -> QString {
    switch (block->blockType()) {
    case Block::Heading1:
        return QStringLiteral("# ") + content;
    case Block::Heading2:
        return QStringLiteral("## ") + content;
    case Block::Heading3:
        return QStringLiteral("### ") + content;
    case Block::Heading4:
        return QStringLiteral("#### ") + content;
    case Block::BulletList:
        return indentPrefix(block) + QStringLiteral("- ") + content;
    case Block::NumberedList:
        return indentPrefix(block) + QString::number(qMax(1, ordinal)) +
               QStringLiteral(". ") + content;
    case Block::Todo:
        return indentPrefix(block) +
               (block->checked() ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ")) +
               content;
    case Block::Quote: {
        // One quote block per contiguous run at one depth; empty content
        // lines write the depth's markers with no trailing space. The depth
        // is indentLevel + 1 (nested quotes, decision 11).
        const int depth = block->indentLevel() + 1;
        QString prefix;
        for (int d = 0; d < depth; ++d)
            prefix += QStringLiteral("> ");
        const QStringList lines = content.split(QLatin1Char('\n'));
        QStringList quoted;
        for (const QString &line : lines)
            quoted.append(line.isEmpty() ? prefix.trimmed() : prefix + line);
        return quoted.join(QLatin1Char('\n'));
    }
    case Block::CodeBlock: {
        const QString fence = fenceFor(content);
        QString result = fence + block->language() + QLatin1Char('\n');
        if (!content.isEmpty())
            result += content + QLatin1Char('\n');
        result += fence;
        return result;
    }
    case Block::MathBlock: {
        // A $$ … $$ display-math fence (decision 12). Canonical form is
        // multi-line; a hand-authored single-line $$x$$ normalizes to it.
        QString result = QStringLiteral("$$\n");
        if (!content.isEmpty())
            result += content + QLatin1Char('\n');
        result += QStringLiteral("$$");
        return result;
    }
    case Block::Divider:
        return QStringLiteral("---");
    case Block::Table:
        // Canonicalize on save (decision 8): a hand-authored ragged/padded
        // table squares up, while a Kvit-written table round-trips identically.
        return TableData::serialize(TableData::parse(content));
    case Block::Callout: {
        // > [!type][-] Title  header, then "> " body lines. Folded writes
        // the '-' marker; expanded writes none (a hand-authored '+' thus
        // normalizes to no marker — a documented normalization).
        QString header = QStringLiteral("> [!") + block->language()
                       + QStringLiteral("]");
        if (block->checked())
            header += QLatin1Char('-');
        if (!block->calloutTitle().isEmpty())
            header += QLatin1Char(' ') + block->calloutTitle();
        QStringList out;
        // A callout is multi-line, so its attribute tag rides the header line
        // (phase12 decision 1) rather than trailing the last body line; the
        // outer re-attach below skips callouts for this reason.
        out << BlockAttributes::attachTag(header, block->attributes());
        if (!content.isEmpty()) {
            for (const QString &line : content.split(QLatin1Char('\n')))
                out << (line.isEmpty() ? QStringLiteral(">")
                                       : QStringLiteral("> ") + line);
        }
        return out.join(QLatin1Char('\n'));
    }
    case Block::Paragraph:
    default:
        return content;
    }
    }();

    // A callout embeds its tag on the header line above; every other block
    // type carries it as a trailing tag on its single/last line.
    if (block->blockType() == Block::Callout)
        return base;
    return BlockAttributes::attachTag(base, block->attributes());
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
    // funnels through parse, so the LLM-markdown repairs live here
    // (llm-normalization.md). A no-op on canonical serializer output.
    const QString normalized = LlmNormalizer::normalize(markdown);

    const QStringList lines = normalized.split(QLatin1Char('\n'));

    // Accumulators for multi-line blocks
    QStringList paragraphRun;
    QStringList quoteRun;
    int quoteDepth = 1;   // nested-quote depth of the current run ("> " count)
    // The <!--kvit ...--> attribute payload split off a line in the current
    // run (phase12 decision 1); last non-empty line wins, applied on flush.
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
    // block (phase10-plan.md decisions 6, 7): the type reuses `language`,
    // the fold state reuses `checked` ('-' collapsed), the title its own
    // field, and the remaining lines are the multi-paragraph body.
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
            // Nested-quote depth rides indentLevel (decision 11), clamped like
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

    int i = 0;
    while (i < lines.size()) {
        // Split a trailing <!--kvit ...--> attribute tag off this line
        // (phase12 decision 1) before classifying. A tag-free line is
        // returned unchanged, so existing documents parse byte-identically.
        // Verbatim regions (code/math fences, table body) read the original
        // `lines` in their inner loops, so their content is never stripped.
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
            ++i;
            bool closed = false;
            while (i < lines.size()) {
                if (isClosingFence(lines.at(i), openLen, fenceChar)) {
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
            // Ingest character-diagram tagging (diagrams-prd.md §7.1): rewrite an
            // eligible untagged fence to `diagram`.
            data.language = classifyFenceLanguage(language, data.content);
            // Ingest straightening (§7.5): a diagram fence has its LLM
            // alignment flaws conservatively repaired, in the same pass
            // family as the LLM markdown normalizations — idempotent,
            // divergence-armed .bak, undoable on paste.
            {
                const QString id = data.language.trimmed().toLower();
                if (id == QLatin1String("diagram")
                    || id == QLatin1String("text-diagram")
                    || id == QLatin1String("ascii-diagram"))
                    data.content = DiagramRepair::repair(data.content);
            }
            blocks.append(data);
            continue;
        }

        // A $$ … $$ fence becomes a MathBlock (phase10-plan.md decision 12):
        // verbatim TeX, like a code fence but with $$ delimiters. Both the
        // multi-line form ($$ on its own line) and a single-line $$x$$ are
        // recognized; the latter normalizes to multi-line on save.
        {
            const QString t = line.trimmed();
            const bool singleLine = t.length() > 4
                && t.startsWith(QLatin1String("$$"))
                && t.endsWith(QLatin1String("$$"));
            if (t == QLatin1String("$$") || singleLine) {
                flushRuns();
                BlockData data;
                data.type = Block::MathBlock;
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
        // rows) becomes a Table block (phase10-plan.md decision 8). Content is
        // the raw table markdown; serializeBlock canonicalizes it on save.
        if (i + 1 < lines.size()
            && TableData::looksLikeTableStart(line, lines[i + 1])) {
            flushRuns();
            QStringList tableLines;
            tableLines << line << lines[i + 1];
            int j = i + 2;
            while (j < lines.size() && !lines[j].trimmed().isEmpty()
                   && lines[j].contains('|')) {
                tableLines << lines[j];
                ++j;
            }
            BlockData data;
            data.type = Block::Table;
            data.content = tableLines.join(QLatin1Char('\n'));
            blocks.append(data);
            i = j;
            continue;
        }

        // A lone image/media expression on its own (un-indented) line becomes
        // an Image or Media block (phase10-plan.md decision 4): the whole line
        // must be one ![alt|width](path "caption"), so ![…] mid-prose stays
        // literal. The category is by file extension.
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
        // one block; a depth change starts a new block (nested quotes,
        // decision 11 — the flat model represents depth, not nesting).
        if (line.startsWith(QStringLiteral("> ")) || line == QStringLiteral(">")
            || line.startsWith(QStringLiteral(">>"))) {
            flushParagraph();
            int depth = 0;
            QString rest = line;
            while (rest.startsWith(QStringLiteral("> "))) {
                ++depth;
                rest = rest.mid(2);
            }
            if (rest == QStringLiteral(">")) {   // trailing bare marker
                ++depth;
                rest.clear();
            }
            if (depth == 0)   // a bare leading ">" with no space
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
        // Five and six hashes map to Heading4 (llm-normalization.md fix 9)
        // — the same squaring-up philosophy as ragged table rows. Lossy (a
        // reload demotes "#####" to "####"), accepted and recorded there.
        // Seven or more stay literal.
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

    const QList<BlockData> blocks = parse(markdown);
    QList<Block::State> states;
    states.reserve(qMax(1, blocks.size()));
    for (const BlockData &data : blocks) {
        Block::State state;
        state.type = data.type;
        state.content = data.content;
        state.indentLevel = data.indentLevel;
        state.checked = data.checked;
        state.language = data.language;
        state.calloutTitle = data.calloutTitle;
        state.attributes = data.attributes;
        states.append(state);
    }

    if (states.isEmpty()) {
        Block::State empty;
        states.append(empty);
    }

    model->replaceAllBlocksInternal(states);
}
