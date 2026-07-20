// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentsearch.h"
#include "blockmodel.h"
#include "block.h"
#include "blockeditorengine.h"
#include "undostack.h"
#include "perflog.h"

#include <QAbstractItemModel>
#include <QRegularExpression>

#include <utility>

namespace {

// Word classification consistent with DocumentSelection's snapping:
// letters, digits, and underscore are word characters.
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// The regex the current options call for; wholeWord wraps the pattern so
// the two options compose.
QRegularExpression compiledPattern(const QString &query, bool caseSensitive,
                                   bool wholeWord)
{
    QString pattern = wholeWord
        ? QStringLiteral("\\b(?:%1)\\b").arg(query)
        : query;
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (!caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, options);
}

} // namespace

DocumentSearch::DocumentSearch(QObject *parent)
    : QObject(parent)
{
}

void DocumentSearch::setModel(BlockModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &DocumentSearch::scheduleRecompute);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &DocumentSearch::scheduleRecompute);
        connect(m_model, &QAbstractItemModel::rowsMoved,
                this, &DocumentSearch::scheduleRecompute);
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &DocumentSearch::scheduleRecompute);
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &,
                       const QList<int> &roles) {
                    // Matches depend on content and structure only; ignore
                    // checked/ordinal churn.
                    if (roles.isEmpty() || roles.contains(BlockModel::ContentRole))
                        scheduleRecompute();
                });
    }
    recompute();
}

// ---- properties ----

void DocumentSearch::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    recompute();
}

void DocumentSearch::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    recompute();
}

void DocumentSearch::setCaseSensitive(bool on)
{
    if (m_caseSensitive == on)
        return;
    m_caseSensitive = on;
    emit optionsChanged();
    recompute();
}

void DocumentSearch::setWholeWord(bool on)
{
    if (m_wholeWord == on)
        return;
    m_wholeWord = on;
    emit optionsChanged();
    recompute();
}

void DocumentSearch::setUseRegex(bool on)
{
    if (m_useRegex == on)
        return;
    m_useRegex = on;
    emit optionsChanged();
    recompute();
}

void DocumentSearch::setPreserveCase(bool on)
{
    if (m_preserveCase == on)
        return;
    m_preserveCase = on;
    emit optionsChanged();
    // Preserve case affects replacements only, never the match set.
}

void DocumentSearch::setInSelectionOnly(bool on)
{
    if (m_inSelectionOnly == on)
        return;
    m_inSelectionOnly = on;
    emit optionsChanged();
    recompute();
}

// ---- seeding and navigation ----

void DocumentSearch::setActiveCursor(int blockIndex, int mdPos)
{
    m_seedId = idAt(blockIndex);
    m_seedDisplayPos = displayPosition(blockIndex, mdPos);
}

void DocumentSearch::next()
{
    if (m_matches.isEmpty())
        return;
    setCurrent(m_current < 0 ? 0 : (m_current + 1) % m_matches.size());
}

void DocumentSearch::previous()
{
    if (m_matches.isEmpty())
        return;
    setCurrent(m_current < 0 ? m_matches.size() - 1
                             : (m_current - 1 + m_matches.size()) % m_matches.size());
}

void DocumentSearch::setCurrent(int flatIndex)
{
    if (flatIndex == m_current)
        return;
    m_current = flatIndex;
    if (m_current >= 0 && m_current < m_matches.size()) {
        // Navigation moves the seed with the user.
        const Match &m = m_matches.at(m_current);
        m_seedId = idAt(m.blockIndex);
        m_seedDisplayPos = m.start;
    }
    bumpRevision();
}

QVariantList DocumentSearch::matchesForBlock(int blockIndex) const
{
    QVariantList out;
    for (int flat : m_byBlock.value(blockIndex)) {
        const Match &m = m_matches.at(flat);
        out.append(QVariantMap{
            {QStringLiteral("start"), m.start},
            {QStringLiteral("length"), m.length},
            {QStringLiteral("current"), flat == m_current},
        });
    }
    return out;
}

