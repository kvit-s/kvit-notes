// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "theme.h"

#include <QGuiApplication>
#include <QStyleHints>

#include "settingsstore.h"
#include "perflog.h"

// The three built-in token tables (features.md §10.1). The light table
// is the application's pre-Phase-9 appearance verbatim; dark and sepia
// were composed against it and are reviewed through the theme
// storyboards like every visual state.
namespace {

Theme::Tokens lightTokens()
{
    Theme::Tokens t;
    t.windowBackground = QColor("#ffffff");
    t.panelBackground = QColor("#f4f4f4");
    t.listBackground = QColor("#fafafa");
    t.footerBackground = QColor("#f5f5f5");
    t.popupBackground = QColor("#ffffff");
    t.chipBackground = QColor("#f2f2f0");
    t.bannerBackground = QColor("#fdf3d8");
    t.codePanelBackground = QColor("#f6f6f4");
    t.textPrimary = QColor("#1a1a1a");
    t.textSecondary = QColor("#555555");
    t.textMuted = QColor("#666666");
    t.textFaint = QColor("#999999");
    t.textDisabled = QColor("#aaaaaa");
    t.bannerText = QColor("#8a6d1a");
    t.onAccent = QColor("#ffffff");
    t.border = QColor("#dddddd");
    t.borderStrong = QColor("#b0b0b0");
    t.quoteBar = QColor("#c0c0c0");
    t.mutedGlyph = QColor("#a5b2bd");
    t.hoverTint = QColor("#ebebeb");
    t.blockHoverTint = QColor("#fafafa");
    t.focusTint = QColor("#eaf2fb");
    t.focusRing = QColor("#1f6feb");   // strong keyboard-focus outline
    t.selectionTint = QColor("#dce8f5");
    t.selectionActiveTint = QColor("#c8dff5");
    t.blockSelectionTint = QColor("#dbe9f9");
    t.accent = QColor("#4a90d9");
    t.danger = QColor("#b3261e");
    t.dangerBright = QColor("#e05c5c");
    t.success = QColor("#27ae60");
    t.warning = QColor("#f39c12");
    t.pinColor = QColor("#e0a04c");
    t.marker = QColor("#b8b8b8");
    t.inlineCodeBackground = QColor("#f0f0ee");
    t.highlightBackground = QColor("#fdf3a9");
    t.link = QColor("#2970c8");
    t.searchMatchBackground = QColor("#b5dcff");
    t.searchCurrentBackground = QColor("#ffb454");
    t.codeKeyword = QColor("#a626a4");
    t.codeType = QColor("#4078f2");
    t.codeString = QColor("#50a14f");
    t.codeComment = QColor("#a0a1a7");
    t.codeNumber = QColor("#986801");
    t.calloutTip = QColor("#2a9d8f");
    t.axisAttention = QColor("#d99a3d");
    t.axisAttentionText = QColor("#b06a10");
    t.axisAgent = QColor("#4aa3a3");
    t.axisAgentText = QColor("#1f7a7a");
    t.scopeDiscovered = QColor("#8a5cc0");
    t.signalHard = QColor("#c0392b");
    t.signalSoft = QColor("#d99a3d");
    t.signalHygiene = QColor("#8a5cc0");
    t.hatchAlt = QColor("#ffffff");
    return t;
}

Theme::Tokens darkTokens()
{
    Theme::Tokens t;
    t.windowBackground = QColor("#1e1e1e");
    t.panelBackground = QColor("#252526");
    t.listBackground = QColor("#212122");
    t.footerBackground = QColor("#2a2a2b");
    t.popupBackground = QColor("#2d2d30");
    t.chipBackground = QColor("#37373a");
    t.bannerBackground = QColor("#3a3320");
    t.codePanelBackground = QColor("#262624");
    t.textPrimary = QColor("#e8e8e8");
    t.textSecondary = QColor("#c8c8c8");
    t.textMuted = QColor("#a8a8a8");
    t.textFaint = QColor("#848484");
    t.textDisabled = QColor("#5f5f5f");
    t.bannerText = QColor("#d9c37a");
    t.onAccent = QColor("#ffffff");
    t.border = QColor("#3c3c3c");
    t.borderStrong = QColor("#5a5a5a");
    t.quoteBar = QColor("#5a5a5a");
    t.mutedGlyph = QColor("#6f7d8a");
    t.hoverTint = QColor("#333336");
    t.blockHoverTint = QColor("#232324");
    t.focusTint = QColor("#263544");
    t.focusRing = QColor("#58a6ff");   // strong keyboard-focus outline
    t.selectionTint = QColor("#2d4356");
    t.selectionActiveTint = QColor("#35506b");
    t.blockSelectionTint = QColor("#2a3f52");
    t.accent = QColor("#5c9fe0");
    t.danger = QColor("#e06c60");
    t.dangerBright = QColor("#e05c5c");
    t.success = QColor("#5abd82");
    t.warning = QColor("#e0a34c");
    t.pinColor = QColor("#e0a04c");
    t.marker = QColor("#6f6f6f");
    t.inlineCodeBackground = QColor("#333330");
    t.highlightBackground = QColor("#6b5c17");
    t.link = QColor("#6fb1ff");
    t.searchMatchBackground = QColor("#264f78");
    t.searchCurrentBackground = QColor("#96601f");
    t.codeKeyword = QColor("#c678dd");
    t.codeType = QColor("#61afef");
    t.codeString = QColor("#98c379");
    t.codeComment = QColor("#7f848e");
    t.codeNumber = QColor("#d19a66");
    t.calloutTip = QColor("#4db6ac");
    t.axisAttention = QColor("#d9a04c");
    t.axisAttentionText = QColor("#e6b877");
    t.axisAgent = QColor("#4aa3a3");
    t.axisAgentText = QColor("#7cc7c4");
    t.scopeDiscovered = QColor("#a37fd4");
    t.signalHard = QColor("#e06c60");
    t.signalSoft = QColor("#e0a34c");
    t.signalHygiene = QColor("#a37fd4");
    t.hatchAlt = QColor("#1e1e1e");
    return t;
}

Theme::Tokens sepiaTokens()
{
    Theme::Tokens t;
    t.windowBackground = QColor("#f6efdf");
    t.panelBackground = QColor("#eee5d0");
    t.listBackground = QColor("#f2ead8");
    t.footerBackground = QColor("#ece2cc");
    t.popupBackground = QColor("#faf4e6");
    t.chipBackground = QColor("#ece3cb");
    t.bannerBackground = QColor("#eadfb2");
    t.codePanelBackground = QColor("#efe7d1");
    t.textPrimary = QColor("#3d3427");
    t.textSecondary = QColor("#5a4f3d");
    t.textMuted = QColor("#6e6250");
    t.textFaint = QColor("#8a7d68");
    t.textDisabled = QColor("#a99a80");
    t.bannerText = QColor("#7a6318");
    t.onAccent = QColor("#faf4e6");
    t.border = QColor("#ded2b8");
    t.borderStrong = QColor("#b8a888");
    t.quoteBar = QColor("#c4b492");
    t.mutedGlyph = QColor("#a3937a");
    t.hoverTint = QColor("#e8dfc9");
    t.blockHoverTint = QColor("#f0e8d6");
    t.focusTint = QColor("#ece0c4");
    t.focusRing = QColor("#9a6a2b");   // strong keyboard-focus outline
    t.selectionTint = QColor("#e2d4b4");
    t.selectionActiveTint = QColor("#d8c69e");
    t.blockSelectionTint = QColor("#e0d2b2");
    t.accent = QColor("#9a6b2f");
    t.danger = QColor("#a33b30");
    t.dangerBright = QColor("#c25a4e");
    t.success = QColor("#4e7a3a");
    t.warning = QColor("#b07a1f");
    t.pinColor = QColor("#b07a2a");
    t.marker = QColor("#b0a284");
    t.inlineCodeBackground = QColor("#ece3cb");
    t.highlightBackground = QColor("#f0dd8c");
    t.link = QColor("#8a5a20");
    t.searchMatchBackground = QColor("#cfd9a8");
    t.searchCurrentBackground = QColor("#e8a94e");
    t.codeKeyword = QColor("#9a2f8a");
    t.codeType = QColor("#2f6ab0");
    t.codeString = QColor("#4e7a3a");
    t.codeComment = QColor("#9a8a6a");
    t.codeNumber = QColor("#8a5a20");
    t.calloutTip = QColor("#3a8a7a");
    t.axisAttention = QColor("#c08a2e");
    t.axisAttentionText = QColor("#8a5f14");
    t.axisAgent = QColor("#3f8f8a");
    t.axisAgentText = QColor("#2a6b66");
    t.scopeDiscovered = QColor("#7a4fa8");
    t.signalHard = QColor("#a33b30");
    t.signalSoft = QColor("#b07a1f");
    t.signalHygiene = QColor("#7a4fa8");
    t.hatchAlt = QColor("#f6efdf");
    return t;
}

const QString kSettingsThemeId = QStringLiteral("theme.id");
const QString kSettingsAccent = QStringLiteral("theme.accent");
const QString kSettingsHighlight = QStringLiteral("theme.highlight");
const QString kSettingsReducedMotion = QStringLiteral("view.reducedMotion");

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    refresh();

