// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class SettingsStore;
class QQmlEngine;
class QJSEngine;

// The theme token object: one C++ QObject
// exposing every color the application draws with, as a Q_PROPERTY per
// token. QML binds to the tokens — no hardcoded colors in new QML —
// and the engine's highlighter reads the inline
// tokens from the same object, so both layers restyle
// from one source. All tokens notify through the single themeChanged
// signal: a theme switch re-evaluates every binding wholesale, which
// is the intended (user-rare) whole-window repaint.
//
// Built-in themes are rows in one static table: "light", "dark",
// "sepia". "system" resolves to light or dark from the OS setting
// (QStyleHints::colorScheme, followed live). The chosen theme id and
// the §10.3 overrides (accent, highlight) live in the settings store.
class Theme : public QObject
{
    Q_OBJECT

    // "light" | "dark" | "sepia" | "system" (persisted).
    Q_PROPERTY(QString themeId READ themeId WRITE setThemeId NOTIFY themeChanged)
    // Reduced motion (§14.3): one source every animation and the typewriter
    // scroll reads. motionScale is 0 when on (all motion stilled), 1 when off,
    // so a duration written `150 * theme.motionScale` becomes instant.
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion
                   NOTIFY reducedMotionChanged)
    Q_PROPERTY(qreal motionScale READ motionScale NOTIFY reducedMotionChanged)
    // What themeId currently renders as: never "system".
    Q_PROPERTY(QString resolvedTheme READ resolvedTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)

    // §10.3 customization: "" means the theme's own value.
    Q_PROPERTY(QString accentOverride READ accentOverride
                   WRITE setAccentOverride NOTIFY themeChanged)
    Q_PROPERTY(QString highlightOverride READ highlightOverride
                   WRITE setHighlightOverride NOTIFY themeChanged)

    // Surfaces.
    Q_PROPERTY(QColor windowBackground READ windowBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor panelBackground READ panelBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor listBackground READ listBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor footerBackground READ footerBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor popupBackground READ popupBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor chipBackground READ chipBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor bannerBackground READ bannerBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor codePanelBackground READ codePanelBackground NOTIFY themeChanged)

    // Text.
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY themeChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY themeChanged)
    Q_PROPERTY(QColor bannerText READ bannerText NOTIFY themeChanged)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY themeChanged)

    // Lines and glyphs.
    Q_PROPERTY(QColor border READ border NOTIFY themeChanged)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY themeChanged)
    Q_PROPERTY(QColor quoteBar READ quoteBar NOTIFY themeChanged)
    Q_PROPERTY(QColor mutedGlyph READ mutedGlyph NOTIFY themeChanged)

    // Interactive tints.
    Q_PROPERTY(QColor hoverTint READ hoverTint NOTIFY themeChanged)
    Q_PROPERTY(QColor blockHoverTint READ blockHoverTint NOTIFY themeChanged)
    Q_PROPERTY(QColor focusTint READ focusTint NOTIFY themeChanged)
    Q_PROPERTY(QColor focusRing READ focusRing NOTIFY themeChanged)
    Q_PROPERTY(QColor selectionTint READ selectionTint NOTIFY themeChanged)
    Q_PROPERTY(QColor selectionActiveTint READ selectionActiveTint NOTIFY themeChanged)
    Q_PROPERTY(QColor blockSelectionTint READ blockSelectionTint NOTIFY themeChanged)

    // Accent and semantic colors.
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QColor dangerBright READ dangerBright NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor pinColor READ pinColor NOTIFY themeChanged)

    // Inline styling (read by the engine's highlighter).
    Q_PROPERTY(QColor marker READ marker NOTIFY themeChanged)
    Q_PROPERTY(QColor inlineCodeBackground READ inlineCodeBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor highlightBackground READ highlightBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor link READ link NOTIFY themeChanged)
    Q_PROPERTY(QColor searchMatchBackground READ searchMatchBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor searchCurrentBackground READ searchCurrentBackground NOTIFY themeChanged)

    // Code-block syntax highlighting: the five
    // token colors the code highlighter paints, read the same way the inline
    // tokens above are. codeType is the "function/type" color.
    Q_PROPERTY(QColor codeKeyword READ codeKeyword NOTIFY themeChanged)
    Q_PROPERTY(QColor codeType READ codeType NOTIFY themeChanged)
    Q_PROPERTY(QColor codeString READ codeString NOTIFY themeChanged)
    Q_PROPERTY(QColor codeComment READ codeComment NOTIFY themeChanged)
    Q_PROPERTY(QColor codeNumber READ codeNumber NOTIFY themeChanged)

    // Callout tip accent: the five other callout types reuse
    // accent/warning/success/danger/textMuted; tip needs its own hue.
    Q_PROPERTY(QColor calloutTip READ calloutTip NOTIFY themeChanged)

    // Portfolio-dashboard vocabulary (kvit-hub new-UI spec §2). The two effort
    // axes own two hues used for nothing else; discovered scope and hygiene
    // signals share violet. Defined here so the dashboard and the editor stay
    // one token system; the editor itself does not draw with them.
    Q_PROPERTY(QColor axisAttention READ axisAttention NOTIFY themeChanged)
    Q_PROPERTY(QColor axisAttentionText READ axisAttentionText NOTIFY themeChanged)
    Q_PROPERTY(QColor axisAgent READ axisAgent NOTIFY themeChanged)
    Q_PROPERTY(QColor axisAgentText READ axisAgentText NOTIFY themeChanged)
    Q_PROPERTY(QColor scopeDiscovered READ scopeDiscovered NOTIFY themeChanged)
    Q_PROPERTY(QColor signalHard READ signalHard NOTIFY themeChanged)
    Q_PROPERTY(QColor signalSoft READ signalSoft NOTIFY themeChanged)
    Q_PROPERTY(QColor signalHygiene READ signalHygiene NOTIFY themeChanged)
    // The stroke drawn across a hatched (bounded) bar; per theme, so the hatch
    // survives every ground the bar can sit on.
    Q_PROPERTY(QColor hatchAlt READ hatchAlt NOTIFY themeChanged)

    // User-data colors (folder and tag palettes): content, not chrome,
    // so identical in every theme.
    Q_PROPERTY(QStringList colorPalette READ colorPalette CONSTANT)
    // Soft span tints offered by the highlight-color picker (§10.3):
    // text sits ON these, so the saturated folder palette above would
    // be wrong. Choice candidates, not chrome — theme-independent.
    Q_PROPERTY(QStringList highlightPalette READ highlightPalette CONSTANT)

