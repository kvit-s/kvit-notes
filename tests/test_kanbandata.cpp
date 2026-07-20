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
};

QTEST_MAIN(TestKanbanData)
#include "test_kanbandata.moc"
