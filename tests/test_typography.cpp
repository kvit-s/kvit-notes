// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>

#include "typography.h"
#include "settingsstore.h"
#include "blockeditorengine.h"
#include "block.h"

// Typography settings: the frozen ratio type scale, clamped
// setters, persistence, and the engine's
// line-height / mono-family plumbing.
class TestTypography : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultScaleIsLegacyPixelValues();
    void testScaleDerivesFromBase_data();
    void testScaleDerivesFromBase();
    void testClamps();
    void testSignalDiscipline();
    void testPersistsThroughSettings();
    void testCorruptSettingsClampOnLoad();
    void testMonospaceFamilies();
    void testResetToDefaults();
    void testEngineAppliesLineHeight();
    void testEngineLineHeightSurvivesRebuild();
    void testEngineMonoFamilyRestylesInlineCode();

private:
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

void TestTypography::testDefaultScaleIsLegacyPixelValues()
{
    // The legacy hard-coded sizes, now derived: defaults render pixel-identical.
    Typography t;
    QCOMPARE(t.baseSize(), 15);
    QCOMPARE(t.sizeForBlockType(Block::Heading1), 32);
    QCOMPARE(t.sizeForBlockType(Block::Heading2), 24);
    QCOMPARE(t.sizeForBlockType(Block::Heading3), 20);
    QCOMPARE(t.sizeForBlockType(Block::Heading4), 17);
    QCOMPARE(t.sizeForBlockType(Block::CodeBlock), 13);
    QCOMPARE(t.sizeForBlockType(Block::Paragraph), 15);
    QCOMPARE(t.sizeForBlockType(Block::BulletList), 15);
    QCOMPARE(t.sizeForBlockType(Block::Quote), 15);
    QCOMPARE(t.lineHeight(), 1.0);
    QCOMPARE(t.paragraphSpacing(), 8);
    QCOMPARE(t.maxContentWidth(), 0);
    QCOMPARE(t.monoFamily(), QString("monospace"));
    QCOMPARE(t.fontFamily(), QString());
}

void TestTypography::testScaleDerivesFromBase_data()
{
    QTest::addColumn<int>("base");
    QTest::addColumn<int>("h1");
    QTest::addColumn<int>("code");
    // qRound(base * ratio): the scale stays coherent at any base.
    QTest::newRow("base 12") << 12 << 26 << 10;  // 25.6, 10.4
    QTest::newRow("base 18") << 18 << 38 << 16;  // 38.4, 15.6
    QTest::newRow("base 20") << 20 << 43 << 17;  // 42.67, 17.33
}

void TestTypography::testScaleDerivesFromBase()
{
    QFETCH(int, base);
    QFETCH(int, h1);
    QFETCH(int, code);
    Typography t;
    t.setBaseSize(base);
    QCOMPARE(t.sizeForBlockType(Block::Heading1), h1);
    QCOMPARE(t.sizeForBlockType(Block::CodeBlock), code);
    QCOMPARE(t.sizeForBlockType(Block::Paragraph), base);
    // Order always holds: H1 > H2 > H3 > H4 >= body > code... at least
    // never inverted.
    QVERIFY(t.sizeForBlockType(Block::Heading1)
            > t.sizeForBlockType(Block::Heading2));
    QVERIFY(t.sizeForBlockType(Block::Heading2)
            > t.sizeForBlockType(Block::Heading3));
    QVERIFY(t.sizeForBlockType(Block::Heading3)
            > t.sizeForBlockType(Block::Heading4));
    QVERIFY(t.sizeForBlockType(Block::Heading4)
            >= t.sizeForBlockType(Block::Paragraph));
    QVERIFY(t.sizeForBlockType(Block::Paragraph)
            > t.sizeForBlockType(Block::CodeBlock));
}

void TestTypography::testClamps()
{
    Typography t;
    t.setBaseSize(5);
    QCOMPARE(t.baseSize(), Typography::MinBaseSize);
    t.setBaseSize(99);
    QCOMPARE(t.baseSize(), Typography::MaxBaseSize);
    t.setLineHeight(0.3);
    QCOMPARE(t.lineHeight(), Typography::MinLineHeight);
    t.setLineHeight(5.0);
    QCOMPARE(t.lineHeight(), Typography::MaxLineHeight);
    t.setParagraphSpacing(-4);
    QCOMPARE(t.paragraphSpacing(), 0);
    t.setParagraphSpacing(400);
    QCOMPARE(t.paragraphSpacing(), Typography::MaxParagraphSpacing);
    t.setMaxContentWidth(50);   // too narrow to be usable
    QCOMPARE(t.maxContentWidth(), Typography::MinContentWidth);
    t.setMaxContentWidth(0);    // 0 = off is always legal
    QCOMPARE(t.maxContentWidth(), 0);
}

