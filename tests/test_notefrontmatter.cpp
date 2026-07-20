// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "notefrontmatter.h"

// Unit suite for the pure front-matter functions.
// The contracts under test:
//  - split() is byte-preserving and strict: divider-led documents are body
//  - parse() consumes only fully-understood values; everything else is
//    preserved verbatim in unknownLines
//  - serialize() writes only non-default keys and re-emits unknown lines,
//    producing no block at all when there is nothing to say
class TestNoteFrontMatter : public QObject
{
    Q_OBJECT

private slots:
    // split()
    void testSplitRecognition_data();
    void testSplitRecognition();
    void testSplitBytePreservation_data();
    void testSplitBytePreservation();
    void testSplitBodyOnlyFile();
    void testSplitCloseFenceAtEof();
    void testSplitCrlf();

    // parse()
    void testParseEmpty();
    void testParseKnownKeys_data();
    void testParseKnownKeys();
    void testParseTagsForms_data();
    void testParseTagsForms();
    void testParseUnparseableValuesStayUnknown_data();
    void testParseUnparseableValuesStayUnknown();
    void testParseForeignKeysPreserved();
    void testParseForeignBlockListPreserved();
    void testParseCommentsAndBlanksPreserved();
    void testParseDuplicateKeyLastWins();

    // serialize()
    void testSerializeDefaultsIsEmpty();
    void testSerializeCanonicalOrder();
    void testSerializeTagQuoting_data();
    void testSerializeTagQuoting();
    void testSerializeUnknownLinesAfterKnown();
    void testSerializeKeylessDegradesToVisibleBody();

    // round trips
    void testParseSerializeRoundTrip_data();
    void testParseSerializeRoundTrip();
    void testSerializeIsCanonicalFixedPoint();
    void testForeignBlockSurvivesMetadataEdit();

    // Writing goal
    void testGoalParseSerializeRoundTrip();
    void testGoalInvalidValueStaysUnknown();

    // General key map + typed accessors
    void testFieldsMapExtraction();
    void testFieldsMapNeverSerialized();
    void testTypedAccessors();
};

// ---------------------------------------------------------------- split()

void TestNoteFrontMatter::testSplitRecognition_data()
{
    QTest::addColumn<QString>("fileText");
    QTest::addColumn<bool>("present");

    QTest::newRow("plain text")
        << QStringLiteral("Just a paragraph\n") << false;
    QTest::newRow("empty file") << QString() << false;
    QTest::newRow("simple block")
        << QStringLiteral("---\ntags: [a]\n---\nBody\n") << true;
    QTest::newRow("all known keys")
        << QStringLiteral("---\ntags: [a, b]\ncreated: 2026-07-06T10:00:00\n"
                          "pinned: true\nfavorite: false\n---\nBody\n")
        << true;
    QTest::newRow("foreign keys only")
        << QStringLiteral("---\nlayout: post\ntitle: Hello\n---\nBody\n")
        << true;
    QTest::newRow("unterminated fence")
        << QStringLiteral("---\ntags: [a]\nno closing fence\n") << false;
    QTest::newRow("fence not at byte 0")
        << QStringLiteral("\n---\ntags: [a]\n---\nBody\n") << false;
    QTest::newRow("leading spaces before fence")
        << QStringLiteral(" ---\ntags: [a]\n---\nBody\n") << false;
    QTest::newRow("four dashes is not a fence")
        << QStringLiteral("----\ntags: [a]\n----\nBody\n") << false;

    // The divider ambiguity (notefrontmatter.h header comment): a document
    // whose first block is a divider also starts with "---".
    QTest::newRow("divider-led document with prose between dividers")
        << QStringLiteral("---\n\nSome text\n\n---\nMore\n") << false;
    QTest::newRow("two leading dividers, nothing between")
        << QStringLiteral("---\n\n---\nBody\n") << false;
    QTest::newRow("divider then heading then divider")
        << QStringLiteral("---\n\n# Title\n\n---\n") << false;
    // "key: value" prose between dividers is indistinguishable from
    // metadata and reads as front-matter — the same ambiguity every
    // front-matter tool accepts. Pinned so the outcome is deliberate.
    QTest::newRow("key-shaped prose between dividers reads as front-matter")
        << QStringLiteral("---\nnote: buy milk\n---\nBody\n") << true;
    QTest::newRow("comment-only block is not front-matter")
        << QStringLiteral("---\n# just a comment\n---\nBody\n") << false;
    QTest::newRow("blank-only block is not front-matter")
        << QStringLiteral("---\n\n\n---\nBody\n") << false;
    QTest::newRow("key without space after colon is not a mapping")
        << QStringLiteral("---\nkey:value\n---\nBody\n") << false;
    QTest::newRow("block list under a key is mapping-shaped")
        << QStringLiteral("---\ntags:\n- a\n- b\n---\nBody\n") << true;
}