QVariantMap DocumentSearch::currentMatchInfo() const
{
    if (m_current < 0 || m_current >= m_matches.size())
        return {{QStringLiteral("found"), false}};
    const Match &m = m_matches.at(m_current);
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("blockIndex"), m.blockIndex},
        {QStringLiteral("start"), m.start},
        {QStringLiteral("length"), m.length},
        // Markdown coordinate of the match start: what the QML layer
        // needs for focusAtPosition and the scroll fine adjust (their
        // engine mappings speak markdown, not display).
        {QStringLiteral("mdStart"), markdownPosition(m.blockIndex, m.start)},
    };
}

int DocumentSearch::markdownPosition(int blockIndex, int displayPos) const
{
    Block *block = m_model ? m_model->blockAt(blockIndex) : nullptr;
    if (!block)
        return 0;
    if (block->blockType() == Block::CodeBlock)
        return qBound(0, displayPos, static_cast<int>(block->content().length()));
    return BlockEditorEngine::documentToMarkdown(block->content(),
                                                 QList<int>(), displayPos);
}

// ---- in-selection domain ----

void DocumentSearch::setBlockDomain(const QVariantList &indexes)
{
    const int before = m_revision;
    m_domainMode = NoDomain;
    m_domainIds.clear();
    for (const QVariant &v : indexes) {
        const QString id = idAt(v.toInt());
        if (!id.isEmpty())
            m_domainIds.insert(id);
    }
    if (!m_domainIds.isEmpty())
        m_domainMode = BlockDomain;
    recompute();
    if (m_revision == before)
        bumpRevision(); // hasDomain is observable even with no matches
}

void DocumentSearch::setTextDomain(int startIndex, int startPos,
                                   int endIndex, int endPos)
{
    const int before = m_revision;
    if (startIndex > endIndex || (startIndex == endIndex && startPos > endPos)) {
        std::swap(startIndex, endIndex);
        std::swap(startPos, endPos);
    }
    m_domainStartId = idAt(startIndex);
    m_domainEndId = idAt(endIndex);
    m_domainStartMd = startPos;
    m_domainEndMd = endPos;
    m_domainMode = (!m_domainStartId.isEmpty() && !m_domainEndId.isEmpty())
                       ? TextDomain : NoDomain;
    recompute();
    if (m_revision == before)
        bumpRevision();
}

void DocumentSearch::clearDomain()
{
    if (m_domainMode == NoDomain)
        return;
    m_domainMode = NoDomain;
    m_domainIds.clear();
    m_domainStartId.clear();
    m_domainEndId.clear();
    const int before = m_revision;
    recompute();
    if (m_revision == before)
        bumpRevision();
}

bool DocumentSearch::matchInDomain(const Match &match) const
{
    if (!m_inSelectionOnly || m_domainMode == NoDomain)
        return true;
    if (m_domainMode == BlockDomain)
        return m_domainIds.contains(idAt(match.blockIndex));

    // Text domain: the match must lie entirely inside the range. Edges
    // resolve at call time so the domain follows moved blocks.
    int sIdx = indexOfId(m_domainStartId);
    int eIdx = indexOfId(m_domainEndId);
    int sMd = m_domainStartMd;
    int eMd = m_domainEndMd;
    if (sIdx < 0 || eIdx < 0)
        return true; // handled (cleared) by recompute's validation
    if (sIdx > eIdx || (sIdx == eIdx && sMd > eMd)) {
        std::swap(sIdx, eIdx);
        std::swap(sMd, eMd);
    }
    if (match.blockIndex < sIdx || match.blockIndex > eIdx)
        return false;
    if (match.blockIndex == sIdx && match.start < displayPosition(sIdx, sMd))
        return false;
    if (match.blockIndex == eIdx
        && match.start + match.length > displayPosition(eIdx, eMd))
        return false;
    return true;
}

// ---- replace ----

QString DocumentSearch::finalReplacement(const QString &replacement,
                                         const Match &match) const
{
    QString result = m_useRegex
        ? substituteCaptures(replacement, match.captures)
        : replacement;
    if (m_preserveCase) {
        const QString matched = m_useRegex && !match.captures.isEmpty()
            ? match.captures.first()
            : searchableText(match.blockIndex).mid(match.start, match.length);
        result = applyPreserveCase(result, matched);
    }
    return result;
}

