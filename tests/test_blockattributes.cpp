// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "blockattributes.h"
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "blockkindregistry.h"
#include "undostack.h"

#include <QMetaEnum>
#include <QSet>

// The per-block presentation attribute mechanism. Covers the
// pure parse/serialize core (BlockAttributes), the typed QML reads, the editing
// helpers, and the serializer split/re-attach — including the byte-identical
// no-attribute case that keeps documents without attributes unchanged.
class TestBlockAttributes : public QObject
{
    Q_OBJECT

private slots:
    // ---- stripTag ----
    void stripParagraphTag();
    void stripHeadingTag();
    void stripDividerTag();
    void stripImageTag();
    void stripNoTagIsByteIdentical();
    void stripMidLineCommentNotStripped();
    void stripNormalizesSeparatorWhitespace();
    void stripCanonicalizesPayloadOrder();

    // ---- attachTag ----
    void attachEmptyPayloadIsNoop();
    void attachThenStripRoundTrips();
    void attachUsesTwoSpaceSeparator();

    // ---- parseMap / serializeMap ----
    void parseFlagsAndValues();
    void serializeSortsKeys();
    void unknownKeysPassThrough();
    void parseEmptyIsEmptyMap();
    void laterTokenWinsOnDuplicateKey();

    // ---- typed reads ----
    void typedReads();

    // ---- editing helpers ----
    void editingHelpers();

    // ---- serializer integration (the foundation gate) ----
    void serializerParagraphAlignRoundTrips();
    void serializerHeadingImageDividerCalloutRoundTrip();
    void serializerUnstyledIsByteIdentical();
    void serializerDropsAttributeIntoModelState();

    // ---- every block kind survives serialize -> parse -> serialize ----
    void serializerAttributeRoundTripAllKinds_data();
    void serializerAttributeRoundTripAllKinds();
    void attributeRoundTripCoversEveryBlockType();
    void serializerAttributeRoundTripFenceKinds_data();
    void serializerAttributeRoundTripFenceKinds();

    // ---- reading what older versions wrote ----
    void legacyTrailingCodeFenceTagParses();
    void legacyTrailingMathFenceTagParses();
    void legacyTrailingTableRowTagParses();
    void codeContentKeepingACommentIsVerbatim();
};

// ---------- stripTag ----------

void TestBlockAttributes::stripParagraphTag()
{
    QString attrs;
    const QString content =
        BlockAttributes::stripTag(QStringLiteral("Some text.  <!--kvit align=center-->"), &attrs);
    QCOMPARE(content, QStringLiteral("Some text."));
    QCOMPARE(attrs, QStringLiteral("align=center"));
}

void TestBlockAttributes::stripHeadingTag()
{
    QString attrs;
    const QString content =
        BlockAttributes::stripTag(QStringLiteral("# Title  <!--kvit align=right-->"), &attrs);
    QCOMPARE(content, QStringLiteral("# Title"));
    QCOMPARE(attrs, QStringLiteral("align=right"));
}

void TestBlockAttributes::stripDividerTag()
{
    QString attrs;
    const QString content =
        BlockAttributes::stripTag(QStringLiteral("---  <!--kvit style=dashed width=50%-->"), &attrs);
    QCOMPARE(content, QStringLiteral("---"));
    // Canonical (sorted) order: style before width.
    QCOMPARE(attrs, QStringLiteral("style=dashed width=50%"));
}

void TestBlockAttributes::stripImageTag()
{
    QString attrs;
    const QString content = BlockAttributes::stripTag(
        QStringLiteral("![alt|420](x.png)  <!--kvit align=center rounded shadow-->"), &attrs);
    QCOMPARE(content, QStringLiteral("![alt|420](x.png)"));
    QCOMPARE(attrs, QStringLiteral("align=center rounded shadow"));
}

void TestBlockAttributes::stripNoTagIsByteIdentical()
{
    // A line without a kvit tag comes back exactly as given, and no payload.
    for (const QString &line : {QStringLiteral("Plain paragraph."),
                                QStringLiteral("# Heading"),
                                QStringLiteral("---"),
                                QStringLiteral("Text with <!-- a normal comment -->"),
                                QStringLiteral("- list item"),
                                QString()}) {
        QString attrs = QStringLiteral("SENTINEL");
        const QString out = BlockAttributes::stripTag(line, &attrs);
        QCOMPARE(out, line);
        QCOMPARE(attrs, QStringLiteral("SENTINEL"));  // untouched
    }
}

