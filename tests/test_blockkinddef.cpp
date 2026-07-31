// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QMetaEnum>
#include <QSet>

#include "block.h"
#include "blockkind.h"
#include "blockkinddef.h"
#include "blockkindregistry.h"
#include "blockkinds.h"
#include "blockmodel.h"
#include "documentexporter.h"
#include "documentserializer.h"

// One class per block kind, and what that buys.
//
// About twenty kinds of block exist, and thirteen places used to decide
// something per kind: the serializer switched on a type enum, the exporter
// compared fence-language strings, the outline, the typography and the
// toolbar each carried a list of their own. Nothing checked that a new kind
// reached all of them, and the last one added did not — a `query` fence
// matched none of the exporter's four language branches, so every HTML and
// PDF export printed the query's own `from:`/`where:` spec as a code
// listing, which is the one part of the block a reader never sees on screen.
//
// A pure virtual on BlockKindDef makes most of that a compile error. Three
// things it cannot make a compile error are checked here, over every kind
// rather than over a list written by hand:
//
//   * an enumerator with no class, which resolves to a paragraph at runtime;
//   * a kind whose markdown does not survive a save and a reload;
//   * a kind that exports as nothing, in either format.
class TestBlockKindDef : public QObject
{
    Q_OBJECT

private:
    // One sample per kind: markdown that parses to a block of that kind.
    //
    // Keyed by the kind's id() rather than by its enumerator, so a kind added
    // without a sample here is named in the failure by the same string its
    // implementation carries.
    static QHash<QString, QString> samples()
    {
        return {
            { QStringLiteral("paragraph"), QStringLiteral("Ordinary prose.") },
            { QStringLiteral("heading1"), QStringLiteral("# Title") },
            { QStringLiteral("heading2"), QStringLiteral("## Section") },
            { QStringLiteral("heading3"), QStringLiteral("### Subsection") },
            { QStringLiteral("heading4"), QStringLiteral("#### Minor") },
            { QStringLiteral("bulletlist"), QStringLiteral("- an item") },
            { QStringLiteral("numberedlist"), QStringLiteral("1. an item") },
            { QStringLiteral("todo"), QStringLiteral("- [ ] a task") },
            { QStringLiteral("quote"), QStringLiteral("> quoted") },
            { QStringLiteral("codeblock"),
              QStringLiteral("```python\nreturn 1\n```") },
            { QStringLiteral("divider"), QStringLiteral("---") },
            { QStringLiteral("image"), QStringLiteral("![alt](pic.png)") },
            { QStringLiteral("media"), QStringLiteral("![clip](clip.mp4)") },
            { QStringLiteral("embed"),
              QStringLiteral("![](https://example.com/page)") },
            { QStringLiteral("callout"),
              QStringLiteral("> [!info] Heads up\n> the body") },
            { QStringLiteral("mathblock"), QStringLiteral("$$\na^2 + b^2\n$$") },
            { QStringLiteral("table"),
              QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |") },
            { QStringLiteral("kanban"),
              QStringLiteral("```kanban\n## To do\n- [ ] a card\n```") },
            { QStringLiteral("toc"), QStringLiteral("```toc\nContents\n```") },
            { QStringLiteral("mermaid"),
              QStringLiteral("```mermaid\nflowchart LR\nA-->B\n```") },
            { QStringLiteral("query"),
              QStringLiteral("```query\nfrom: Projects/\n```") },
        };
    }

private slots:
    // Every enumerator has exactly one definition.
    //
    // The compiler does not require one: an enumerator is just a number, and
    // a kind with no class resolves to the paragraph, so the block draws and
    // exports as prose and nothing says otherwise. The enumeration is walked
    // from its metaobject rather than from a list here, so this cannot fall
    // behind it.
    void everyEnumeratorHasADefinition()
    {
        const QMetaEnum kinds = QMetaEnum::fromType<BlockKinds::Kind>();
        QVERIFY(kinds.isValid());
        QVERIFY(kinds.keyCount() > 0);

        QSet<const BlockKindDef *> seen;
        for (int i = 0; i < kinds.keyCount(); ++i) {
            const QString name = QString::fromLatin1(kinds.key(i));
            const BlockKind kind = static_cast<BlockKind>(kinds.value(i));
            const BlockKindDef *def = BlockKindDefs::builtin(kind);
            QVERIFY2(def != nullptr,
                     qPrintable(QStringLiteral(
                                    "BlockKind::%1 has no definition; a block "
                                    "of that kind resolves to a paragraph")
                                    .arg(name)));
            QCOMPARE(def->kind(), kind);
            // Two enumerators answered by one instance would mean one of them
            // silently behaves as the other.
            QVERIFY2(!seen.contains(def),
                     qPrintable(QStringLiteral("BlockKind::%1 shares its "
                                               "definition with another kind")
                                    .arg(name)));
            seen.insert(def);
        }
        QCOMPARE(BlockKindDefs::builtins().size(), kinds.keyCount());
    }

    // Every kind answers the identity questions, and answers them distinctly.
    void everyKindHasAUsableIdentity()
    {
        QSet<QString> ids;
        QSet<QString> languages;
        for (const BlockKindDef *def : BlockKindDefs::builtins()) {
            QVERIFY(!def->id().isEmpty());
            QCOMPARE(def->id(), def->id().toLower());
            QVERIFY2(!ids.contains(def->id()), qPrintable(def->id()));
            ids.insert(def->id());

            // A fence language is what selects a kind, so two kinds claiming
            // one would make the second unreachable.
            const QString language = def->fenceLanguage();
            if (language.isEmpty())
                continue;
            QVERIFY2(!languages.contains(language), qPrintable(language));
            languages.insert(language);
        }
    }

    // Every kind has a sample below, and every sample names a kind. A kind
    // added without one would pass the rest of this suite by not being
    // exercised at all.
    void everyKindHasASample()
    {
        const QHash<QString, QString> corpus = samples();
        for (const BlockKindDef *def : BlockKindDefs::builtins()) {
            QVERIFY2(corpus.contains(def->id()),
                     qPrintable(QStringLiteral(
                                    "no sample for the '%1' kind; add one to "
                                    "samples() in this file")
                                    .arg(def->id())));
        }
        for (auto it = corpus.constBegin(); it != corpus.constEnd(); ++it) {
            bool matched = false;
            for (const BlockKindDef *def : BlockKindDefs::builtins())
                matched = matched || def->id() == it.key();
            QVERIFY2(matched, qPrintable(it.key()));
        }
    }

    void everySampleParsesToItsOwnKind_data() { addSampleRows(); }

    // The sample is what the rest of the suite rests on: if it parses to some
    // other kind, every case below is testing the wrong class and passing.
    void everySampleParsesToItsOwnKind()
    {
        QFETCH(QString, id);
        QFETCH(QString, markdown);

        DocumentSerializer serializer;
        const QList<Block::State> blocks = serializer.parse(markdown);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(BlockKindDefs::forState(blocks.first())->id(), id);
    }

    void everyKindRoundTripsItsMarkdown_data() { addSampleRows(); }

    // The one that protects the reader's files. A save writes what the kind
    // serializes, and a load parses it back; a kind whose two halves disagree
    // rewrites notes on every open, and a document manager that compares the
    // bytes it would write against the bytes it read starts copying the whole
    // vault to .bak files.
    void everyKindRoundTripsItsMarkdown()
    {
        QFETCH(QString, id);
        QFETCH(QString, markdown);

        DocumentSerializer serializer;
        serializer.setTrailingNewline(false);
        BlockModel model;
        serializer.loadIntoModel(&model, markdown);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.blockAt(0)->kind()->id(), id);
        QCOMPARE(serializer.serialize(&model), markdown);
    }

