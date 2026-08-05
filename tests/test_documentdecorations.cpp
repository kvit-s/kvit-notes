// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QRectF>
#include <QSignalSpy>
#include <QUrl>

#include <memory>

#include "documentdecorations.h"

namespace {

// A stand-in for the document view. The real one is the block list in
// qml/main.qml, which answers these three questions out of laid-out Qt Quick
// items; here they answer out of arithmetic, so the forwarding itself can be
// checked without a window.
class FakeDocumentView : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QVariant decorationBlockGeometry(QVariant blockIndex)
    {
        askedBlock = blockIndex.toInt();
        return QVariant::fromValue(QRectF(0, 100.0 * askedBlock, 300, 40));
    }

    Q_INVOKABLE QVariant decorationLineGeometry(QVariant blockIndex, QVariant line)
    {
        askedBlock = blockIndex.toInt();
        askedLine = line.toInt();
        return QVariant::fromValue(
            QRectF(0, 100.0 * askedBlock + 20.0 * askedLine, 300, 20));
    }

    Q_INVOKABLE QVariant decorationContainerGeometry(QVariant id)
    {
        askedId = id.toString();
        return QVariant::fromValue(QRectF(0, 500, 300, 60));
    }

    // A marked phrase that wraps occupies one rectangle per visual line, so
    // this answer is a list where the three above are single rectangles.
    Q_INVOKABLE QVariant decorationSpanRects(QVariant id)
    {
        askedId = id.toString();
        return QVariant::fromValue(QVariantList{
            QVariant::fromValue(QRectF(10, 200, 80, 20)),
            QVariant::fromValue(QRectF(0, 220, 40, 20)),
        });
    }

    int askedBlock = -1;
    int askedLine = -1;
    QString askedId;
};

QUrl demoSource()
{
    return QUrl(QStringLiteral("qrc:/decorations/Demo.qml"));
}

} // namespace

// Where a linked module may draw inside the document view.
//
// The seam's whole premise is that decorations are DRAWN rather than
// INSERTED: nothing here touches the document, the model or the undo stack,
// and the suite that proves that end to end is DecorationShellTests, which
// loads the real shell with a demonstration module installed. What is checked
// here is the registry itself — placement, movement, ownership between two
// modules at once, and the geometry questions being forwarded to whatever the
// view is.
class TestDocumentDecorations : public QObject
{
    Q_OBJECT

private slots:
    // The open editor, which registers nothing. Every answer is empty and
    // nothing is reserved, so the document view does no per-row work at all.
    void anEmptyRegistryDecoratesNothing()
    {
        DocumentDecorations decorations;

        QVERIFY(!decorations.isActive());
        QVERIFY(!decorations.marginColumnReserved());
        QCOMPARE(decorations.containerCount(), 0);
        QCOMPARE(decorations.marginItemCount(), 0);
        QVERIFY(decorations.containersAfter(0).isEmpty());
        QVERIFY(decorations.marginItemsForBlock(0).isEmpty());
        QVERIFY(!decorations.hasSpans());
        QCOMPARE(decorations.spanCount(), 0);
        QVERIFY(decorations.spansForBlock(0).isEmpty());
    }

    void aContainerIsRegisteredAfterItsBlock()
    {
        DocumentDecorations decorations;
        QSignalSpy changed(&decorations, &DocumentDecorations::changed);

        QObject context;
        context.setObjectName(QStringLiteral("module-state"));
        const QString id = decorations.addContainer(QStringLiteral("demo"), 3,
                                                    demoSource(), &context);

        QVERIFY(!id.isEmpty());
        QCOMPARE(changed.count(), 1);
        QVERIFY(decorations.isActive());
        QVERIFY(decorations.containersAfter(2).isEmpty());

        const QVariantList after = decorations.containersAfter(3);
        QCOMPARE(after.size(), 1);
        const QVariantMap entry = after.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(entry.value(QStringLiteral("owner")).toString(),
                 QStringLiteral("demo"));
        QCOMPARE(entry.value(QStringLiteral("source")).toUrl(), demoSource());
        QCOMPARE(entry.value(QStringLiteral("block")).toInt(), 3);
        QCOMPARE(entry.value(QStringLiteral("context")).value<QObject *>(),
                 &context);
    }

