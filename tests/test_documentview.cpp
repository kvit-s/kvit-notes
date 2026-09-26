// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentdecorations.h"
#include "documentheights.h"
#include "documentoutline.h"
#include "documentsearch.h"
#include "documentselection.h"
#include "documentstats.h"
#include "notecollection.h"
#include "qmlservices.h"
#include "undostack.h"
#include "vaultsettings.h"

namespace {

// Warnings the shell and the surfaces emit while this suite runs, captured
// the way tests/test_shell.cpp captures them and for the same reason: QML
// reports a binding it could not resolve as a warning and then carries on
// with an undefined value, so a surface wired to nothing would otherwise pass
// every assertion below about counts that are zero.
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

// A document holding one of every kind the renderer this surface replaces
// could not draw, plus the ones it could.
//
// The five fenced kinds are the point of the list. A task board, a table of
// contents, a Mermaid diagram and a collection query are each stored as a
// code block with a language, and a table as its pipe characters, so a
// renderer that switches on the block TYPE draws all five as their source
// text. The editor picks a delegate per kind from the registry, which is what
// these indexes assert.
QString sampleMarkdown()
{
    return QStringLiteral(
        "# Release notes\n"              // 0  heading
        "\n"
        "A paragraph with **bold** in it.\n"  // 1  paragraph
        "\n"
        "| Column A | Column B |\n"       // 2  table
        "|---|---|\n"
        "| one | two |\n"
        "\n"
        "```mermaid\n"                    // 3  Mermaid diagram
        "flowchart TD\n"
        "  A --> B\n"
        "```\n"
        "\n"
        "```kanban\n"                     // 4  task board
        "## To do\n"
        "- a card\n"
        "```\n"
        "\n"
        "```query\n"                      // 5  collection query
        "tag: release\n"
        "```\n"
        "\n"
        "```toc\n"                        // 6  table of contents
        "```\n"
        "\n"
        "```py\n"                         // 7  code fence
        "print(\"hello\")\n"
        "```\n"
        "\n"
        "- [ ] an unchecked task\n");     // 8  to-do
}

constexpr int kHeading = 0;
constexpr int kParagraph = 1;
constexpr int kTable = 2;
constexpr int kDiagram = 3;
constexpr int kBoard = 4;
constexpr int kQuery = 5;
constexpr int kToc = 6;
constexpr int kCode = 7;
constexpr int kTodo = 8;
constexpr int kBlockCount = 9;

} // namespace

