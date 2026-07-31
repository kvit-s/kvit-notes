// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "block.h"
#include "blockkinddef.h"
#include "blockkindregistry.h"
#include "blockkinds.h"

namespace {
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
    , m_kind(BlockKindDefs::builtin(BlockKind::Paragraph))
    , m_type(Paragraph)
    , m_content("")
    , m_indentLevel(0)
{
}

Block::Block(BlockType type, const QString &content, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_kind(nullptr)
    , m_type(typeFromInt(static_cast<int>(type)))
    , m_content(content)
    , m_indentLevel(0)
{
    refreshKind();
}

Block::Block(const Block &other, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    // The kind and the registry it came from are copied rather than
    // resolved: a copy of a block carrying a module's kind must keep it, and
    // the built-in resolution could not find one.
    , m_kind(other.m_kind)
    , m_kindRegistry(other.m_kindRegistry)
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
        refreshKind();
        invalidateCache();
        emit blockTypeChanged();
    }
}

void Block::setContent(const QString &content)
{
    if (m_content != content) {
        m_content = content;
        // The content decides the kind for one of them: an image expression
        // whose URL names a web page is a preview card rather than a picture.
        refreshKind();
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
        // A fence's language is what selects its kind, and the kind is what
        // answers the text projections. Retagging a ```python fence as
        // ```query used to leave the cached text alone, because the language
        // was not part of what the text was computed from; it is now.
        refreshKind();
        invalidateCache();
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

void Block::setKindRegistry(const BlockKindRegistry *registry)
{
    if (m_kindRegistry == registry)
        return;
    m_kindRegistry = registry;
    refreshKind();
}

void Block::refreshKind()
{
    const State s = state();
    const BlockKindDef *resolved = m_kindRegistry ? m_kindRegistry->defFor(s)
                                                  : BlockKindDefs::forState(s);
    if (m_kind == resolved)
        return;
    m_kind = resolved;
    invalidateCache();
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

    // The kind answers what the text is. It used to be answered here, with
    // two comparisons against a block type — one for the code block, whose
    // text is its content verbatim, and one for the to-do, whose metadata
    // tail is chrome. A kind added afterwards silently got the third branch,
    // whether or not that was right for it.
    const State s = state();
    m_cachedDisplayText = m_kind->displayText(s);

    // The counts are taken from the STATISTICS projection, not the displayed
    // one. They are the same string for every kind today, and they are
    // separate here because they fail differently: a wrong display string is
    // a cosmetic defect in an outline entry, while a wrong count is written
    // into the vault's sidecar index and read back on the next cold start.
    const QString counted = m_kind->statisticsText(s);
    m_cachedWordCount = countWords(counted);
    m_cachedCharsWithSpaces = countChars(counted, true);
    m_cachedCharsNoSpaces = countChars(counted, false);
    m_textCacheValid = true;
}
