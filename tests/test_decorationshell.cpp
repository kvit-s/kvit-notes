// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentdecorations.h"
#include "documentserializer.h"
#include "extensionregistry.h"
#include "undostack.h"

namespace {

// A demonstration module: the smallest thing that uses the whole seam.
//
// It fills the pinned document-header slot through the extension registry,
// and draws its containers and margin glyphs by registering them with the
// composition's DocumentDecorations — which is how a real module reaches
// them too, since a module builds the AppContext it runs on. Its QML lives in
// its own resource (tests/decoration_demo.qrc), like a module's would; the
// editor never names those files, it loads the URL it was handed.
class DemoModule : public KvitExtension
{
public:
    QString name() const override { return QStringLiteral("decoration-demo"); }
    QString qmlNamespace() const override { return QStringLiteral("demo"); }

    QString qmlSlot(const QString &slot) const override
    {
        if (slot == QLatin1String(KvitSlots::DocumentHeader))
            return QStringLiteral("qrc:/decorations/DecorationDemoHeader.qml");
        return QString();
    }
};

QUrl containerQml()
{
    return QUrl(QStringLiteral("qrc:/decorations/DecorationDemoContainer.qml"));
}
QUrl emptyContainerQml()
{
    return QUrl(QStringLiteral("qrc:/decorations/DecorationDemoEmpty.qml"));
}
QUrl marginQml()
{
    return QUrl(QStringLiteral("qrc:/decorations/DecorationDemoMargin.qml"));
}

// Warnings the shell emits while this suite runs, captured the way
// tests/test_shell.cpp captures them and for the same reason: QML reports a
// binding it could not resolve as a warning and then carries on with an
// undefined value, so a decoration seam that wired up to nothing would
// otherwise pass every assertion below about heights that are zero.
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

// One long paragraph, guaranteed to wrap several times in the editor column,
// so a margin glyph has more than one line to be beside.
QString wrappingParagraph()
{
    QString text;
    for (int i = 0; i < 60; ++i)
        text += QStringLiteral("wrapping paragraph text ");
    return text.trimmed();
}

// A paragraph whose display text and markdown differ, so a span addressed in
// display coordinates lands somewhere the markdown offset would not:
// "This is **bold** text" renders as "This is bold text", putting "bold" at
// display 8 and at markdown 10.
const QString kMarkedParagraph = QStringLiteral("This is **bold** text");
constexpr int kMarkedStart = 8;   // "bold" in the DISPLAY text
constexpr int kMarkedLength = 4;

QColor washColor()
{
    return QColor(QStringLiteral("#5533aa"));
}
QColor outlineColor()
{
    return QColor(QStringLiteral("#cc4400"));
}

} // namespace

