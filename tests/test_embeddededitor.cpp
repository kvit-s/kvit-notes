// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentdecorations.h"
#include "documentheights.h"
#include "documentoutline.h"
#include "documentsearch.h"
#include "documentselection.h"
#include "documentserializer.h"
#include "documentstats.h"
#include "qmlservices.h"
#include "undostack.h"

namespace {

// Warnings the shell and the boxes emit while this suite runs, captured the
// way tests/test_shell.cpp captures them and for the same reason: QML reports
// a binding it could not resolve as a warning and then carries on with an
// undefined value, so a box wired to nothing would otherwise pass every
// assertion below about counts that did not move.
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

// What a message typed into the box looks like: a heading, some prose and a
// word the note's find bar is searching for. Each part is there to be looked
// for afterwards in a projection that belongs to the NOTE — the heading in
// the note's outline, the words in the note's statistics, "salmon" in the
// note's match list.
const QString kMessageMarkdown = QStringLiteral(
    "# Message heading\n"
    "\n"
    "A line about salmon in the message.\n");

// The word the note's find bar is left searching for throughout, present once
// in the note and once in anything typed into the box.
const QString kSharedWord = QStringLiteral("salmon");

// QTest offers no whole-string key helper for a QWindow, only for a widget,
// so a run of characters is one click each. The text of each event is the
// character itself, which is what the text area inserts.
void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(window, c.toLatin1(), Qt::NoModifier, 1);
}

} // namespace

