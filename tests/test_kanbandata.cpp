// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QRandomGenerator>
#include <QtTest>

#include "kanbandata.h"

using namespace KanbanData;

// Corpus for the kanban board parse/serialize/mutate core: columns, cards,
// done state, labels, due dates, descriptions, and the mutations — pinned
// before the board delegate paints any of it.
class TestKanbanData : public QObject
{
    Q_OBJECT

    static QString cal() { return QString::fromUtf8("\xF0\x9F\x93\x85"); }

private slots:
    void parseColumnsAndCards()
    {
        const QString md =
            "## To do\n- [ ] first #urgent " + cal() + " 2026-07-15\n"
            "  a description line\n- [x] done card\n## In progress\n- [ ] wip";
        const Board b = parse(md);
        QCOMPARE(b.columnCount(), 2);
        QCOMPARE(b.columns[0].name, QString("To do"));
        QCOMPARE(b.columns[0].cards.size(), 2);
        const Card &c0 = b.columns[0].cards[0];
        QCOMPARE(c0.title, QString("first"));
        QCOMPARE(c0.done, false);
        QCOMPARE(c0.labels, QStringList({"urgent"}));
        QCOMPARE(c0.due, QString("2026-07-15"));
        QCOMPARE(c0.description, QString("a description line"));
        QCOMPARE(b.columns[0].cards[1].done, true);
        QCOMPARE(b.columns[1].name, QString("In progress"));
        QCOMPARE(b.columns[1].cards.size(), 1);
    }

    void serializeRoundTrips()
    {
        const QString md =
            "## To do\n- [ ] first #urgent " + cal() + " 2026-07-15\n"
            "  desc\n- [x] done\n## Done\n- [x] shipped #release";
        QCOMPARE(serialize(parse(md)), md);
    }

    void looksLikeBoard()
    {
        QVERIFY(KanbanData::looksLikeBoard("## Column\n- [ ] card"));
        QVERIFY(!KanbanData::looksLikeBoard("just some code\nno headers"));
    }

    void mutations()
    {
        const QString md = "## A\n- [ ] one\n- [ ] two\n## B\n- [ ] three";

        // Move a card between columns to a target index.
        QString moved = moveCard(md, 0, 0, 1, 1);
        Board mb = parse(moved);
        QCOMPARE(mb.columns[0].cards.size(), 1);
        QCOMPARE(mb.columns[1].cards.size(), 2);
        QCOMPARE(mb.columns[1].cards[1].title, QString("one"));

        // Same-column reorder: dragging the first card down to sit before
        // the original third card (toIndex names the pre-removal slot).
        const QString three = "## A\n- [ ] one\n- [ ] two\n- [ ] three";
        Board down = parse(moveCard(three, 0, 0, 0, 2));
        QCOMPARE(down.columns[0].cards.size(), 3);
        QStringList downTitles;
        for (const Card &c : down.columns[0].cards) downTitles << c.title;
        QCOMPARE(downTitles, QStringList({"two", "one", "three"}));

        // Same-column upward move needs no index adjustment.
        Board up = parse(moveCard(three, 0, 2, 0, 0));
        QStringList upTitles;
        for (const Card &c : up.columns[0].cards) upTitles << c.title;
        QCOMPARE(upTitles, QStringList({"three", "one", "two"}));

        // Dropping on the column background appends (toIndex == card count).
        Board app = parse(moveCard(three, 0, 0, 0, 3));
        QStringList appTitles;
        for (const Card &c : app.columns[0].cards) appTitles << c.title;
        QCOMPARE(appTitles, QStringList({"two", "three", "one"}));

        // Toggle done.
        QCOMPARE(parse(toggleCardDone(md, 0, 0)).columns[0].cards[0].done, true);

        // Add card / column.
        QCOMPARE(parse(addCard(md, 0, "new")).columns[0].cards.size(), 3);
        QCOMPARE(parse(addColumn(md, "C")).columnCount(), 3);

        // Remove card / column.
        QCOMPARE(parse(removeCard(md, 0, 0)).columns[0].cards.size(), 1);
        QCOMPARE(parse(removeColumn(md, 1)).columnCount(), 1);

        // Move a column.
        QCOMPARE(parse(moveColumn(md, 0, 1)).columns[0].name, QString("B"));
    }