// What a linked module may draw inside the document view, driven through the
// shipped shell rather than through a mirror of it.
//
// The seam has three parts and this suite exercises all three with one
// demonstration module: a container drawn between two blocks, a glyph in the
// reserved margin column beside one visual line of one block, and the pinned
// strip above the scrolling document. The properties worth defending are that
// none of it reaches the document — the model's indices and count are what
// they would have been, an edit still round-trips byte for byte, and the undo
// stack neither records a decoration nor is deepened by one.
class TestDecorationShell : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));

        // Installed before the shell loads, exactly as a module's main()
        // would: the header slot resolves as the Loader is created.
        m_context->extensions()->install(std::make_unique<DemoModule>());
        // And the module reserves its margin column once, at startup, rather
        // than when its first glyph appears — so no text ever moves sideways
        // under the reader.
        decorations()->setMarginColumnReserved(true);

        m_context->installContextProperties(&m_engine);

        g_warnings.clear();
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!m_engine.rootObjects().isEmpty());
        m_warningsAfterLoad = g_warnings.size();
    }

    void cleanupTestCase()
    {
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        // Each case starts from a three-paragraph note and no registrations.
        decorations()->clear();
        BlockModel *model = blocks();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Paragraph, QStringLiteral("first block"));
        model->insertBlock(1, Block::Paragraph, QStringLiteral("second block"));
        model->insertBlock(2, Block::Paragraph, QStringLiteral("third block"));
        undo()->clear();
        QCoreApplication::processEvents();

        // And from a note the view has finished laying out. A case that
        // leaves a long wrapping paragraph in block 0 is followed by a row
        // that still reports the old height for a turn or two, and a case
        // that measures a row before decorating it measures that stale
        // number. It shows on a real display rather than offscreen, where
        // the first process events happen to be enough.
        QTRY_COMPARE(row(0) ? row(0)->property("textLineCount").toInt() : -1, 1);
    }

    void theShellLoadsWithTheModuleInstalled()
    {
        if (m_warningsAfterLoad > 0) {
            QFAIL(qPrintable(QStringLiteral("Loading the shell with a module "
                                            "installed produced warnings:\n  ")
                             + g_warnings.mid(0, m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

    // C1c. The fourth slot: a strip above the document that stays put while
    // the document scrolls under it.
    void thePinnedHeaderStripIsFilled()
    {
        QQuickItem *header = item("extensionDocumentHeader");
        QVERIFY(header);
        QCOMPARE(header->property("source").toString(),
                 QStringLiteral("qrc:/decorations/DecorationDemoHeader.qml"));
        QVERIFY(header->property("active").toBool());
        QTRY_COMPARE(header->height(), 24.0);

        QVERIFY(item("demoDocumentHeader"));

        // The strip is outside the scrolling area, and the scrolling area
        // reserves exactly its height, so the document begins below it rather
        // than under it.
        QQuickItem *scroll = item("editorScrollView");
        QVERIFY(scroll);
        QVERIFY(scroll->y() >= header->y() + header->height());
    }

    // C1a. The container is drawn after its block and sized by its content.
    //
    // The registration lands in the moment after the blocks were inserted,
    // which is deliberate: it is when a module places its anchors as a note
    // opens, and it used to be the one moment the row below stayed
    // overlapped, because a ListView drops a delegate's size change while one
    // of its own add transitions is running. See the note where that
    // transition used to be, in qml/main.qml.
    void aContainerIsDrawnAfterItsBlockAndSizesToItsContent()
    {
        QQuickItem *first = waitForRow(0);
        QVERIFY(first);
        const qreal undecorated = first->height();

        decorations()->addContainer(QStringLiteral("demo"), 0, containerQml());
        QTRY_COMPARE(rowHeight(0), undecorated + 40.0);

        QQuickItem *box = childItem(row(0), QStringLiteral("demoDecorationBox"));
        QVERIFY(box);
        QCOMPARE(box->height(), 40.0);
        // Between the two blocks: below the first row's own content, and
        // above the second row.
        QCOMPARE(box->mapToItem(row(0), QPointF(0, 0)).y(), undecorated);
        QTRY_VERIFY(rowY(1) >= rowY(0) + rowHeight(0));
    }

    // The same registration through the geometry queries a module actually
    // uses, since it cannot see the Qt Quick items from its own code.
    void aModuleCanLearnWhereItsContainerLanded()
    {
        const QString id =
            decorations()->addContainer(QStringLiteral("demo"), 1, containerQml());
        // The container exists a turn before the row it grew has been laid
        // out again, and a module asking for geometry between those two
        // moments is asking about a row mid-change. Waiting for the row to
        // account for it is what the assertions below are about.
        QTRY_COMPARE(decorations()->containerGeometry(id).height(), 40.0);
        QTRY_COMPARE(decorations()->blockGeometry(1).bottom(),
                     decorations()->containerGeometry(id).bottom());

        const QRectF block = decorations()->blockGeometry(1);
        const QRectF container = decorations()->containerGeometry(id);
        QVERIFY(!block.isNull());
        QVERIFY(!container.isNull());
        // The container sits inside the row it belongs to, at its foot.
        QVERIFY(container.top() >= block.top());

        // An id nobody registered, and a row the list has not built, are the
        // same answer: nothing, rather than a guess.
        QVERIFY(decorations()->containerGeometry(QStringLiteral("nope")).isNull());
        QVERIFY(decorations()->blockGeometry(999).isNull());
    }

    // A container that has nothing to show costs the reader nothing.
    void aContainerWithNoContentTakesNoVerticalSpace()
    {
        QQuickItem *first = waitForRow(0);
        QVERIFY(first);
        const qreal undecorated = first->height();
        decorations()->addContainer(QStringLiteral("demo"), 0, emptyContainerQml());
        QTRY_VERIFY(childItem(row(0), QStringLiteral("demoEmptyDecoration")));
        QCOMPARE(rowHeight(0), undecorated);
    }

    // Placement is dynamic: the module moves its anchors as the document
    // changes, and the drawn container follows without being re-registered.
    void movingAContainerMovesWhatIsDrawn()
    {
        const QString id =
            decorations()->addContainer(QStringLiteral("demo"), 0, containerQml());
        QTRY_VERIFY(childItem(row(0), QStringLiteral("demoDecorationBox")));

        decorations()->setContainerBlock(id, 2);
        QTRY_VERIFY(childItem(row(2), QStringLiteral("demoDecorationBox")));
        QVERIFY(!childItem(row(0), QStringLiteral("demoDecorationBox")));

        decorations()->removeContainer(id);
        QTRY_VERIFY(!childItem(row(2), QStringLiteral("demoDecorationBox")));
    }

    // The property the whole design rests on: a container is rendered, never
    // inserted. Consumers store anchors as block indices, and a container
    // that renumbered the blocks below it would invalidate every stored
    // anchor after the first.
    void containersLeaveTheModelsIndicesAlone()
    {
        BlockModel *model = blocks();
        const int countBefore = model->count();
        QStringList contentBefore;
        for (int i = 0; i < countBefore; ++i)
            contentBefore << model->getContent(i);

        decorations()->addContainer(QStringLiteral("demo"), 0, containerQml());
        decorations()->addContainer(QStringLiteral("demo"), 1, containerQml());
        decorations()->addMarginItem(QStringLiteral("demo"), 1, 0, marginQml());
        QTRY_VERIFY(childItem(row(1), QStringLiteral("demoDecorationBox")));

        QCOMPARE(model->count(), countBefore);
        for (int i = 0; i < countBefore; ++i)
            QCOMPARE(model->getContent(i), contentBefore.at(i));

        // And each row is still the row of the block with its own index: the
        // rows are what the shell's own navigation and focus arithmetic
        // indexes into.
        for (int i = 0; i < countBefore; ++i)
            QCOMPARE(row(i)->property("index").toInt(), i);
    }

    // The undo stack never sees a decoration. Registering, moving and
    // removing containers is not an edit, and an edit made with containers
    // present still round-trips byte for byte.
    void decorationsNeitherRecordNorDeepenAnUndoStep()
    {
        BlockModel *model = blocks();
        DocumentSerializer serializer;
        const QString before = serializer.serialize(model);
        const int depthBefore = undo()->count();

        const QString id =
            decorations()->addContainer(QStringLiteral("demo"), 0, containerQml());
        decorations()->setContainerBlock(id, 1);
        decorations()->addMarginItem(QStringLiteral("demo"), 0, 0, marginQml());
        QTRY_VERIFY(childItem(row(1), QStringLiteral("demoDecorationBox")));

        QCOMPARE(undo()->count(), depthBefore);
        QCOMPARE(undo()->canUndo(), depthBefore > 0);

        // One edit, with the decorations drawn, undone: one step, and the
        // document is what it was down to the byte.
        model->updateContent(1, QStringLiteral("second block, edited"));
        QCOMPARE(undo()->count(), depthBefore + 1);
        undo()->undo();
        QCOMPARE(serializer.serialize(model), before);
        QCOMPARE(undo()->count(), depthBefore + 1);

        // Taking the decorations back afterwards is not a step either.
        decorations()->removeAll(QStringLiteral("demo"));
        QCOMPARE(undo()->count(), depthBefore + 1);
    }

    // Block-to-block movement is index arithmetic over the model's rows —
    // `itemAtIndex(index + 1)`, in the delegates' key handling — so a
    // container between two blocks is not on the path at all. The caret is
    // never routed into a container and never trapped in one; a module's
    // content is reached by clicking it or by the ordinary tab chain.
    void keyboardNavigationCrossesADecoratedBoundary()
    {
        decorations()->addContainer(QStringLiteral("demo"), 1, containerQml());
        QTRY_VERIFY(childItem(row(1), QStringLiteral("demoDecorationBox")));

        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(window, "focusBlockAtIndex",
                                          Q_ARG(QVariant, 2), Q_ARG(QVariant, false),
                                          Q_ARG(QVariant, QString())));

        QQuickItem *list = item("blockListView");
        QVERIFY(list);
        QTRY_COMPARE(list->property("currentIndex").toInt(), 2);
        // The row the step landed on is the block after the decorated one,
        // and it sits below the container rather than behind it.
        QCOMPARE(row(2)->property("index").toInt(), 2);
        QTRY_VERIFY(rowY(2) >= rowY(1) + rowHeight(1));
    }

    // C1b. The column is reserved for the run, so the first glyph to
    // appear moves no text sideways.
    void theMarginColumnIsReservedWhetherOrNotAnythingIsInIt()
    {
        QQuickItem *list = item("blockListView");
        QVERIFY(list);
        QQuickItem *first = waitForRow(0);
        QVERIFY(first);

        const qreal reserved = first->property("marginColumnWidth").toReal();
        QVERIFY(reserved > 0);
        QCOMPARE(first->width(), list->width() - reserved);

        // Drawing into it changes no row's width.
        decorations()->addMarginItem(QStringLiteral("demo"), 0, 0, marginQml());
        QTRY_VERIFY(childItem(row(0), QStringLiteral("demoMarginGlyph")));
        QCOMPARE(rowWidth(0), list->width() - reserved);
    }

    // A glyph is addressed by block AND line, and lands beside the wrapped
    // line it names rather than beside the paragraph.
    void aMarginGlyphLandsBesideItsOwnLine()
    {
        BlockModel *model = blocks();
        model->updateContent(0, wrappingParagraph());
        QTRY_VERIFY(row(0)->property("textLineCount").toInt() >= 3);

        const int line = 2;
        decorations()->addMarginItem(QStringLiteral("demo"), 0, line, marginQml());
        QQuickItem *glyph = nullptr;
        QTRY_VERIFY(glyph = childItem(row(0), QStringLiteral("demoMarginGlyph")));

        const qreal lineHeight = row(0)->property("textLineHeight").toReal();
        const qreal origin = row(0)->property("textLineOrigin").toReal();
        QVERIFY(lineHeight > 0);

        // Inside the reserved column, beside the third line.
        QQuickItem *column = childItem(row(0), QStringLiteral("blockMarginColumn"));
        QVERIFY(column);
        QCOMPARE(column->x(), row(0)->width());
        const qreal glyphTop = glyph->mapToItem(row(0), QPointF(0, 0)).y();
        QCOMPARE(glyphTop, origin + line * lineHeight);

        // And the same position a module would read out of the geometry
        // query, which is what it uses to decide the glyph belongs there.
        const QRectF geometry = decorations()->lineGeometry(0, line);
        QVERIFY(!geometry.isNull());
        QCOMPARE(geometry.top(), row(0)->y() + glyphTop);
        QCOMPARE(geometry.height(), lineHeight);
        // Line 0 is a different place, or the addressing would be a fiction.
        QVERIFY(decorations()->lineGeometry(0, 0).top() < geometry.top());
    }

    // Two modules decorating one document. Neither sees the other's entries
    // taken back, which is what lets a second module be installed at all.
    void twoModulesDecorateTheSameBlockWithoutColliding()
    {
        QQuickItem *first = waitForRow(0);
        QVERIFY(first);
        const qreal undecorated = first->height();

        decorations()->addContainer(QStringLiteral("first"), 0, containerQml());
        decorations()->addContainer(QStringLiteral("second"), 0, containerQml());
        QTRY_COMPARE(childItems(row(0), QStringLiteral("demoDecorationBox")).size(), 2);
        // Stacked, not overlapping: the row makes room for both.
        QTRY_COMPARE(rowHeight(0), undecorated + 80.0);

        decorations()->removeAll(QStringLiteral("second"));
        QTRY_COMPARE(childItems(row(0), QStringLiteral("demoDecorationBox")).size(), 1);
    }

    // C3. A marked range inside a block: the editor paints a wash behind the
    // characters the module named, addressed the way a search hit is —
    // display coordinates, the text with the hidden markers taken out. This
    // paragraph's display text and markdown differ by two characters at the
    // marked word, so an implementation that took the offset for a markdown
    // one would paint "ld t" instead of "bold".
    void aWashLandsOnTheCharactersItNames()
    {
        BlockModel *model = blocks();
        model->updateContent(0, kMarkedParagraph);
        decorations()->addSpan(QStringLiteral("demo"), 0, kMarkedStart,
                               kMarkedLength, DocumentDecorations::Wash,
                               washColor());

        QTRY_COMPARE(editorText(0), QStringLiteral("This is bold text"));
        QTRY_COMPARE(backgroundAt(0, kMarkedStart), washColor());

        // Exactly those characters: the space before the word and the one
        // after it are untinted, and the tinted run reads "bold".
        QCOMPARE(backgroundAt(0, kMarkedStart + kMarkedLength - 1), washColor());
        QVERIFY(!backgroundAt(0, kMarkedStart - 1).isValid());
        QVERIFY(!backgroundAt(0, kMarkedStart + kMarkedLength).isValid());
        QCOMPARE(editorText(0).mid(kMarkedStart, kMarkedLength),
                 QStringLiteral("bold"));

        // And the markdown offset the same characters have is a different
        // number, which is what the case is about.
        QCOMPARE(model->getContent(0).indexOf(QStringLiteral("bold")), 10);
    }

    // The two channels compose on one range: the wash is a background and the
    // outline a border, so a categorical mark and a "this is the current one"
    // mark can sit on the same words without either hiding the other.
    void aWashAndAnOutlineCoverTheSameCharacters()
    {
        blocks()->updateContent(0, kMarkedParagraph);
        decorations()->addSpan(QStringLiteral("demo"), 0, kMarkedStart,
                               kMarkedLength, DocumentDecorations::Wash,
                               washColor());
        decorations()->addSpan(QStringLiteral("demo"), 0, kMarkedStart,
                               kMarkedLength, DocumentDecorations::Outline,
                               outlineColor());

        QTRY_COMPARE(backgroundAt(0, kMarkedStart), washColor());

        QList<QQuickItem *> outlines;
        QTRY_VERIFY((outlines = visibleOutlines(0)).size() == 1);
        QCOMPARE(outlines.first()->property("border")
                     .value<QObject *>()->property("color").value<QColor>(),
                 outlineColor());
        QVERIFY(outlines.first()->width() > 0);
        QVERIFY(outlines.first()->height() > 0);
    }

    // Overlap and nesting are ordinary. Both outlines are drawn, one box
    // inside the other, rather than one replacing the other.
    void overlappingSpansAreBothDrawn()
    {
        blocks()->updateContent(0, kMarkedParagraph);
        decorations()->addSpan(QStringLiteral("demo"), 0, 0, 12,
                               DocumentDecorations::Outline, outlineColor());
        decorations()->addSpan(QStringLiteral("demo"), 0, kMarkedStart,
                               kMarkedLength, DocumentDecorations::Outline,
                               washColor());

        QList<QQuickItem *> outlines;
        QTRY_VERIFY((outlines = visibleOutlines(0)).size() == 2);
        // The nested one is inside the wider one, which is what "both drawn"
        // has to mean for a reader looking at the block.
        QVERIFY(outlines.at(1)->width() < outlines.at(0)->width());
        QVERIFY(outlines.at(1)->x() > outlines.at(0)->x());
    }

    // Placement is dynamic: the module recomputes its anchors after an edit
    // and moves a span by its id, without tearing it down.
    void aSpanMovedAfterAnEditLandsOnItsNewPosition()
    {
        blocks()->updateContent(0, kMarkedParagraph);
        const QString id = decorations()->addSpan(
            QStringLiteral("demo"), 0, kMarkedStart, kMarkedLength,
            DocumentDecorations::Wash, washColor());

        QTRY_COMPARE(backgroundAt(0, kMarkedStart), washColor());

        // Six characters go in ahead of the marked word; the module works
        // out where its anchor is now and says so.
        blocks()->updateContent(0, QStringLiteral("Well, ") + kMarkedParagraph);
        QVERIFY(decorations()->setSpanRange(id, 0, kMarkedStart + 6,
                                            kMarkedLength));

        QTRY_COMPARE(backgroundAt(0, kMarkedStart + 6), washColor());
        QVERIFY(!backgroundAt(0, kMarkedStart).isValid());
        QCOMPARE(editorText(0).mid(kMarkedStart + 6, kMarkedLength),
                 QStringLiteral("bold"));
    }

    // Nothing enters the document. A note carrying spans round-trips byte for
    // byte, and the undo stack is the depth it would have been.
    void spansNeitherRecordNorDeepenAnUndoStep()
    {
        BlockModel *model = blocks();
        DocumentSerializer serializer;
        model->updateContent(0, kMarkedParagraph);
        undo()->clear();
        const QString before = serializer.serialize(model);
        const int countBefore = model->count();

        const QString id = decorations()->addSpan(
            QStringLiteral("demo"), 0, kMarkedStart, kMarkedLength,
            DocumentDecorations::Wash | DocumentDecorations::Outline,
            washColor());
        QTRY_VERIFY(editorDocument(0));
        QTRY_COMPARE(visibleOutlines(0).size(), 1);

        QCOMPARE(undo()->count(), 0);
        QCOMPARE(model->count(), countBefore);
        QCOMPARE(serializer.serialize(model), before);

        // An edit made with the span drawn, then undone: one step, and the
        // note is what it was down to the byte.
        model->updateContent(1, QStringLiteral("second block, edited"));
        QCOMPARE(undo()->count(), 1);
        undo()->undo();
        QCOMPARE(serializer.serialize(model), before);
        QCOMPARE(undo()->count(), 1);

        decorations()->setSpanRange(id, 0, 0, 4);
        decorations()->removeSpan(id);
        QCOMPARE(undo()->count(), 1);
        QCOMPARE(serializer.serialize(model), before);
    }

    // The core draws what it is handed. A block with no text of its own has
    // no characters to mark, and says so rather than guessing at a position.
    void aSpanOnABlockWithNoTextDrawsNothing()
    {
        BlockModel *model = blocks();
        model->insertBlock(3, Block::Divider, QString());
        QTRY_VERIFY(row(3));

        const QString id = decorations()->addSpan(
            QStringLiteral("demo"), 3, 0, 5,
            DocumentDecorations::Wash | DocumentDecorations::Outline,
            washColor());
        QCoreApplication::processEvents();

        QVERIFY(visibleOutlines(3).isEmpty());
        QVERIFY(decorations()->spanRects(id).isEmpty());
        QCOMPARE(model->count(), 4);
    }

    // Where a span was drawn, through the query a module actually uses: it
    // cannot see a Qt Quick item, and it anchors surfaces of its own to
    // marked text. The answer is in the block list's content coordinates,
    // the same space the other three geometry answers are in.
    void aModuleCanLearnWhereItsSpanLanded()
    {
        blocks()->updateContent(0, kMarkedParagraph);
        const QString id = decorations()->addSpan(
            QStringLiteral("demo"), 0, kMarkedStart, kMarkedLength,
            DocumentDecorations::Outline, outlineColor());

        QList<QQuickItem *> outlines;
        QTRY_VERIFY((outlines = visibleOutlines(0)).size() == 1);

        QVariantList rects;
        QTRY_VERIFY(!(rects = decorations()->spanRects(id)).isEmpty());
        QCOMPARE(rects.size(), 1);
        const QRectF marked = rects.first().toRectF();
        const QRectF block = decorations()->blockGeometry(0);
        QVERIFY(!block.isNull());

        // The same rectangle the outline was drawn at, lifted out of the row
        // into the list's coordinates.
        const QPointF drawn = outlines.first()->mapToItem(row(0), QPointF(0, 0));
        QCOMPARE(marked.left(), row(0)->x() + drawn.x());
        QCOMPARE(marked.top(), row(0)->y() + drawn.y());
        QCOMPARE(marked.width(), outlines.first()->width());
        // Inside the block it marks, and to the right of where the line
        // starts, since it marks the third word rather than the first.
        QVERIFY(block.contains(marked.center()));
        QVERIFY(marked.left() > block.left());

        // An id nobody registered is nothing, rather than a guess.
        QVERIFY(decorations()->spanRects(QStringLiteral("nope")).isEmpty());
    }

    // A marked block renders through the editing engine rather than through
    // the lightweight read-only shell, because the wash is painted by the
    // engine's highlighter and that is the one place it is implemented. The
    // row goes back to the shell when the module takes its span away, so a
    // note nobody marked pays nothing for the capability.
    void aMarkedBlockRendersThroughTheEditorAndGoesBackAfterwards()
    {
        // A block nothing has touched yet, because a row that has already
        // been edited or clicked in keeps its editor for reasons of its own
        // and would say nothing about spans.
        BlockModel *model = blocks();
        const int index = model->count();
        model->insertBlock(index, Block::Paragraph,
                           QStringLiteral("an untouched paragraph"));
        QVERIFY(waitForRow(index));
        QTRY_VERIFY(row(index)->property("useReadOnlyShell").toBool());
        QVERIFY(!row(index)->property("hasDecorationSpans").toBool());

        const QString id = decorations()->addSpan(
            QStringLiteral("demo"), index, 0, 2, DocumentDecorations::Wash,
            washColor());
        QTRY_VERIFY(row(index)->property("hasDecorationSpans").toBool());
        QTRY_VERIFY(!row(index)->property("useReadOnlyShell").toBool());
        QTRY_VERIFY(row(index)->property("editorLoaderActive").toBool());
        QTRY_COMPARE(backgroundAt(index, 0), washColor());

        decorations()->removeSpan(id);
        QTRY_VERIFY(!row(index)->property("hasDecorationSpans").toBool());
        QTRY_VERIFY(row(index)->property("useReadOnlyShell").toBool());
    }

    void noWarningsAppearedWhileDecorating()
    {
        if (g_warnings.size() > m_warningsAfterLoad) {
            QFAIL(qPrintable(QStringLiteral("Decorating the document produced "
                                            "warnings:\n  ")
                             + g_warnings.mid(m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

private:
    BlockModel *blocks() { return m_context->blockModel(); }
    UndoStack *undo() { return m_context->undoStack(); }
    DocumentDecorations *decorations() { return m_context->documentDecorations(); }

    QQuickItem *item(const QString &objectName)
    {
        QObject *window = m_engine.rootObjects().value(0);
        return window ? window->findChild<QQuickItem *>(objectName) : nullptr;
    }

    // The delegate for a model row, or null while the list has yet to build
    // it. Every geometry case waits on this rather than assuming.
    QQuickItem *row(int index)
    {
        QQuickItem *list = item(QStringLiteral("blockListView"));
        if (!list)
            return nullptr;
        QQuickItem *result = nullptr;
        if (!QMetaObject::invokeMethod(list, "itemAtIndex",
                                       Q_RETURN_ARG(QQuickItem *, result),
                                       Q_ARG(int, index)))
            return nullptr;
        return result;
    }

    // The same row, waited for: the list builds a delegate a turn or two
    // after the model changes, so every geometry case starts here.
    QQuickItem *waitForRow(int index)
    {
        QQuickItem *result = nullptr;
        for (int attempt = 0; attempt < 200 && !result; ++attempt) {
            result = row(index);
            if (!result)
                QTest::qWait(10);
        }
        return result;
    }

    // The QTextDocument the row's editor laid out, or null while the row is
    // still on the lightweight shell. A marked block always latches, so this
    // is what every span case waits on.
    QTextDocument *editorDocument(int index)
    {
        QQuickItem *area = childItem(row(index), QStringLiteral("blockTextArea"));
        if (!area)
            return nullptr;
        auto *handle = qvariant_cast<QQuickTextDocument *>(
            area->property("textDocument"));
        return handle ? handle->textDocument() : nullptr;
    }

    // The text one row's editor is showing, empty while it has none.
    QString editorText(int index)
    {
        QTextDocument *doc = editorDocument(index);
        return doc ? doc->toPlainText() : QString();
    }

    // The background the layout actually renders at a position, or an
    // invalid color where it renders none.
    //
    // The document is looked up on every call rather than held: a row that
    // takes an edit rebuilds its editor, and a QTextDocument captured before
    // that is freed memory by the time the next attempt of a QTRY_ loop runs.
    QColor backgroundAt(int index, int pos)
    {
        QTextDocument *doc = editorDocument(index);
        if (!doc || !doc->firstBlock().layout())
            return QColor();
        const auto formats = doc->firstBlock().layout()->formats();
        for (const auto &range : formats) {
            if (pos < range.start || pos >= range.start + range.length)
                continue;
            if (range.format.background().style() != Qt::NoBrush)
                return range.format.background().color();
        }
        return QColor();
    }

    // The outline boxes drawn over one row's text. The layer carries an item
    // per marked run and hides the ones that are wash-only, so the visible
    // ones are what a reader sees.
    QList<QQuickItem *> visibleOutlines(int index)
    {
        QList<QQuickItem *> visible;
        const QList<QQuickItem *> drawn =
            childItems(row(index), QStringLiteral("decorationSpanOutline"));
        for (QQuickItem *item : drawn) {
            if (item->isVisible())
                visible.append(item);
        }
        return visible;
    }

    // Geometry of a row that may not exist yet, for the QTRY_ macros: a
    // negative answer means "not built", which no real geometry is.
    qreal rowHeight(int index)
    {
        QQuickItem *r = row(index);
        return r ? r->height() : -1;
    }
    qreal rowWidth(int index)
    {
        QQuickItem *r = row(index);
        return r ? r->width() : -1;
    }
    qreal rowY(int index)
    {
        QQuickItem *r = row(index);
        return r ? r->y() : -1;
    }

    // Search one row's VISUAL tree.
    //
    // Not findChild: an item a Repeater created is owned by the delegate
    // model rather than by the item it is drawn inside, so it is a visual
    // child of the row and not a QObject child of anything under it. A row's
    // visual subtree holds its own decorations and nobody else's, since a
    // delegate is a child of the list's content item rather than of another
    // delegate.
    static void collectItems(QQuickItem *parent, const QString &objectName,
                             QList<QQuickItem *> &found)
    {
        if (!parent)
            return;
        const QList<QQuickItem *> children = parent->childItems();
        for (QQuickItem *child : children) {
            if (child->objectName() == objectName)
                found.append(child);
            collectItems(child, objectName, found);
        }
    }
    static QQuickItem *childItem(QQuickItem *parent, const QString &objectName)
    {
        QList<QQuickItem *> found;
        collectItems(parent, objectName, found);
        return found.value(0);
    }
    static QList<QQuickItem *> childItems(QQuickItem *parent,
                                          const QString &objectName)
    {
        QList<QQuickItem *> found;
        collectItems(parent, objectName, found);
        return found;
    }

    QTemporaryDir m_dir;
    QQmlApplicationEngine m_engine;
    std::unique_ptr<AppContext> m_context;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestDecorationShell)
#include "test_decorationshell.moc"