void TestBlockAttributes::stripMidLineCommentNotStripped()
{
    // The tag must be at end of line to count as attributes; a kvit comment
    // with text after it is left literal.
    QString attrs;
    const QString line = QStringLiteral("a  <!--kvit align=center-->  trailing");
    const QString out = BlockAttributes::stripTag(line, &attrs);
    QCOMPARE(out, line);
    QVERIFY(attrs.isEmpty());
}

void TestBlockAttributes::stripNormalizesSeparatorWhitespace()
{
    // One space, a tab, or many spaces before the tag all parse; the content
    // keeps no trailing separator whitespace.
    QString attrs;
    QCOMPARE(BlockAttributes::stripTag(QStringLiteral("x <!--kvit rounded-->"), &attrs),
             QStringLiteral("x"));
    QCOMPARE(BlockAttributes::stripTag(QStringLiteral("x\t<!--kvit rounded-->"), &attrs),
             QStringLiteral("x"));
}

void TestBlockAttributes::stripCanonicalizesPayloadOrder()
{
    QString attrs;
    BlockAttributes::stripTag(QStringLiteral("x  <!--kvit shadow rounded align=left-->"), &attrs);
    QCOMPARE(attrs, QStringLiteral("align=left rounded shadow"));  // sorted
}

// ---------- attachTag ----------

void TestBlockAttributes::attachEmptyPayloadIsNoop()
{
    QCOMPARE(BlockAttributes::attachTag(QStringLiteral("Some text."), QString()),
             QStringLiteral("Some text."));
    QCOMPARE(BlockAttributes::attachTag(QStringLiteral("---"), QStringLiteral("   ")),
             QStringLiteral("---"));
}

void TestBlockAttributes::attachThenStripRoundTrips()
{
    const QString content = QStringLiteral("Once upon a time.");
    const QString payload = QStringLiteral("dropcap=3");
    const QString tagged = BlockAttributes::attachTag(content, payload);
    QString back;
    QCOMPARE(BlockAttributes::stripTag(tagged, &back), content);
    QCOMPARE(back, payload);
}

void TestBlockAttributes::attachUsesTwoSpaceSeparator()
{
    QCOMPARE(BlockAttributes::attachTag(QStringLiteral("t"), QStringLiteral("align=center")),
             QStringLiteral("t  <!--kvit align=center-->"));
}

// ---------- parseMap / serializeMap ----------

void TestBlockAttributes::parseFlagsAndValues()
{
    const QMap<QString, QString> m =
        BlockAttributes::parseMap(QStringLiteral("align=center rounded shadow"));
    QCOMPARE(m.value(QStringLiteral("align")), QStringLiteral("center"));
    QVERIFY(m.contains(QStringLiteral("rounded")));
    QVERIFY(m.value(QStringLiteral("rounded")).isEmpty());
    QVERIFY(m.contains(QStringLiteral("shadow")));
}

void TestBlockAttributes::serializeSortsKeys()
{
    QMap<QString, QString> m;
    m.insert(QStringLiteral("width"), QStringLiteral("50%"));
    m.insert(QStringLiteral("style"), QStringLiteral("dashed"));
    m.insert(QStringLiteral("color"), QStringLiteral("#ccc"));
    QCOMPARE(BlockAttributes::serializeMap(m),
             QStringLiteral("color=#ccc style=dashed width=50%"));
}

void TestBlockAttributes::unknownKeysPassThrough()
{
    // A key the delegates never read still round-trips untouched.
    const QString payload = QStringLiteral("align=center futurekey=42 mystery");
    QCOMPARE(BlockAttributes::canonical(payload),
             QStringLiteral("align=center futurekey=42 mystery"));
}

void TestBlockAttributes::parseEmptyIsEmptyMap()
{
    QVERIFY(BlockAttributes::parseMap(QString()).isEmpty());
    QVERIFY(BlockAttributes::parseMap(QStringLiteral("   ")).isEmpty());
    QCOMPARE(BlockAttributes::serializeMap({}), QString());
}