    void unmodelledContentSurvivesMutation()
    {
        const QString md =
            "An introductory paragraph about this board.\n"
            "\n"
            "<!-- a note the parser does not model -->\n"
            "## To do\n"
            "- [ ] one\n"
            "- [ ] two\n";
        // Only the toggled checkbox differs; everything else, including the
        // trailing newline, comes back byte for byte.
        const QString expected =
            "An introductory paragraph about this board.\n"
            "\n"
            "<!-- a note the parser does not model -->\n"
            "## To do\n"
            "- [x] one\n"
            "- [ ] two\n";
        QCOMPARE(toggleCardDone(md, 0, 0), expected);
    }

    void triviaInsideAColumnSurvivesEveryMutation()
    {
        const QString md =
            "Intro prose.\n"
            "## To do\n"
            "<!-- why this column exists -->\n"
            "- [ ] one\n"
            "\n"
            "> a quoted aside\n"
            "- [ ] two\n"
            "## Done\n"
            "- [x] shipped\n";
        const QStringList marks{ "Intro prose.", "<!-- why this column exists -->",
                                 "> a quoted aside" };
        struct { const char *what; QString out; } cases[] = {
            { "toggle",       toggleCardDone(md, 0, 0) },
            { "removeCard",   removeCard(md, 0, 0) },
            { "addCard",      addCard(md, 0, "new") },
            { "moveCard",     moveCard(md, 0, 0, 1, 0) },
            { "setCard",      setCard(md, 0, 1, "t", true, {}, "", "") },
            { "setCardLine",  setCardLine(md, 0, 1, "typed text #tag") },
            { "setCardDescription",
                              setCardDescription(md, 0, 1, "typed\nlines") },
            { "addColumn",    addColumn(md, "C") },
            { "renameColumn", renameColumn(md, 0, "Backlog") },
            { "removeColumn", removeColumn(md, 0) },
            { "moveColumn",   moveColumn(md, 0, 1) },
        };
        for (const auto &c : cases) {
            for (const QString &mark : marks) {
                QVERIFY2(c.out.contains(mark),
                         qPrintable(QString("%1 dropped %2:\n%3")
                                        .arg(c.what, mark, c.out)));
            }
        }
    }

    // The mutation an untouched card sees is no mutation at all: its source
    // line is re-emitted verbatim rather than re-rendered from the model.
    void untouchedCardsKeepTheirSourceLine()
    {
        const QString md = "## A\n* [ ]  odd   spacing #a #b\n- [ ] plain";
        QCOMPARE(toggleCardDone(md, 0, 1),
                 QString("## A\n* [ ]  odd   spacing #a #b\n- [x] plain"));
        // Toggling the odd one edits only its checkbox.
        QCOMPARE(toggleCardDone(md, 0, 0),
                 QString("## A\n* [x]  odd   spacing #a #b\n- [ ] plain"));
    }

    void urlFragmentIsNotALabel()
    {
        const QString md = "## A\n- [ ] Read https://example.com/#intro";
        const Board b = parse(md);
        QCOMPARE(b.columns[0].cards[0].title,
                 QString("Read https://example.com/#intro"));
        QVERIFY(b.columns[0].cards[0].labels.isEmpty());
        QCOMPARE(serialize(b), md);
        // Rewriting the card through the editor keeps the fragment too.
        const QString out = setCard(md, 0, 0, "Read https://example.com/#intro",
                                    false, {}, "", "");
        QCOMPARE(out, md);
    }

    void labelsAreRecognizedOnlyAtTokenBoundaries()
    {
        const Board b = parse("## A\n- [ ] #lead mid #tag C#sharp a#b end");
        const Card &c = b.columns[0].cards[0];
        QCOMPARE(c.labels, QStringList({ "lead", "tag" }));
        QCOMPARE(c.title, QString("mid C#sharp a#b end"));
    }

    void literalHashesRoundTripThroughTheEscape()
    {
        const QStringList titles{
            "#hashtag as text",
            "\\#already escaped",
            "\\\\#two slashes",
            "issue #42 and #43",
            "trailing hash #",
            "Read https://example.com/#intro",
        };
        for (const QString &title : titles) {
            const QString md = setCard("## A\n- [ ] x", 0, 0, title, false,
                                       QStringList({ "real" }), "", "");
            // By value: binding a reference here would dangle, because the
            // Board parse() returns is a temporary that dies at the
            // semicolon (caught by AddressSanitizer).
            const Card c = parse(md).columns[0].cards[0];
            QVERIFY2(c.title == title,
                     qPrintable(QString("title %1 came back as %2 (source %3)")
                                    .arg(title, c.title, md)));
            QCOMPARE(c.labels, QStringList({ "real" }));
        }
    }

