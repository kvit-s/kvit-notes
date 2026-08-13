// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentheights.h"
#include "undostack.h"

namespace {

// Warnings the shell emits while this suite runs, captured the way
// tests/test_shell.cpp captures them and for the same reason: QML reports a
// binding it could not resolve as a warning and then carries on with an
// undefined value, so a scrollbar wired to nothing would otherwise pass every
// assertion below about numbers that came out as zero.
QStringList g_warnings;
QtMessageHandler g_previousHandler = nullptr;

void capturingHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // The runner's problems rather than the shell's: see test_shell.cpp.
        if (!message.contains(QLatin1String("pipewire"))
            && !message.contains(QLatin1String("font family aliases")))
            g_warnings << message;
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

QString wrappingParagraph()
{
    QString text;
    for (int i = 0; i < 40; ++i)
        text += QStringLiteral("a wrapped paragraph of running prose ");
    return text.trimmed();
}

QString fence()
{
    return QStringLiteral("for (int i = 0; i < 10; ++i) {\n"
                          "    total += weigh(i);\n"
                          "    report(total);\n"
                          "}");
}

// What a swing is here, which is the measure the wandering handle was
// reported in: the difference between the largest and smallest value a series
// took, as a fraction of its mean.
qreal swing(const QList<qreal> &series)
{
    if (series.isEmpty())
        return 0;
    qreal low = series.first();
    qreal high = series.first();
    qreal sum = 0;
    for (qreal value : series) {
        low = qMin(low, value);
        high = qMax(high, value);
        sum += value;
    }
    const qreal mean = sum / series.size();
    return mean > 0 ? (high - low) / mean : 0;
}

} // namespace