// A markdown document drawn somewhere other than the editor pane, drawn by
// the editor (qml/DocumentView.qml, selection.md "A document drawn
// read-only").
//
// The surface is one BlockEditor with `readOnly` set, over a document it owns
// outright. Two properties are worth defending, and they pull against each
// other.
//
// The first is that it draws what the editor draws. The renderer it replaces
// switched on the block type and had a case for about ten of them, so a
// table came out as its pipe characters and a Mermaid diagram, a task board,
// a query and a table of contents each came out as the source inside their
// fence. Here every row is the delegate the kind registry names, which is
// what the cases below assert by delegate type rather than by appearance.
//
// The second is that nothing reaches the document. Typing, the keys that
// would delete or convert or undo, the checkbox on a to-do, the strip of
// handles down the left, the drop area and the block menu are each refused
// where they are raised, and the open note the surface is drawn beside is
// untouched by any of it.
class TestDocumentView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));

        m_vaultRoot = m_dir.filePath(QStringLiteral("vault"));
        QVERIFY(QDir().mkpath(m_vaultRoot));
        writeNote(QStringLiteral("Notes.md"), QStringLiteral("A note.\n"));
        QVERIFY(m_context->openVaultRoot(m_vaultRoot));

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
        m_surfaces.clear();
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        BlockModel *model = m_context->blockModel();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Heading1, QStringLiteral("# Note heading"));
        model->insertBlock(1, Block::Paragraph,
                           QStringLiteral("The note says one thing."));
        noteSelection()->clear();
        m_context->undoStack()->clear();
        m_surfaces.clear();
        QCoreApplication::processEvents();
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

    // Every kind reaches the screen through the delegate its registry entry
    // names, which is the whole reason for drawing with the editor: five of
    // these nine were the source text inside a fence before.
    void everyBlockKindIsDrawnByItsOwnDelegate()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QCOMPARE(view->property("blockCount").toInt(), kBlockCount);

        QCOMPARE(delegateOf(view, kHeading), QStringLiteral("TextBlockDelegate"));
        QCOMPARE(delegateOf(view, kParagraph), QStringLiteral("TextBlockDelegate"));
        QCOMPARE(delegateOf(view, kTable), QStringLiteral("TableBlock"));
        QCOMPARE(delegateOf(view, kDiagram), QStringLiteral("DiagramBlock"));
        QCOMPARE(delegateOf(view, kBoard), QStringLiteral("KanbanBlock"));
        QCOMPARE(delegateOf(view, kQuery), QStringLiteral("QueryBlock"));
        QCOMPARE(delegateOf(view, kToc), QStringLiteral("TocBlock"));
        QCOMPARE(delegateOf(view, kCode), QStringLiteral("CodeBlockDelegate"));
        QCOMPARE(delegateOf(view, kTodo), QStringLiteral("TodoDelegate"));
    }

    // And the table is a table rather than the characters of one: its own
    // cell items exist, which is the thing a reader came for.
    void aTableIsDrawnAsAGridRatherThanAsItsSource()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QQuickItem *row = rowOf(view, kTable);
        QVERIFY(row);
        QCOMPARE(row->property("columns").toInt(), 2);
        QCOMPARE(row->property("dataRows").toInt(), 1);
    }

    // Nothing the keyboard can do reaches the document.
    void typingChangesNothing()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        const QString before = markdownOf(view);
        const int countBefore = view->property("blockCount").toInt();

        focusRow(view, kParagraph);
        QQuickWindow *window = shellWindow();

        // One key at a time, each checked on its own, so a hole names the
        // keystroke that found it rather than "something changed".
        const QList<QPair<QString, std::function<void()>>> keys = {
            {QStringLiteral("typing"), [&] {
                 for (const QChar c : QStringLiteral("typed"))
                     QTest::keyClick(window, c.toLatin1(), Qt::NoModifier, 1);
             }},
            {QStringLiteral("Return"), [&] { QTest::keyClick(window, Qt::Key_Return); }},
            {QStringLiteral("Shift+Return"), [&] {
                 QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier); }},
            {QStringLiteral("Backspace"), [&] { QTest::keyClick(window, Qt::Key_Backspace); }},
            {QStringLiteral("Delete"), [&] { QTest::keyClick(window, Qt::Key_Delete); }},
            {QStringLiteral("Tab"), [&] { QTest::keyClick(window, Qt::Key_Tab); }},
            {QStringLiteral("Ctrl+1"), [&] {
                 QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier); }},
            {QStringLiteral("Ctrl+V"), [&] {
                 QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier); }},
            {QStringLiteral("Ctrl+X"), [&] {
                 QTest::keyClick(window, Qt::Key_X, Qt::ControlModifier); }},
            {QStringLiteral("Ctrl+D"), [&] {
                 QTest::keyClick(window, Qt::Key_D, Qt::ControlModifier); }},
            {QStringLiteral("Ctrl+Z"), [&] {
                 QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier); }},
        };
        for (const auto &entry : keys) {
            focusRow(view, kParagraph);
            entry.second();
            QCoreApplication::processEvents();
            QVERIFY2(view->property("blockCount").toInt() == countBefore
                         && markdownOf(view) == before,
                     qPrintable(QStringLiteral("%1 changed the document")
                                    .arg(entry.first)));
        }
        QVERIFY(!surfaceUndo(view)->canUndo());
    }

    // The keys that read still work: arrows move the caret, Ctrl+A selects
    // the whole document as text and Ctrl+C copies it as markdown.
    void selectingAndCopyingStillWork()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QVERIFY(QMetaObject::invokeMethod(view, "selectAll"));
        QCoreApplication::processEvents();

        QVERIFY(view->property("hasSelection").toBool());
        QString selected;
        QVERIFY(QMetaObject::invokeMethod(view, "selectedMarkdown",
                                          Q_RETURN_ARG(QVariant, m_ret)));
        selected = m_ret.toString();
        QVERIFY(selected.contains(QLatin1String("Release notes")));
        QVERIFY(selected.contains(QLatin1String("flowchart TD")));

        QVERIFY(QMetaObject::invokeMethod(view, "clearSelection"));
        QCoreApplication::processEvents();
        QVERIFY(!view->property("hasSelection").toBool());
    }

    // The strip of handles down the left is gone, and so is the drag it
    // starts: a read-only row reports no gutter whatever the host asked for.
    void thereIsNoGutterAndNoDrag()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QQuickItem *row = rowOf(view, kParagraph);
        QVERIFY(row);
        QCOMPARE(row->property("gutterShown").toBool(), false);
        QCOMPARE(row->property("gutterInset").toReal(), 0.0);
        QCOMPARE(row->property("readOnly").toBool(), true);

        QObject *editor = view->property("editor").value<QObject *>();
        QVERIFY(editor);
        QCOMPARE(editor->property("blockDrag").value<QObject *>(),
                 static_cast<QObject *>(nullptr));

        // Even with the host asking for a gutter, which it cannot have.
        editor->setProperty("showGutter", true);
        QCoreApplication::processEvents();
        QCOMPARE(row->property("gutterShown").toBool(), false);
    }

    // A to-do's checkbox still shows whether the task is done and cannot be
    // pressed to change it.
    void aTodoCheckboxIsDrawnAndNotPressable()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QQuickItem *row = rowOf(view, kTodo);
        QVERIFY(row);
        QQuickItem *box =
            row->findChild<QQuickItem *>(QStringLiteral("todoCheckbox"));
        QVERIFY(box);
        QVERIFY(box->isVisible());
        QCOMPARE(box->isEnabled(), false);

        const QString before = markdownOf(view);
        QMetaObject::invokeMethod(box, "clicked");
        QCoreApplication::processEvents();
        QCOMPARE(markdownOf(view), before);
    }

    // What a caller knows about part of the document it asked to be drawn,
    // registered on the surface's own decoration seam rather than on the
    // window's.
    void aCallerCanMarkRangesAndTheNotesSeamIsUntouched()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        auto *spans = view->property("decorations").value<DocumentDecorations *>();
        QVERIFY(spans);
        QVERIFY(spans != m_context->documentDecorations());

        const QString id = spans->addSpan(QStringLiteral("test"), kParagraph, 0, 9,
                                          DocumentDecorations::Wash,
                                          QColor(QStringLiteral("#5533aa")));
        QVERIFY(!id.isEmpty());
        QCOMPARE(spans->spanCount(), 1);
        QVERIFY(spans->hasSpans());
        QCOMPARE(m_context->documentDecorations()->spanCount(), 0);

        // And it is drawn: the marked block reports rectangles for it once the
        // row has been laid out.
        QMetaObject::invokeMethod(view, "forceLayout");
        QCoreApplication::processEvents();
        QTRY_VERIFY(!spans->spanRects(id).isEmpty());
    }

    // The surface owns its document, so the note beside it does not move.
    void theOpenNoteIsUntouched()
    {
        const int noteBlocks = m_context->blockModel()->count();
        const int noteUndo = m_context->undoStack()->count();
        const int noteHeights = m_context->documentHeights()->count();
        QObject *noteView = m_context->documentDecorations()->documentView();

        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        focusRow(view, kParagraph);
        for (const QChar c : QStringLiteral("typed"))
            QTest::keyClick(shellWindow(), c.toLatin1(), Qt::NoModifier, 1);
        QCoreApplication::processEvents();

        QCOMPARE(m_context->blockModel()->count(), noteBlocks);
        QCOMPARE(m_context->blockModel()->getContent(0),
                 QStringLiteral("# Note heading"));
        QCOMPARE(m_context->undoStack()->count(), noteUndo);
        QVERIFY(!m_context->undoStack()->canUndo());
        QCOMPARE(m_context->documentHeights()->count(), noteHeights);
        QCOMPARE(m_context->documentDecorations()->documentView(), noteView);

        noteOutline()->rebuildNow();
        QVERIFY(!noteOutline()->hasSlug(QStringLiteral("release-notes")));
        QCOMPARE(noteSearch()->model(), m_context->blockModel());
    }

    // The surface is as tall as its document, so it can sit inside a
    // scrolling area it does not own.
    void theSurfaceIsAsTallAsWhatItDraws()
    {
        QQuickItem *shortView = makeView(QStringLiteral("one line"));
        QQuickItem *longView = makeView(sampleMarkdown());
        QVERIFY(shortView);
        QVERIFY(longView);
        QVERIFY(shortView->height() > 0);
        QVERIFY2(longView->height() > shortView->height(),
                 qPrintable(QStringLiteral("nine blocks (%1) were not taller "
                                           "than one (%2)")
                                .arg(longView->height())
                                .arg(shortView->height())));
    }

    // ---- a picture's width ----
    //
    // An image block whose markdown carries no width — ![alt](chart.png)
    // rather than ![alt|180](chart.png) — draws the picture at the picture's
    // own width. The width it draws at and the number of pixels it asks the
    // decoder for used to be read out of each other, which QML reports as a
    // binding loop and breaks by refusing to re-evaluate: the block settled
    // at the 320 px it falls back to before the file has been read, having
    // decoded the file once per pass around the circle.
    void aPictureWithNoStoredWidthIsDrawnAtItsOwnWidth()
    {
        const QString file = writePicture(QStringLiteral("chart.png"), 360, 240);
        QVERIFY(!file.isEmpty());
        const int before = g_warnings.size();

        QQuickItem *view = makeView(QStringLiteral("![A chart](") + file
                                    + QStringLiteral(")\n"));
        QVERIFY(view);
        QCOMPARE(view->property("blockCount").toInt(), 1);
        QCOMPARE(delegateOf(view, 0), QStringLiteral("ImageBlock"));
        QQuickItem *row = rowOf(view, 0);
        QVERIFY(row);
        // Otherwise the width below is the pane's cap rather than the
        // picture's own size, and the case proves nothing.
        QVERIFY(row->property("maxWidth").toInt() > 360);

        QQuickItem *picture =
            row->findChild<QQuickItem *>(QStringLiteral("imagePicture"));
        QVERIFY(picture);
        QTRY_COMPARE(picture->property("status").toInt(), 1 /* Image.Ready */);

        QQuickItem *frame =
            row->findChild<QQuickItem *>(QStringLiteral("imageAccessible"));
        QVERIFY(frame);
        QTRY_COMPARE(qRound(frame->width()), 360);
        QTRY_COMPARE(qRound(frame->height()), 240);

        for (const QString &warning : g_warnings.mid(before)) {
            QVERIFY2(!warning.contains(QLatin1String("Binding loop")),
                     qPrintable(warning));
        }
    }

    // The geometry comes from the measured file rather than from the decoded
    // picture, so a row is the right shape whether or not it is holding one.
    // A recycled row is where that shows: the list drops the decoded picture
    // of a row scrolled out of view, and then asks it how tall it is in order
    // to place the rows below it.
    void aRecycledRowKeepsThePicturesShape()
    {
        const QString file = writePicture(QStringLiteral("chart.png"), 360, 240);
        QVERIFY(!file.isEmpty());
        QQuickItem *view = makeView(QStringLiteral("![A chart](") + file
                                    + QStringLiteral(")\n"));
        QVERIFY(view);
        QQuickItem *row = rowOf(view, 0);
        QVERIFY(row);
        QVERIFY(row->property("maxWidth").toInt() > 360);
        QQuickItem *frame =
            row->findChild<QQuickItem *>(QStringLiteral("imageAccessible"));
        QVERIFY(frame);
        QTRY_COMPARE(qRound(frame->height()), 240);

        // Pooled: the row lets go of the picture rather than sitting in the
        // recycle pool holding a decoded pixmap for a block nobody is looking
        // at, which is what leaves it with nothing of its own to measure.
        row->setProperty("isPooled", true);
        QQuickItem *image =
            row->findChild<QQuickItem *>(QStringLiteral("imagePicture"));
        QVERIFY(image);
        QTRY_VERIFY(image->property("source").toString().isEmpty());
        QCOMPARE(qRound(frame->width()), 360);
        QCOMPARE(qRound(frame->height()), 240);
    }

    // ---- where a picture is looked up ----
    //
    // A drawn document is usually not in the vault's root folder, and it can
    // name a picture in two ways its own folder does not answer: from the
    // vault's root, `assets/a.png`, which is how a note in a subfolder names
    // a pasted picture; and from a website's root, `/images/a.png`, which is
    // found in the vault's site folder. Both are looked up in the open vault
    // unless the host names another folder.
    void aPictureIsFoundFromTheVaultRootAndFromTheSiteFolder()
    {
        QVERIFY(QDir(m_vaultRoot).mkpath(QStringLiteral("Sub")));
        QVERIFY(QDir(m_vaultRoot).mkpath(QStringLiteral("assets")));
        QVERIFY(QDir(m_vaultRoot).mkpath(QStringLiteral("static/images")));
        const QString fromRoot =
            writePicture(QStringLiteral("assets/from-root.png"), 60, 40);
        const QString fromSite =
            writePicture(QStringLiteral("static/images/from-site.png"), 60, 40);
        QVERIFY(!fromRoot.isEmpty() && !fromSite.isEmpty());
        VaultSettings *settings = m_context->noteCollection()->vaultSettings();
        QVERIFY(settings->setSiteFolder(QStringLiteral("static")));
        const auto reset = qScopeGuard([settings] { settings->resetSiteFolder(); });

        QQuickItem *view = makeView(QStringLiteral(
            "![](assets/from-root.png)\n\n![](/images/from-site.png)\n"));
        QVERIFY(view);
        view->setProperty("baseDir", m_vaultRoot + QStringLiteral("/Sub"));
        QCOMPARE(view->property("blockCount").toInt(), 2);

        QTRY_COMPARE(pictureFileOf(view, 0), fromRoot);
        QTRY_COMPARE(pictureFileOf(view, 1), fromSite);
    }

    // A host drawing a file out of a copy of the vault kept in another folder
    // names that folder, and gets the copy's pictures rather than the ones in
    // the open vault. The site folder comes with it: the vault's setting, taken
    // inside the copy.
    void aHostDrawingACopyOfTheVaultGetsTheCopysPictures()
    {
        QVERIFY(QDir(m_vaultRoot).mkpath(QStringLiteral("static/images")));
        QVERIFY(!writePicture(QStringLiteral("static/images/shared.png"), 60, 40)
                     .isEmpty());
        const QString copy = m_dir.filePath(QStringLiteral("copy"));
        QVERIFY(QDir(copy).mkpath(QStringLiteral("static/images")));
        const QString copied = copy + QStringLiteral("/static/images/shared.png");
        QImage picture(50, 30, QImage::Format_RGB32);
        picture.fill(Qt::darkMagenta);
        QVERIFY(picture.save(copied));
        VaultSettings *settings = m_context->noteCollection()->vaultSettings();
        QVERIFY(settings->setSiteFolder(QStringLiteral("static")));
        const auto reset = qScopeGuard([settings] { settings->resetSiteFolder(); });

        QQuickItem *view = makeView(QStringLiteral("![](/images/shared.png)\n"));
        QVERIFY(view);
        QTRY_COMPARE(pictureFileOf(view, 0),
                     m_vaultRoot + QStringLiteral("/static/images/shared.png"));

        view->setProperty("baseDir", copy);
        view->setProperty("assetRoot", copy);
        QCOMPARE(view->property("siteRoot").toString(),
                 copy + QStringLiteral("/static"));
        QTRY_COMPARE(pictureFileOf(view, 0), copied);
    }

    // A host that does its own scrolling. This surface is as tall as the
    // document it draws, so its own list has nothing to scroll and the wheel
    // turned over it belongs to whatever the host put it inside.
    //
    // The editor takes the wheel with a handler of its own rather than
    // leaving it to the flickable (qml/WheelScroller.qml), and a handler that
    // is enabled takes the event out of the delivery that would otherwise
    // have reached the host, whether or not it does anything with it —
    // declining the event inside the signal does not put it back. So the
    // handler has to be disabled outright, and that is what this asserts. A
    // host embedding this surface in its own ScrollView is where it shows:
    // with the handler enabled over a list with nothing to scroll, the pane
    // did not move at all.
    void theWheelHandlerStandsDownWhenThereIsNothingToScroll()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QObject *scroller = view->findChild<QObject *>(
            QStringLiteral("editorWheelScroller"));
        QVERIFY(scroller);
        QVERIFY2(!scroller->property("scrollable").toBool(),
                 "the surface grew with its document, so its list should have "
                 "had nothing to scroll");
        // Looked up from the surface rather than from the scroller: the
        // handler attaches itself to the list, so that is where it lives.
        QObject *handler = view->findChild<QObject *>(
            QStringLiteral("editorWheelHandler"));
        QVERIFY(handler);
        QVERIFY2(!handler->property("enabled").toBool(),
                 "the wheel handler was left enabled over a list with nothing "
                 "to scroll, which takes the wheel away from the host");
    }

    // ---- a long document ----
    //
    // A surface that grows with its document builds every row, since every
    // row is inside the list's own height, and a long file opened that way
    // took seconds and made every frame pay for all of it. A surface that
    // scrolls builds the rows on screen and recycles the rest, as the note's
    // editor does, and draws the editor's scroll bar, since the reader has no
    // other way to see how far through the document they are.
    void aSurfaceThatScrollsBuildsOnlyTheRowsOnScreen()
    {
        QStringList paragraphs;
        for (int i = 0; i < 1500; ++i)
            paragraphs << QStringLiteral("Paragraph %1 of a long document, "
                                         "with enough words to wrap once.")
                              .arg(i);
        QQuickItem *view = makeScrollingView(
            paragraphs.join(QStringLiteral("\n\n")), 400);
        QVERIFY(view);
        QCOMPARE(view->property("blockCount").toInt(), 1500);

        const int atTop = builtRows(view);
        QVERIFY2(atTop > 0 && atTop < 100,
                 qPrintable(QStringLiteral("a 400 px surface built %1 of 1500 "
                                           "rows").arg(atTop)));

        QObject *handler = view->findChild<QObject *>(
            QStringLiteral("editorWheelHandler"));
        QVERIFY(handler);
        QVERIFY2(handler->property("enabled").toBool(),
                 "a surface that scrolls its document should take the wheel");
        auto *bar = view->findChild<QQuickItem *>(
            QStringLiteral("editorScrollBar"));
        QVERIFY(bar);
        QVERIFY2(bar->isVisible(), "a surface that scrolls drew no scroll bar");
        // Its height is the host's to give. The list's content height is an
        // estimate for every row it has not built, so a layout that took it
        // as the surface's preferred size would be sizing to a guess.
        QCOMPARE(view->implicitHeight(), 0.0);

        QObject *editor = view->property("editor").value<QObject *>();
        QVERIFY(editor);
        QMetaObject::invokeMethod(editor, "setEditorContentY",
                                  Q_ARG(QVariant, QVariant(1.0e9)));
        QTRY_VERIFY(rowOf(view, 1499));
        const int atEnd = builtRows(view);
        QVERIFY2(atEnd < 100,
                 qPrintable(QStringLiteral("scrolled to the end, the surface "
                                           "held %1 rows").arg(atEnd)));
        QVERIFY2(!rowOf(view, 0), "the first row was not recycled");
    }

    // ---- the pointer over a read-only row ----
    //
    // The hover tint marks the block the strip and the block menu would act
    // on. A read-only row has neither, and a document scrolled under a
    // resting pointer lit each block in turn.
    void aReadOnlyRowDrawsNoHoverTint()
    {
        QQuickItem *view = makeView(sampleMarkdown());
        QVERIFY(view);
        QQuickWindow *window = shellWindow();
        QVERIFY(window);
        for (int index = 0; index < kBlockCount; ++index) {
            QQuickItem *row = rowOf(view, index);
            QVERIFY(row);
            const QPointF middle = row->mapToScene(
                QPointF(row->width() / 2, qMin(row->height() / 2, 12.0)));
            QTest::mouseMove(window, middle.toPoint());
            QCoreApplication::processEvents();
            const QString tinted = hoverTintedItem(row);
            QVERIFY2(tinted.isEmpty(),
                     qPrintable(QStringLiteral("row %1 (%2) drew the hover tint "
                                               "on %3")
                                    .arg(index).arg(delegateOf(view, index),
                                                    tinted)));
        }
        QTest::mouseMove(window, QPoint(1, 1));
    }

    // ---- where a paragraph's words sit ----
    //
    // A paragraph is drawn as plain text until something needs the editing
    // engine, and by the engine's text area from then on. The two have to
    // put the words in the same place, or the words move the moment a row
    // becomes an editor: a press in the note, a comment or a search match in
    // a drawn document. In a drawn document every paragraph with bold or a
    // link in it, which is an editor from the start, sat to the right of its
    // plain neighbours and wrapped a narrower column.
    void aParagraphsWordsStayPutWhenItBecomesAnEditor()
    {
        // In the note, where the strip beside each row is drawn. Found before
        // any surface is made, so the only block list is the note's.
        auto *noteList = shellWindow()->findChild<QQuickItem *>(
            QStringLiteral("blockListView"));
        QVERIFY(noteList);
        QQuickItem *noteRow = nullptr;
        QTRY_VERIFY((noteRow = listRow(noteList, 1)));
        QVERIFY2(!noteRow->property("editorLoaderActive").toBool(),
                 "the note's paragraph should start as plain text");
        const QRectF noteBefore = wordsOf(noteRow);
        QMetaObject::invokeMethod(noteRow, "activateEditor");
        QTRY_VERIFY(textAreaOf(noteRow) && textAreaOf(noteRow)->isVisible());
        compareWords(noteBefore, wordsOf(noteRow), "the note's paragraph");
        if (QTest::currentTestFailed())
            return;
        // A heading is drawn by the same two layers.
        QQuickItem *headingRow = nullptr;
        QTRY_VERIFY((headingRow = listRow(noteList, 0)));
        QVERIFY2(!headingRow->property("editorLoaderActive").toBool(),
                 "the note's heading should start as plain text");
        const QRectF headingBefore = wordsOf(headingRow);
        QMetaObject::invokeMethod(headingRow, "activateEditor");
        QTRY_VERIFY(textAreaOf(headingRow) && textAreaOf(headingRow)->isVisible());
        compareWords(headingBefore, wordsOf(headingRow), "the note's heading");
        if (QTest::currentTestFailed())
            return;

        // In a drawn document, where there is no strip.
        QQuickItem *view = makeView(QStringLiteral(
            "Plain words in a paragraph with nothing marked up in it.\n\n"
            "A paragraph with **bold** words in it."));
        QVERIFY(view);
        QQuickItem *plain = rowOf(view, 0);
        QQuickItem *marked = rowOf(view, 1);
        QVERIFY(plain && marked);
        QVERIFY2(!plain->property("editorLoaderActive").toBool(),
                 "the plain paragraph should start as plain text");
        QTRY_VERIFY(textAreaOf(marked) && textAreaOf(marked)->isVisible());
        const QRectF plainBefore = wordsOf(plain);
        const QRectF markedWords = wordsOf(marked);
        QVERIFY2(qAbs(plainBefore.left() - markedWords.left()) < 0.5
                     && qAbs(plainBefore.width() - markedWords.width()) < 0.5,
                 qPrintable(QStringLiteral("a plain paragraph's words start at "
                                           "x %1 in a column %2 wide, and a "
                                           "marked-up one's at x %3 in %4")
                                .arg(plainBefore.left()).arg(plainBefore.width())
                                .arg(markedWords.left()).arg(markedWords.width())));
        QMetaObject::invokeMethod(plain, "activateEditor");
        QTRY_VERIFY(textAreaOf(plain) && textAreaOf(plain)->isVisible());
        compareWords(plainBefore, wordsOf(plain), "the drawn paragraph");
    }

    // ---- the first drag over a plain paragraph ----
    //
    // A paragraph with no markup is drawn as plain text until something needs
    // the editing engine, and a press is what does: the row becomes an editor
    // under the pointer. The text area that selects was not there when the
    // press arrived, so the drag that followed selected nothing, and only a
    // second drag in the same paragraph did. A reader of a drawn document
    // sweeps text more than anything else, and each paragraph failed once.
    void theFirstDragOverAPlainParagraphSelects()
    {
        QQuickItem *view = makeView(QStringLiteral(
            "Plain words in a paragraph with nothing marked up in it.\n\n"
            "A second paragraph under it."));
        QVERIFY(view);
        QQuickItem *row = rowOf(view, 0);
        QVERIFY(row);
        QVERIFY2(!row->property("editorLoaderActive").toBool(),
                 "the paragraph should start as plain text");

        const QPoint from = textPoint(row, 4);
        const QPoint to = textPoint(row, 160);
        sweep(from, to);

        QObject *editable = row->property("editable").value<QObject *>();
        QVERIFY(editable);
        QObject *area = editable->property("textArea").value<QObject *>();
        QVERIFY(area);
        const QString selected = area->property("selectedText").toString();
        QVERIFY2(selected.length() >= 5,
                 qPrintable(QStringLiteral("the first drag selected \"%1\"")
                                .arg(selected)));
        QVERIFY(QStringLiteral("Plain words in a paragraph with nothing marked "
                               "up in it.").contains(selected));
    }

    // The same drag carried on into the paragraph below becomes the
    // document's own range, which is what a host reads and Ctrl+C copies.
    void theFirstDragFromAPlainParagraphReachesTheNext()
    {
        QQuickItem *view = makeView(QStringLiteral(
            "Plain words in a paragraph with nothing marked up in it.\n\n"
            "A second paragraph under it."));
        QVERIFY(view);
        QQuickItem *first = rowOf(view, 0);
        QQuickItem *second = rowOf(view, 1);
        QVERIFY(first);
        QVERIFY(second);
        QVERIFY(!first->property("editorLoaderActive").toBool());

        sweep(textPoint(first, 60), textPoint(second, 40));

        QVERIFY2(view->property("hasSelection").toBool(),
                 "a drag from one paragraph into the next made no range");
        QVERIFY(QMetaObject::invokeMethod(view, "selectedRange",
                                          Q_RETURN_ARG(QVariant, m_ret)));
        const QVariantMap range = m_ret.toMap();
        QCOMPARE(range.value(QStringLiteral("startIndex")).toInt(), 0);
        QCOMPARE(range.value(QStringLiteral("endIndex")).toInt(), 1);
        // Where the press was, as the editor hit-tests it. Plain text can
        // only say which half of the row a point is in, which would put the
        // start at 0 here, sixty pixels into the line.
        const int start = range.value(QStringLiteral("startPos")).toInt();
        QVERIFY2(start > 3 && start < 20,
                 qPrintable(QStringLiteral("the range started at %1").arg(start)));
    }

