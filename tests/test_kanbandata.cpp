// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "kanbandata.h"

using namespace KanbanData;

// Corpus for the kanban board parse/serialize/mutate core (phase10-plan.md
// decision 9): columns, cards, done state, labels, due dates, descriptions,
// and the mutations — pinned before the board delegate paints any of it.
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