    // Placement is dynamic: a module re-places its anchors as the user types,
    // and moving one must not tear the drawn item down and build it again.
    // Keeping the id across the move is what makes that possible.
    void aContainerMovesWithoutBeingReRegistered()
    {
        DocumentDecorations decorations;
        const QString id =
            decorations.addContainer(QStringLiteral("demo"), 1, demoSource());
        const int before = decorations.revision();

        QVERIFY(decorations.setContainerBlock(id, 4));
        QVERIFY(decorations.revision() > before);
        QVERIFY(decorations.containersAfter(1).isEmpty());
        QCOMPARE(decorations.containersAfter(4).size(), 1);
        QCOMPARE(decorations.containersAfter(4).first().toMap()
                     .value(QStringLiteral("id")).toString(), id);

        // Re-placing an entry where it already is changes nothing, so a
        // module that recomputes every anchor on every keystroke does not
        // make the view re-render the ones that did not move.
        const int settled = decorations.revision();
        QVERIFY(decorations.setContainerBlock(id, 4));
        QCOMPARE(decorations.revision(), settled);

        QVERIFY(!decorations.setContainerBlock(QStringLiteral("no-such-id"), 2));
    }

    void anAnchorNamingNoBlockDrawsNothing()
    {
        DocumentDecorations decorations;
        decorations.addContainer(QStringLiteral("demo"), 900, demoSource());

        // The registration stands — the module owns it and will move it — but
        // no row asks for block 900, so nothing is drawn and nothing costs.
        QCOMPARE(decorations.containerCount(), 1);
        QVERIFY(decorations.containersAfter(0).isEmpty());
    }

    void aMarginItemIsAddressedByBlockAndLine()
    {
        DocumentDecorations decorations;
        const QString id = decorations.addMarginItem(QStringLiteral("demo"), 2, 5,
                                                     demoSource());

        const QVariantList beside = decorations.marginItemsForBlock(2);
        QCOMPARE(beside.size(), 1);
        QCOMPARE(beside.first().toMap().value(QStringLiteral("line")).toInt(), 5);

        QVERIFY(decorations.setMarginItemPosition(id, 7, 0));
        QVERIFY(decorations.marginItemsForBlock(2).isEmpty());
        QCOMPARE(decorations.marginItemsForBlock(7).size(), 1);

        // A negative line is a caller's arithmetic having gone below zero;
        // the first line is the nearest true answer.
        decorations.addMarginItem(QStringLiteral("demo"), 7, -3, demoSource());
        QCOMPARE(decorations.marginItemsForBlock(7).last().toMap()
                     .value(QStringLiteral("line")).toInt(), 0);
    }

    // ---- marked spans ----

    // A span names a run of display characters in one block and the colors
    // to paint it in. The two channels compose, so an entry may carry both.
    void aSpanMarksARunOfCharactersInOneBlock()
    {
        DocumentDecorations decorations;
        QSignalSpy changed(&decorations, &DocumentDecorations::changed);

        const QString id = decorations.addSpan(
            QStringLiteral("demo"), 2, 6, 5,
            DocumentDecorations::Wash | DocumentDecorations::Outline,
            QColor(QStringLiteral("#5533aa")));

        QVERIFY(!id.isEmpty());
        QCOMPARE(changed.count(), 1);
        QVERIFY(decorations.hasSpans());
        QVERIFY(decorations.isActive());
        QVERIFY(decorations.spansForBlock(1).isEmpty());

        const QVariantList marked = decorations.spansForBlock(2);
        QCOMPARE(marked.size(), 1);
        const QVariantMap entry = marked.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(entry.value(QStringLiteral("owner")).toString(),
                 QStringLiteral("demo"));
        QCOMPARE(entry.value(QStringLiteral("start")).toInt(), 6);
        QCOMPARE(entry.value(QStringLiteral("length")).toInt(), 5);
        // One color per channel the entry uses, as a string the drawing end
        // reads directly.
        QCOMPARE(QColor(entry.value(QStringLiteral("wash")).toString()),
                 QColor(QStringLiteral("#5533aa")));
        QCOMPARE(QColor(entry.value(QStringLiteral("outline")).toString()),
                 QColor(QStringLiteral("#5533aa")));
    }