    void everyKindCarriesItsAttributeTag_data() { addSampleRows(); }

    // A block's presentation attributes ride an HTML comment on one of its
    // lines, and which line is the kind's answer: for a code fence, a math
    // fence, a table and a callout it has to be the OPENING one, because
    // their last line is a terminator the parser requires to be bare. A
    // tagged closing fence never closes its block, so on the next load the
    // rest of the note is swallowed as that block's content.
    void everyKindCarriesItsAttributeTag()
    {
        QFETCH(QString, id);
        QFETCH(QString, markdown);

        DocumentSerializer serializer;
        serializer.setTrailingNewline(false);
        BlockModel model;
        // A sentinel after the block: if the tag stopped a fence from
        // closing, the sentinel is read as part of the block above it.
        serializer.loadIntoModel(&model, markdown + QStringLiteral("\n\nafter"));
        QCOMPARE(model.count(), 2);
        model.setBlockAttributes(0, QStringLiteral("align=center"));

        const QString written = serializer.serialize(&model);
        QVERIFY(written.contains(QStringLiteral("<!--kvit align=center-->")));

        BlockModel reloaded;
        serializer.loadIntoModel(&reloaded, written);
        QCOMPARE(reloaded.count(), 2);
        QCOMPARE(reloaded.blockAt(0)->kind()->id(), id);
        QCOMPARE(reloaded.blockAt(0)->attributes(),
                 QStringLiteral("align=center"));
        QCOMPARE(reloaded.blockAt(1)->content(), QStringLiteral("after"));
        // And the round trip is stable a second time.
        QCOMPARE(serializer.serialize(&reloaded), written);
    }

    void everyKindExportsSomething_data() { addSampleRows(); }