    // serialize(parse(x)) == x for arbitrary content, and every unmodelled
    // line survives any single mutation byte for byte. This is the fuzz gate
    // the H8 fix rests on.
    void mutationPreservationProperty()
    {
        QRandomGenerator rng(0x4b616e62u); // fixed seed: failures reproduce
        const QStringList triviaShapes{
            QStringLiteral("<!-- note %1 -->"),
            QStringLiteral("Prose paragraph %1."),
            QStringLiteral("> quoted aside %1"),
            QStringLiteral("1. an ordinary list item %1"),
            QStringLiteral("| a | table %1 |"),
            QStringLiteral(""),
        };
        int nextMark = 0;

        for (int iter = 0; iter < 300; ++iter) {
            QStringList lines;
            QStringList marks; // the identifiable trivia, in document order
            auto sprinkle = [&] {
                const int n = rng.bounded(3);
                for (int i = 0; i < n; ++i) {
                    const QString shape = triviaShapes[rng.bounded(triviaShapes.size())];
                    const QString line = shape.contains(QLatin1String("%1"))
                        ? shape.arg(nextMark++) : shape;
                    lines << line;
                    if (!line.isEmpty())
                        marks << line;
                }
            };

            sprinkle(); // preamble
            const int cols = rng.bounded(4);
            QList<int> cardCounts;
            for (int c = 0; c < cols; ++c) {
                lines << QStringLiteral("## Column %1").arg(c);
                sprinkle();
                const int cards = rng.bounded(4);
                cardCounts << cards;
                for (int k = 0; k < cards; ++k) {
                    lines << QStringLiteral("- [%1] card %2-%3 #tag%3")
                                 .arg(rng.bounded(2) ? "x" : " ").arg(c).arg(k);
                    if (rng.bounded(2))
                        lines << QStringLiteral("  description of %1-%2").arg(c).arg(k);
                    sprinkle();
                }
            }
            const QString md = lines.join(QLatin1Char('\n'));

            QCOMPARE(serialize(parse(md)), md);

            QStringList outs;
            if (cols > 0) {
                const int c = rng.bounded(cols);
                const int other = rng.bounded(cols);
                outs << addCard(md, c, "added")
                     << addColumn(md, "Added")
                     << renameColumn(md, c, "Renamed")
                     << removeColumn(md, c)
                     << moveColumn(md, c, other);
                if (cardCounts[c] > 0) {
                    const int k = rng.bounded(cardCounts[c]);
                    outs << toggleCardDone(md, c, k)
                         << removeCard(md, c, k)
                         << setCard(md, c, k, "rewritten", true,
                                    QStringList({ "l" }), "2026-01-01", "d")
                         << setCardLine(md, c, k, "typed over #l")
                         << setCardLine(md, c, k, "stamped", "2026-07-26")
                         << setCardDescription(md, c, k, "typed\nover")
                         << toggleCardDone(md, c, k, "2026-07-26")
                         << moveCard(md, c, k, other,
                                     rng.bounded(cardCounts[other] + 1));
                }
            } else {
                outs << addColumn(md, "Added");
            }

            for (const QString &out : outs) {
                QStringList seen;
                for (const QString &line : out.split(QLatin1Char('\n')))
                    if (marks.contains(line))
                        seen << line;
                QStringList sortedSeen = seen;
                QStringList sortedMarks = marks;
                sortedSeen.sort();
                sortedMarks.sort();
                QVERIFY2(sortedSeen == sortedMarks,
                         qPrintable(QString("iteration %1: trivia lost\n"
                                            "--- in ---\n%2\n--- out ---\n%3")
                                        .arg(iter).arg(md, out)));
                // Blank lines are unmodelled too; a mutation may relocate the
                // run at an insertion point but must not consume it.
                const auto blanks = [](const QString &s) {
                    if (s.isEmpty())
                        return 0; // no lines at all, not one empty line
                    int n = 0;
                    for (const QString &l : s.split(QLatin1Char('\n')))
                        if (l.isEmpty()) ++n;
                    return n;
                };
                // A board that a removal emptied out has nothing left to hang
                // a blank line on, so exempt that degenerate case.
                QVERIFY2(out.isEmpty() || blanks(out) == blanks(md),
                         qPrintable(QString("iteration %1: blank lines %2 -> %3\n"
                                            "--- in ---\n%4\n--- out ---\n%5")
                                        .arg(iter).arg(blanks(md))
                                        .arg(blanks(out)).arg(md, out)));
            }
        }
    }

