// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <cmath>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>

#include "theme.h"
#include "settingsstore.h"
#include "blockeditorengine.h"

// The theme token object: the three built-in token tables, system
// resolution, the accent and highlight overrides, persistence through the
// settings store, and the engine's highlighter taking its inline colors
// from the theme with rehighlight-on-change.
class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultIsLightTable();
    void testThemeSwitchSwapsTokens();
    void testInvalidThemeIdRejected();
    void testSystemResolvesToLightOrDark();
    void testTablesAreCompleteAndDistinct();
    void testDarkAndSepiaKeepContrast();
    void testFocusRingIsVisible();
    void testHighContrastMeetsStricterFloor();
    void testReducedMotionScale();
    void testAccentOverride();
    void testHighlightOverride();
    void testInvalidOverrideClears();
    void testPersistsThroughSettings();
    void testFirstStartDefaultsToSystem();
    void testStaleSettingsValueFallsBack();
    void testEngineTakesColorsFromTheme();
    void testEngineRehighlightsOnThemeChange();
    void testEngineWithoutThemeUsesFallbacks();

private:
    // The rendered char format at a document position (the highlighter's
    // ranges are non-overlapping), mirroring test_blockeditorengine.
    static QTextCharFormat formatAt(QTextDocument &doc, int pos)
    {
        const auto formats = doc.firstBlock().layout()->formats();
        for (const auto &fr : formats) {
            if (pos >= fr.start && pos < fr.start + fr.length)
                return fr.format;
        }
        return QTextCharFormat();
    }
};

void TestTheme::testDefaultIsLightTable()
{
    Theme theme;
    QCOMPARE(theme.themeId(), QString("light"));
    QCOMPARE(theme.resolvedTheme(), QString("light"));

    const Theme::Tokens &light = Theme::tokensFor(QStringLiteral("light"));
    QCOMPARE(theme.windowBackground(), light.windowBackground);
    QCOMPARE(theme.textPrimary(), light.textPrimary);
    QCOMPARE(theme.accent(), light.accent);
    QCOMPARE(theme.marker(), light.marker);

    // The light table IS the pre-Phase-9 appearance: the engine's
    // documented fallback values are its inline tokens.
    QCOMPARE(theme.marker(), QColor("#b8b8b8"));
    QCOMPARE(theme.link(), QColor("#2970c8"));
    QCOMPARE(theme.highlightBackground(), QColor("#fdf3a9"));
    QCOMPARE(theme.inlineCodeBackground(), QColor("#f0f0ee"));
    QCOMPARE(theme.searchMatchBackground(), QColor("#b5dcff"));
    QCOMPARE(theme.searchCurrentBackground(), QColor("#ffb454"));
}