    // A kind that renders as nothing is the export defect this design exists
    // to prevent, and it is invisible: the file is written, it opens, and the
    // block is simply not in it.
    void everyKindExportsSomething()
    {
        QFETCH(QString, id);
        QFETCH(QString, markdown);

        // A table of contents lists the document's headings, so it is
        // exported inside a document that has one. In a note with none it
        // writes nothing, which is the right answer and not a defect.
        const bool isToc = id == QLatin1String("toc");
        if (isToc)
            markdown = QStringLiteral("# A heading\n\n") + markdown;

        DocumentExporter exporter;
        const QString html = exporter.htmlForMarkdown(markdown);
        const int bodyAt = html.indexOf(QStringLiteral("<body>"));
        QVERIFY(bodyAt > 0);
        const QString body =
            html.mid(bodyAt + 6,
                     html.indexOf(QStringLiteral("</body>")) - bodyAt - 6).trimmed();
        QVERIFY2(!body.isEmpty(),
                 qPrintable(QStringLiteral("the '%1' kind exports as no HTML "
                                           "at all").arg(id)));

        const QString text = exporter.plainTextForMarkdown(markdown).trimmed();
        QVERIFY2(!text.isEmpty(),
                 qPrintable(QStringLiteral("the '%1' kind exports as no plain "
                                           "text at all").arg(id)));
        if (isToc) {
            // The heading alone would satisfy the two assertions above, so
            // the entry the block itself contributes is named.
            QVERIFY(html.contains(QStringLiteral("class=\"toc\"")));
            QVERIFY(text.count(QStringLiteral("A heading")) == 2);
        }
    }

    void everyKindExportsSomethingOtherThanItsSource_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("sourceFragment");

        // The fenced kinds the editor draws from something other than their
        // own text. Writing the source is what a `.txt` export did for all of
        // them, and what the HTML export did for a query.
        QTest::newRow("kanban")
            << QStringLiteral("kanban")
            << QStringLiteral("```kanban\n## To do\n- [ ] a card\n```")
            << QStringLiteral("- [ ] a card");
        // A query is deliberately absent. With no collection open there is
        // nothing to ask, and writing an empty table would claim the query
        // matched nothing — a different statement from never having run it —
        // so its spec going out as source is the truthful answer. The
        // exporter suite covers the case where a vault IS open.
        QTest::newRow("toc")
            << QStringLiteral("toc")
            << QStringLiteral("# One\n\n```toc\nstale body\n```")
            << QStringLiteral("stale body");
    }

    void everyKindExportsSomethingOtherThanItsSource()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, sourceFragment);

        DocumentExporter exporter;
        QVERIFY(!exporter.htmlForMarkdown(markdown).contains(sourceFragment));
        QVERIFY(!exporter.plainTextForMarkdown(markdown).contains(sourceFragment));
    }

    // The predicates, checked against each other rather than one by one.
    void thePredicatesAreConsistent()
    {
        for (const BlockKindDef *def : BlockKindDefs::builtins()) {
            const QString id = def->id();

            // Verbatim means the block's content IS its text, so a search
            // match's offsets are markdown offsets and a replace splices
            // straight in. A kind that claims it while its search text is
            // something else rewrites the wrong span of a reader's content.
            if (def->isVerbatim()) {
                Block::State state;
                state.type = Block::CodeBlock;
                state.content = QStringLiteral("a *b* c");
                QCOMPARE(def->searchText(state), state.content);
                QVERIFY2(!def->foldsLineBreaks(), qPrintable(id));
            }

            // A heading level is 1 to 4 or nothing at all, and only the four
            // heading kinds have one.
            QVERIFY2(def->headingLevel() >= 0 && def->headingLevel() <= 4,
                     qPrintable(id));
            QCOMPARE(def->headingLevel() > 0, id.startsWith(QStringLiteral("heading")));
        }
    }

    // The five kinds that share one delegate publish one value between them,
    // and no other kind shares. Those five zeros are what keep the most
    // common conversion in the editor — a paragraph becoming a heading, run
    // every time someone types "# " at the start of a line — from destroying
    // the delegate the caret is sitting in.
    void onlyTheTextKindsShareADelegateKind()
    {
        QHash<int, QStringList> byDelegateKind;
        for (const BlockKindDef *def : BlockKindDefs::builtins())
            byDelegateKind[def->delegateKind()].append(def->id());

        for (auto it = byDelegateKind.constBegin();
             it != byDelegateKind.constEnd(); ++it) {
            if (it.key() == 0)
                continue;
            QVERIFY2(it.value().size() == 1,
                     qPrintable(QStringLiteral("delegate kind %1 is shared by "
                                               "%2")
                                    .arg(it.key())
                                    .arg(it.value().join(QStringLiteral(", ")))));
        }
        QStringList shared = byDelegateKind.value(0);
        shared.sort();
        QCOMPARE(shared, QStringList({ QStringLiteral("heading1"),
                                       QStringLiteral("heading2"),
                                       QStringLiteral("heading3"),
                                       QStringLiteral("heading4"),
                                       QStringLiteral("paragraph") }));
    }

private:
    static void addSampleRows()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("markdown");
        const QHash<QString, QString> corpus = samples();
        // Rows in the order the kinds are registered, so a failure reads in
        // the same order the implementation does.
        for (const BlockKindDef *def : BlockKindDefs::builtins()) {
            const auto sample = corpus.constFind(def->id());
            if (sample == corpus.constEnd())
                continue;   // everyKindHasASample reports this
            QTest::newRow(qPrintable(def->id())) << def->id() << *sample;
        }
    }
};

QTEST_MAIN(TestBlockKindDef)
#include "test_blockkinddef.moc"