    void setCardOverwritesFields()
    {
        const QString md = "## A\n- [ ] one";
        const QString out = setCard(md, 0, 0, "renamed", true,
                                    QStringList({"x", "y"}), "2026-01-01", "notes");
        const Board b = parse(out);
        const Card &c = b.columns[0].cards[0];
        QCOMPARE(c.title, QString("renamed"));
        QCOMPARE(c.done, true);
        QCOMPARE(c.labels, QStringList({"x", "y"}));
        QCOMPARE(c.due, QString("2026-01-01"));
        QCOMPARE(c.description, QString("notes"));
    }

    // The inline editor edits a card's own line, so what it writes back is
    // what was typed — spacing and all — and the labels and the due date are
    // whatever the next read finds in it. Nothing is re-rendered from the
    // fields, which is the difference from setCard() above.
    void inlineLineEditsKeepWhatWasTyped()
    {
        const QString md = "## A\n* [x]  odd   spacing #a\n- [ ] plain";
        // The text goes on the line character for character; the indent,
        // bullet and checkbox in front of it stay as they were.
        QCOMPARE(setCardLine(md, 0, 0, "still   odd   #a #b"),
                 QString("## A\n* [x] still   odd   #a #b\n- [ ] plain"));
        // And the fields come back out of that line.
        const Card c = parse(setCardLine(md, 0, 1,
                                         "Ship it #release " + cal() + " 2026-08-01"))
                           .columns[0].cards[1];
        QCOMPARE(c.title, QString("Ship it"));
        QCOMPARE(c.labels, QStringList({ "release" }));
        QCOMPARE(c.due, QString("2026-08-01"));
        QCOMPARE(c.done, false);
        // A line break would open a second card, so it becomes a space.
        QCOMPARE(setCardLine("## A\n- [ ] x", 0, 0, "one\ntwo"),
                 QString("## A\n- [ ] one two"));
        // An out-of-range card leaves the board alone.
        QCOMPARE(setCardLine(md, 0, 9, "nope"), md);
        QCOMPARE(setCardLine(md, 3, 0, "nope"), md);
    }

    // A card's description is the indented run under it: the inline editor
    // writes every line of it back, so a description with several lines —
    // formulas, wiki-links, anything — reads back as it was typed.
    void inlineDescriptionEditsKeepTheirLines()
    {
        const QString md = "## A\n- [ ] one\n  old note\n- [ ] two";
        const QString out = setCardDescription(
            md, 0, 0, "See [[Design notes]]\nand $E = mc^2$");
        QCOMPARE(out, QString("## A\n- [ ] one\n  See [[Design notes]]\n"
                              "  and $E = mc^2$\n- [ ] two"));
        QCOMPARE(parse(out).columns[0].cards[0].description,
                 QString("See [[Design notes]]\nand $E = mc^2$"));
        // The card's own line is untouched by a description edit.
        QCOMPARE(parse(out).columns[0].cards[0].title, QString("one"));
        // Emptying it removes the run rather than leaving a blank line.
        QCOMPARE(setCardDescription(md, 0, 0, ""),
                 QString("## A\n- [ ] one\n- [ ] two"));
        QCOMPARE(setCardDescription(md, 0, 9, "nope"), md);
    }

    // A description is prose, and prose has paragraphs and indented structure.
    // Ending the run at the first blank line cut every such description in
    // half, and trimming each line flattened whatever was nested inside one —
    // both invisibly, until the card was edited and the flattened form went
    // back to the file.
    void descriptionsKeepTheirBlankLinesAndTheirNesting()
    {
        const QString md = "## A\n- [ ] one\n"
                           "  first paragraph\n"
                           "\n"
                           "  second paragraph\n"
                           "  - a nested item\n"
                           "    - deeper still\n"
                           "- [ ] two";
        const KanbanData::Board board = parse(md);
        QCOMPARE(board.columns[0].cards.size(), 2);
        QCOMPARE(board.columns[0].cards[0].description,
                 QString("first paragraph\n\nsecond paragraph\n"
                         "- a nested item\n  - deeper still"));

        // Untouched, the board is byte-identical.
        QCOMPARE(serialize(board), md);

        // And a description written back from the field keeps the shape it was
        // read with, so the next parse reads the same thing again.
        const QString out = setCardDescription(
            md, 0, 0, board.columns[0].cards[0].description);
        QCOMPARE(out, md);
    }