    // One channel at a time is the ordinary case: a wash marks the phrase, an
    // outline says which one is current, and the two are separate entries
    // precisely so they can be different colors on the same characters.
    void aSpanUsesOnlyTheChannelsItNames()
    {
        DocumentDecorations decorations;
        decorations.addSpan(QStringLiteral("demo"), 0, 0, 4,
                            DocumentDecorations::Wash,
                            QColor(QStringLiteral("#5533aa")));
        decorations.addSpan(QStringLiteral("demo"), 0, 2, 4,
                            DocumentDecorations::Outline,
                            QColor(QStringLiteral("#cc4400")));

        const QVariantList marked = decorations.spansForBlock(0);
        QCOMPARE(marked.size(), 2);
        QVERIFY(marked.at(0).toMap().value(QStringLiteral("outline"))
                    .toString().isEmpty());
        QVERIFY(!marked.at(0).toMap().value(QStringLiteral("wash"))
                     .toString().isEmpty());
        QVERIFY(marked.at(1).toMap().value(QStringLiteral("wash"))
                    .toString().isEmpty());
        QVERIFY(!marked.at(1).toMap().value(QStringLiteral("outline"))
                     .toString().isEmpty());
        // Overlapping and nesting are ordinary: both entries stand, in
        // registration order, which is the order they paint in.
        QCOMPARE(marked.at(0).toMap().value(QStringLiteral("start")).toInt(), 0);
        QCOMPARE(marked.at(1).toMap().value(QStringLiteral("start")).toInt(), 2);
    }