void TestNoteFrontMatter::testSplitRecognition()
{
    QFETCH(QString, fileText);
    QFETCH(bool, present);

    NoteFrontMatter::Split s = NoteFrontMatter::split(fileText);
    QCOMPARE(s.present, present);
    if (!present) {
        QVERIFY(s.block.isEmpty());
        QCOMPARE(s.body, fileText);
    }
}

void TestNoteFrontMatter::testSplitBytePreservation_data()
{
    QTest::addColumn<QString>("fileText");

    QTest::newRow("simple")
        << QStringLiteral("---\ntags: [a]\n---\nBody line\n");
    QTest::newRow("body starts with blank line")
        << QStringLiteral("---\npinned: true\n---\n\n# Heading\n");
    QTest::newRow("body contains dividers")
        << QStringLiteral("---\ntags: [a]\n---\n---\n\ntext\n\n---\n");
    QTest::newRow("foreign obsidian block")
        << QStringLiteral("---\naliases: [note, n]\ncssclass: wide\n"
                          "tags: [a]\n---\nBody\n");
    QTest::newRow("empty body")
        << QStringLiteral("---\ntags: [a]\n---\n");
}

void TestNoteFrontMatter::testSplitBytePreservation()
{
    QFETCH(QString, fileText);

    NoteFrontMatter::Split s = NoteFrontMatter::split(fileText);
    QVERIFY(s.present);
    QCOMPARE(s.block + s.body, fileText);
    QVERIFY(s.block.startsWith(QStringLiteral("---\n")));
    QVERIFY(s.block.endsWith(QStringLiteral("---\n")));
}

void TestNoteFrontMatter::testSplitBodyOnlyFile()
{
    const QString text = QStringLiteral("# Heading\n\nParagraph\n");
    NoteFrontMatter::Split s = NoteFrontMatter::split(text);
    QVERIFY(!s.present);
    QCOMPARE(s.body, text);
}

void TestNoteFrontMatter::testSplitCloseFenceAtEof()
{
    // No trailing newline after the closing fence.
    const QString text = QStringLiteral("---\ntags: [a]\n---");
    NoteFrontMatter::Split s = NoteFrontMatter::split(text);
    QVERIFY(s.present);
    QCOMPARE(s.block, text);
    QCOMPARE(s.body, QString());
}

void TestNoteFrontMatter::testSplitCrlf()
{
    const QString text =
        QStringLiteral("---\r\ntags: [a]\r\npinned: true\r\n---\r\nBody\r\n");
    NoteFrontMatter::Split s = NoteFrontMatter::split(text);
    QVERIFY(s.present);
    QCOMPARE(s.block + s.body, text);
    QCOMPARE(s.body, QStringLiteral("Body\r\n"));

    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(s.block);
    QCOMPARE(meta.tags, QStringList{QStringLiteral("a")});
    QCOMPARE(meta.pinned, true);
}