void TestTypography::testSignalDiscipline()
{
    Typography t;
    QSignalSpy spy(&t, &Typography::typographyChanged);
    t.setBaseSize(18);
    QCOMPARE(spy.count(), 1);
    t.setBaseSize(18);          // unchanged: silent
    QCOMPARE(spy.count(), 1);
    t.setBaseSize(99);          // clamps to 28: one change
    t.setBaseSize(40);          // clamps to 28 again: silent
    QCOMPARE(spy.count(), 2);
}

void TestTypography::testPersistsThroughSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.json");

    {
        SettingsStore store;
        QVERIFY(store.open(path));
        Typography t;
        t.setSettings(&store);
        t.setFontFamily("Serif Family");
        t.setBaseSize(18);
        t.setLineHeight(1.4);
        t.setParagraphSpacing(14);
        t.setMaxContentWidth(700);
        t.setMonoFamily("Mono Family");
        store.flush();
    }

    SettingsStore reopened;
    QVERIFY(reopened.open(path));
    Typography t;
    t.setSettings(&reopened);
    QCOMPARE(t.fontFamily(), QString("Serif Family"));
    QCOMPARE(t.baseSize(), 18);
    QCOMPARE(t.lineHeight(), 1.4);
    QCOMPARE(t.paragraphSpacing(), 14);
    QCOMPARE(t.maxContentWidth(), 700);
    QCOMPARE(t.monoFamily(), QString("Mono Family"));
}

void TestTypography::testCorruptSettingsClampOnLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    QVERIFY(store.open(dir.filePath("settings.json")));
    store.setValue("typography.fontSize", 400);
    store.setValue("typography.lineHeight", -3);
    store.setValue("typography.maxContentWidth", 10);

    Typography t;
    t.setSettings(&store);
    QCOMPARE(t.baseSize(), Typography::MaxBaseSize);
    QCOMPARE(t.lineHeight(), Typography::MinLineHeight);
    QCOMPARE(t.maxContentWidth(), Typography::MinContentWidth);
}

void TestTypography::testMonospaceFamilies()
{
    Typography t;
    const QStringList families = t.monospaceFamilies();
    QVERIFY(!families.isEmpty());
    // The generic alias is always offered (it is the default value).
    QVERIFY(families.contains("monospace"));
}

void TestTypography::testResetToDefaults()
{
    Typography t;
    t.setBaseSize(20);
    t.setLineHeight(1.6);
    t.setMaxContentWidth(600);
    t.resetToDefaults();
    QCOMPARE(t.baseSize(), 15);
    QCOMPARE(t.lineHeight(), 1.0);
    QCOMPARE(t.maxContentWidth(), 0);
    QCOMPARE(t.monoFamily(), QString("monospace"));
}

void TestTypography::testEngineAppliesLineHeight()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("some **bold** text");

    engine.setLineHeight(1.5);
    QCOMPARE(doc.firstBlock().blockFormat().lineHeightType(),
             int(QTextBlockFormat::ProportionalHeight));
    QCOMPARE(doc.firstBlock().blockFormat().lineHeight(), 150.0);

    // Never an undo step and never a model edit: guarded internal.
    QSignalSpy edited(&engine, &BlockEditorEngine::markdownEdited);
    engine.setLineHeight(1.2);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(doc.firstBlock().blockFormat().lineHeight(), 120.0);
}

void TestTypography::testEngineLineHeightSurvivesRebuild()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setLineHeight(1.5);

    // A model-driven rebuild replaces text; the height must re-apply.
    engine.setMarkdown("first version");
    engine.setMarkdown("completely different **text** now");
    QCOMPARE(doc.firstBlock().blockFormat().lineHeight(), 150.0);
}

void TestTypography::testEngineMonoFamilyRestylesInlineCode()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("see `code` here");
    const int codePos = doc.toPlainText().indexOf("code");
    QCOMPARE(formatAt(doc, codePos).fontFamilies().toStringList(),
             QStringList{ "monospace" });

    engine.setMonoFontFamily("Courier Test");
    QCOMPARE(formatAt(doc, codePos).fontFamilies().toStringList(),
             QStringList{ "Courier Test" });

    // Empty resets to the generic alias rather than an empty family.
    engine.setMonoFontFamily("");
    QCOMPARE(formatAt(doc, codePos).fontFamilies().toStringList(),
             QStringList{ "monospace" });
}

QTEST_MAIN(TestTypography)
#include "test_typography.moc"
