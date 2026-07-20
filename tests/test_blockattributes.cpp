// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "blockattributes.h"
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "undostack.h"

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

QTEST_MAIN(TestBlockAttributes)
#include "test_blockattributes.moc"