// ---------------------------------------------------------------- parse()

void TestNoteFrontMatter::testParseEmpty()
{
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(QString());
    QCOMPARE(meta.tags, QStringList());
    QVERIFY(!meta.created.isValid());
    QCOMPARE(meta.pinned, false);
    QCOMPARE(meta.favorite, false);
    QCOMPARE(meta.unknownLines, QStringList());
}

void TestNoteFrontMatter::testParseKnownKeys_data()
{
    QTest::addColumn<QString>("block");
    QTest::addColumn<QStringList>("tags");
    QTest::addColumn<QDateTime>("created");
    QTest::addColumn<bool>("pinned");
    QTest::addColumn<bool>("favorite");

    QTest::newRow("everything")
        << QStringLiteral("---\ntags: [work, ideas]\n"
                          "created: 2026-07-06T10:30:00\n"
                          "pinned: true\nfavorite: true\n---\n")
        << (QStringList() << QStringLiteral("work") << QStringLiteral("ideas"))
        << QDateTime(QDate(2026, 7, 6), QTime(10, 30, 0)) << true << true;
    QTest::newRow("date only becomes start of day")
        << QStringLiteral("---\ncreated: 2026-07-06\n---\n")
        << QStringList() << QDate(2026, 7, 6).startOfDay() << false << false;
    QTest::newRow("booleans false explicitly")
        << QStringLiteral("---\npinned: false\nfavorite: false\ntags: [a]\n---\n")
        << QStringList{QStringLiteral("a")} << QDateTime() << false << false;
    QTest::newRow("capitalized booleans")
        << QStringLiteral("---\npinned: True\nfavorite: FALSE\ntags: [a]\n---\n")
        << QStringList{QStringLiteral("a")} << QDateTime() << true << false;
    QTest::newRow("quoted created")
        << QStringLiteral("---\ncreated: \"2026-07-06T10:30:00\"\n---\n")
        << QStringList() << QDateTime(QDate(2026, 7, 6), QTime(10, 30, 0))
        << false << false;
}

void TestNoteFrontMatter::testParseKnownKeys()
{
    QFETCH(QString, block);
    QFETCH(QStringList, tags);
    QFETCH(QDateTime, created);
    QFETCH(bool, pinned);
    QFETCH(bool, favorite);

    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.tags, tags);
    QCOMPARE(meta.created, created);
    QCOMPARE(meta.pinned, pinned);
    QCOMPARE(meta.favorite, favorite);
    QCOMPARE(meta.unknownLines, QStringList());
}

void TestNoteFrontMatter::testParseTagsForms_data()
{
    QTest::addColumn<QString>("block");
    QTest::addColumn<QStringList>("tags");

    QTest::newRow("flow list")
        << QStringLiteral("---\ntags: [a, b, c]\n---\n")
        << (QStringList() << "a" << "b" << "c");
    QTest::newRow("flow list, no spaces")
        << QStringLiteral("---\ntags: [a,b]\n---\n")
        << (QStringList() << "a" << "b");
    QTest::newRow("empty flow list")
        << QStringLiteral("---\ntags: []\npinned: true\n---\n") << QStringList();
    QTest::newRow("bare key, no value")
        << QStringLiteral("---\ntags:\npinned: true\n---\n") << QStringList();
    QTest::newRow("single scalar")
        << QStringLiteral("---\ntags: work\n---\n")
        << QStringList{QStringLiteral("work")};
    QTest::newRow("quoted entries")
        << QStringLiteral("---\ntags: [\"a b\", 'c']\n---\n")
        << (QStringList() << "a b" << "c");
    QTest::newRow("block list")
        << QStringLiteral("---\ntags:\n- a\n- b\n---\n")
        << (QStringList() << "a" << "b");
    QTest::newRow("indented block list")
        << QStringLiteral("---\ntags:\n  - a\n  - b\n---\n")
        << (QStringList() << "a" << "b");
    QTest::newRow("tag with spaces inside")
        << QStringLiteral("---\ntags: [project x]\n---\n")
        << QStringList{QStringLiteral("project x")};
    QTest::newRow("empty entries dropped")
        << QStringLiteral("---\ntags: [a, , b]\n---\n")
        << (QStringList() << "a" << "b");
}