void TestBlockAttributes::laterTokenWinsOnDuplicateKey()
{
    QCOMPARE(BlockAttributes::canonical(QStringLiteral("align=left align=right")),
             QStringLiteral("align=right"));
}

// ---------- typed reads ----------

void TestBlockAttributes::typedReads()
{
    BlockAttributes a;
    const QString p = QStringLiteral("align=center dropcap=3 rounded color=#fff3cd");
    QVERIFY(a.has(p, QStringLiteral("rounded")));
    QVERIFY(!a.has(p, QStringLiteral("shadow")));
    QCOMPARE(a.str(p, QStringLiteral("align")), QStringLiteral("center"));
    QCOMPARE(a.str(p, QStringLiteral("missing"), QStringLiteral("def")), QStringLiteral("def"));
    QCOMPARE(a.num(p, QStringLiteral("dropcap")), 3);
    QCOMPARE(a.num(p, QStringLiteral("missing"), 7), 7);
    QCOMPARE(a.str(p, QStringLiteral("color")), QStringLiteral("#fff3cd"));
    // A flag read as a number falls back to the default.
    QCOMPARE(a.num(p, QStringLiteral("rounded"), -1), -1);
}

// ---------- editing helpers ----------

void TestBlockAttributes::editingHelpers()
{
    BlockAttributes a;
    QString p;
    p = a.withValue(p, QStringLiteral("align"), QStringLiteral("center"));
    QCOMPARE(p, QStringLiteral("align=center"));
    p = a.withFlag(p, QStringLiteral("rounded"), true);
    QCOMPARE(p, QStringLiteral("align=center rounded"));
    p = a.withValue(p, QStringLiteral("align"), QStringLiteral("right"));  // replace
    QCOMPARE(p, QStringLiteral("align=right rounded"));
    p = a.withFlag(p, QStringLiteral("rounded"), false);                   // remove flag
    QCOMPARE(p, QStringLiteral("align=right"));
    p = a.without(p, QStringLiteral("align"));
    QVERIFY(p.isEmpty());
}

// ---------- serializer integration (the foundation gate) ----------

void TestBlockAttributes::serializerParagraphAlignRoundTrips()
{
    const QString md = QStringLiteral("Some text.  <!--kvit align=center-->");
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    BlockModel model;
    ser.loadIntoModel(&model, md);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(model.blockAt(0)->content(), QStringLiteral("Some text."));
    QCOMPARE(model.blockAt(0)->attributes(), QStringLiteral("align=center"));
    QCOMPARE(ser.serialize(&model), md);
}

void TestBlockAttributes::serializerHeadingImageDividerCalloutRoundTrip()
{
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString md = QStringLiteral(
        "# Title  <!--kvit align=center-->\n\n"
        "---  <!--kvit style=dashed width=50%-->\n\n"
        "![alt|420](x.png)  <!--kvit align=right rounded shadow-->\n\n"
        "> [!info] Note  <!--kvit color=#fff3cd-->\n> body line");
    BlockModel model;
    ser.loadIntoModel(&model, md);
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.blockAt(0)->attributes(), QStringLiteral("align=center"));
    QCOMPARE(model.blockAt(1)->blockType(), Block::Divider);
    QCOMPARE(model.blockAt(1)->attributes(), QStringLiteral("style=dashed width=50%"));
    QCOMPARE(model.blockAt(2)->blockType(), Block::Image);
    QCOMPARE(model.blockAt(2)->attributes(), QStringLiteral("align=right rounded shadow"));
    QCOMPARE(model.blockAt(3)->blockType(), Block::Callout);
    QCOMPARE(model.blockAt(3)->attributes(), QStringLiteral("color=#fff3cd"));
    QCOMPARE(model.blockAt(3)->content(), QStringLiteral("body line"));
    QCOMPARE(ser.serialize(&model), md);
}

void TestBlockAttributes::serializerUnstyledIsByteIdentical()
{
    // A document with no attributes must serialize exactly as it was read.
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString md = QStringLiteral(
        "# Heading\n\n"
        "A paragraph with **bold** and a [link](u).\n\n"
        "- one\n- two\n\n"
        "---\n\n"
        "> a quote");
    BlockModel model;
    ser.loadIntoModel(&model, md);
    for (int i = 0; i < model.count(); ++i)
        QVERIFY(model.blockAt(i)->attributes().isEmpty());
    QCOMPARE(ser.serialize(&model), md);
}