    // The `<!--kvit …-->` comment is this application's own namespace, and an
    // edit rewrites the whole comment from the two fields this version knows.
    // A field it does not know has to survive that, or opening a board in an
    // older build and touching one card deletes what a newer one recorded.
    void unknownStampFieldsSurviveAnEdit()
    {
        const QString md = "## A\n- [ ] Ship it <!--kvit created=2026-07-20 "
                           "sprint=2026-31 modified=2026-07-26-->";
        const KanbanData::Board board = parse(md);
        QCOMPARE(board.columns[0].cards[0].created, QString("2026-07-20"));
        QCOMPARE(board.columns[0].cards[0].modified, QString("2026-07-26"));

        const QString out = toggleCardDone(md, 0, 0, "2026-08-01");
        QVERIFY2(out.contains(QStringLiteral("sprint=2026-31")),
                 qPrintable(QStringLiteral("a field this version does not "
                                           "interpret was deleted by an edit "
                                           "that had nothing to do with it: %1")
                                .arg(out)));
        QCOMPARE(parse(out).columns[0].cards[0].modified, QString("2026-08-01"));
    }

    // The text the inline editor is handed for a card's line: what the file
    // has, for a card that has a line, and what serialize() is about to write
    // for one a mutation just synthesized. Either way, putting it straight
    // back through setCardLine() is a no-op.
    void cardLineTextIsWhatTheFileHas()
    {
        KanbanTools tools;
        const QString md = "## A\n* [x]  odd   spacing #a\n- [ ] plain";
        const auto lineOf = [&tools](const QString &board, int col, int idx) {
            const QVariantList columns =
                tools.parse(board).value(QStringLiteral("columns")).toList();
            const QVariantList cards =
                columns[col].toMap().value(QStringLiteral("cards")).toList();
            return cards[idx].toMap().value(QStringLiteral("line")).toString();
        };
        QCOMPARE(lineOf(md, 0, 0), QString(" odd   spacing #a"));
        QCOMPARE(setCardLine(md, 0, 0, lineOf(md, 0, 0)), md);
        // A card with no source line renders its text from the fields.
        const QString added = addCard(md, 0, "fresh");
        QCOMPARE(lineOf(added, 0, 2), QString("fresh"));
        QCOMPARE(setCardLine(added, 0, 2, lineOf(added, 0, 2)), added);
    }

    // A card's dates ride in an HTML comment at the end of its line, so they
    // stay out of the text the reader types and out of every other markdown
    // tool's way. What the reader edits is the line without them.
    void cardDatesRideInACommentOffTheLine()
    {
        const QString md =
            "## A\n- [ ] Ship it #release <!--kvit created=2026-07-20 "
            "modified=2026-07-26-->\n- [ ] plain";
        const Board b = parse(md);
        const Card &c = b.columns[0].cards[0];
        QCOMPARE(c.created, QString("2026-07-20"));
        QCOMPARE(c.modified, QString("2026-07-26"));
        // Nothing in the comment reaches the title, the labels or the due date.
        QCOMPARE(c.title, QString("Ship it"));
        QCOMPARE(c.labels, QStringList({ "release" }));
        QCOMPARE(c.due, QString());
        QCOMPARE(b.columns[0].cards[1].created, QString());
        // And the board comes back byte for byte.
        QCOMPARE(serialize(b), md);

        // A card written with only the day it was added reads that day as
        // both, because it has not been changed since.
        const Card once =
            parse("## A\n- [ ] new <!--kvit created=2026-07-20-->")
                .columns[0].cards[0];
        QCOMPARE(once.created, QString("2026-07-20"));
        QCOMPARE(once.modified, QString("2026-07-20"));

        // The editor is handed the line without the comment, and putting that
        // text straight back leaves the line as it was.
        KanbanTools tools;
        const QVariantList columns =
            tools.parse(md).value(QStringLiteral("columns")).toList();
        const QVariantMap card =
            columns[0].toMap().value(QStringLiteral("cards")).toList()[0].toMap();
        QCOMPARE(card.value(QStringLiteral("line")).toString(),
                 QString("Ship it #release"));
        QCOMPARE(card.value(QStringLiteral("created")).toString(),
                 QString("2026-07-20"));
        QCOMPARE(setCardLine(md, 0, 0, QStringLiteral("Ship it #release")), md);
    }