    // "system" follows the OS setting live (§10.1). qGuiApp is null in
    // pure-QObject unit tests; those exercise the fixed themes.
    if (qGuiApp) {
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged,
                this, [this]() {
                    if (m_themeId == QLatin1String("system"))
                        refresh();
                });
    }
}

void Theme::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    // First start (no persisted choice) follows the OS color scheme; an
    // explicit pick in Settings persists and wins from then on.
    m_themeId = m_settings->value(kSettingsThemeId,
                                  QStringLiteral("system")).toString();
    if (!availableThemes().contains(m_themeId))
        m_themeId = QStringLiteral("system");  // stale or hand-edited
    m_accentOverride = m_settings->value(kSettingsAccent).toString();
    m_highlightOverride = m_settings->value(kSettingsHighlight).toString();
    const bool reduced = m_settings->value(kSettingsReducedMotion, false).toBool();
    if (reduced != m_reducedMotion) {
        m_reducedMotion = reduced;
        emit reducedMotionChanged();
    }
    refresh();
}

void Theme::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced)
        return;
    m_reducedMotion = reduced;
    if (m_settings)
        m_settings->setValue(kSettingsReducedMotion, reduced);
    emit reducedMotionChanged();
}

QString Theme::displayName(const QString &themeId) const
{
    if (themeId == QLatin1String("highContrast"))
        return tr("High contrast");
    if (themeId.isEmpty())
        return themeId;
    return themeId.at(0).toUpper() + themeId.mid(1);
}