void TestBlockAttributes::serializerDropsAttributeIntoModelState()
{
    // Setting attributes on the model then serializing produces the tag; the
    // value survives a parse back into a fresh model.
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    BlockModel model;
    ser.loadIntoModel(&model, QStringLiteral("Once upon a time."));
    model.setBlockAttributes(0, QStringLiteral("dropcap=3"));
    const QString out = ser.serialize(&model);
    QCOMPARE(out, QStringLiteral("Once upon a time.  <!--kvit dropcap=3-->"));
    BlockModel model2;
    ser.loadIntoModel(&model2, out);
    QCOMPARE(model2.blockAt(0)->attributes(), QStringLiteral("dropcap=3"));
    QCOMPARE(model2.blockAt(0)->content(), QStringLiteral("Once upon a time."));
}

// ---------- every block kind survives serialize -> parse -> serialize ----------

namespace {

// One row of the all-kinds coverage table: a fully specified block carrying an
// attribute payload. The same list drives the round-trip test and the
// completeness check below, so a new Block::BlockType fails the build's test
// run until it is given a row here.
struct KindCase
{
    const char *name;
    Block::State state;
};

QList<KindCase> allKindCases()
{
    auto make = [](Block::BlockType type, const QString &content,
                   const QString &attributes) {
        Block::State s;
        s.type = type;
        s.content = content;
        s.attributes = attributes;
        return s;
    };

    QList<KindCase> cases;
    cases << KindCase{ "Paragraph",
                       make(Block::Paragraph, QStringLiteral("Some text."),
                            QStringLiteral("align=center")) };
    cases << KindCase{ "Heading1",
                       make(Block::Heading1, QStringLiteral("Title"),
                            QStringLiteral("align=center")) };
    cases << KindCase{ "Heading2",
                       make(Block::Heading2, QStringLiteral("Section"),
                            QStringLiteral("align=right")) };
    cases << KindCase{ "Heading3",
                       make(Block::Heading3, QStringLiteral("Subsection"),
                            QStringLiteral("align=left")) };
    cases << KindCase{ "Heading4",
                       make(Block::Heading4, QStringLiteral("Detail"),
                            QStringLiteral("align=center")) };
    cases << KindCase{ "BulletList",
                       make(Block::BulletList, QStringLiteral("an item"),
                            QStringLiteral("align=right")) };
    cases << KindCase{ "NumberedList",
                       make(Block::NumberedList, QStringLiteral("first"),
                            QStringLiteral("align=right")) };

    Block::State todo = make(Block::Todo, QStringLiteral("do the thing"),
                             QStringLiteral("align=left"));
    todo.checked = true;
    cases << KindCase{ "Todo", todo };

    Block::State quote = make(Block::Quote,
                              QStringLiteral("first line\nsecond line"),
                              QStringLiteral("align=center"));
    quote.indentLevel = 1;
    cases << KindCase{ "Quote", quote };

    Block::State code = make(Block::CodeBlock,
                             QStringLiteral("int main()\n{\n    return 0;\n}"),
                             QStringLiteral("align=center wrap"));
    code.language = QStringLiteral("cpp");
    cases << KindCase{ "CodeBlock", code };

    cases << KindCase{ "Divider",
                       make(Block::Divider, QString(),
                            QStringLiteral("style=dashed width=50%")) };
    cases << KindCase{ "Image",
                       make(Block::Image, QStringLiteral("![alt|420](x.png)"),
                            QStringLiteral("align=right rounded shadow")) };
    cases << KindCase{ "Media",
                       make(Block::Media, QStringLiteral("![clip|420](clip.mp4)"),
                            QStringLiteral("align=center")) };

    Block::State callout = make(Block::Callout,
                                QStringLiteral("body line\nsecond body line"),
                                QStringLiteral("color=#fff3cd"));
    callout.language = QStringLiteral("info");
    callout.calloutTitle = QStringLiteral("Note");
    cases << KindCase{ "Callout", callout };

    cases << KindCase{ "MathBlock",
                       make(Block::MathBlock,
                            QStringLiteral("E = mc^2 \\\\\n\\alpha + \\beta"),
                            QStringLiteral("align=left")) };
    cases << KindCase{ "Table",
                       make(Block::Table,
                            QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"),
                            QStringLiteral("align=center")) };
    return cases;
}

// The sentinel paragraphs that bracket the block under test. A block whose
// serialization the parser cannot terminate swallows the trailing one, which
// is the data-loss symptom the round trip is really guarding.
const char *const kLeadText = "lead paragraph";
const char *const kTrailText = "trailing paragraph";

} // namespace