void TestNoteFrontMatter::testParseTagsForms()
{
    QFETCH(QString, block);
    QFETCH(QStringList, tags);

    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.tags, tags);
    QCOMPARE(meta.unknownLines, QStringList());
}

void TestNoteFrontMatter::testParseUnparseableValuesStayUnknown_data()
{
    QTest::addColumn<QString>("line");

    QTest::newRow("pinned: yes") << QStringLiteral("pinned: yes");
    QTest::newRow("favorite: 1") << QStringLiteral("favorite: 1");
    QTest::newRow("created: someday") << QStringLiteral("created: someday");
    QTest::newRow("created: 2026-13-99") << QStringLiteral("created: 2026-13-99");
    QTest::newRow("tags: [unclosed") << QStringLiteral("tags: [unclosed");
}

void TestNoteFrontMatter::testParseUnparseableValuesStayUnknown()
{
    QFETCH(QString, line);

    // A known key with a value we do not fully understand is preserved
    // verbatim, never half-parsed or dropped.
    const QString block = QStringLiteral("---\n%1\n---\n").arg(line);
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.unknownLines, QStringList{line});
    QCOMPARE(meta.favorite, false);
    QCOMPARE(meta.pinned, false);
    QVERIFY(!meta.created.isValid());
    QCOMPARE(meta.tags, QStringList());
}

void TestNoteFrontMatter::testParseForeignKeysPreserved()
{
    const QString block = QStringLiteral(
        "---\nlayout: post\ntags: [a]\ntitle: My Note\n---\n");
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.tags, QStringList{QStringLiteral("a")});
    QCOMPARE(meta.unknownLines,
             (QStringList() << QStringLiteral("layout: post")
                            << QStringLiteral("title: My Note")));
}

void TestNoteFrontMatter::testParseForeignBlockListPreserved()
{
    // A foreign key owns its continuation lines; they travel with it.
    const QString block = QStringLiteral(
        "---\naliases:\n  - note\n  - n\npinned: true\n---\n");
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.pinned, true);
    QCOMPARE(meta.unknownLines,
             (QStringList() << QStringLiteral("aliases:")
                            << QStringLiteral("  - note")
                            << QStringLiteral("  - n")));
}

void TestNoteFrontMatter::testParseCommentsAndBlanksPreserved()
{
    const QString block = QStringLiteral(
        "---\n# my metadata\n\ntags: [a]\n---\n");
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.tags, QStringList{QStringLiteral("a")});
    QCOMPARE(meta.unknownLines,
             (QStringList() << QStringLiteral("# my metadata") << QString()));
}

void TestNoteFrontMatter::testParseDuplicateKeyLastWins()
{
    const QString block =
        QStringLiteral("---\npinned: true\npinned: false\n---\n");
    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(block);
    QCOMPARE(meta.pinned, false);
    // Duplicates are consumed, not preserved: the next serialize
    // canonicalizes to a single line.
    QCOMPARE(meta.unknownLines, QStringList());
}

// ------------------------------------------------------------ serialize()

void TestNoteFrontMatter::testSerializeDefaultsIsEmpty()
{
    NoteFrontMatter::Metadata meta;
    QCOMPARE(NoteFrontMatter::serialize(meta), QString());
}