private:
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

    QQuickWindow *shellWindow()
    {
        return qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0));
    }

    UndoStack *surfaceUndo(QQuickItem *view)
    {
        QObject *editor = view->property("editor").value<QObject *>();
        return editor ? editor->property("undoStack").value<UndoStack *>()
                      : nullptr;
    }

    // The QML type that drew a row, which is how a case says "a table, not
    // the characters of one". A QML component's metaobject is named after its
    // file with a generated suffix, so the comparison is against the part
    // before it.
    QString delegateOf(QQuickItem *view, int index)
    {
        QQuickItem *row = rowOf(view, index);
        if (!row)
            return QStringLiteral("<no row>");
        const QString name = QString::fromLatin1(row->metaObject()->className());
        const int cut = name.indexOf(QLatin1String("_QML"));
        return cut >= 0 ? name.left(cut) : name;
    }

    QQuickItem *rowOf(QQuickItem *view, int index)
    {
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(view, "blockItem",
                                  Q_RETURN_ARG(QVariant, m_ret),
                                  Q_ARG(QVariant, QVariant(index)));
        row = m_ret.value<QQuickItem *>();
        return row;
    }

    // The whole document as markdown, which is what every "nothing changed"
    // assertion compares.
    QString markdownOf(QQuickItem *view)
    {
        auto *blocks = view->property("blocks").value<BlockModel *>();
        if (!blocks)
            return QString();
        QStringList out;
        for (int i = 0; i < blocks->count(); ++i)
            out << blocks->getContent(i);
        return out.join(QLatin1Char('\n'));
    }

    void focusRow(QQuickItem *view, int index)
    {
        QQuickWindow *window = shellWindow();
        QVERIFY(window);
        window->requestActivate();
        QObject *editor = view->property("editor").value<QObject *>();
        QVERIFY(editor);
        QMetaObject::invokeMethod(editor, "focusBlockAtIndex",
                                  Q_ARG(QVariant, QVariant(index)),
                                  Q_ARG(QVariant, QVariant(true)),
                                  Q_ARG(QVariant, QVariant(QString())));
        QTRY_VERIFY(focusIsInside(view));
    }

    bool focusIsInside(QQuickItem *view)
    {
        QQuickWindow *window = shellWindow();
        QQuickItem *item = window ? window->activeFocusItem() : nullptr;
        while (item) {
            if (item == view)
                return true;
            item = item->parentItem();
        }
        return false;
    }

    // A surface created in the shell's own engine and parented into its
    // window, exactly as a consumer's QML would create one.
    QQuickItem *makeView(const QString &markdown)
    {
        QObject *window = m_engine.rootObjects().value(0);
        auto *content = window ? window->property("contentItem")
                                     .value<QQuickItem *>() : nullptr;
        if (!content)
            return nullptr;
        QQmlComponent component(
            &m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Kvit/DocumentView.qml")));
        if (component.isError())
            qWarning() << component.errorString();
        auto *view = qobject_cast<QQuickItem *>(component.create());
        if (!view)
            return nullptr;
        m_surfaces.push_back(std::unique_ptr<QQuickItem>(view));
        view->setParentItem(content);
        view->setWidth(600);
        view->setProperty("markdown", markdown);
        // A list builds its rows on a clean stack and places them on the next
        // polish, so a surface is not measurable in the turn it was given its
        // document.
        for (int attempt = 0; attempt < 200; ++attempt) {
            QCoreApplication::processEvents();
            const int count = view->property("blockCount").toInt();
            if (count > 0 && rowOf(view, count - 1))
                break;
            QTest::qWait(5);
        }
        QMetaObject::invokeMethod(view, "forceLayout");
        QCoreApplication::processEvents();
        return view;
    }

    // A surface that scrolls its own document inside `height` pixels. Only
    // the first row is waited for, since the rest are built as they come on
    // screen.
    QQuickItem *makeScrollingView(const QString &markdown, qreal height)
    {
        QObject *window = m_engine.rootObjects().value(0);
        auto *content = window ? window->property("contentItem")
                                     .value<QQuickItem *>() : nullptr;
        if (!content)
            return nullptr;
        QQmlComponent component(
            &m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Kvit/DocumentView.qml")));
        auto *view = qobject_cast<QQuickItem *>(component.create());
        if (!view)
            return nullptr;
        m_surfaces.push_back(std::unique_ptr<QQuickItem>(view));
        view->setParentItem(content);
        view->setProperty("growsWithDocument", false);
        view->setWidth(600);
        view->setHeight(height);
        view->setProperty("markdown", markdown);
        for (int attempt = 0; attempt < 200; ++attempt) {
            QCoreApplication::processEvents();
            if (view->property("blockCount").toInt() > 0 && rowOf(view, 0))
                break;
            QTest::qWait(5);
        }
        QMetaObject::invokeMethod(view, "forceLayout");
        QCoreApplication::processEvents();
        return view;
    }

    // How many rows the surface's list holds right now, the recycled ones in
    // its pool left out.
    int builtRows(QQuickItem *view)
    {
        QObject *editor = view->property("editor").value<QObject *>();
        auto *list = editor ? editor->property("listView").value<QQuickItem *>()
                            : nullptr;
        auto *content = list ? list->property("contentItem").value<QQuickItem *>()
                             : nullptr;
        if (!content)
            return -1;
        int built = 0;
        for (QQuickItem *child : content->childItems()) {
            if (child->isVisible() && child->property("index").isValid()
                && child->opacity() > 0)
                ++built;
        }
        return built;
    }

    // The first visible item inside `row` filled with the hover tint, by
    // class name, or "" when there is none.
    QString hoverTintedItem(QQuickItem *row)
    {
        QQmlExpression expression(qmlContext(row), row,
                                  QStringLiteral("Theme.blockHoverTint"));
        const QColor tint = expression.evaluate().value<QColor>();
        if (!tint.isValid())
            return QStringLiteral("<no tint colour>");
        std::function<QString(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->isVisible() || item->opacity() <= 0)
                return QString();
            const QVariant colour = item->property("color");
            if (colour.isValid() && colour.value<QColor>() == tint
                && item->width() > 0 && item->height() > 0)
                return QString::fromLatin1(item->metaObject()->className());
            for (QQuickItem *child : item->childItems()) {
                const QString found = walk(child);
                if (!found.isEmpty())
                    return found;
            }
            return QString();
        };
        return walk(row);
    }

    // The row a list built at `index`, or null.
    QQuickItem *listRow(QQuickItem *list, int index)
    {
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex",
                                  Q_RETURN_ARG(QQuickItem *, row),
                                  Q_ARG(int, index));
        return row;
    }

    // The engine's text area in a text row, or null while it has none.
    QQuickItem *textAreaOf(QQuickItem *row)
    {
        QObject *editable = row->property("editable").value<QObject *>();
        return editable ? editable->property("textArea").value<QQuickItem *>()
                        : nullptr;
    }

    // Where a text row's words are drawn, in scene coordinates: the column
    // its lines wrap in, from the plain text while that is what shows and
    // from inside the text area's padding once the engine draws them.
    QRectF wordsOf(QQuickItem *row)
    {
        QQuickItem *area = textAreaOf(row);
        if (area && area->isVisible()) {
            const qreal left = area->property("leftPadding").toReal();
            const qreal right = area->property("rightPadding").toReal();
            const qreal top = area->property("topPadding").toReal();
            return QRectF(area->mapToScene(QPointF(left, top)),
                          QSizeF(area->width() - left - right, 1));
        }
        auto *text = row->findChild<QQuickItem *>(QStringLiteral("readOnlyText"));
        if (!text || !text->isVisible())
            return {};
        return QRectF(text->mapToScene(QPointF()), QSizeF(text->width(), 1));
    }

    void compareWords(const QRectF &plain, const QRectF &editor,
                      const char *what)
    {
        QVERIFY2(!plain.isNull() && !editor.isNull(),
                 qPrintable(QStringLiteral("%1 has no words to measure")
                                .arg(QLatin1String(what))));
        QVERIFY2(qAbs(plain.left() - editor.left()) < 0.5
                     && qAbs(plain.top() - editor.top()) < 0.5
                     && qAbs(plain.width() - editor.width()) < 0.5,
                 qPrintable(QStringLiteral("%1 drew its words at (%2, %3) in a "
                                           "column %4 wide, and at (%5, %6) in "
                                           "%7 once it became an editor")
                                .arg(QLatin1String(what))
                                .arg(plain.left()).arg(plain.top())
                                .arg(plain.width())
                                .arg(editor.left()).arg(editor.top())
                                .arg(editor.width())));
    }

    // A scene point `dx` pixels into the first line of a text row's text.
    QPoint textPoint(QQuickItem *row, qreal dx)
    {
        auto *text = row->findChild<QQuickItem *>(QStringLiteral("readOnlyText"));
        const QPointF local = text
            ? row->mapFromItem(text, QPointF(dx, 6)) : QPointF(dx + 16, 16);
        return row->mapToScene(local).toPoint();
    }

    // Press at `from`, move to `to` a few pixels at a time with a frame's
    // pause between, and release there: a drag a person makes, rather than
    // one jump the list would take for a click at the far end.
    void sweep(const QPoint &from, const QPoint &to)
    {
        QQuickWindow *window = shellWindow();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::qWait(20);
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) {
            const QPoint at = from + (to - from) * i / steps;
            QTest::mouseMove(window, at);
            QTest::qWait(10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTest::qWait(20);
        QCoreApplication::processEvents();
    }

    // The file an image row resolved its path to, or "" when it found none.
    QString pictureFileOf(QQuickItem *view, int index)
    {
        QQuickItem *row = rowOf(view, index);
        return row ? QUrl(row->property("resolvedSource").toString()).toLocalFile()
                   : QString();
    }

    // A picture in the vault, answered as the absolute path an expression
    // names it by.
    QString writePicture(const QString &relPath, int width, int height)
    {
        const QString path = m_vaultRoot + QLatin1Char('/') + relPath;
        QImage picture(width, height, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        return picture.save(path) ? path : QString();
    }

    void writeNote(const QString &relPath, const QString &text)
    {
        QFile file(m_vaultRoot + QLatin1Char('/') + relPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text.toUtf8());
        file.close();
    }

    QTemporaryDir m_dir;
    QString m_vaultRoot;
    // The context before the engine, so the engine is torn down first: the
    // shell's rows read the composition's objects through bindings.
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    std::vector<std::unique_ptr<QQuickItem>> m_surfaces;
    QVariant m_ret;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestDocumentView)
#include "test_documentview.moc"