void TestBlockAttributes::serializerAttributeRoundTripAllKinds_data()
{
    QTest::addColumn<int>("caseIndex");
    const QList<KindCase> cases = allKindCases();
    for (int i = 0; i < cases.size(); ++i)
        QTest::newRow(cases.at(i).name) << i;
}

void TestBlockAttributes::serializerAttributeRoundTripAllKinds()
{
    QFETCH(int, caseIndex);
    const KindCase kind = allKindCases().at(caseIndex);

    Block::State lead;
    lead.content = QString::fromLatin1(kLeadText);
    Block::State trail;
    trail.content = QString::fromLatin1(kTrailText);

    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    BlockModel model;
    model.replaceAllBlocksInternal({ lead, kind.state, trail });

    const QString markdown = ser.serialize(&model);

    BlockModel reloaded;
    ser.loadIntoModel(&reloaded, markdown);

    QCOMPARE(reloaded.count(), 3);
    QCOMPARE(reloaded.blockAt(0)->content(), QString::fromLatin1(kLeadText));
    // The document after the styled block must survive intact.
    QCOMPARE(reloaded.blockAt(2)->blockType(), Block::Paragraph);
    QCOMPARE(reloaded.blockAt(2)->content(), QString::fromLatin1(kTrailText));

    const Block *back = reloaded.blockAt(1);
    QCOMPARE(back->blockType(), kind.state.type);
    QCOMPARE(back->content(), kind.state.content);
    QCOMPARE(back->attributes(), kind.state.attributes);
    QCOMPARE(back->language(), kind.state.language);
    QCOMPARE(back->calloutTitle(), kind.state.calloutTitle);
    QCOMPARE(back->checked(), kind.state.checked);
    QCOMPARE(back->indentLevel(), kind.state.indentLevel);

    // Serialize -> parse -> serialize is a fixed point.
    QCOMPARE(ser.serialize(&reloaded), markdown);
}

void TestBlockAttributes::attributeRoundTripCoversEveryBlockType()
{
    QSet<int> covered;
    for (const KindCase &kind : allKindCases())
        covered.insert(int(kind.state.type));

    const QMetaEnum types = QMetaEnum::fromType<Block::BlockType>();
    QVERIFY(types.isValid());
    for (int i = 0; i < types.keyCount(); ++i) {
        QVERIFY2(covered.contains(types.value(i)),
                 qPrintable(QStringLiteral(
                     "Block::%1 has no row in allKindCases(); add one so its "
                     "attributes are known to round-trip")
                                .arg(QString::fromLatin1(types.key(i)))));
    }
}

void TestBlockAttributes::serializerAttributeRoundTripFenceKinds_data()
{
    QTest::addColumn<QString>("language");
    // The registry's fence languages (kanban, toc, mermaid, query) are code
    // blocks with a bespoke delegate, so they inherit the code-fence
    // serialization and need the same guarantee.
    for (const QString &language : BlockKindRegistry::instance().languages())
        QTest::newRow(qPrintable(language)) << language;
}

void TestBlockAttributes::serializerAttributeRoundTripFenceKinds()
{
    QFETCH(QString, language);

    Block::State fence;
    fence.type = Block::CodeBlock;
    fence.language = language;
    fence.content = QStringLiteral("body line one\nbody line two");
    fence.attributes = QStringLiteral("align=center height=320");
    Block::State trail;
    trail.content = QString::fromLatin1(kTrailText);

    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    BlockModel model;
    model.replaceAllBlocksInternal({ fence, trail });

    const QString markdown = ser.serialize(&model);
    BlockModel reloaded;
    ser.loadIntoModel(&reloaded, markdown);

    QCOMPARE(reloaded.count(), 2);
    QCOMPARE(reloaded.blockAt(0)->blockType(), Block::CodeBlock);
    QCOMPARE(reloaded.blockAt(0)->language(), language);
    QCOMPARE(reloaded.blockAt(0)->content(), fence.content);
    QCOMPARE(reloaded.blockAt(0)->attributes(), fence.attributes);
    QCOMPARE(reloaded.blockAt(1)->content(), QString::fromLatin1(kTrailText));
    QCOMPARE(ser.serialize(&reloaded), markdown);
}