    // Placement is dynamic here too: as the user types, a module recomputes
    // where its anchors landed and moves each span by its id.
    void aSpanMovesByItsIdRatherThanBeingRebuilt()
    {
        DocumentDecorations decorations;
        const QString id = decorations.addSpan(
            QStringLiteral("demo"), 0, 4, 3, DocumentDecorations::Wash,
            QColor(QStringLiteral("#5533aa")));
        const int before = decorations.revision();

        QVERIFY(decorations.setSpanRange(id, 1, 10, 6));
        QVERIFY(decorations.revision() > before);
        QVERIFY(decorations.spansForBlock(0).isEmpty());
        const QVariantMap moved = decorations.spansForBlock(1).first().toMap();
        QCOMPARE(moved.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(moved.value(QStringLiteral("start")).toInt(), 10);
        QCOMPARE(moved.value(QStringLiteral("length")).toInt(), 6);

        // Re-placing a span where it already is costs no re-render, which is
        // what lets a module recompute every anchor on every keystroke.
        const int settled = decorations.revision();
        QVERIFY(decorations.setSpanRange(id, 1, 10, 6));
        QCOMPARE(decorations.revision(), settled);

        // Recoloring is the same story: the id stands, the appearance moves.
        QVERIFY(decorations.setSpanStyle(id, DocumentDecorations::Outline,
                                         QColor(QStringLiteral("#cc4400"))));
        const QVariantMap restyled = decorations.spansForBlock(1).first().toMap();
        QVERIFY(restyled.value(QStringLiteral("wash")).toString().isEmpty());
        QCOMPARE(QColor(restyled.value(QStringLiteral("outline")).toString()),
                 QColor(QStringLiteral("#cc4400")));

        QVERIFY(!decorations.setSpanRange(QStringLiteral("no-such-id"), 0, 0, 1));
        QVERIFY(!decorations.setSpanStyle(QStringLiteral("no-such-id"),
                                          DocumentDecorations::Wash, QColor()));
    }

    // An entry with no channel to draw through, or no color to draw in, is
    // refused rather than carried and never painted.
    void aSpanWithNothingToDrawIsRefused()
    {
        DocumentDecorations decorations;
        QVERIFY(decorations.addSpan(QStringLiteral("demo"), 0, 0, 4,
                                    DocumentDecorations::NoStyle,
                                    QColor(QStringLiteral("#5533aa")))
                    .isEmpty());
        QVERIFY(decorations.addSpan(QStringLiteral("demo"), 0, 0, 4,
                                    DocumentDecorations::Wash, QColor())
                    .isEmpty());
        QVERIFY(decorations.addSpan(QStringLiteral("demo"), 0, 0, 0,
                                    DocumentDecorations::Wash,
                                    QColor(QStringLiteral("#5533aa")))
                    .isEmpty());
        QVERIFY(!decorations.hasSpans());
        QVERIFY(!decorations.isActive());
    }

    void removingASpanTakesItOutOfTheView()
    {
        DocumentDecorations decorations;
        const QString id = decorations.addSpan(
            QStringLiteral("demo"), 0, 0, 4, DocumentDecorations::Wash,
            QColor(QStringLiteral("#5533aa")));

        QVERIFY(decorations.removeSpan(id));
        QVERIFY(!decorations.removeSpan(id));
        QVERIFY(!decorations.hasSpans());
        QVERIFY(!decorations.isActive());
    }

    // Two modules at once. Neither can see the other's entries removed, and
    // the drawing order is registration order, so a document with two modules
    // decorating one block looks the same on every run.
    void twoModulesContributeWithoutColliding()
    {
        DocumentDecorations decorations;
        const QString first =
            decorations.addContainer(QStringLiteral("first"), 0, demoSource());
        const QString second =
            decorations.addContainer(QStringLiteral("second"), 0, demoSource());
        decorations.addMarginItem(QStringLiteral("second"), 0, 0, demoSource());
        decorations.addSpan(QStringLiteral("second"), 0, 0, 3,
                            DocumentDecorations::Wash,
                            QColor(QStringLiteral("#5533aa")));

        const QVariantList after = decorations.containersAfter(0);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after.at(0).toMap().value(QStringLiteral("id")).toString(), first);
        QCOMPARE(after.at(1).toMap().value(QStringLiteral("id")).toString(), second);

        decorations.removeAll(QStringLiteral("second"));
        QCOMPARE(decorations.containersAfter(0).size(), 1);
        QCOMPARE(decorations.containersAfter(0).first().toMap()
                     .value(QStringLiteral("id")).toString(), first);
        QVERIFY(decorations.marginItemsForBlock(0).isEmpty());
        QVERIFY(decorations.spansForBlock(0).isEmpty());
    }

    void removingAnEntryTakesItOutOfTheView()
    {
        DocumentDecorations decorations;
        const QString container =
            decorations.addContainer(QStringLiteral("demo"), 0, demoSource());
        const QString margin =
            decorations.addMarginItem(QStringLiteral("demo"), 0, 1, demoSource());

        QVERIFY(decorations.removeContainer(container));
        QVERIFY(!decorations.removeContainer(container));
        QVERIFY(decorations.removeMarginItem(margin));
        QVERIFY(!decorations.isActive());
    }

    // Ownership of the context objects stays with the module. This holds a
    // guarded pointer, so a module that destroys its own state before taking
    // its registration back hands QML a null rather than a dangling pointer.
    void aContextObjectDestroyedByItsModuleReadsAsNull()
    {
        DocumentDecorations decorations;
        auto context = std::make_unique<QObject>();
        decorations.addContainer(QStringLiteral("demo"), 0, demoSource(),
                                 context.get());
        QCOMPARE(decorations.containersAfter(0).first().toMap()
                     .value(QStringLiteral("context")).value<QObject *>(),
                 context.get());

        context.reset();
        QCOMPARE(decorations.containersAfter(0).first().toMap()
                     .value(QStringLiteral("context")).value<QObject *>(),
                 nullptr);
    }