bool DocumentSearch::replaceCurrent(const QString &replacement)
{
    if (!m_model || m_patternError || m_current < 0 || m_current >= m_matches.size())
        return false;

    const Match m = m_matches.at(m_current);
    Block *block = m_model->blockAt(m.blockIndex);
    if (!block)
        return false;

    const bool verbatim = isVerbatimBlock(m.blockIndex);
    const QString repl = finalReplacement(replacement, m);
    const ReplaceResult r = replaceRange(block->content(), verbatim,
                                         m.start, m.start + m.length, repl);

    applyContentUpdates(QStringLiteral("Replace"), {{m.blockIndex, r.content}});

    // Seed just past the replacement so the current match advances to the
    // next remaining one.
    m_seedId = idAt(m.blockIndex);
    m_seedDisplayPos = verbatim
        ? r.mdEnd
        : BlockEditorEngine::markdownToDocument(r.content, QList<int>(), r.mdEnd);
    recomputeNow();
    return true;
}

int DocumentSearch::replaceAll(const QString &replacement)
{
    if (!m_model || m_patternError || m_matches.isEmpty())
        return 0;

    // One snapshot; per block right-to-left so earlier display positions
    // stay valid as lengths change.
    QList<QPair<int, QString>> updates;
    int replaced = 0;
    QList<int> blocks = m_byBlock.keys();
    std::sort(blocks.begin(), blocks.end());
    for (int blockIndex : blocks) {
        Block *block = m_model->blockAt(blockIndex);
        if (!block)
            continue;
        const bool verbatim = isVerbatimBlock(blockIndex);
        QString content = block->content();
        const QList<int> flats = m_byBlock.value(blockIndex); // ascending
        for (int i = flats.size() - 1; i >= 0; --i) {
            const Match &m = m_matches.at(flats.at(i));
            const QString repl = finalReplacement(replacement, m);
            content = replaceRange(content, verbatim,
                                   m.start, m.start + m.length, repl).content;
            ++replaced;
        }
        updates.append({blockIndex, content});
    }

    applyContentUpdates(QStringLiteral("Replace All"), updates);
    recomputeNow();
    return replaced;
}

QVariantList DocumentSearch::previewReplacements(const QString &replacement) const
{
    QVariantList rows;
    const int contextChars = 30;
    for (const Match &m : m_matches) {
        const QString text = searchableText(m.blockIndex);
        const int matchEnd = m.start + m.length;
        int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, m.start - 1));
        lineStart = (m.start == 0) ? 0 : lineStart + 1;
        int lineEnd = text.indexOf(QLatin1Char('\n'), matchEnd);
        if (lineEnd < 0)
            lineEnd = text.length();

        const int prefixFrom = qMax(lineStart, m.start - contextChars);
        QString prefix = text.mid(prefixFrom, m.start - prefixFrom);
        if (prefixFrom > lineStart)
            prefix.prepend(QStringLiteral("…"));
        const int suffixTo = qMin(lineEnd, matchEnd + contextChars);
        QString suffix = text.mid(matchEnd, suffixTo - matchEnd);
        if (suffixTo < lineEnd)
            suffix.append(QStringLiteral("…"));

        rows.append(QVariantMap{
            {QStringLiteral("blockIndex"), m.blockIndex},
            {QStringLiteral("prefix"), prefix},
            {QStringLiteral("matched"), text.mid(m.start, m.length)},
            {QStringLiteral("replacement"), finalReplacement(replacement, m)},
            {QStringLiteral("suffix"), suffix},
        });
    }
    return rows;
}

void DocumentSearch::applyContentUpdates(const QString &macroName,
                                         const QList<QPair<int, QString>> &updates)
{
    UndoStack *stack = m_model ? m_model->undoStack() : nullptr;
    if (stack)
        stack->beginMacro(macroName);
    for (const auto &update : updates)
        m_model->updateContent(update.first, update.second);
    if (stack) {
        stack->endMacro();
        // A replace is never part of the typing merge window: the next
        // keystroke must not fold into this step.
        stack->breakMerge();
    }
}

// ---- recompute ----