    // The day comes from the caller, so these transforms stay pure and their
    // tests do not depend on the day they run on. No day means no dates, which
    // is why every test above still reads the boards it wrote.
    void mutationsStampTheDayTheyHappenOn()
    {
        const QString added = addCard("## A", 0, "one", "2026-07-20");
        QCOMPARE(added, QString("## A\n- [ ] one <!--kvit created=2026-07-20-->"));
        QCOMPARE(addCard("## A", 0, "one"), QString("## A\n- [ ] one"));

        // A later edit keeps the day the card was added and records its own.
        const QString edited = setCardLine(added, 0, 0, "one two", "2026-07-26");
        QCOMPARE(edited, QString("## A\n- [ ] one two "
                                 "<!--kvit created=2026-07-20 modified=2026-07-26-->"));
        // Editing again the same day rewrites nothing but the text.
        QCOMPARE(setCardLine(edited, 0, 0, "one three", "2026-07-26"),
                 QString("## A\n- [ ] one three "
                         "<!--kvit created=2026-07-20 modified=2026-07-26-->"));

        // The description, the fields and the checkbox are changes too.
        QVERIFY(setCardDescription(added, 0, 0, "note", "2026-07-26")
                    .contains(QStringLiteral("modified=2026-07-26")));
        QVERIFY(setCard(added, 0, 0, "t", true, {}, "", "", "2026-07-26")
                    .contains(QStringLiteral("modified=2026-07-26")));
        const QString toggled = toggleCardDone(added, 0, 0, "2026-07-26");
        QVERIFY2(toggled.startsWith(QStringLiteral("## A\n- [x] one ")),
                 qPrintable(toggled));
        QVERIFY(toggled.contains(QStringLiteral("modified=2026-07-26")));

        // Carrying a card to another column is a change; sliding it inside
        // the column it is already in is not.
        const QString two = "## A\n- [ ] one <!--kvit created=2026-07-20-->\n"
                            "- [ ] two\n## B";
        QVERIFY(moveCard(two, 0, 0, 1, 0, "2026-07-26")
                    .contains(QStringLiteral("modified=2026-07-26")));
        QVERIFY(!moveCard(two, 0, 0, 0, 2, "2026-07-26")
                     .contains(QStringLiteral("modified=")));

        // A day the calendar does not have stamps nothing, the same test the
        // due date is held to.
        QCOMPARE(addCard("## A", 0, "one", "2026-02-30"),
                 QString("## A\n- [ ] one"));

        // A card that predates the dates keeps an empty `created`: the board
        // shows what it knows rather than claiming the card was made today.
        const Card old = parse(setCardLine("## A\n- [ ] old", 0, 0, "older",
                                           "2026-07-26")).columns[0].cards[0];
        QCOMPARE(old.created, QString());
        QCOMPARE(old.modified, QString("2026-07-26"));
    }

    // What the card editor accepts has to survive being written to the board
    // and read back. Labels used to go out unescaped: `client work` came back
    // as the label `client` plus the title word `work`, and `a#b` lost its
    // tail. The quoted spelling closes that, and the bare spelling is still
    // used wherever it fits.
    void labelsAcceptedByTheEditorRoundTrip_data()
    {
        QTest::addColumn<QStringList>("labels");
        QTest::newRow("plain") << QStringList{"urgent"};
        QTest::newRow("space") << QStringList{"client work"};
        QTest::newRow("hash inside") << QStringList{"a#b"};
        QTest::newRow("leading hash") << QStringList{"#nested"};
        QTest::newRow("quote") << QStringList{"say \"hi\""};
        QTest::newRow("backslash") << QStringList{"a\\b"};
        QTest::newRow("escaped quote") << QStringList{"a\\\"b"};
        QTest::newRow("tab") << QStringList{"a\tb"};
        QTest::newRow("several")
            << QStringList{"client work", "plain", "a \"b\" c"};
    }

