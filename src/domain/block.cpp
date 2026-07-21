// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "block.h"
#include "inlinemarkdown.h"
#include "todometa.h"

namespace {
bool hasDisplayMarkers(const QString &text)
{
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '*':
        case '_':
        case '~':
        case '=':
        case '+':
        case '^':
        case '`':
        case '$':
        case '<':
        case '[':
            return true;
        default:
            break;
        }
    }
    return false;
}

int countWords(const QString &text)
{
    int words = 0;
    bool inWord = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
    }
    return words;
}

// Code points, not UTF-16 code units: a low surrogate is the second half
// of an astral character (emoji), so it never counts on its own — 🙂 is 1,
// not 2. Zero-width joiners and variation selectors are invisible glue,
// also skipped, so 👨‍👩‍👧 counts as its 3 visible people. Approximate but
// fast; skin-tone modifiers still count, accepted.
bool isInvisibleJoiner(QChar ch)
{
    const ushort u = ch.unicode();
    return u == 0x200D || (u >= 0xFE00 && u <= 0xFE0F);
}

int countChars(const QString &text, bool withSpaces)
{
    int chars = 0;
    for (const QChar ch : text) {
        if (ch.isLowSurrogate() || isInvisibleJoiner(ch))
            continue;
        if (!withSpaces && ch.isSpace())
            continue;
        ++chars;
    }
    return chars;
}
} // namespace

Block::Block(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_type(Paragraph)
    , m_content("")
    , m_indentLevel(0)
{
}

Block::Block(BlockType type, const QString &content, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_type(typeFromInt(static_cast<int>(type)))
    , m_content(content)
    , m_indentLevel(0)
{
}

Block::Block(const Block &other, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_type(other.m_type)
    , m_content(other.m_content)
    , m_indentLevel(other.m_indentLevel)
    , m_checked(other.m_checked)
    , m_language(other.m_language)
    , m_calloutTitle(other.m_calloutTitle)
    , m_attributes(other.m_attributes)
{
}

QString Block::blockId() const
{
    return m_id;
}

Block::BlockType Block::blockType() const
{
    return m_type;
}

QString Block::content() const
{
    return m_content;
}

int Block::indentLevel() const
{
    return m_indentLevel;
}

bool Block::checked() const
{
    return m_checked;
}

QString Block::language() const
{
    return m_language;
}

QString Block::calloutTitle() const
{
    return m_calloutTitle;
}

QString Block::attributes() const
{
    return m_attributes;
}

QString Block::displayText() const
{
    ensureTextCache();
    return m_cachedDisplayText;
}

const QString &Block::displayTextRef() const
{
    ensureTextCache();
    return m_cachedDisplayText;
}

int Block::wordCount() const
{
    ensureTextCache();
    return m_cachedWordCount;
}

int Block::charCount(bool withSpaces) const
{
    ensureTextCache();
    return withSpaces ? m_cachedCharsWithSpaces : m_cachedCharsNoSpaces;
}

void Block::setBlockType(BlockType type)
{
    // An unknown value is rejected rather than coerced: the caller passed
    // something that names no block, and turning a heading into a paragraph
    // would be a silent edit. Restore paths that have no caller to reject
    // use setState(), which coerces through sanitized().
    if (!isValidType(static_cast<int>(type)))
        return;
    if (m_type != type) {
        m_type = type;
        invalidateCache();
        emit blockTypeChanged();
    }
}

void Block::setContent(const QString &content)
{
    if (m_content != content) {
        m_content = content;
        invalidateCache();
        emit contentChanged();
    }
}

void Block::setIndentLevel(int level)
{
    const int newLevel = clampIndent(level);
    if (m_indentLevel != newLevel) {
        m_indentLevel = newLevel;
        emit indentLevelChanged();
    }
}

void Block::setChecked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        emit checkedChanged();
    }
}

void Block::setLanguage(const QString &language)
{
    if (m_language != language) {
        m_language = language;
        emit languageChanged();
    }
}

void Block::setCalloutTitle(const QString &title)
{
    if (m_calloutTitle != title) {
        m_calloutTitle = title;
        emit calloutTitleChanged();
    }
}

void Block::setAttributes(const QString &attributes)
{
    if (m_attributes != attributes) {
        m_attributes = attributes;
        emit attributesChanged();
    }
}

Block::State Block::state() const
{
    State s;
    s.type = m_type;
    s.content = m_content;
    s.indentLevel = m_indentLevel;
    s.checked = m_checked;
    s.language = m_language;
    s.calloutTitle = m_calloutTitle;
    s.attributes = m_attributes;
    return s;
}

Block::State Block::sanitized(const State &state)
{
    State s = state;
    s.type = typeFromInt(static_cast<int>(state.type));
    s.indentLevel = clampIndent(state.indentLevel);
    return s;
}

void Block::setState(const State &state)
{
    const State s = sanitized(state);
    setBlockType(s.type);
    setContent(s.content);
    setIndentLevel(s.indentLevel);
    setChecked(s.checked);
    setLanguage(s.language);
    setCalloutTitle(s.calloutTitle);
    setAttributes(s.attributes);
}

void Block::invalidateCache() const
{
    m_textCacheValid = false;
}

void Block::ensureTextCache() const
{
    if (m_textCacheValid)
        return;

    if (m_type == CodeBlock) {
        m_cachedDisplayText = m_content;
    } else {
        const QString source =
            m_type == Todo ? TodoMeta::displayText(m_content) : m_content;
        m_cachedDisplayText = hasDisplayMarkers(source)
            ? InlineMarkdown::displayText(source)
            : source;
    }

    m_cachedWordCount = countWords(m_cachedDisplayText);
    m_cachedCharsWithSpaces = countChars(m_cachedDisplayText, true);
    m_cachedCharsNoSpaces = countChars(m_cachedDisplayText, false);
    m_textCacheValid = true;
}