void DocumentSearch::scheduleRecompute()
{
    // No-op while the bar is closed or empty: the typing path pays only
    // this check (§21.7 discipline). Domains still need validation, but
    // they are validated on the next recompute anyway.
    if (!m_active || m_query.isEmpty())
        return;
    if (m_recomputeQueued)
        return;
    m_recomputeQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_recomputeQueued = false;
        recompute();
    }, Qt::QueuedConnection);
}

void DocumentSearch::recomputeNow()
{
    recompute();
}

void DocumentSearch::recompute()
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("search.doc_recompute"),
        QVariantMap{
            {QStringLiteral("queryLength"), m_query.size()},
            {QStringLiteral("active"), m_active},
            {QStringLiteral("blocks"), m_model ? m_model->count() : 0},
        });
    const DomainMode oldDomain = m_domainMode;

    // Validate the domain against the live document: dead ids prune out
    // of a block domain; a text domain with a vanished edge is gone.
    if (m_domainMode == BlockDomain) {
        QSet<QString> alive;
        for (const QString &id : m_domainIds) {
            if (indexOfId(id) >= 0)
                alive.insert(id);
        }
        m_domainIds = alive;
        if (m_domainIds.isEmpty())
            m_domainMode = NoDomain;
    } else if (m_domainMode == TextDomain) {
        if (indexOfId(m_domainStartId) < 0 || indexOfId(m_domainEndId) < 0)
            m_domainMode = NoDomain;
    }

    QList<Match> matches;
    QHash<int, QList<int>> byBlock;
    int current = -1;
    bool patternError = false;

    if (m_model && m_active && !m_query.isEmpty()) {
        // Compile once up front so an invalid pattern is one error state,
        // not one per block.
        if (m_useRegex && !compiledPattern(m_query, m_caseSensitive,
                                           m_wholeWord).isValid()) {
            patternError = true;
        } else {
            const int count = m_model->count();
            const int queryLength = int(m_query.length());
            const bool unrestrictedDomain =
                !m_inSelectionOnly || m_domainMode == NoDomain;
            int reserve = qMin(count * 4, 65536);
            if (!m_useRegex && queryLength > 0) {
                const int estimatedHighMatchCount =
                    m_model->documentCharCount()
                    / qMax(4, queryLength * 8);
                reserve = qMax(reserve,
                               qMin(estimatedHighMatchCount, 262144));
            }
            matches.reserve(reserve);
            byBlock.reserve(qMin(count, 4096));
            for (int i = 0; i < count; ++i) {
                Block *block = m_model->blockAt(i);
                if (!block)
                    continue;
                const QString &text = block->displayTextRef();
                if (text.isEmpty())
                    continue;
                QList<int> blockMatchIndexes;
                const auto appendMatch = [&](int start, int length) {
                    Match m;
                    m.blockIndex = i;
                    m.start = start;
                    m.length = length;
                    if (unrestrictedDomain || matchInDomain(m)) {
                        blockMatchIndexes.append(matches.size());
                        matches.append(m);
                    }
                };
                if (!m_useRegex) {
                    const Qt::CaseSensitivity cs =
                        m_caseSensitive ? Qt::CaseSensitive
                                        : Qt::CaseInsensitive;
                    int from = 0;
                    const auto wordAt = [&text](int pos) {
                        return pos >= 0 && pos < text.length()
                            && isWordChar(text.at(pos));
                    };
                    if (queryLength == 1) {
                        const QChar needle = m_caseSensitive
                            ? m_query.at(0)
                            : m_query.at(0).toCaseFolded();
                        for (int idx = 0; idx < text.length(); ++idx) {
                            const QChar ch = m_caseSensitive
                                ? text.at(idx)
                                : text.at(idx).toCaseFolded();
                            if (ch != needle)
                                continue;
                            const int end = idx + 1;
                            const bool boundaryOk = !m_wholeWord
                                || (wordAt(idx - 1) != wordAt(idx)
                                    && wordAt(end - 1) != wordAt(end));
                            if (boundaryOk)
                                appendMatch(idx, queryLength);
                        }
                    } else if (!m_wholeWord && unrestrictedDomain) {
                        while (true) {
                            const int idx =
                                static_cast<int>(text.indexOf(m_query, from, cs));
                            if (idx < 0)
                                break;
                            appendMatch(idx, queryLength);
                            from = idx + queryLength;
                        }
                    } else {
                        while (true) {
                            const int idx =
                                static_cast<int>(text.indexOf(m_query, from, cs));
                            if (idx < 0)
                                break;
                            const int end = idx + queryLength;
                            const bool boundaryOk = !m_wholeWord
                                || (wordAt(idx - 1) != wordAt(idx)
                                    && wordAt(end - 1) != wordAt(end));
                            if (boundaryOk) {
                                appendMatch(idx, queryLength);
                                from = end;
                            } else {
                                from = idx + 1;
                            }
                        }
                    }
                } else {
                    QList<Match> found = scanText(text, m_query, m_caseSensitive,
                                                  m_wholeWord, m_useRegex);
                    for (Match &m : found) {
                        m.blockIndex = i;
                        if (matchInDomain(m)) {
                            blockMatchIndexes.append(matches.size());
                            matches.append(m);
                        }
                    }
                }
                if (!blockMatchIndexes.isEmpty())
                    byBlock.insert(i, blockMatchIndexes);
            }
            if (!matches.isEmpty()) {
                const int seedIdx = indexOfId(m_seedId);
                current = 0; // wrap fallback: the document's first match
                if (seedIdx >= 0) {
                    for (int i = 0; i < matches.size(); ++i) {
                        const Match &m = matches.at(i);
                        if (m.blockIndex > seedIdx
                            || (m.blockIndex == seedIdx
                                && m.start >= m_seedDisplayPos)) {
                            current = i;
                            break;
                        }
                    }
                }
            }
        }
    }

    const bool changed = matches != m_matches || current != m_current
        || patternError != m_patternError || m_domainMode != oldDomain;
    m_matches = std::move(matches);
    m_byBlock = std::move(byBlock);
    m_current = current;
    m_patternError = patternError;
    if (changed)
        bumpRevision();
    perf.addContext(QStringLiteral("matches"), m_matches.size());
    perf.addContext(QStringLiteral("patternError"), m_patternError);
}