    void labelsAcceptedByTheEditorRoundTrip()
    {
        QFETCH(QStringList, labels);
        const QString md = setCard("## A\n- [ ] x", 0, 0, "the title", false,
                                   labels, "", "");
        const Card c = parse(md).columns[0].cards[0];
        QVERIFY2(c.labels == labels,
                 qPrintable(QStringLiteral("labels came back as [%1] "
                                           "(source %2)")
                                .arg(c.labels.join(QLatin1Char('|')), md)));
        QCOMPARE(c.title, QString("the title"));
        // And the board text itself round-trips unchanged.
        QCOMPARE(serialize(parse(md)), md);
    }

    // The bare spelling stays the default, so boards written by hand or by
    // earlier versions are not rewritten into quoted syntax.
    void ordinaryLabelsKeepTheBareSpelling()
    {
        const QString md = setCard("## A\n- [ ] x", 0, 0, "t", false,
                                   QStringList{"urgent", "home"}, "", "");
        QCOMPARE(md, QString("## A\n- [ ] t #urgent #home"));
    }

    // A board written before quoting existed still reads exactly as it did.
    void quotedLabelSyntaxDoesNotDisturbOlderBoards()
    {
        const Board b = parse("## A\n- [ ] t #urgent #a\\b #c\"d");
        QCOMPARE(b.columns[0].cards[0].labels,
                 QStringList({"urgent", "a\\b", "c\"d"}));
    }

    // A due value the grammar cannot hold used to be written after the
    // calendar marker anyway, where the parser did not recognize it: the text
    // reappeared in the title and the due field came back empty. It is now
    // refused at the boundary, and isValidDue() lets the editor say so before
    // the user loses it. Reader and writer apply the same test, so a value one
    // accepts is a value the other accepts.
    void invalidDueValuesAreRefusedRatherThanCorrupted_data()
    {
        QTest::addColumn<QString>("due");
        QTest::addColumn<bool>("valid");
        QTest::newRow("iso") << QStringLiteral("2026-07-15") << true;
        QTest::newRow("word") << QStringLiteral("tomorrow") << false;
        QTest::newRow("us order") << QStringLiteral("07/15/2026") << false;
        QTest::newRow("no such day") << QStringLiteral("2026-02-30") << false;
        QTest::newRow("month 13") << QStringLiteral("2026-13-01") << false;
        QTest::newRow("empty") << QString() << false;
    }

    void invalidDueValuesAreRefusedRatherThanCorrupted()
    {
        QFETCH(QString, due);
        QFETCH(bool, valid);
        QCOMPARE(KanbanData::isValidDue(due), valid);

        const QString md = setCard("## A\n- [ ] x", 0, 0, "the title", false,
                                   {}, due, "");
        const Card c = parse(md).columns[0].cards[0];
        QCOMPARE(c.due, valid ? due : QString());
        // Either way the title is the title: a rejected value never leaks
        // into it.
        QCOMPARE(c.title, QString("the title"));

        // And a card line carrying that value directly reads back the same
        // way, so what the writer refuses is exactly what the reader refuses.
        const Card direct = parse("## A\n- [ ] the title " + cal() + " " + due)
                                .columns[0].cards[0];
        QCOMPARE(direct.due, valid ? due : QString());
    }

    // The mirror of the hash rule: a title that genuinely reads like a due
    // date keeps its text, because the marker is escaped on the way out.
    void dueMarkersInTitlesRoundTripThroughTheEscape()
    {
        const QStringList titles{
            cal() + " 2026-07-15",
            "due " + cal() + " 2026-07-15 for real",
            "\\" + cal() + " 2026-07-15",
            cal() + " not a date",
            // Shape of a date, but not a day the calendar has. The reader
            // leaves it as text, so the writer must not escape it either.
            cal() + " 2026-02-30",
        };
        for (const QString &title : titles) {
            const QString md = setCard("## A\n- [ ] x", 0, 0, title, false, {},
                                       "2026-01-01", "");
            const Card c = parse(md).columns[0].cards[0];
            QVERIFY2(c.title == title,
                     qPrintable(QStringLiteral("title %1 came back as %2 "
                                               "(source %3)")
                                    .arg(title, c.title, md)));
            QCOMPARE(c.due, QString("2026-01-01"));
        }
    }
};

QTEST_MAIN(TestKanbanData)
#include "test_kanbandata.moc"