void TestNoteFrontMatter::testSerializeCanonicalOrder()
{
    NoteFrontMatter::Metadata meta;
    meta.tags = QStringList() << "b" << "a";
    meta.created = QDateTime(QDate(2026, 7, 6), QTime(10, 30, 0));
    meta.pinned = true;
    meta.favorite = true;
    QCOMPARE(NoteFrontMatter::serialize(meta),
             QStringLiteral("---\n"
                            "tags: [b, a]\n"
                            "created: 2026-07-06T10:30:00\n"
                            "pinned: true\n"
                            "favorite: true\n"
                            "---\n"));

    // Single non-default key: exactly one line, no false booleans.
    NoteFrontMatter::Metadata onlyFavorite;
    onlyFavorite.favorite = true;
    QCOMPARE(NoteFrontMatter::serialize(onlyFavorite),
             QStringLiteral("---\nfavorite: true\n---\n"));
}

void TestNoteFrontMatter::testSerializeTagQuoting_data()
{
    QTest::addColumn<QString>("tag");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << QStringLiteral("work") << QStringLiteral("work");
    QTest::newRow("inner space unquoted")
        << QStringLiteral("project x") << QStringLiteral("project x");
    QTest::newRow("comma") << QStringLiteral("a,b") << QStringLiteral("\"a,b\"");
    QTest::newRow("colon") << QStringLiteral("a:b") << QStringLiteral("\"a:b\"");
    QTest::newRow("bracket") << QStringLiteral("a[b") << QStringLiteral("\"a[b\"");
    QTest::newRow("hash") << QStringLiteral("#tag") << QStringLiteral("\"#tag\"");
    QTest::newRow("leading space")
        << QStringLiteral(" pad") << QStringLiteral("\" pad\"");
}

void TestNoteFrontMatter::testSerializeTagQuoting()
{
    QFETCH(QString, tag);
    QFETCH(QString, expected);

    NoteFrontMatter::Metadata meta;
    meta.tags = QStringList{tag};
    QCOMPARE(NoteFrontMatter::serialize(meta),
             QStringLiteral("---\ntags: [%1]\n---\n").arg(expected));

    // And the quoting round-trips through parse.
    NoteFrontMatter::Metadata back =
        NoteFrontMatter::parse(NoteFrontMatter::serialize(meta));
    QCOMPARE(back.tags, QStringList{tag});
}

void TestNoteFrontMatter::testSerializeUnknownLinesAfterKnown()
{
    NoteFrontMatter::Metadata meta;
    meta.pinned = true;
    meta.unknownLines = QStringList()
        << QStringLiteral("layout: post") << QStringLiteral("aliases:")
        << QStringLiteral("  - n");
    QCOMPARE(NoteFrontMatter::serialize(meta),
             QStringLiteral("---\n"
                            "pinned: true\n"
                            "layout: post\n"
                            "aliases:\n"
                            "  - n\n"
                            "---\n"));
}

void TestNoteFrontMatter::testSerializeKeylessDegradesToVisibleBody()
{
    // Pathological: all known keys default and only non-key unknown lines
    // (comments) remain. serialize() still emits the block — dropping the
    // lines would destroy bytes — but split() will not recognize a
    // keyless block, so on reload the text degrades to VISIBLE body
    // content rather than vanishing. Deliberate, per notefrontmatter.h.
    NoteFrontMatter::Metadata meta;
    meta.unknownLines = QStringList{QStringLiteral("# a comment")};
    const QString block = NoteFrontMatter::serialize(meta);
    QCOMPARE(block, QStringLiteral("---\n# a comment\n---\n"));
    QVERIFY(!NoteFrontMatter::split(block + QStringLiteral("Body\n")).present);
}

// ------------------------------------------------------------ round trips