void DocumentSearch::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}

// ---- model helpers ----

QString DocumentSearch::idAt(int index) const
{
    if (!m_model)
        return QString();
    Block *block = m_model->blockAt(index);
    return block ? block->blockId() : QString();
}

int DocumentSearch::indexOfId(const QString &id) const
{
    if (!m_model || id.isEmpty())
        return -1;
    const int count = m_model->count();
    for (int i = 0; i < count; ++i) {
        Block *block = m_model->blockAt(i);
        if (block && block->blockId() == id)
            return i;
    }
    return -1;
}

bool DocumentSearch::isVerbatimBlock(int blockIndex) const
{
    Block *block = m_model ? m_model->blockAt(blockIndex) : nullptr;
    return block && block->blockType() == Block::CodeBlock;
}

QString DocumentSearch::searchableText(int blockIndex) const
{
    Block *block = m_model ? m_model->blockAt(blockIndex) : nullptr;
    if (!block)
        return QString();
    // Display text: what the user sees. Code blocks are verbatim —
    // their content IS the display. Todo metadata tails are chrome and
    // are stripped by the block's derived display cache.
    return block->displayText();
}

int DocumentSearch::displayPosition(int blockIndex, int mdPos) const
{
    Block *block = m_model ? m_model->blockAt(blockIndex) : nullptr;
    if (!block)
        return 0;
    if (block->blockType() == Block::CodeBlock)
        return qBound(0, mdPos, static_cast<int>(block->content().length()));
    return BlockEditorEngine::markdownToDocument(block->content(),
                                                 QList<int>(), mdPos);
}

// ---- pure helpers ----

