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

    Q_PROPERTY(QString blockId READ blockId CONSTANT)
    Q_PROPERTY(BlockType blockType READ blockType WRITE setBlockType NOTIFY blockTypeChanged)
    Q_PROPERTY(QString content READ content WRITE setContent NOTIFY contentChanged)
    Q_PROPERTY(int indentLevel READ indentLevel WRITE setIndentLevel NOTIFY indentLevelChanged)
    Q_PROPERTY(bool checked READ checked WRITE setChecked NOTIFY checkedChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // Callout title. A callout reuses `language` for its type ([!info],
    // [!warning], …) and `checked` for its fold state; only the title line
    // needs its own field. Empty for every other block type.
    Q_PROPERTY(QString calloutTitle READ calloutTitle WRITE setCalloutTitle NOTIFY calloutTitleChanged)
    // Per-block presentation attributes: the canonical payload of the block's
    // trailing <!--kvit ...--> tag (alignment, divider style, callout color,
    // image effects, drop cap, embed size). Empty for an unstyled block, which
    // serializes byte-identically. BlockAttributes is the pure parser;
    // delegates read typed values off this string.
    Q_PROPERTY(QString attributes READ attributes WRITE setAttributes NOTIFY attributesChanged)

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