// Two editors in one window, each with its own document
// (qml/CompactEditor.qml, planning/embeddable-editor.md).
//
// A block editor takes the eight objects that hold one document's state —
// the model, the selection, the undo stack, and the five projections over
// them — and every one of those defaults to the window's own. An editor
// embedded somewhere else that leaves any of them unset therefore shares it
// with the open note, and the failure is silent: nothing errors, the note's
// undo history simply grows a step that belongs to a message, its outline
// grows a heading nobody wrote in it, and its word count counts a document
// the reader cannot see.
//
// So the property worth defending is separation, object by object, and that
// is what this suite asserts: a compact editor owns all eight, typing into it
// moves none of the note's six, and an undo in it is an undo of it alone.
// Driven through the shipped shell, so the box is created in the same
// composition the application runs in and the note it must not disturb is the
// real one.
class TestEmbeddedEditor : public QObject
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
        m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kvit/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!m_engine.rootObjects().isEmpty());
        m_warningsAfterLoad = g_warnings.size();
    }

    void cleanupTestCase()
    {
        m_boxes.clear();
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        // Every case starts from the same note: a heading, a paragraph
        // holding the word the find bar is looking for, and one more
        // paragraph. Small enough to state the numbers below outright.
        BlockModel *model = m_context->blockModel();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Heading1, QStringLiteral("# Note heading"));
        model->insertBlock(1, Block::Paragraph,
                           QStringLiteral("The note mentions salmon once."));
        model->insertBlock(2, Block::Paragraph,
                           QStringLiteral("And then it says nothing more."));
        noteSelection()->clear();
        m_context->undoStack()->clear();

        // The note's find bar, left open on a word the message will also
        // contain. Without this the match list is empty in both the before
        // and the after, which is a comparison that cannot fail.
        noteSearch()->setQuery(kSharedWord);
        noteSearch()->setActive(true);
        noteSearch()->recomputeNow();

        m_boxes.clear();
        QCoreApplication::processEvents();
        noteOutline()->rebuildNow();
    }

    void theShellLoadsWithNoWarnings()
    {
        if (m_warningsAfterLoad > 0) {
            QFAIL(qPrintable(QStringLiteral("Loading the shell produced "
                                            "warnings:\n  ")
                             + g_warnings.mid(0, m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

    // The registration work, checked as addresses. An instance that merely
    // exists proves nothing: what matters is that it is not the window's, and
    // that the six that take a model have been pointed at the box's own.
    void aBoxOwnsEveryObjectThatHoldsItsDocument()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        QObject *surface = box->property("editor").value<QObject *>();
        QVERIFY(surface);

        auto *blocks = surface->property("blocks").value<BlockModel *>();
        auto *selection =
            surface->property("selection").value<DocumentSelection *>();
        auto *undo = surface->property("undoStack").value<UndoStack *>();
        auto *search = surface->property("search").value<DocumentSearch *>();
        auto *outline = surface->property("outline").value<DocumentOutline *>();
        auto *heights = surface->property("heights").value<DocumentHeights *>();
        auto *stats = surface->property("stats").value<DocumentStats *>();
        auto *decorations =
            surface->property("decorations").value<DocumentDecorations *>();

        QVERIFY(blocks);
        QVERIFY(selection);
        QVERIFY(undo);
        QVERIFY(search);
        QVERIFY(outline);
        QVERIFY(heights);
        QVERIFY(stats);
        QVERIFY(decorations);

        // None of the eight is the window's.
        QVERIFY(blocks != m_context->blockModel());
        QVERIFY(selection != noteSelection());
        QVERIFY(undo != m_context->undoStack());
        QVERIFY(search != noteSearch());
        QVERIFY(outline != noteOutline());
        QVERIFY(heights != m_context->documentHeights());
        QVERIFY(stats != noteStats());
        QVERIFY(decorations != m_context->documentDecorations());

        // And the seven that address a document address the box's own, which
        // is the half of the wiring QML could not do before the model became
        // a settable property on each of them.
        QCOMPARE(selection->model(), blocks);
        QCOMPARE(search->model(), blocks);
        QCOMPARE(outline->model(), blocks);
        QCOMPARE(heights->model(), blocks);
        QCOMPARE(stats->model(), blocks);
        QCOMPARE(blocks->undoStack(), undo);
    }

    // The regression the whole seam is for: a message typed into the box
    // reaches none of the six projections that belong to the note.
    void typingInTheBoxMovesNothingThatBelongsToTheNote()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);

        const int noteBlocks = m_context->blockModel()->count();
        const int noteUndoDepth = m_context->undoStack()->count();
        const int noteOutlineRows = noteOutline()->rowCount();
        const int noteWords =
            noteStats()->documentStats().value(QStringLiteral("words")).toInt();
        const int noteMatches = noteSearch()->matchCount();
        const int noteHeightRows = m_context->documentHeights()->count();
        QObject *noteDocumentView = m_context->documentDecorations()->documentView();
        QVERIFY(noteDocumentView);

        setBoxMarkdown(box, kMessageMarkdown);
        typeIntoBox(box, QStringLiteral(" Typed by hand."));

        // The box has the message.
        QCOMPARE(boxBlocks(box)->count(), 2);
        QVERIFY(boxMarkdown(box).contains(QLatin1String("Typed by hand.")));

        // The note has none of it, projection by projection.
        QCOMPARE(m_context->blockModel()->count(), noteBlocks);
        QCOMPARE(m_context->blockModel()->getContent(0),
                 QStringLiteral("# Note heading"));
        QCOMPARE(m_context->undoStack()->count(), noteUndoDepth);
        QVERIFY(!m_context->undoStack()->canUndo());

        noteOutline()->rebuildNow();
        QCOMPARE(noteOutline()->rowCount(), noteOutlineRows);
        QVERIFY(!noteOutline()->hasSlug(QStringLiteral("message-heading")));

        QCOMPARE(noteStats()->documentStats()
                     .value(QStringLiteral("words")).toInt(), noteWords);

        noteSearch()->recomputeNow();
        QCOMPARE(noteSearch()->matchCount(), noteMatches);

        // The height table still measures the note's rows and no others: its
        // row count is the note's block count, whatever the box measured.
        QCOMPARE(m_context->documentHeights()->count(), noteHeightRows);
        QCOMPARE(m_context->documentHeights()->count(), noteBlocks);

        // And the decoration seam still answers geometry from the note's
        // block list rather than from the box's, which is what a second
        // editor would have taken over by registering itself last.
        QCOMPARE(m_context->documentDecorations()->documentView(),
                 noteDocumentView);

        // The box's own projections did see it, so the assertions above are
        // about separation rather than about a box that does nothing.
        boxOutline(box)->rebuildNow();
        QVERIFY(boxOutline(box)->hasSlug(QStringLiteral("message-heading")));
        QVERIFY(boxStats(box)->documentStats()
                    .value(QStringLiteral("words")).toInt() > 0);
    }

    // The undo stacks are separate in both directions: an undo in the box
    // restores the box, and the note's own step is still waiting on its own
    // stack afterwards.
    void undoInTheBoxUndoesOnlyTheBox()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        setBoxMarkdown(box, QStringLiteral("first"));

        // One step on each stack.
        m_context->blockModel()->updateContent(
            1, QStringLiteral("The note was edited here."));
        boxBlocks(box)->updateContent(0, QStringLiteral("second"));

        QVERIFY(m_context->undoStack()->canUndo());
        QVERIFY(boxUndo(box)->canUndo());
        QCOMPARE(boxBlocks(box)->getContent(0), QStringLiteral("second"));

        boxUndo(box)->undo();

        QCOMPARE(boxBlocks(box)->getContent(0), QStringLiteral("first"));
        QCOMPARE(m_context->blockModel()->getContent(1),
                 QStringLiteral("The note was edited here."));
        QVERIFY(m_context->undoStack()->canUndo());
    }

    // Two boxes in one window keep separate documents as well, which is the
    // same property one step further out: nothing about the seam is a
    // singleton that the FIRST embedded editor happens to take.
    void twoBoxesInOneWindowKeepSeparateDocuments()
    {
        QQuickItem *first = makeBox();
        QQuickItem *second = makeBox();
        QVERIFY(first);
        QVERIFY(second);

        setBoxMarkdown(first, QStringLiteral("to the first box"));
        setBoxMarkdown(second, QStringLiteral("to the second box"));

        QVERIFY(boxBlocks(first) != boxBlocks(second));
        QVERIFY(boxUndo(first) != boxUndo(second));
        QCOMPARE(boxBlocks(first)->getContent(0),
                 QStringLiteral("to the first box"));
        QCOMPARE(boxBlocks(second)->getContent(0),
                 QStringLiteral("to the second box"));

        boxBlocks(first)->updateContent(0, QStringLiteral("edited"));
        QCOMPARE(boxBlocks(second)->getContent(0),
                 QStringLiteral("to the second box"));
        QVERIFY(!boxUndo(second)->canUndo());
    }

    // Enter sends rather than making the next block, which is the one
    // behavioural difference between a composer and a document editor. The
    // flag is the editor's (`returnCreatesBlock`), and the box turns it into
    // the signal a host connects to.
    void enterSendsInsteadOfMakingTheNextBlock()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        setBoxMarkdown(box, QStringLiteral("a message"));

        QSignalSpy submitted(box, SIGNAL(submitted(QString)));
        typeIntoBox(box, QString());
        QTest::keyClick(shellWindow(), Qt::Key_Return);
        QCoreApplication::processEvents();

        QCOMPARE(submitted.count(), 1);
        QCOMPARE(submitted.at(0).at(0).toString().trimmed(),
                 QStringLiteral("a message"));
        // And the box is unchanged: the keystroke never reached the split.
        QCOMPARE(boxBlocks(box)->count(), 1);

        // With the flag off it is an ordinary editor again.
        box->setProperty("returnSubmits", false);
        typeIntoBox(box, QString());
        QTest::keyClick(shellWindow(), Qt::Key_Return);
        QTRY_COMPARE(boxBlocks(box)->count(), 2);
        QCOMPARE(submitted.count(), 1);
    }

    // Shift+Enter is the new line, as it is in every chat window, and it
    // stays inside the one block rather than making another.
    void shiftEnterBreaksTheLineInsideTheMessage()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        setBoxMarkdown(box, QStringLiteral("first line"));

        QSignalSpy submitted(box, SIGNAL(submitted(QString)));
        typeIntoBox(box, QString());
        QTest::keyClick(shellWindow(), Qt::Key_Return, Qt::ShiftModifier);
        typeText(shellWindow(), QStringLiteral("second line"));
        QCoreApplication::processEvents();

        QCOMPARE(submitted.count(), 0);
        QCOMPARE(boxBlocks(box)->count(), 1);
        QVERIFY(boxMarkdown(box).contains(QLatin1String("second line")));
    }

    // The box is as tall as what has been typed into it, and stops.
    void theBoxGrowsWithItsContentAndStopsAtItsCap()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        box->setProperty("maximumLines", 6);

        setBoxMarkdown(box, QStringLiteral("one line"));
        settle(box);
        const qreal oneLine = box->height();
        QVERIFY(oneLine > 0);

        setBoxMarkdown(box, QStringLiteral("one line\n\ntwo\n\nthree"));
        settle(box);
        const qreal threeBlocks = box->height();
        QVERIFY2(threeBlocks > oneLine,
                 qPrintable(QStringLiteral("three blocks (%1) did not grow "
                                           "past one (%2)")
                                .arg(threeBlocks).arg(oneLine)));

        QString many;
        for (int i = 0; i < 40; ++i)
            many += QStringLiteral("line %1\n\n").arg(i);
        setBoxMarkdown(box, many);
        settle(box);
        const qreal capped = box->height();

        const qreal lineUnit = box->property("lineUnit").toReal();
        const qreal margin = box->property("contentMargin").toReal();
        QCOMPARE(capped, 6 * lineUnit + 2 * margin);
        // Forty blocks is far past the cap, so the box scrolls inside itself
        // rather than growing: what it holds is taller than what it shows.
        QVERIFY(listContentHeight(box) > capped);
    }

    // No strip down the left, and the row's text starts where the strip
    // would have been. The note's own rows still have theirs, which is what
    // makes this a property of the box rather than of the build.
    void theBoxDrawsNoGutterAndTheNotesRowsStillDo()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        setBoxMarkdown(box, QStringLiteral("a message"));
        settle(box);

        QQuickItem *boxRow = rowOf(box, 0);
        QVERIFY(boxRow);
        QCOMPARE(boxRow->property("gutterInset").toReal(), 0.0);
        QCOMPARE(boxRow->property("gutterShown").toBool(), false);

        QQuickItem *noteRow = noteRowAt(0);
        QVERIFY(noteRow);
        QCOMPARE(noteRow->property("gutterInset").toReal(), 44.0);
        QCOMPARE(noteRow->property("gutterShown").toBool(), true);
    }

    // What an empty box says it is for. The editor's own hint is "Type
    // something..."; a composer says what the box is for, and the string
    // reaches the row's text area through the surface rather than being
    // written into the delegate.
    void theBoxCarriesItsOwnEmptyHintDownToTheRow()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        box->setProperty("placeholderText",
                         QStringLiteral("Message the agent..."));
        typeIntoBox(box, QString());

        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = rowOf(box, 0)
                         ? rowOf(box, 0)->findChild<QQuickItem *>(
                               QStringLiteral("blockTextArea"))
                         : nullptr));
        QCOMPARE(field->property("placeholderText").toString(),
                 QStringLiteral("Message the agent..."));

        // The note's own rows kept the editor's hint.
        QQuickItem *noteRow = noteRowAt(1);
        QVERIFY(noteRow);
        QQuickItem *noteField =
            noteRow->findChild<QQuickItem *>(QStringLiteral("blockTextArea"));
        if (noteField) {
            QCOMPARE(noteField->property("placeholderText").toString(),
                     QStringLiteral("Type something..."));
        }
    }

    // Markdown in, markdown out, which is the whole of what a host stores
    // and sends.
    void markdownGoesInAndComesBackOut()
    {
        QQuickItem *box = makeBox();
        QVERIFY(box);
        const QString message = QStringLiteral(
            "# A heading\n"
            "\n"
            "A paragraph with **bold** in it.\n"
            "\n"
            "- one\n"
            "- two\n"
            "\n"
            "```py\n"
            "print(\"hello\")\n"
            "```\n");
        setBoxMarkdown(box, message);
        QCOMPARE(boxMarkdown(box).trimmed(), message.trimmed());

        // And clearing leaves one empty block, so there is somewhere to type.
        QMetaObject::invokeMethod(box, "clear");
        QCoreApplication::processEvents();
        QCOMPARE(boxBlocks(box)->count(), 1);
        QVERIFY(box->property("empty").toBool());
        QVERIFY(!boxUndo(box)->canUndo());
    }