// ---------- reading what older versions wrote ----------

void TestBlockAttributes::legacyTrailingCodeFenceTagParses()
{
    // Kvit once appended the tag after the CLOSING fence, which no longer
    // closed the block. Notes on disk still carry that shape, so the parser
    // accepts it and rewrites it to the canonical opener form.
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString legacy = QStringLiteral(
        "```cpp\nint x = 1;\n```  <!--kvit align=center-->\n\n"
        "trailing paragraph");
    BlockModel model;
    ser.loadIntoModel(&model, legacy);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.blockAt(0)->blockType(), Block::CodeBlock);
    QCOMPARE(model.blockAt(0)->language(), QStringLiteral("cpp"));
    QCOMPARE(model.blockAt(0)->content(), QStringLiteral("int x = 1;"));
    QCOMPARE(model.blockAt(0)->attributes(), QStringLiteral("align=center"));
    QCOMPARE(model.blockAt(1)->content(), QStringLiteral("trailing paragraph"));
    QCOMPARE(ser.serialize(&model),
             QStringLiteral("```cpp  <!--kvit align=center-->\n"
                            "int x = 1;\n```\n\ntrailing paragraph"));
}

void TestBlockAttributes::legacyTrailingMathFenceTagParses()
{
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString legacy = QStringLiteral(
        "$$\nE = mc^2\n$$  <!--kvit align=left-->\n\ntrailing paragraph");
    BlockModel model;
    ser.loadIntoModel(&model, legacy);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.blockAt(0)->blockType(), Block::MathBlock);
    QCOMPARE(model.blockAt(0)->content(), QStringLiteral("E = mc^2"));
    QCOMPARE(model.blockAt(0)->attributes(), QStringLiteral("align=left"));
    QCOMPARE(model.blockAt(1)->content(), QStringLiteral("trailing paragraph"));
    QCOMPARE(ser.serialize(&model),
             QStringLiteral("$$  <!--kvit align=left-->\nE = mc^2\n$$\n\n"
                            "trailing paragraph"));
}

void TestBlockAttributes::legacyTrailingTableRowTagParses()
{
    // The table tag once rode the last data row, where it landed inside the
    // final cell on reparse.
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString legacy = QStringLiteral(
        "| A | B |\n| --- | --- |\n| 1 | 2 |  <!--kvit align=center-->\n\n"
        "trailing paragraph");
    BlockModel model;
    ser.loadIntoModel(&model, legacy);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.blockAt(0)->blockType(), Block::Table);
    QCOMPARE(model.blockAt(0)->content(),
             QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"));
    QCOMPARE(model.blockAt(0)->attributes(), QStringLiteral("align=center"));
    QCOMPARE(model.blockAt(1)->content(), QStringLiteral("trailing paragraph"));
}

void TestBlockAttributes::codeContentKeepingACommentIsVerbatim()
{
    // A kvit-shaped comment on an ordinary code line is content, not an
    // attribute: only a line that is otherwise a bare fence closer is read as
    // the legacy tagged form.
    DocumentSerializer ser;
    ser.setTrailingNewline(false);
    const QString md = QStringLiteral(
        "```html\n<p>x</p>  <!--kvit align=center-->\n```\n\ntrailing paragraph");
    BlockModel model;
    ser.loadIntoModel(&model, md);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.blockAt(0)->blockType(), Block::CodeBlock);
    QCOMPARE(model.blockAt(0)->content(),
             QStringLiteral("<p>x</p>  <!--kvit align=center-->"));
    QVERIFY(model.blockAt(0)->attributes().isEmpty());
    QCOMPARE(ser.serialize(&model), md);
}

QTEST_MAIN(TestBlockAttributes)
#include "test_blockattributes.moc"