QList<DocumentSearch::Match> DocumentSearch::scanText(
    const QString &text, const QString &query, bool caseSensitive,
    bool wholeWord, bool useRegex, bool *patternError)
{
    if (patternError)
        *patternError = false;
    QList<Match> matches;
    if (query.isEmpty() || text.isEmpty())
        return matches;

    if (useRegex) {
        const QRegularExpression re = compiledPattern(query, caseSensitive,
                                                      wholeWord);
        if (!re.isValid()) {
            if (patternError)
                *patternError = true;
            return matches;
        }
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            // A match must cover at least one character; zero-length
            // matches would never terminate navigation or replace.
            if (m.capturedLength(0) == 0)
                continue;
            Match match;
            match.start = static_cast<int>(m.capturedStart(0));
            match.length = static_cast<int>(m.capturedLength(0));
            match.captures = m.capturedTexts();
            matches.append(match);
        }
        return matches;
    }

    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive
                                                 : Qt::CaseInsensitive;
    // Word boundaries use \b's transition rule so plain whole-word and
    // regex whole-word (which wraps the pattern in \b) agree on every
    // query, punctuation-edged ones included.
    const auto wordAt = [&text](int i) {
        return i >= 0 && i < text.length() && isWordChar(text.at(i));
    };
    int from = 0;
    while (true) {
        const int idx = static_cast<int>(text.indexOf(query, from, cs));
        if (idx < 0)
            break;
        const int end = idx + static_cast<int>(query.length());
        const bool boundaryOk = !wholeWord
            || (wordAt(idx - 1) != wordAt(idx)
                && wordAt(end - 1) != wordAt(end));
        if (boundaryOk) {
            Match match;
            match.start = idx;
            match.length = static_cast<int>(query.length());
            matches.append(match);
            from = end; // matches never overlap
        } else {
            from = idx + 1;
        }
    }
    return matches;
}

QString DocumentSearch::substituteCaptures(const QString &replacement,
                                           const QStringList &captures)
{
    QString out;
    out.reserve(replacement.size());
    for (int i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement.at(i);
        if (c != QLatin1Char('$') || i + 1 >= replacement.size()) {
            out.append(c);
            continue;
        }
        const QChar next = replacement.at(i + 1);
        if (next == QLatin1Char('$')) {
            out.append(QLatin1Char('$'));
            ++i;
        } else if (next == QLatin1Char('&')) {
            out.append(captures.value(0));
            ++i;
        } else if (next.isDigit() && next != QLatin1Char('0')) {
            out.append(captures.value(next.digitValue()));
            ++i;
        } else {
            out.append(c);
        }
    }
    return out;
}

QString DocumentSearch::applyPreserveCase(const QString &replacement,
                                          const QString &matched)
{
    bool hasLetter = false;
    bool allUpper = true;
    bool allLower = true;
    for (const QChar c : matched) {
        if (!c.isLetter())
            continue;
        hasLetter = true;
        if (c.isLower())
            allUpper = false;
        if (c.isUpper())
            allLower = false;
    }
    if (!hasLetter)
        return replacement;
    if (allUpper && matched.length() > 1)
        return replacement.toUpper();
    if (allLower)
        return replacement.toLower();

    // Capitalized: first letter upper, every other letter lower.
    bool capitalized = matched.at(0).isUpper();
    for (int i = 1; capitalized && i < matched.size(); ++i) {
        if (matched.at(i).isLetter() && matched.at(i).isUpper())
            capitalized = false;
    }
    if (capitalized && !replacement.isEmpty()) {
        QString out = replacement.toLower();
        out[0] = out.at(0).toUpper();
        return out;
    }
    return replacement; // mixed case: as typed
}

DocumentSearch::ReplaceResult DocumentSearch::replaceRange(
    const QString &content, bool verbatim, int displayStart, int displayEnd,
    const QString &replacement)
{
    ReplaceResult result;
    if (verbatim) {
        // Code content: display IS markdown; splice directly.
        result.content = content.left(displayStart) + replacement
                         + content.mid(displayEnd);
        result.mdEnd = displayStart + static_cast<int>(replacement.length());
        return result;
    }
    // Select-and-type semantics: the cut contract handles
    // markers — partial spans keep theirs, fully covered spans go.
    const BlockEditorEngine::EditResult cut = BlockEditorEngine::cutRangeResult(
        content, QList<int>(), displayStart, displayEnd);
    result.content = cut.markdown;
    result.content.insert(cut.mdEditEnd, replacement);
    result.mdEnd = cut.mdEditEnd + static_cast<int>(replacement.length());
    return result;
}