void TestNoteFrontMatter::testParseSerializeRoundTrip_data()
{
    QTest::addColumn<QStringList>("tags");
    QTest::addColumn<QDateTime>("created");
    QTest::addColumn<bool>("pinned");
    QTest::addColumn<bool>("favorite");
    QTest::addColumn<QStringList>("unknownLines");

    QTest::newRow("defaults")
        << QStringList() << QDateTime() << false << false << QStringList();
    QTest::newRow("full")
        << (QStringList() << "work" << "a b" << "x,y")
        << QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5)) << true << true
        << (QStringList() << QStringLiteral("layout: post"));
    QTest::newRow("tags only")
        << QStringList{QStringLiteral("solo")} << QDateTime() << false << false
        << QStringList();
    QTest::newRow("unknown block list")
        << QStringList() << QDateTime() << true << false
        << (QStringList() << QStringLiteral("aliases:")
                          << QStringLiteral("  - n"));
}

void TestNoteFrontMatter::testParseSerializeRoundTrip()
{
    QFETCH(QStringList, tags);
    QFETCH(QDateTime, created);
    QFETCH(bool, pinned);
    QFETCH(bool, favorite);
    QFETCH(QStringList, unknownLines);

    NoteFrontMatter::Metadata meta;
    meta.tags = tags;
    meta.created = created;
    meta.pinned = pinned;
    meta.favorite = favorite;
    meta.unknownLines = unknownLines;

    NoteFrontMatter::Metadata back =
        NoteFrontMatter::parse(NoteFrontMatter::serialize(meta));
    QVERIFY(back == meta);
}

void TestNoteFrontMatter::testSerializeIsCanonicalFixedPoint()
{
    // serialize(parse(x)) is idempotent: a second pass changes nothing.
    const QString foreign = QStringLiteral(
        "---\ntitle: Hello\ntags:\n- b\n- a\npinned: true\n"
        "created: 2026-07-06\n---\n");
    const QString once =
        NoteFrontMatter::serialize(NoteFrontMatter::parse(foreign));
    const QString twice =
        NoteFrontMatter::serialize(NoteFrontMatter::parse(once));
    QCOMPARE(twice, once);
}

void TestNoteFrontMatter::testForeignBlockSurvivesMetadataEdit()
{
    // The step-2 collection flow: parse a foreign block, change one known
    // field, serialize — every foreign line must still be there.
    const QString file = QStringLiteral(
        "---\nlayout: post\naliases:\n  - n\ntags: [a]\ncssclass: wide\n"
        "---\n# Body\n");
    NoteFrontMatter::Split s = NoteFrontMatter::split(file);
    QVERIFY(s.present);

    NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(s.block);
    meta.pinned = true;
    const QString rewritten = NoteFrontMatter::serialize(meta) + s.body;

    QVERIFY(rewritten.contains(QStringLiteral("layout: post\n")));
    QVERIFY(rewritten.contains(QStringLiteral("aliases:\n  - n\n")));
    QVERIFY(rewritten.contains(QStringLiteral("cssclass: wide\n")));
    QVERIFY(rewritten.contains(QStringLiteral("tags: [a]\n")));
    QVERIFY(rewritten.contains(QStringLiteral("pinned: true\n")));
    QVERIFY(rewritten.endsWith(QStringLiteral("---\n# Body\n")));
}

void TestNoteFrontMatter::testGoalParseSerializeRoundTrip()
{
    const NoteFrontMatter::Metadata m =
        NoteFrontMatter::parse(QStringLiteral("---\ngoal: 500\n---\n"));
    QCOMPARE(m.goal, 500);
    QVERIFY(m.unknownLines.isEmpty());

    // Serialize a goal in canonical order (after favorite), then round-trips.
    NoteFrontMatter::Metadata w;
    w.favorite = true;
    w.goal = 1200;
    const QString block = NoteFrontMatter::serialize(w);
    QVERIFY(block.contains(QStringLiteral("goal: 1200")));
    QCOMPARE(NoteFrontMatter::parse(block).goal, 1200);

    // A zero goal is unset: it serializes to nothing.
    NoteFrontMatter::Metadata z;
    z.goal = 0;
    QVERIFY(!NoteFrontMatter::serialize(z).contains(QStringLiteral("goal")));
}