    void aRegistrationWithNoQmlIsRefused()
    {
        DocumentDecorations decorations;
        QVERIFY(decorations.addContainer(QStringLiteral("demo"), 0, QUrl()).isEmpty());
        QVERIFY(decorations.addMarginItem(QStringLiteral("demo"), 0, 0,
                                          QUrl()).isEmpty());
        QVERIFY(!decorations.isActive());
    }

    // Reserving the column is a layout change — every row has less width to
    // set its text in — so it has to reach the view the same way a
    // registration does.
    void reservingTheMarginColumnIsAnnouncedOnce()
    {
        DocumentDecorations decorations;
        QSignalSpy reserved(&decorations,
                            &DocumentDecorations::marginColumnReservedChanged);
        QSignalSpy changed(&decorations, &DocumentDecorations::changed);

        decorations.setMarginColumnReserved(true);
        QCOMPARE(reserved.count(), 1);
        QCOMPARE(changed.count(), 1);

        decorations.setMarginColumnReserved(true);
        QCOMPARE(reserved.count(), 1);
        QCOMPARE(changed.count(), 1);

        QVERIFY(decorations.marginColumnEms() > 0);
    }

    void clearTakesEveryEntryBack()
    {
        DocumentDecorations decorations;
        decorations.addContainer(QStringLiteral("a"), 0, demoSource());
        decorations.addMarginItem(QStringLiteral("b"), 1, 1, demoSource());
        decorations.addSpan(QStringLiteral("c"), 1, 0, 4,
                            DocumentDecorations::Wash,
                            QColor(QStringLiteral("#5533aa")));
        decorations.clear();

        QVERIFY(!decorations.isActive());
        QCOMPARE(decorations.containerCount(), 0);
        QCOMPARE(decorations.marginItemCount(), 0);
        QCOMPARE(decorations.spanCount(), 0);
    }

    // Positions exist only once the view has laid the rows out, so the three
    // geometry questions are forwarded to it. With no view — a composition
    // whose shell has not loaded, or a window that has closed — each answers
    // with a null rectangle rather than with a guess.
    void geometryQuestionsGoToTheDocumentView()
    {
        DocumentDecorations decorations;
        QVERIFY(decorations.blockGeometry(2).isNull());
        QVERIFY(decorations.lineGeometry(2, 1).isNull());
        QVERIFY(decorations.containerGeometry(QStringLiteral("id")).isNull());
        QVERIFY(decorations.spanRects(QStringLiteral("id")).isEmpty());

        FakeDocumentView view;
        decorations.setDocumentView(&view);

        QCOMPARE(decorations.blockGeometry(2), QRectF(0, 200, 300, 40));
        QCOMPARE(view.askedBlock, 2);

        QCOMPARE(decorations.lineGeometry(1, 3), QRectF(0, 160, 300, 20));
        QCOMPARE(view.askedBlock, 1);
        QCOMPARE(view.askedLine, 3);

        QCOMPARE(decorations.containerGeometry(QStringLiteral("container-1")),
                 QRectF(0, 500, 300, 60));
        QCOMPARE(view.askedId, QStringLiteral("container-1"));

        // A span's answer is one rectangle per visual line it crosses, and a
        // consumer reads them as rectangles rather than as a list of numbers.
        const QVariantList rects = decorations.spanRects(QStringLiteral("span-1"));
        QCOMPARE(view.askedId, QStringLiteral("span-1"));
        QCOMPARE(rects.size(), 2);
        QCOMPARE(rects.at(0).toRectF(), QRectF(10, 200, 80, 20));
        QCOMPARE(rects.at(1).toRectF(), QRectF(0, 220, 40, 20));
    }

    // A window closing destroys the view while the module and this object
    // live on. The next question is answered with a null rectangle, which is
    // the whole reason the view is held through a guarded pointer.
    void aClosedViewAnswersNothingRatherThanCrashing()
    {
        DocumentDecorations decorations;
        {
            FakeDocumentView view;
            decorations.setDocumentView(&view);
            QVERIFY(!decorations.blockGeometry(0).isNull());
        }
        QVERIFY(decorations.blockGeometry(0).isNull());
    }
};

QTEST_MAIN(TestDocumentDecorations)
#include "test_documentdecorations.moc"
