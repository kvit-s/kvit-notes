// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCK_H
#define BLOCK_H

#include <QObject>
#include <QString>
#include <QUuid>

class Block : public QObject
{
    Q_OBJECT

    // Every property is read-only to the property system. A block is owned by
    // its BlockModel, and the model is what turns a change into dataChanged,
    // cached-count maintenance, projection invalidation, and an undo step —
    // so a write that goes straight at the object (from QML, or through the
    // meta-object) would leave edited content on a clean undo stack. QML asks
    // the model instead: BlockModel::updateContent, setChecked, updateType,
    // convertBlock, changeIndent, setBlockAttributes. The C++ setters below
    // stay public because the model and its undo commands call them, and the
    // model watches the NOTIFY signals so even a direct C++ write is
    // published.
    Q_PROPERTY(QString blockId READ blockId CONSTANT)
    Q_PROPERTY(BlockType blockType READ blockType NOTIFY blockTypeChanged)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(int indentLevel READ indentLevel NOTIFY indentLevelChanged)
    Q_PROPERTY(bool checked READ checked NOTIFY checkedChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    // Callout title. A callout reuses `language` for its type ([!info],
    // [!warning], …) and `checked` for its fold state; only the title line
    // needs its own field. Empty for every other block type.
    Q_PROPERTY(QString calloutTitle READ calloutTitle NOTIFY calloutTitleChanged)
    // Per-block presentation attributes: the canonical payload of the block's
    // trailing <!--kvit ...--> tag (alignment, divider style, callout color,
    // image effects, drop cap, embed size). Empty for an unstyled block, which
    // serializes byte-identically. BlockAttributes is the pure parser;
    // delegates read typed values off this string.
    Q_PROPERTY(QString attributes READ attributes NOTIFY attributesChanged)

public:
    // Values are persisted (model roles, tests); append only.
    enum BlockType {
        Paragraph = 0,
        Heading1,
        Heading2,
        Heading3,
        BulletList,
        NumberedList,
        Todo,
        Quote,
        CodeBlock,
        Divider,
        // Appended out of order (values are persisted, so Heading4
        // cannot sit beside its siblings): the fourth heading level of
        // features.md §1.2.2, required by the §4.2 block menu.
        Heading4,
        // Wave-2 types, appended so persisted values never renumber. Image
        // and Media store their markdown expression (![alt|width](path
        // "caption")) in content, like the table/kanban container types;
        // Callout carries its type and title as fields over the quote
        // machinery; MathBlock holds a $$ fence verbatim.
        Image,
        Callout,
        MathBlock,
        Media,
        // Table: content is the raw pipe-table markdown; TableData
        // parses/serializes/mutates it.
        Table
    };
    Q_ENUM(BlockType)

    // Highest value the append-only enum currently defines. Persisted files,
    // QML, and the model all hand block types across as plain ints, so every
    // boundary screens them against this range instead of casting blindly:
    // an out-of-range value used to survive into the model, where
    // serialization falls through its switch and the round trip loses the
    // block.
    static constexpr BlockType LastType = Table;
    static bool isValidType(int value)
    {
        return value >= static_cast<int>(Paragraph)
               && value <= static_cast<int>(LastType);
    }
    // Coercion for the restore paths (a persisted file, an undo state) where
    // there is no caller to reject: an unknown type reads as a paragraph, so
    // its text survives.
    static BlockType typeFromInt(int value)
    {
        return isValidType(value) ? static_cast<BlockType>(value) : Paragraph;
    }

    // Indentation depth limit (features.md §3.3). It lives here, not on the
    // model, because every write that can set a level goes through Block —
    // which is the only way to guarantee an over-deep level never reaches
    // serialization, where it would clamp on reload and make the round trip
    // lossy. BlockModel::MaxIndentLevel aliases this.
    static constexpr int MaxIndentLevel = 4;
    static int clampIndent(int level)
    {
        return level < 0 ? 0 : (level > MaxIndentLevel ? MaxIndentLevel : level);
    }

    // Everything that defines a block besides its identity. Undo commands
    // capture and restore this as a unit so no field can be missed when
    // block state grows.
    struct State {
        BlockType type = Paragraph;
        QString content;
        int indentLevel = 0;
        bool checked = false;
        QString language;
        QString calloutTitle;
        QString attributes;
    };

    // A State with a valid type and an in-range indent level. Every path that
    // builds a State from outside data (deserialization, undo capture, the
    // model's own conversions) runs it through here, so validation is stated
    // once rather than per call site.
    static State sanitized(const State &state);

    // The list family shares indentation and tight serialization.
    static bool isListFamily(BlockType type)
    {
        return type == BulletList || type == NumberedList || type == Todo;
    }

    explicit Block(QObject *parent = nullptr);
    explicit Block(BlockType type, const QString &content, QObject *parent = nullptr);
    Block(const Block &other, QObject *parent = nullptr);

    QString blockId() const;
    BlockType blockType() const;
    QString content() const;
    int indentLevel() const;
    bool checked() const;
    QString language() const;
    QString calloutTitle() const;
    QString attributes() const;
    QString displayText() const;
    const QString &displayTextRef() const;
    int wordCount() const;
    int charCount(bool withSpaces = true) const;

    void setBlockType(BlockType type);
    void setContent(const QString &content);
    void setIndentLevel(int level);
    void setChecked(bool checked);
    void setLanguage(const QString &language);
    void setCalloutTitle(const QString &title);
    void setAttributes(const QString &attributes);

    State state() const;
    void setState(const State &state);

signals:
    void blockTypeChanged();
    void contentChanged();
    void indentLevelChanged();
    void checkedChanged();
    void languageChanged();
    void calloutTitleChanged();
    void attributesChanged();

private:
    void invalidateCache() const;
    void ensureTextCache() const;

    QString m_id;
    BlockType m_type;
    QString m_content;
    int m_indentLevel;
    bool m_checked = false;
    QString m_language;
    QString m_calloutTitle;
    QString m_attributes;
    mutable bool m_textCacheValid = false;
    mutable QString m_cachedDisplayText;
    mutable int m_cachedWordCount = 0;
    mutable int m_cachedCharsWithSpaces = 0;
    mutable int m_cachedCharsNoSpaces = 0;
};

#endif // BLOCK_H