private:
    // ---- the window's own objects, reached the way anything outside
    // AppContext reaches a service: through the table the composition
    // publishes to its QML engine.
    DocumentSelection *noteSelection()
    {
        return qobject_cast<DocumentSelection *>(
            m_context->services()->lookup(&DocumentSelection::staticMetaObject));
    }
    DocumentSearch *noteSearch()
    {
        return qobject_cast<DocumentSearch *>(
            m_context->services()->lookup(&DocumentSearch::staticMetaObject));
    }
    DocumentOutline *noteOutline()
    {
        return qobject_cast<DocumentOutline *>(
            m_context->services()->lookup(&DocumentOutline::staticMetaObject));
    }
    DocumentStats *noteStats()
    {
        return qobject_cast<DocumentStats *>(
            m_context->services()->lookup(&DocumentStats::staticMetaObject));
    }

    QQuickWindow *shellWindow()
    {
        return qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0));
    }

    QQuickItem *noteRowAt(int index)
    {
        QQuickItem *list = shellWindow()
            ? shellWindow()->findChild<QQuickItem *>(QStringLiteral("blockListView"))
            : nullptr;
        return list ? rowAt(list, index) : nullptr;
    }

    // ---- the box's own objects -------------------------------------------
    QObject *surfaceOf(QQuickItem *box)
    {
        return box->property("editor").value<QObject *>();
    }
    BlockModel *boxBlocks(QQuickItem *box)
    {
        return surfaceOf(box)->property("blocks").value<BlockModel *>();
    }
    UndoStack *boxUndo(QQuickItem *box)
    {
        return surfaceOf(box)->property("undoStack").value<UndoStack *>();
    }
    DocumentOutline *boxOutline(QQuickItem *box)
    {
        return surfaceOf(box)->property("outline").value<DocumentOutline *>();
    }
    DocumentStats *boxStats(QQuickItem *box)
    {
        return surfaceOf(box)->property("stats").value<DocumentStats *>();
    }
    QQuickItem *boxListView(QQuickItem *box)
    {
        QObject *surface = surfaceOf(box);
        return surface ? surface->property("listView").value<QQuickItem *>()
                       : nullptr;
    }
    qreal listContentHeight(QQuickItem *box)
    {
        QQuickItem *list = boxListView(box);
        return list ? list->property("contentHeight").toReal() : 0;
    }

    // ---- driving a box ---------------------------------------------------
    QString boxMarkdown(QQuickItem *box)
    {
        QString out;
        QMetaObject::invokeMethod(box, "markdown", Q_RETURN_ARG(QVariant, m_ret));
        out = m_ret.toString();
        return out;
    }
    void setBoxMarkdown(QQuickItem *box, const QString &text)
    {
        QMetaObject::invokeMethod(box, "setMarkdown",
                                  Q_ARG(QVariant, QVariant(text)));
        settle(box);
    }

    // A box is not measurable in the turn it was given its document: the list
    // builds its rows on a clean stack and positions them on the next polish.
    // Every case that reads a height or reaches for a row starts here.
    void settle(QQuickItem *box)
    {
        for (int attempt = 0; attempt < 200; ++attempt) {
            QCoreApplication::processEvents();
            if (boxBlocks(box)->count() > 0 && rowOf(box, 0))
                break;
            QTest::qWait(5);
        }
        QQuickItem *list = boxListView(box);
        if (list)
            QMetaObject::invokeMethod(list, "forceLayout");
        QCoreApplication::processEvents();
    }

    // Put the caret at the end of the box's last block and type. The window
    // is activated first: key events go to the window's focus item, and a
    // window that was never activated has none.
    //
    // At the END of the LAST block rather than wherever the caret happens to
    // be, because `focusEditor()` lands on the first one and typing there
    // would type into the middle of whatever the case put in block 0 — a
    // heading whose text the case then looks for in an outline would come
    // back with the typing spliced into it.
    void typeIntoBox(QQuickItem *box, const QString &text)
    {
        QQuickWindow *window = shellWindow();
        QVERIFY(window);
        window->requestActivate();
        QObject *surface = surfaceOf(box);
        QVERIFY(surface);
        QMetaObject::invokeMethod(
            surface, "focusBlockAtIndex",
            Q_ARG(QVariant, QVariant(boxBlocks(box)->count() - 1)),
            Q_ARG(QVariant, QVariant(true)),
            Q_ARG(QVariant, QVariant(QString())));
        QTRY_VERIFY(focusIsInside(box));
        if (!text.isEmpty()) {
            typeText(window, text);
            QCoreApplication::processEvents();
        }
    }

    bool focusIsInside(QQuickItem *box)
    {
        QQuickWindow *window = shellWindow();
        QQuickItem *item = window ? window->activeFocusItem() : nullptr;
        while (item) {
            if (item == box)
                return true;
            item = item->parentItem();
        }
        return false;
    }

    // ---- rows ------------------------------------------------------------
    QQuickItem *rowOf(QQuickItem *box, int index)
    {
        QQuickItem *list = boxListView(box);
        return list ? rowAt(list, index) : nullptr;
    }

    // The delegate the list is currently drawing at `index`. Asked of the
    // view rather than found by walking its children, because the rows are
    // pooled and a recycled one still sits under the content item.
    QQuickItem *rowAt(QQuickItem *list, int index)
    {
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex",
                                  Q_RETURN_ARG(QQuickItem *, row),
                                  Q_ARG(int, index));
        return row;
    }

    // A box created in the shell's own engine and parented into its window,
    // exactly as a host's QML would create one.
    QQuickItem *makeBox()
    {
        QObject *window = m_engine.rootObjects().value(0);
        auto *content = window ? window->property("contentItem")
                                     .value<QQuickItem *>() : nullptr;
        if (!content)
            return nullptr;
        QQmlComponent component(
            &m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Kvit/CompactEditor.qml")));
        if (component.isError())
            qWarning() << component.errorString();
        auto *box = qobject_cast<QQuickItem *>(component.create());
        if (!box)
            return nullptr;
        m_boxes.push_back(std::unique_ptr<QQuickItem>(box));
        box->setParentItem(content);
        box->setWidth(420);
        settle(box);
        return box;
    }

    QTemporaryDir m_dir;
    // The context before the engine, so the engine is torn down first: the
    // shell's rows read the composition's objects through bindings, and a
    // composition destroyed under a live shell re-evaluates every one of them
    // against an object that has just gone.
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    std::vector<std::unique_ptr<QQuickItem>> m_boxes;
    QVariant m_ret;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestEmbeddedEditor)
#include "test_embeddededitor.moc"