public:
    struct Tokens {
        QColor windowBackground, panelBackground, listBackground,
            footerBackground, popupBackground, chipBackground,
            bannerBackground, codePanelBackground;
        QColor textPrimary, textSecondary, textMuted, textFaint,
            textDisabled, bannerText, onAccent;
        QColor border, borderStrong, quoteBar, mutedGlyph;
        QColor hoverTint, blockHoverTint, focusTint, selectionTint,
            selectionActiveTint, blockSelectionTint, focusRing;
        QColor accent, danger, dangerBright, success, warning, pinColor;
        QColor marker, inlineCodeBackground, highlightBackground, link,
            searchMatchBackground, searchCurrentBackground;
        QColor codeKeyword, codeType, codeString, codeComment, codeNumber;
        QColor calloutTip;
        QColor axisAttention, axisAttentionText, axisAgent, axisAgentText,
            scopeDiscovered, signalHard, signalSoft, signalHygiene, hatchAlt;
    };

    explicit Theme(QObject *parent = nullptr);

    // Reads theme.id / theme.accent / theme.highlight and saves them
    // back on change. Optional: without a store the theme is
    // session-scoped (tests).
    void setSettings(SettingsStore *settings);

    QString themeId() const { return m_themeId; }
    void setThemeId(const QString &themeId);
    QString resolvedTheme() const { return m_resolved; }
    QStringList availableThemes() const;

    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool reduced);
    qreal motionScale() const { return m_reducedMotion ? 0.0 : 1.0; }
    // Friendly label for a theme id (the menu uses it); handles camelCase ids.
    Q_INVOKABLE QString displayName(const QString &themeId) const;

    QString accentOverride() const { return m_accentOverride; }
    void setAccentOverride(const QString &hex);
    QString highlightOverride() const { return m_highlightOverride; }
    void setHighlightOverride(const QString &hex);

    QColor windowBackground() const { return m_tokens.windowBackground; }
    QColor panelBackground() const { return m_tokens.panelBackground; }
    QColor listBackground() const { return m_tokens.listBackground; }
    QColor footerBackground() const { return m_tokens.footerBackground; }
    QColor popupBackground() const { return m_tokens.popupBackground; }
    QColor chipBackground() const { return m_tokens.chipBackground; }
    QColor bannerBackground() const { return m_tokens.bannerBackground; }
    QColor codePanelBackground() const { return m_tokens.codePanelBackground; }
    QColor textPrimary() const { return m_tokens.textPrimary; }
    QColor textSecondary() const { return m_tokens.textSecondary; }
    QColor textMuted() const { return m_tokens.textMuted; }
    QColor textFaint() const { return m_tokens.textFaint; }
    QColor textDisabled() const { return m_tokens.textDisabled; }
    QColor bannerText() const { return m_tokens.bannerText; }
    QColor onAccent() const { return m_tokens.onAccent; }
    QColor border() const { return m_tokens.border; }
    QColor borderStrong() const { return m_tokens.borderStrong; }
    QColor quoteBar() const { return m_tokens.quoteBar; }
    QColor mutedGlyph() const { return m_tokens.mutedGlyph; }
    QColor hoverTint() const { return m_tokens.hoverTint; }
    QColor blockHoverTint() const { return m_tokens.blockHoverTint; }
    QColor focusTint() const { return m_tokens.focusTint; }
    QColor focusRing() const { return m_tokens.focusRing; }
    QColor selectionTint() const { return m_tokens.selectionTint; }
    QColor selectionActiveTint() const { return m_tokens.selectionActiveTint; }
    QColor blockSelectionTint() const { return m_tokens.blockSelectionTint; }
    QColor accent() const { return m_tokens.accent; }
    QColor danger() const { return m_tokens.danger; }
    QColor dangerBright() const { return m_tokens.dangerBright; }
    QColor success() const { return m_tokens.success; }
    QColor warning() const { return m_tokens.warning; }
    QColor pinColor() const { return m_tokens.pinColor; }
    QColor marker() const { return m_tokens.marker; }
    QColor inlineCodeBackground() const { return m_tokens.inlineCodeBackground; }
    QColor highlightBackground() const { return m_tokens.highlightBackground; }
    QColor link() const { return m_tokens.link; }
    QColor searchMatchBackground() const { return m_tokens.searchMatchBackground; }
    QColor searchCurrentBackground() const { return m_tokens.searchCurrentBackground; }
    QColor codeKeyword() const { return m_tokens.codeKeyword; }
    QColor codeType() const { return m_tokens.codeType; }
    QColor codeString() const { return m_tokens.codeString; }
    QColor codeComment() const { return m_tokens.codeComment; }
    QColor codeNumber() const { return m_tokens.codeNumber; }
    QColor calloutTip() const { return m_tokens.calloutTip; }
    QColor axisAttention() const { return m_tokens.axisAttention; }
    QColor axisAttentionText() const { return m_tokens.axisAttentionText; }
    QColor axisAgent() const { return m_tokens.axisAgent; }
    QColor axisAgentText() const { return m_tokens.axisAgentText; }
    QColor scopeDiscovered() const { return m_tokens.scopeDiscovered; }
    QColor signalHard() const { return m_tokens.signalHard; }
    QColor signalSoft() const { return m_tokens.signalSoft; }
    QColor signalHygiene() const { return m_tokens.signalHygiene; }
    QColor hatchAlt() const { return m_tokens.hatchAlt; }

    QStringList colorPalette() const;
    QStringList highlightPalette() const;

    // The unmodified token table of a built-in theme (tests compare
    // against these; the settings dialog previews swatches from them).
    static const Tokens &tokensFor(const QString &resolvedTheme);

    // Swatch colors for the settings dialog's theme cards ("system"
    // previews as its current resolution).
    Q_INVOKABLE QVariantMap themePreview(const QString &themeId) const;

signals:
    void themeChanged();
    void reducedMotionChanged();

private:
    void refresh();
    QString resolveSystem() const;

    SettingsStore *m_settings = nullptr;
    QString m_themeId = QStringLiteral("light");
    bool m_reducedMotion = false;
    QString m_resolved = QStringLiteral("light");
    QString m_accentOverride;
    QString m_highlightOverride;
    Tokens m_tokens;
};

#endif // THEME_H