// The editor's scrollbar, drawn from the document-height table.
//
// The defect this suite is the evidence for: a QQuickListView keeps no
// per-row height memory, so it estimates the document from the rows it has
// built at that moment, and in a note of very unequal rows that estimate —
// and with it the length of the scrollbar handle — moves on every notch of
// the wheel. Reading the note through teaches the view nothing it keeps, so
// the walk back up is no better than the walk down.
//
// The cases here read a note through and back and record what the handle did
// at each stop, rather than asserting anything about how it was computed. One
// of them runs the same walk past the list's own estimate, so that a note
// whose rows turned out to be equal after all could not pass the others.
class TestScrollMetrics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));
        m_context->installContextProperties(&m_engine);

        g_warnings.clear();
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!m_engine.rootObjects().isEmpty());
        m_warningsAfterLoad = g_warnings.size();

        QVERIFY(listView());
        QVERIFY(scrollBar());
    }

    void cleanupTestCase()
    {
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    // A note of thirty repetitions of heading, one-line paragraph, wrapped
    // paragraph, fenced block and list: the shape the wandering handle was
    // measured on, and the shape any note of prose with code in it has.
    void aNoteOfVeryUnequalRowsReadDownAndBackKeepsTheHandlesSize()
    {
        buildUnequalNote(30);

        const QList<qreal> down = readThrough(true);
        const QList<qreal> up = readThrough(false);
        QVERIFY2(down.size() > 8, "the note did not scroll far enough to say");
        QVERIFY2(up.size() > 8, "the note did not scroll far enough to say");

        // The same walk gave 25.7% down and 25.3% back up before the table
        // existed. What is left is what the rows nobody has built yet are
        // worth being wrong about.
        QVERIFY2(swing(down) < 0.05,
                 qPrintable(QStringLiteral("the handle swung %1% reading down")
                                .arg(swing(down) * 100, 0, 'f', 1)));
        QVERIFY2(swing(up) < 0.05,
                 qPrintable(QStringLiteral("the handle swung %1% reading back up")
                                .arg(swing(up) * 100, 0, 'f', 1)));
    }

    // And the same walk through the list's own estimate, which is what the
    // handle was drawn from before. Without this the case above would pass on
    // a note whose rows turned out to be equal after all, and prove nothing.
    void theListsOwnEstimateIsWhatMovesInTheSameNote()
    {
        buildUnequalNote(30);
        QList<qreal> ratios;
        driveThroughTheNote(true, &ratios, nullptr);
        QVERIFY2(swing(ratios) > 0.10,
                 qPrintable(QStringLiteral("the list's own estimate moved only "
                                           "%1%, so this note is not the note "
                                           "the case above is about")
                                .arg(swing(ratios) * 100, 0, 'f', 1)));
    }

    // What the table reports after a full read is what the list reports when
    // it has been made to build every row and estimate none.
    void theTotalAfterAFullReadIsTheDocumentsHeight()
    {
        buildUnequalNote(10);
        readThrough(true);
        QTRY_COMPARE(heights()->measuredCount(), blocks()->count());

        QQuickItem *view = listView();
        const qreal fromTable = heights()->totalHeight();

        // A cache buffer past the whole document leaves nothing to estimate.
        // The list fills a buffer that size a few rows at a time, so this
        // waits for the rows rather than for an interval.
        QMetaObject::invokeMethod(view, "positionViewAtBeginning");
        settle();
        const QVariant buffer = view->property("cacheBuffer");
        view->setProperty("cacheBuffer", 200000);
        for (int attempt = 0;
             attempt < 300 && builtRows() < blocks()->count(); ++attempt)
            settle();
        QCOMPARE(builtRows(), blocks()->count());
        const qreal fromList = view->property("contentHeight").toReal();
        view->setProperty("cacheBuffer", buffer);
        settle();

        // Within one block's height, which in this note runs from a 50 px
        // list item to a 425 px wrapped paragraph.
        QVERIFY2(qAbs(fromTable - fromList) < 260,
                 qPrintable(QStringLiteral("the table says %1 and the list "
                                           "with every row built says %2")
                                .arg(fromTable).arg(fromList)));
    }

    // Dragging the handle to a fraction of the bar puts that fraction of the
    // document above the viewport. The fraction resolves to a block rather
    // than to a content offset, because contentY has its zero at originY and
    // originY moves as rows are built and discarded.
    void draggingTheHandlePutsThatFractionOfTheDocumentAbove()
    {
        buildUnequalNote(30);
        readThrough(true);
        QQuickItem *view = listView();

        for (qreal fraction : {0.25, 0.5, 0.75}) {
            QMetaObject::invokeMethod(scrollBar(), "scrollTo",
                                      Q_ARG(QVariant, QVariant(fraction)));
            settle();

            const qreal range = scrollBar()->property("scrollRange").toReal();
            QVERIFY(range > 0);
            const qreal above = documentAboveTheViewport();
            // Within a viewport, because a drag lands on a block and then
            // goes as far into it as the remainder asks for, and the block it
            // lands on is a whole row wherever inside it the fraction fell.
            QVERIFY2(qAbs(above - fraction * range) < view->height(),
                     qPrintable(QStringLiteral("a drag to %1 left %2 of %3 "
                                               "above the viewport")
                                    .arg(fraction).arg(above).arg(range)));
        }
    }

    // The reader is not stopped short of the end of a document that has
    // nothing left to scroll, whatever the table still estimates.
    void theHandleReachesTheEndOfTheBarAtTheEndOfTheDocument()
    {
        buildUnequalNote(30);
        scrollToTheEnd();

        const qreal position = scrollBar()->property("position").toReal();
        const qreal size = scrollBar()->property("size").toReal();
        QVERIFY2(qAbs(position + size - 1.0) < 0.01,
                 qPrintable(QStringLiteral("the handle sat at %1 + %2")
                                .arg(position).arg(size)));
    }

    // A row's height is only meaningful at the width it was measured at.
    void aWidthChangeThrowsTheMeasurementsAway()
    {
        buildUnequalNote(20);
        readThrough(true);
        QVERIFY(heights()->measuredCount() > 40);
        const qreal beforeWidth = heights()->measuredWidth();

        QQuickWindow *window = shellWindow();
        const int width = window->width();
        window->setWidth(width - 240);
        settle();

        QVERIFY(heights()->measuredWidth() < beforeWidth);
        // Only the rows laid out again at the new width are in the table; the
        // hundred measured at the old one are gone.
        QVERIFY2(heights()->measuredCount() < 40,
                 qPrintable(QStringLiteral("%1 measurements survived a resize")
                                .arg(heights()->measuredCount())));
        window->setWidth(width);
        settle();
    }

    // A note whose rows are all alike scrolls exactly as it did: the handle
    // never changed size there, and it still does not.
    void aNoteOfEqualRowsScrollsAsItAlwaysDid()
    {
        BlockModel *model = blocks();
        clearNote();
        for (int i = 0; i < 120; ++i) {
            model->insertBlock(model->count(), Block::Paragraph,
                               QStringLiteral("an ordinary one-line paragraph"));
        }
        settle();

        const QList<qreal> sizes = readThrough(true);
        QVERIFY(sizes.size() > 8);
        QVERIFY2(swing(sizes) < 0.02,
                 qPrintable(QStringLiteral("the handle swung %1% over a note "
                                           "of equal rows")
                                .arg(swing(sizes) * 100, 0, 'f', 1)));
    }

    // One bar over the document, at the edge of the scrolling area, where the
    // one a ScrollView draws for itself would have been.
    //
    // There are three in the tree: the two a ScrollView attaches to itself and
    // this one. The attached pair are driven from C++ against the flickable's
    // own estimate, which is the behaviour being replaced, so the shell
    // switches them off — and a bar left on would draw over this one at a
    // different length, which is the sort of thing a screenshot shows and no
    // assertion about numbers would.
    void oneBarIsDrawnAndItIsThisOne()
    {
        buildUnequalNote(20);
        QQuickItem *bar = scrollBar();
        QQuickItem *view = item(QStringLiteral("editorScrollView"));
        QVERIFY(bar);
        QVERIFY(view);
        QVERIFY(bar->width() > 0);
        QCOMPARE(bar->height(), view->height());
        QCOMPARE(bar->mapToItem(view, QPointF(bar->width(), 0)).x(), view->width());

        QList<QQuickItem *> shown;
        for (QQuickItem *candidate : scrollBarsUnder(view->parentItem())) {
            if (candidate->isVisible())
                shown.append(candidate);
        }
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.first(), bar);
    }

    // A standalone bar is not told when the view it scrolls is moving, and the
    // style draws it at zero opacity until it is: this is the property that
    // decides whether the reader can see the handle at all.
    void theBarShowsItselfWhileTheViewMoves()
    {
        buildUnequalNote(20);
        QQuickItem *bar = scrollBar();
        QVERIFY(!bar->property("active").toBool());

        QMetaObject::invokeMethod(listView(), "flick",
                                  Q_ARG(double, 0), Q_ARG(double, -1200));
        QTRY_VERIFY(bar->property("active").toBool());
        QTRY_VERIFY(!listView()->property("moving").toBool());
        QTRY_VERIFY(!bar->property("active").toBool());
    }

    // A note that does not scroll has no handle to draw, which is the state
    // the bar reports as a size of one.
    void aNoteThatDoesNotScrollFillsTheBar()
    {
        clearNote();
        blocks()->insertBlock(0, Block::Paragraph, QStringLiteral("short"));
        settle();
        QTRY_COMPARE(scrollBar()->property("size").toReal(), 1.0);
    }

    void noWarningsAppearedWhileScrolling()
    {
        if (g_warnings.size() > m_warningsAfterLoad) {
            QFAIL(qPrintable(QStringLiteral("Scrolling the document produced "
                                            "warnings:\n  ")
                             + g_warnings.mid(m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

private:
    BlockModel *blocks() { return m_context->blockModel(); }
    DocumentHeights *heights() { return m_context->documentHeights(); }

    QQuickWindow *shellWindow()
    {
        return qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0));
    }
    QQuickItem *item(const QString &objectName)
    {
        QObject *window = m_engine.rootObjects().value(0);
        return window ? window->findChild<QQuickItem *>(objectName) : nullptr;
    }
    QQuickItem *listView() { return item(QStringLiteral("blockListView")); }
    QQuickItem *scrollBar() { return item(QStringLiteral("editorScrollBar")); }

    // How many rows the list currently has, the cached ones included. A block
    // row is one that answers to the interface every block delegate
    // implements; the list keeps items in its content item that are not rows.
    int builtRows()
    {
        QQuickItem *content = listView()->childItems().value(0);
        if (!content)
            return 0;
        int rows = 0;
        const QList<QQuickItem *> children = content->childItems();
        for (QQuickItem *child : children) {
            if (child->property("blockContentHeight").isValid())
                ++rows;
        }
        return rows;
    }

    // Every scrollbar in a subtree, the ones a control attached to itself
    // included. They are QQuickScrollBar subclasses whatever QML file the
    // style built them from, which is what this asks.
    static QList<QQuickItem *> scrollBarsUnder(QQuickItem *parent)
    {
        QList<QQuickItem *> found;
        if (!parent)
            return found;
        const QList<QQuickItem *> children = parent->childItems();
        for (QQuickItem *child : children) {
            if (child->inherits("QQuickScrollBar"))
                found.append(child);
            found.append(scrollBarsUnder(child));
        }
        return found;
    }

    void clearNote()
    {
        BlockModel *model = blocks();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        m_context->undoStack()->clear();
        settle();
    }

    void buildUnequalNote(int repetitions)
    {
        clearNote();
        BlockModel *model = blocks();
        for (int i = 0; i < repetitions; ++i) {
            model->insertBlock(model->count(), Block::Heading2,
                               QStringLiteral("Section %1").arg(i + 1));
            model->insertBlock(model->count(), Block::Paragraph,
                               QStringLiteral("One line about section %1.")
                                   .arg(i + 1));
            model->insertBlock(model->count(), Block::Paragraph,
                               wrappingParagraph());
            model->insertBlock(model->count(), Block::CodeBlock, fence());
            model->insertBlock(model->count(), Block::BulletList,
                               QStringLiteral("a list item"));
        }
        m_context->undoStack()->clear();
        settle();
        QMetaObject::invokeMethod(listView(), "positionViewAtBeginning");
        settle();
    }

    // One turn of the event loop plus the frame the list lays out in. The
    // rows report their heights as they are built, so everything this suite
    // measures is behind this.
    void settle()
    {
        QCoreApplication::processEvents();
        QTest::qWait(40);
        QCoreApplication::processEvents();
    }

    // The far end of the view's own scroll range. originY is in it because
    // contentY's zero is not the top of the document: a ListView anchors its
    // content coordinates on the rows it has and estimates the space above
    // the first of them, so the origin drifts as the reader goes down.
    qreal endContentY()
    {
        QQuickItem *view = listView();
        return view->property("originY").toReal()
               + qMax(0.0, view->property("contentHeight").toReal()
                           + view->property("bottomMargin").toReal()
                           - view->height());
    }

    // Take the view to the end and stay there. One assignment is not enough:
    // the list revises how tall it thinks the document is as it builds the
    // rows the jump brought into view, and the end moves out from under a
    // contentY that had reached the old one.
    void scrollToTheEnd()
    {
        for (int attempt = 0; attempt < 50; ++attempt) {
            listView()->setProperty("contentY", endContentY());
            settle();
            if (atTheEnd(listView()->property("contentY").toReal(), true))
                return;
        }
    }

    bool atTheEnd(qreal contentY, bool downward)
    {
        return downward
            ? contentY >= endContentY() - 1
            : contentY <= listView()->property("originY").toReal() + 1;
    }

    // Drive the view through the note in steps of three quarters of a
    // viewport, recording the scrollbar's own size at each stop — and, for
    // the control case, the ratio the list would have drawn it from.
    void driveThroughTheNote(bool downward, QList<qreal> *listRatios,
                             QList<qreal> *handleSizes)
    {
        QQuickItem *view = listView();
        const qreal step = view->height() * 0.75;
        view->setProperty("contentY", downward
                                          ? view->property("originY").toReal()
                                          : endContentY());
        settle();

        for (int stop = 0; stop < 400; ++stop) {
            if (handleSizes)
                handleSizes->append(scrollBar()->property("size").toReal());
            if (listRatios) {
                QObject *area = view->property("visibleArea").value<QObject *>();
                listRatios->append(area->property("heightRatio").toReal());
            }
            const qreal here = view->property("contentY").toReal();
            // Both ends are asked for again at every stop, and once more
            // before giving up on one: the list revises where it thinks the
            // document begins and ends as it builds rows — the whole of what
            // this suite is about — and it can revise after the step that
            // looked like the last one.
            if (atTheEnd(here, downward)) {
                settle();
                if (atTheEnd(here, downward))
                    break;
            }
            const qreal top = view->property("originY").toReal();
            view->setProperty("contentY",
                              qBound(top, downward ? here + step : here - step,
                                     endContentY()));
            settle();
        }
    }

    QList<qreal> readThrough(bool downward)
    {
        QList<qreal> sizes;
        driveThroughTheNote(downward, nullptr, &sizes);
        return sizes;
    }

    // How much of the document is above the top of the viewport, in the
    // table's coordinates: the blocks above the top row plus how far the view
    // is into that row. The same question the bar answers to place its
    // handle, asked here through the rows rather than through the bar.
    qreal documentAboveTheViewport()
    {
        QVariant answer;
        QMetaObject::invokeMethod(scrollBar(), "topRow",
                                  Q_RETURN_ARG(QVariant, answer));
        const QVariantMap top = answer.toMap();
        const int index = top.value(QStringLiteral("index")).toInt();
        if (index < 0)
            return 0;
        return heights()->offsetOf(index)
               + top.value(QStringLiteral("into")).toReal();
    }

    QTemporaryDir m_dir;
    QQmlApplicationEngine m_engine;
    std::unique_ptr<AppContext> m_context;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestScrollMetrics)
#include "test_scrollmetrics.moc"
