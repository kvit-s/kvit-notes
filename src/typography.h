// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TYPOGRAPHY_H
#define TYPOGRAPHY_H

#include <QObject>
#include <QString>
#include <QStringList>

class SettingsStore;

// Typography settings (features.md §10.2, phase9-plan.md decision 4):
// editor font family, base size, line-height multiplier, paragraph
// spacing, maximum content width, and the monospace family for code.
// Heading and code sizes derive from the base by the ratios frozen
// from the pre-Phase-9 defaults — H1 32/15, H2 24/15, H3 20/15,
// H4 17/15, code 13/15 — so the default rendering is pixel-identical
// and one setting scales the whole document coherently.
//
// All properties notify through one typographyChanged signal, and all
// setters clamp, so a hand-edited settings file cannot produce an
// unusable layout. State lives in the settings store.
class Typography : public QObject
{
    Q_OBJECT

    // "" = the application default font.
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily
                   NOTIFY typographyChanged)
    Q_PROPERTY(int baseSize READ baseSize WRITE setBaseSize
                   NOTIFY typographyChanged)
    // Proportional line height: 1.0 = the font's natural height.
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight
                   NOTIFY typographyChanged)
    // Pixels between blocks (the block list's spacing).
    Q_PROPERTY(int paragraphSpacing READ paragraphSpacing
                   WRITE setParagraphSpacing NOTIFY typographyChanged)
    // 0 = fill the editor pane; otherwise the block column is capped
    // at this many pixels and centered.
    Q_PROPERTY(int maxContentWidth READ maxContentWidth
                   WRITE setMaxContentWidth NOTIFY typographyChanged)
    Q_PROPERTY(QString monoFamily READ monoFamily WRITE setMonoFamily
                   NOTIFY typographyChanged)

public:
    // Clamps: wide enough for every reasonable preference, tight
    // enough that a corrupt settings value cannot break the layout.
    static constexpr int MinBaseSize = 10, MaxBaseSize = 28;
    static constexpr qreal MinLineHeight = 1.0, MaxLineHeight = 2.0;
    static constexpr int MinParagraphSpacing = 0, MaxParagraphSpacing = 40;
    static constexpr int MinContentWidth = 300;  // when non-zero

    explicit Typography(QObject *parent = nullptr);

    void setSettings(SettingsStore *settings);

    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family);
    int baseSize() const { return m_baseSize; }
    void setBaseSize(int size);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal height);
    int paragraphSpacing() const { return m_paragraphSpacing; }
    void setParagraphSpacing(int spacing);
    int maxContentWidth() const { return m_maxContentWidth; }
    void setMaxContentWidth(int width);
    QString monoFamily() const { return m_monoFamily; }
    void setMonoFamily(const QString &family);

    // The type-scale: the pixel size a block type renders at, derived
    // from the base size by the frozen ratios (Block::BlockType ints;
    // headings and code blocks differ, everything else is the base).
    Q_INVOKABLE int sizeForBlockType(int blockType) const;

    // Fixed-pitch families for the mono picker (QFontDatabase).
    Q_INVOKABLE QStringList monospaceFamilies() const;

    // Reset every value to the built-in defaults (the dialog's
    // "Reset typography" button).
    Q_INVOKABLE void resetToDefaults();

signals:
    void typographyChanged();

private:
    void save(const QString &key, const QVariant &value);

    SettingsStore *m_settings = nullptr;
    bool m_loading = false;
    QString m_fontFamily;
    int m_baseSize = 15;
    qreal m_lineHeight = 1.0;
    int m_paragraphSpacing = 8;
    int m_maxContentWidth = 0;
    QString m_monoFamily = QStringLiteral("monospace");
};

#endif // TYPOGRAPHY_H