void TestNoteFrontMatter::testGoalInvalidValueStaysUnknown()
{
    // A non-integer or non-positive goal is not understood → preserved
    // verbatim as a foreign line, never silently dropped.
    const NoteFrontMatter::Metadata a =
        NoteFrontMatter::parse(QStringLiteral("---\ngoal: soon\n---\n"));
    QCOMPARE(a.goal, 0);
    QVERIFY(a.unknownLines.contains(QStringLiteral("goal: soon")));

    const NoteFrontMatter::Metadata b =
        NoteFrontMatter::parse(QStringLiteral("---\ngoal: -5\n---\n"));
    QCOMPARE(b.goal, 0);
    QVERIFY(b.unknownLines.contains(QStringLiteral("goal: -5")));
}

// ------------------------------------- general key map

void TestNoteFrontMatter::testFieldsMapExtraction()
{
    const NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(
        QStringLiteral("---\n"
                       "tags: [a, b]\n"
                       "status: active\n"
                       "due: 2026-08-01\n"
                       "priority: 3\n"
                       "status: revised\n"     // duplicates: last wins
                       "# comment\n"
                       "not a mapping line\n"  // malformed -> unknown only
                       "---\n"));
    // Known and foreign keys land uniformly; the malformed line does not.
    QCOMPARE(meta.fields.value("tags"), QString("[a, b]"));
    QCOMPARE(meta.fields.value("status"), QString("revised"));
    QCOMPARE(meta.fields.value("due"), QString("2026-08-01"));
    QCOMPARE(meta.fields.value("priority"), QString("3"));
    QVERIFY(!meta.fields.contains("not a mapping line"));
    QVERIFY(meta.unknownLines.contains(QStringLiteral("not a mapping line")));
}

void TestNoteFrontMatter::testFieldsMapNeverSerialized()
{
    // fields is read-only derived data: serialize() emits from the
    // structured fields plus unknownLines, byte-identically with or
    // without the map populated.
    const QString foreign = QStringLiteral(
        "---\nstatus: active\ntags: [x]\npinned: true\n---\n");
    const NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(foreign);
    QVERIFY(!meta.fields.isEmpty());
    NoteFrontMatter::Metadata scrubbed = meta;
    scrubbed.fields.clear();
    QCOMPARE(NoteFrontMatter::serialize(meta),
             NoteFrontMatter::serialize(scrubbed));
}

void TestNoteFrontMatter::testTypedAccessors()
{
    const NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(
        QStringLiteral("---\n"
                       "title: \"Quoted title\"\n"
                       "due: 2026-08-01\n"
                       "when: 2026-08-01T09:30:00\n"
                       "priority: 3.5\n"
                       "people: [Ann, \"Lee, Jr\"]\n"
                       "csv: a, b , c\n"
                       "text: hello\n"
                       "---\n"));

    QCOMPARE(meta.fieldString("title"), QString("Quoted title"));
    QCOMPARE(meta.fieldString("missing"), QString());

    QCOMPARE(meta.fieldDate("due"), QDate(2026, 8, 1).startOfDay());
    QCOMPARE(meta.fieldDate("when"),
             QDateTime(QDate(2026, 8, 1), QTime(9, 30)));
    QVERIFY(!meta.fieldDate("text").isValid());

    bool ok = false;
    QCOMPARE(meta.fieldNumber("priority", &ok), 3.5);
    QVERIFY(ok);
    meta.fieldNumber("text", &ok);
    QVERIFY(!ok);

    QCOMPARE(meta.fieldList("people"),
             (QStringList() << "Ann" << "Lee, Jr"));
    QCOMPARE(meta.fieldList("csv"), (QStringList() << "a" << "b" << "c"));
    QVERIFY(meta.fieldList("missing").isEmpty());
}

QTEST_MAIN(TestNoteFrontMatter)
#include "test_notefrontmatter.moc"