void TestTheme::testThemeSwitchSwapsTokens()
{
    Theme theme;
    QSignalSpy spy(&theme, &Theme::themeChanged);

    theme.setThemeId(QStringLiteral("dark"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(theme.resolvedTheme(), QString("dark"));
    QCOMPARE(theme.windowBackground(),
             Theme::tokensFor(QStringLiteral("dark")).windowBackground);

    theme.setThemeId(QStringLiteral("sepia"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(theme.windowBackground(),
             Theme::tokensFor(QStringLiteral("sepia")).windowBackground);

    // Setting the current id again is a no-op.
    theme.setThemeId(QStringLiteral("sepia"));
    QCOMPARE(spy.count(), 2);
}

void TestTheme::testInvalidThemeIdRejected()
{
    Theme theme;
    QSignalSpy spy(&theme, &Theme::themeChanged);
    theme.setThemeId(QStringLiteral("neon"));
    QCOMPARE(theme.themeId(), QString("light"));
    QCOMPARE(spy.count(), 0);
}

void TestTheme::testSystemResolvesToLightOrDark()
{
    Theme theme;
    theme.setThemeId(QStringLiteral("system"));
    QCOMPARE(theme.themeId(), QString("system"));
    QVERIFY(theme.resolvedTheme() == QLatin1String("light")
            || theme.resolvedTheme() == QLatin1String("dark"));
    QCOMPARE(theme.windowBackground(),
             Theme::tokensFor(theme.resolvedTheme()).windowBackground);
}

void TestTheme::testTablesAreCompleteAndDistinct()
{
    const QStringList themes{ QStringLiteral("light"), QStringLiteral("dark"),
                              QStringLiteral("sepia"),
                              QStringLiteral("highContrast") };
    for (const QString &id : themes) {
        const Theme::Tokens &t = Theme::tokensFor(id);
        // Spot the full struct through a representative of each group;
        // an unset QColor is invalid.
        const QList<QColor> all{ t.windowBackground, t.panelBackground,
            t.listBackground, t.footerBackground, t.popupBackground,
            t.chipBackground, t.bannerBackground, t.codePanelBackground,
            t.textPrimary, t.textSecondary, t.textMuted, t.textFaint,
            t.textDisabled, t.bannerText, t.onAccent, t.border,
            t.borderStrong, t.quoteBar, t.mutedGlyph, t.hoverTint,
            t.focusTint, t.focusRing, t.selectionTint, t.selectionActiveTint,
            t.blockSelectionTint, t.blockHoverTint, t.accent, t.danger, t.dangerBright,
            t.success, t.warning, t.pinColor, t.marker,
            t.inlineCodeBackground, t.highlightBackground, t.link,
            t.searchMatchBackground, t.searchCurrentBackground,
            t.codeKeyword, t.codeType, t.codeString, t.codeComment,
            t.codeNumber, t.calloutTip,
            t.axisAttention, t.axisAttentionText, t.axisAgent,
            t.axisAgentText, t.scopeDiscovered, t.signalHard, t.signalSoft,
            t.signalHygiene, t.hatchAlt };
        for (const QColor &c : all)
            QVERIFY2(c.isValid(), qPrintable(id + " has an unset token"));

        // The five code-highlight tokens are five
        // distinct colors within each theme, so the token classes are
        // visually separable.
        const QList<QColor> codeTokens{ t.codeKeyword, t.codeType,
            t.codeString, t.codeComment, t.codeNumber };
        for (int i = 0; i < codeTokens.size(); ++i)
            for (int j = i + 1; j < codeTokens.size(); ++j)
                QVERIFY2(codeTokens[i] != codeTokens[j],
                         qPrintable(id + ": code tokens must be distinct"));
    }

    // The three themes are actually different appearances.
    QVERIFY(Theme::tokensFor("light").windowBackground
            != Theme::tokensFor("dark").windowBackground);
    QVERIFY(Theme::tokensFor("light").windowBackground
            != Theme::tokensFor("sepia").windowBackground);
    QVERIFY(Theme::tokensFor("dark").windowBackground
            != Theme::tokensFor("sepia").windowBackground);
}

void TestTheme::testDarkAndSepiaKeepContrast()
{
    // Coarse legibility floor (not a WCAG-grade audit): body
    // text against the editor background keeps a strong luminance gap,
    // and text stays legible over the selection and search tints.
    auto luminance = [](const QColor &c) {
        return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
    };
    for (const QString &id : { QStringLiteral("light"), QStringLiteral("dark"),
                               QStringLiteral("sepia") }) {
        const Theme::Tokens &t = Theme::tokensFor(id);
        QVERIFY2(qAbs(luminance(t.textPrimary)
                      - luminance(t.windowBackground)) > 0.55,
                 qPrintable(id + ": body text vs background"));
        QVERIFY2(qAbs(luminance(t.textPrimary)
                      - luminance(t.selectionTint)) > 0.35,
                 qPrintable(id + ": text vs selection tint"));
        QVERIFY2(qAbs(luminance(t.textPrimary)
                      - luminance(t.searchMatchBackground)) > 0.3,
                 qPrintable(id + ": text vs search tint"));
    }
}

void TestTheme::testFocusRingIsVisible()
{
    // The keyboard-focus ring (§14.1) must stand out against the editor
    // background: a WCAG non-text contrast floor of 3:1 in every theme.
    auto lin = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto relLum = [&](const QColor &c) {
        return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF())
             + 0.0722 * lin(c.blueF());
    };
    auto ratio = [&](const QColor &a, const QColor &b) {
        double la = relLum(a), lb = relLum(b);
        double hi = qMax(la, lb), lo = qMin(la, lb);
        return (hi + 0.05) / (lo + 0.05);
    };
    for (const QString &id : { QStringLiteral("light"), QStringLiteral("dark"),
                               QStringLiteral("sepia") }) {
        const Theme::Tokens &t = Theme::tokensFor(id);
        const double r = ratio(t.focusRing, t.windowBackground);
        QVERIFY2(r >= 3.0,
                 qPrintable(id + ": focus ring vs background is only "
                            + QString::number(r, 'f', 2) + ":1 (need 3:1)"));
    }
}

void TestTheme::testHighContrastMeetsStricterFloor()
{
    // The high-contrast theme holds a stricter floor than the others:
    // WCAG AAA (7:1) for body text and ≥4.5:1 for the focus ring, both vs the
    // editor background.
    auto lin = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto relLum = [&](const QColor &c) {
        return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF())
             + 0.0722 * lin(c.blueF());
    };
    auto ratio = [&](const QColor &a, const QColor &b) {
        double hi = qMax(relLum(a), relLum(b)), lo = qMin(relLum(a), relLum(b));
        return (hi + 0.05) / (lo + 0.05);
    };
    const Theme::Tokens &t = Theme::tokensFor(QStringLiteral("highContrast"));
    const double text = ratio(t.textPrimary, t.windowBackground);
    QVERIFY2(text >= 7.0,
             qPrintable("high contrast body text is only "
                        + QString::number(text, 'f', 2) + ":1 (need 7:1)"));
    const double ring = ratio(t.focusRing, t.windowBackground);
    QVERIFY2(ring >= 4.5,
             qPrintable("high contrast focus ring is only "
                        + QString::number(ring, 'f', 2) + ":1 (need 4.5:1)"));
    // It is selectable and resolves to its own tokens.
    Theme theme;
    theme.setThemeId(QStringLiteral("highContrast"));
    QCOMPARE(theme.resolvedTheme(), QString("highContrast"));
    QCOMPARE(theme.windowBackground(), QColor("#000000"));
    QCOMPARE(theme.displayName("highContrast"), QString("High contrast"));
}

void TestTheme::testReducedMotionScale()
{
    // Reduced motion (§14.3) is one source: motionScale is 1 normally and 0
    // when on, so every animation multiplying by it stills instantly. It
    // persists through the settings store.
    Theme theme;
    QCOMPARE(theme.reducedMotion(), false);
    QCOMPARE(theme.motionScale(), 1.0);
    QSignalSpy spy(&theme, &Theme::reducedMotionChanged);
    theme.setReducedMotion(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(theme.reducedMotion(), true);
    QCOMPARE(theme.motionScale(), 0.0);
    // Setting the same value again is a no-op.
    theme.setReducedMotion(true);
    QCOMPARE(spy.count(), 1);

    // Persistence: a second theme on the same store reads it back.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    store.open(dir.filePath(QStringLiteral("s.json")));
    Theme a;
    a.setSettings(&store);
    a.setReducedMotion(true);
    Theme b;
    b.setSettings(&store);
    QCOMPARE(b.reducedMotion(), true);
    QCOMPARE(b.motionScale(), 0.0);
}

void TestTheme::testAccentOverride()
{
    Theme theme;
    QSignalSpy spy(&theme, &Theme::themeChanged);
    theme.setAccentOverride(QStringLiteral("#aa3366"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(theme.accent(), QColor("#aa3366"));
    QCOMPARE(theme.link(), QColor("#aa3366"));
    // Other tokens are untouched.
    QCOMPARE(theme.textPrimary(),
             Theme::tokensFor(QStringLiteral("light")).textPrimary);

    // The override rides across theme switches...
    theme.setThemeId(QStringLiteral("dark"));
    QCOMPARE(theme.accent(), QColor("#aa3366"));

    // ...and clearing restores the table value.
    theme.setAccentOverride(QString());
    QCOMPARE(theme.accent(), Theme::tokensFor(QStringLiteral("dark")).accent);
    QCOMPARE(theme.link(), Theme::tokensFor(QStringLiteral("dark")).link);
}

void TestTheme::testHighlightOverride()
{
    Theme theme;
    theme.setHighlightOverride(QStringLiteral("#c2f0c2"));
    QCOMPARE(theme.highlightBackground(), QColor("#c2f0c2"));
    QCOMPARE(theme.accent(),
             Theme::tokensFor(QStringLiteral("light")).accent);
    theme.setHighlightOverride(QString());
    QCOMPARE(theme.highlightBackground(),
             Theme::tokensFor(QStringLiteral("light")).highlightBackground);
}

void TestTheme::testInvalidOverrideClears()
{
    Theme theme;
    theme.setAccentOverride(QStringLiteral("#aa3366"));
    theme.setAccentOverride(QStringLiteral("not-a-color"));
    QCOMPARE(theme.accentOverride(), QString());
    QCOMPARE(theme.accent(), Theme::tokensFor(QStringLiteral("light")).accent);
}

void TestTheme::testPersistsThroughSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.json");

    {
        SettingsStore store;
        QVERIFY(store.open(path));
        Theme theme;
        theme.setSettings(&store);
        theme.setThemeId(QStringLiteral("sepia"));
        theme.setAccentOverride(QStringLiteral("#aa3366"));
        theme.setHighlightOverride(QStringLiteral("#c2f0c2"));
        store.flush();
    }

    SettingsStore reopened;
    QVERIFY(reopened.open(path));
    Theme theme;
    theme.setSettings(&reopened);
    QCOMPARE(theme.themeId(), QString("sepia"));
    QCOMPARE(theme.accent(), QColor("#aa3366"));
    QCOMPARE(theme.highlightBackground(), QColor("#c2f0c2"));
}

void TestTheme::testFirstStartDefaultsToSystem()
{
    // No persisted choice yet: follow the OS color scheme (which always
    // resolves to a concrete built-in table).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    QVERIFY(store.open(dir.filePath("settings.json")));

    Theme theme;
    theme.setSettings(&store);
    QCOMPARE(theme.themeId(), QString("system"));
    QVERIFY(theme.resolvedTheme() == QLatin1String("light")
            || theme.resolvedTheme() == QLatin1String("dark"));
}

void TestTheme::testStaleSettingsValueFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    QVERIFY(store.open(dir.filePath("settings.json")));
    store.setValue("theme.id", "neon");

    Theme theme;
    theme.setSettings(&store);
    QCOMPARE(theme.themeId(), QString("system"));
}

void TestTheme::testEngineTakesColorsFromTheme()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);

    Theme theme;
    theme.setThemeId(QStringLiteral("dark"));
    engine.setTheme(&theme);

    engine.setMarkdown("a [link](https://x) and ==mark==");
    const int linkPos = doc.toPlainText().indexOf("link");
    const int markPos = doc.toPlainText().indexOf("mark");
    const Theme::Tokens &dark = Theme::tokensFor(QStringLiteral("dark"));
    QCOMPARE(formatAt(doc, linkPos).foreground().color(), dark.link);
    QCOMPARE(formatAt(doc, markPos).background().color(),
             dark.highlightBackground);
}

void TestTheme::testEngineRehighlightsOnThemeChange()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);

    Theme theme;
    engine.setTheme(&theme);
    engine.setMarkdown("see [link](https://x)");
    const int linkPos = doc.toPlainText().indexOf("link");
    QCOMPARE(formatAt(doc, linkPos).foreground().color(),
             Theme::tokensFor(QStringLiteral("light")).link);

    // No engine call: the theme switch alone restyles the document.
    theme.setThemeId(QStringLiteral("dark"));
    QCOMPARE(formatAt(doc, linkPos).foreground().color(),
             Theme::tokensFor(QStringLiteral("dark")).link);

    // An accent override flows through the same path (the engine never
    // knows overrides exist).
    theme.setAccentOverride(QStringLiteral("#aa3366"));
    QCOMPARE(formatAt(doc, linkPos).foreground().color(), QColor("#aa3366"));
}

void TestTheme::testEngineWithoutThemeUsesFallbacks()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("see [link](https://x) and `code`");
    const int linkPos = doc.toPlainText().indexOf("link");
    const int codePos = doc.toPlainText().indexOf("code");
    QCOMPARE(formatAt(doc, linkPos).foreground().color(), QColor("#2970c8"));
    QCOMPARE(formatAt(doc, codePos).background().color(), QColor("#f0f0ee"));
}

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