QStringList Theme::availableThemes() const
{
    return { QStringLiteral("light"), QStringLiteral("dark"),
             QStringLiteral("sepia"), QStringLiteral("highContrast"),
             QStringLiteral("system") };
}

void Theme::setThemeId(const QString &themeId)
{
    if (m_themeId == themeId || !availableThemes().contains(themeId))
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("theme.switch"),
        QVariantMap{{QStringLiteral("theme"), themeId}});
    m_themeId = themeId;
    if (m_settings)
        m_settings->setValue(kSettingsThemeId, m_themeId);
    refresh();
}

void Theme::setAccentOverride(const QString &hex)
{
    const QString value = (QColor::isValidColorName(hex)) ? hex : QString();
    if (m_accentOverride == value)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("theme.switch"),
        QVariantMap{{QStringLiteral("accentOverride"), value}});
    m_accentOverride = value;
    if (m_settings)
        m_settings->setValue(kSettingsAccent, m_accentOverride);
    refresh();
}

void Theme::setHighlightOverride(const QString &hex)
{
    const QString value = (QColor::isValidColorName(hex)) ? hex : QString();
    if (m_highlightOverride == value)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("theme.switch"),
        QVariantMap{{QStringLiteral("highlightOverride"), value}});
    m_highlightOverride = value;
    if (m_settings)
        m_settings->setValue(kSettingsHighlight, m_highlightOverride);
    refresh();
}

QStringList Theme::colorPalette() const
{
    return { QStringLiteral("#e05c5c"), QStringLiteral("#e0a04c"),
             QStringLiteral("#58a866"), QStringLiteral("#4a90d9"),
             QStringLiteral("#9068c8"), QStringLiteral("#d06ca8") };
}

QStringList Theme::highlightPalette() const
{
    return { QStringLiteral("#fdf3a9"), QStringLiteral("#ffd9a8"),
             QStringLiteral("#c9ecc9"), QStringLiteral("#c9e4ff"),
             QStringLiteral("#f2ccf2") };
}

// High-contrast theme (§14.3): pure black ground, white body text (21:1, WCAG
// AAA), bright saturated accents, and strong white borders so every structural
// edge is visible. A bright-yellow focus ring gives the maximum-contrast
// keyboard indicator. Its stricter contrast floor is asserted in the theme test.
Theme::Tokens highContrastTokens()
{
    Theme::Tokens t;
    t.windowBackground = QColor("#000000");
    t.panelBackground = QColor("#000000");
    t.listBackground = QColor("#050505");
    t.footerBackground = QColor("#000000");
    t.popupBackground = QColor("#0a0a0a");
    t.chipBackground = QColor("#1a1a1a");
    t.bannerBackground = QColor("#1a1a00");
    t.codePanelBackground = QColor("#0a0a0a");
    t.textPrimary = QColor("#ffffff");
    t.textSecondary = QColor("#f0f0f0");
    t.textMuted = QColor("#d8d8d8");
    t.textFaint = QColor("#bcbcbc");
    t.textDisabled = QColor("#8a8a8a");
    t.bannerText = QColor("#ffff00");
    t.onAccent = QColor("#000000");
    t.border = QColor("#ffffff");
    t.borderStrong = QColor("#ffffff");
    t.quoteBar = QColor("#ffffff");
    t.mutedGlyph = QColor("#ffff66");
    t.hoverTint = QColor("#333333");
    t.blockHoverTint = QColor("#1a1a1a");
    t.focusTint = QColor("#00335c");
    t.focusRing = QColor("#ffff00");   // maximum-contrast keyboard outline
    t.selectionTint = QColor("#0055aa");
    t.selectionActiveTint = QColor("#0077dd");
    t.blockSelectionTint = QColor("#004488");
    t.accent = QColor("#33ccff");
    t.danger = QColor("#ff6666");
    t.dangerBright = QColor("#ff8080");
    t.success = QColor("#44ff99");
    t.warning = QColor("#ffbb33");
    t.pinColor = QColor("#ffbb33");
    t.marker = QColor("#ffff66");
    t.inlineCodeBackground = QColor("#222222");
    t.highlightBackground = QColor("#5c5c00");
    t.link = QColor("#66ddff");
    t.searchMatchBackground = QColor("#0055aa");
    t.searchCurrentBackground = QColor("#cc7700");
    t.codeKeyword = QColor("#ff99ff");
    t.codeType = QColor("#99ddff");
    t.codeString = QColor("#99ff99");
    t.codeComment = QColor("#d0d0d0");
    t.codeNumber = QColor("#ffcc88");
    t.calloutTip = QColor("#33ffdd");
    // The soft-signal amber matches the attention hue deliberately: a soft
    // signal always sits inside a chip, so the two never compete on one mark.
    t.axisAttention = QColor("#ffbb33");
    t.axisAttentionText = QColor("#ffcc66");
    t.axisAgent = QColor("#33ddcc");
    t.axisAgentText = QColor("#77ffe8");
    t.scopeDiscovered = QColor("#cc99ff");
    t.signalHard = QColor("#ff6666");
    t.signalSoft = QColor("#ffbb33");
    t.signalHygiene = QColor("#cc99ff");
    t.hatchAlt = QColor("#000000");
    return t;
}

const Theme::Tokens &Theme::tokensFor(const QString &resolvedTheme)
{
    static const Tokens light = lightTokens();
    static const Tokens dark = darkTokens();
    static const Tokens sepia = sepiaTokens();
    static const Tokens highContrast = highContrastTokens();
    if (resolvedTheme == QLatin1String("dark"))
        return dark;
    if (resolvedTheme == QLatin1String("sepia"))
        return sepia;
    if (resolvedTheme == QLatin1String("highContrast"))
        return highContrast;
    return light;
}

QVariantMap Theme::themePreview(const QString &themeId) const
{
    const QString resolved = (themeId == QLatin1String("system"))
        ? resolveSystem() : themeId;
    const Tokens &t = tokensFor(resolved);
    return { { QStringLiteral("background"), t.windowBackground },
             { QStringLiteral("panel"), t.panelBackground },
             { QStringLiteral("text"), t.textPrimary },
             { QStringLiteral("accent"), t.accent },
             { QStringLiteral("border"), t.border } };
}

QString Theme::resolveSystem() const
{
    if (qGuiApp
        && qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        return QStringLiteral("dark");
    return QStringLiteral("light");  // Light and Unknown both render light
}

void Theme::refresh()
{
    m_resolved = (m_themeId == QLatin1String("system")) ? resolveSystem()
                                                        : m_themeId;
    m_tokens = tokensFor(m_resolved);

    // §10.3 overrides ride on top of the table; the engine and QML
    // read plain tokens and never know overrides exist.
    if (!m_accentOverride.isEmpty()) {
        const QColor accent(m_accentOverride);
        m_tokens.accent = accent;
        m_tokens.link = accent;
    }
    if (!m_highlightOverride.isEmpty())
        m_tokens.highlightBackground = QColor(m_highlightOverride);

    emit themeChanged();
}
