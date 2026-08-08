// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// A scriptable launcher for the real editor shell, used to capture what the
// running application actually renders.
//
// It composes the app exactly as src/main.cpp does — same KvitApplication,
// same QML shell, same context objects — then drives the live window with real
// key events and saves frames with QQuickWindow::grabWindow(). Nothing here is
// a test double: the point is to observe the shipped UI, on a scenario named
// by --scenario, rather than to assert anything.
//
// Not built by default; configure with -DKVIT_UI_DRIVER=ON.

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMimeData>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest/QTest>

#include <functional>

#include "appcontext.h"
#include "blockmodel.h"
#include "extensionregistry.h"
#include "documentmanager.h"
#include "kvitapplication.h"
#include "notecollection.h"
#include "vaultwindow.h"
#include "windowregistry.h"

namespace {

// The driver stages a single window; the registry's active window is it.
QQuickWindow *shellWindow(KvitApplication &kvit)
{
    VaultWindow *w = kvit.registry() ? kvit.registry()->activeWindow() : nullptr;
    return w ? w->window() : nullptr;
}

AppContext *activeContext(KvitApplication &kvit)
{
    VaultWindow *w = kvit.registry() ? kvit.registry()->activeWindow() : nullptr;
    return w ? w->context() : nullptr;
}

void settle(int ms)
{
    QTest::qWait(ms);
}

void grab(QQuickWindow *window, const QString &path)
{
    settle(400);
    const QImage frame = window->grabWindow();
    if (frame.isNull() || !frame.save(path))
        qWarning("uidriver: could not save %s", qPrintable(path));
    else
        qInfo("uidriver: wrote %s (%dx%d)", qPrintable(path),
              frame.width(), frame.height());
}

// Take the pointer off the shell, so nothing renders hovered in the frame
// about to be grabbed. A synthetic move to a point outside the window is what
// does it: hover tracking follows delivered events, and warping the real
// cursor away is not portable (Wayland refuses it without the pointer-warp
// protocol, which is the capture environment here).
void clearHover(QQuickWindow *window)
{
    QTest::mouseMove(window, QPoint(-20, -20));
}

// Click a block in the live editor list, so focus lands where a user's click
// would put it. Finds the real delegate through the ListView rather than
// guessing at pixel coordinates.
void clickEditorBlock(QQuickWindow *window, int index)
{
    auto *list = window->findChild<QQuickItem *>(QStringLiteral("blockListView"));
    if (!list) {
        qWarning("uidriver: no blockListView");
        return;
    }
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Qt::DirectConnection,
                              Q_RETURN_ARG(QQuickItem *, item),
                              Q_ARG(int, index));
    if (!item) {
        qWarning("uidriver: no delegate at %d", index);
        return;
    }
    const QPointF center =
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      center.toPoint(), 50);
    qInfo("uidriver: clicked block %d at (%.0f, %.0f)", index,
          center.x(), center.y());
}

// Type into whatever currently has focus in the live window.
void type(QQuickWindow *window, const QString &text)
{
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1(), Qt::NoModifier, 12);
    }
}

// ---------------------------------------------------------------------------
// Tour helpers. These exist for the demo recordings (the `tour-*` scenarios),
// where a human watches the result, so they trade the stills' directness for
// pacing and for a pointer that is visible in a screen capture.
// ---------------------------------------------------------------------------

// Find a live item by objectName, walking the visual tree rather than the
// QObject one.
//
// findChild is not enough here and the difference is easy to trip over: an
// item declared in the shell's own QML (blockListView, say) is a QObject
// child of the window and findChild reaches it, while an item a ListView
// delegate instantiated has its QObject parent set by the delegate's creation
// context and is invisible to that search. Every per-block item this file
// wants — the diagram canvas, its flick area, the copy chip — is of the
// second kind.
QQuickItem *namedItem(QQuickWindow *window, const char *name)
{
    const QString wanted = QLatin1String(name);
    std::function<QQuickItem *(QQuickItem *)> walk =
        [&](QQuickItem *item) -> QQuickItem * {
        if (!item)
            return nullptr;
        if (item->objectName() == wanted)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *hit = walk(child))
                return hit;
        }
        return nullptr;
    };
    if (QQuickItem *hit = walk(window->contentItem()))
        return hit;
    return window->findChild<QQuickItem *>(wanted);
}

// The same search, scoped to one delegate's subtree. A block's editor is
// called `blockTextArea` in every delegate, so searching the window finds
// whichever one happens to come first rather than the one being driven.
QQuickItem *namedItemIn(QQuickItem *root, const char *name)
{
    const QString wanted = QLatin1String(name);
    std::function<QQuickItem *(QQuickItem *)> walk =
        [&](QQuickItem *item) -> QQuickItem * {
        if (!item)
            return nullptr;
        if (item->objectName() == wanted)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *hit = walk(child))
                return hit;
        }
        return nullptr;
    };
    return walk(root);
}

// Every item under `root` with this objectName, in visual-tree order. A board
// has three columns and four cards all called `kanbanCard`, and the tour has
// to aim at one of them in particular rather than at whichever the search
// happens to reach first.
QList<QQuickItem *> namedItemsIn(QQuickItem *root, const char *name)
{
    const QString wanted = QLatin1String(name);
    QList<QQuickItem *> found;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item)
            return;
        if (item->objectName() == wanted && item->isVisible()
            && item->width() > 0)
            found.append(item);
        for (QQuickItem *child : item->childItems())
            walk(child);
    };
    walk(root);
    return found;
}

// A menu row named by the words on it. Menu entries are not given object
// names — several are built by a Repeater from the theme list, and a submenu's
// own row is built by Qt — so the label is the only handle. The `&` that marks
// the access key is written into the text on the platforms that show one, and
// is dropped here so a caller can ask for "Theme".
QQuickItem *itemWithLabel(QQuickWindow *window, const QString &label)
{
    std::function<QQuickItem *(QQuickItem *)> walk =
        [&](QQuickItem *item) -> QQuickItem * {
        if (!item)
            return nullptr;
        const QVariant text = item->property("text");
        if (text.isValid() && item->isVisible() && item->width() > 0) {
            const QString plain =
                text.toString().remove(QLatin1Char('&')).trimmed();
            if (plain == label)
                return item;
        }
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *hit = walk(child))
                return hit;
        }
        return nullptr;
    };
    return walk(window->contentItem());
}

QPoint centerOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2))
        .toPoint();
}

// Move the real pointer to a scene point along a short eased path, delivering
// synthetic moves as it goes.
//
// Both halves are needed and neither substitutes for the other. Hover, press
// and drag state inside the window follow delivered events, so the synthetic
// moves are what actually drives the interface; the pointer a screen recorder
// captures is the operating system's own, so warping it is what gives the
// viewer a visible cause for what happens. Warping is refused on Wayland
// without the pointer-warp protocol, where the interface still responds and
// only the drawn cursor stays put. The recordings are made on Windows, where
// both work.
void glide(QQuickWindow *window, const QPoint &to, int steps = 20,
           int msPerStep = 12)
{
    // Where the pointer starts is not always answerable: at the beginning of
    // a session it is wherever the user left it, and a platform without a
    // real cursor (the VNC plugin the suites use) reports a position that is
    // not in any window at all. Either way, interpolating from it produces
    // coordinates far outside the window, so an implausible origin is
    // replaced by the window's centre, which also gives the viewer a path
    // that starts somewhere sensible.
    QPoint from = window->mapFromGlobal(QCursor::pos());
    if (!QRect(QPoint(0, 0), window->size()).contains(from))
        from = QPoint(int(window->width() / 2), int(window->height() / 2));
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        const qreal eased = 1.0 - (1.0 - t) * (1.0 - t);  // arrive, don't stop dead
        const QPoint p(qRound(from.x() + (to.x() - from.x()) * eased),
                       qRound(from.y() + (to.y() - from.y()) * eased));
        QCursor::setPos(window->mapToGlobal(p));
        QTest::mouseMove(window, p, msPerStep);
    }
}

void clickAt(QQuickWindow *window, const QPoint &scenePos, int settleMs = 400)
{
    glide(window, scenePos);
    settle(150);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scenePos, 60);
    settle(settleMs);
}

// Drag slowly enough to be followed, with a beat on each end.
//
// A drag that a test would do in a few hundred milliseconds reads on video as
// the object teleporting: the viewer sees the result without seeing the grab,
// the travel, or the release. The pauses are what separate those three, and
// the easing keeps the start and the stop from looking mechanical.
void dragFromTo(QQuickWindow *window, const QPoint &from, const QPoint &to,
                int steps = 45, int msPerStep = 40)
{
    glide(window, from);
    settle(700);   // rest on the target, so it shows itself as grabbable
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from, 60);
    settle(600);   // the grab, before anything moves
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        const qreal eased = t * t * (3.0 - 2.0 * t);   // smooth both ends
        const QPoint p(qRound(from.x() + (to.x() - from.x()) * eased),
                       qRound(from.y() + (to.y() - from.y()) * eased));
        QCursor::setPos(window->mapToGlobal(p));
        QTest::mouseMove(window, p, msPerStep);
    }
    settle(700);   // arrived, still held
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to, 80);
}

// Type at a pace a viewer can read. Unlike type() above, the character travels
// as the event's text rather than as a key code, so backslashes, braces,
// carets and dollar signs arrive intact, which the TeX in the tour needs.
void typeSlowly(QQuickWindow *window, const QString &text, int msPerChar = 55)
{
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        settle(msPerChar);
    }
}

// A caption band across the bottom of the frame, naming the feature about to
// be shown. It is drawn into the driver's own window rather than into the
// shipped interface: this tool is off by default and absent from a release
// build, so nothing a user runs can grow a caption. Keeping it inside the
// capture rather than adding it in an editor means it re-renders identically
// on every release and no post-production step can forget it.
QQuickItem *g_caption = nullptr;   // the band currently on screen, if any

void showTitle(QQuickWindow *window, const QString &text, int holdMs = 2400)
{
    if (text.isEmpty())
        return;
    // The full tour captions each feature in turn, so the band a previous
    // segment left has to go rather than accumulating invisibly at zero
    // opacity behind the new one.
    delete g_caption;
    g_caption = nullptr;
    QQmlEngine *engine = qmlEngine(window->contentItem());
    if (!engine) {
        qWarning("uidriver: no QML engine for the caption overlay");
        return;
    }
    static const char *kBand = R"(
import QtQuick
Rectangle {
    id: band
    property string label: ""
    property int holdMs: 2400
    color: "#e6101418"
    radius: 8
    opacity: 0
    Text {
        anchors.centerIn: parent
        width: band.width - 64
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: band.label
        color: "#f2f5f7"
        font.pixelSize: 22
    }
    NumberAnimation on opacity { to: 1; duration: 350 }
    Timer { interval: band.holdMs; running: true; onTriggered: fade.start() }
    NumberAnimation { id: fade; target: band; property: "opacity"
                      to: 0; duration: 600 }
}
)";
    auto *component = new QQmlComponent(engine, window);
    // A qrc URL as the base: a scheme the engine knows is what lets the
    // `import QtQuick` inside resolve. An invented scheme leaves the
    // component permanently unready with no error to print.
    component->setData(QByteArray(kBand),
                       QUrl(QStringLiteral("qrc:/kvit-uidriver-caption.qml")));
    if (!component->isReady()) {
        qWarning("uidriver: caption overlay failed (status %d): %s",
                 int(component->status()), qPrintable(component->errorString()));
        return;
    }
    auto *band = qobject_cast<QQuickItem *>(component->beginCreate(
        engine->rootContext()));
    if (!band) {
        qWarning("uidriver: caption overlay is not an Item: %s",
                 qPrintable(component->errorString()));
        return;
    }
    band->setParentItem(window->contentItem());
    band->setProperty("label", text);
    band->setProperty("holdMs", holdMs);
    band->setZ(9999);
    // Explicit geometry rather than anchors: the parent is assigned from C++
    // after creation, which is too late for an anchor binding to resolve.
    const qreal w = window->width();
    band->setWidth(w - 80);
    band->setHeight(64);
    band->setX(40);
    band->setY(window->height() - 104);
    component->completeCreate();
    g_caption = band;
}

bool openNote(AppContext *ctx, const QString &vault, const QString &relPath,
              int settleMs = 2400)
{
    if (vault.isEmpty()) {
        qWarning("uidriver: --vault is required for the tour scenarios");
        return false;
    }
    const QString path = QDir(vault).filePath(relPath);
    if (!QFile::exists(path)) {
        qWarning("uidriver: no note at %s", qPrintable(path));
        return false;
    }
    ctx->documentManager()->open(QUrl::fromLocalFile(path));
    settle(settleMs);
    qInfo("uidriver: opened %s", qPrintable(relPath));
    return true;
}

// The index of the first block whose markdown contains a marker, so a tour
// aims at content rather than at a position that moves when a demo note is
// edited.
int blockContaining(BlockModel *model, const QString &marker)
{
    for (int i = 0; i < model->count(); ++i) {
        if (model->getContent(i).contains(marker))
            return i;
    }
    return -1;
}

// The delegate for a block index, or nullptr when the list has not realised
// it. Virtualisation means an off-screen block has no item at all.
QQuickItem *delegateAt(QQuickWindow *window, int index)
{
    auto *list = namedItem(window, "blockListView");
    if (!list)
        return nullptr;
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Qt::DirectConnection,
                              Q_RETURN_ARG(QQuickItem *, item),
                              Q_ARG(int, index));
    return item;
}

// Bring a block into the viewport and hand back its delegate. A list that
// virtualises has no item at all for an off-screen index, so a click aimed at
// one goes nowhere; positionViewAtIndex is what makes the delegate exist.
QQuickItem *showBlock(QQuickWindow *window, int index, int settleMs = 800)
{
    if (auto *list = namedItem(window, "blockListView")) {
        QMetaObject::invokeMethod(list, "positionViewAtIndex",
                                  Q_ARG(int, index), Q_ARG(int, 1));
        settle(settleMs);
    }
    return delegateAt(window, index);
}

// Put the caret in a block, whichever way works: the delegate when the list
// has realised it, the index when it has not.
void focusBlock(QQuickWindow *window, int index, int settleMs = 500)
{
    if (QQuickItem *d = delegateAt(window, index))
        clickAt(window, centerOf(d), settleMs);
    else
        clickEditorBlock(window, index);
    settle(300);
}

bool tourMermaid(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Release pipeline.md"))) {
        return false;
    }
    auto *canvas = namedItem(window, "diagramReadCanvas");
    if (!canvas) {
        qWarning("uidriver: no diagramReadCanvas in this note");
        return false;
    }
    const int fence = blockContaining(model,
                                      QStringLiteral("flowchart"));
    if (fence >= 0)
        qInfo("uidriver: source before:\n%s",
              qPrintable(model->getContent(fence)));

    // Show the markdown first. Without it the drag reads as a box
    // sliding for no reason: the fence is what changes, and a viewer
    // has to have seen it before to notice that it did. A diagram
    // shows either its source or its render and never both, so this
    // is three beats — the fence, the drag, the fence again — rather
    // than one with a side panel.
    auto *flick = namedItem(window, "diagramReadFlick");
    if (flick) {
        const QPoint edge =
            flick->mapToScene(QPointF(flick->width() - 24,
                                      flick->height() / 2))
                .toPoint();
        clickAt(window, edge, 600);
        settle(3000);           // long enough to read eight lines
        if (QQuickItem *heading = delegateAt(window, 0))
            clickAt(window, centerOf(heading), 1100);
    }

    // Ask the canvas which node to aim at rather than guessing at
    // pixels: cycleNode selects one and selectionRect reports its box
    // in item coordinates, so this survives a layout change.
    QString nodeId;
    QMetaObject::invokeMethod(canvas, "cycleNode", Qt::DirectConnection,
                              Q_RETURN_ARG(QString, nodeId),
                              Q_ARG(int, 1));
    QRectF box;
    QMetaObject::invokeMethod(canvas, "selectionRect",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QRectF, box));
    if (nodeId.isEmpty() || box.isEmpty()) {
        qWarning("uidriver: no node to drag (id=%s, box empty=%d)",
                 qPrintable(nodeId), int(box.isEmpty()));
        return false;
    }
    settle(900);

    const QPoint from = canvas->mapToScene(box.center()).toPoint();
    const QPoint to = from + QPoint(170, 96);
    qInfo("uidriver: dragging node %s from (%d,%d) to (%d,%d)",
          qPrintable(nodeId), from.x(), from.y(), to.x(), to.y());
    dragFromTo(window, from, to);
    settle(1400);
    if (fence >= 0)
        qInfo("uidriver: source after:\n%s",
              qPrintable(model->getContent(fence)));

    // Then the same fence again, now with the position line the drag
    // wrote at the end of it. This is the payoff, so it holds longest.
    if (QQuickItem *again = namedItem(window, "diagramReadFlick")) {
        const QPoint edge =
            again->mapToScene(QPointF(again->width() - 24,
                                      again->height() / 2))
                .toPoint();
        clickAt(window, edge, 800);
    }
    settle(3600);
    return true;
}

bool tourLivePreview(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    const int idx = blockContaining(model, QStringLiteral("native"));
    QQuickItem *para = idx >= 0 ? delegateAt(window, idx) : nullptr;
    if (!para) {
        qWarning("uidriver: intro paragraph not on screen (idx=%d)",
                 idx);
        return false;
    }
    clickAt(window, centerOf(para), 700);

    // Visit the four spans that reveal something, rather than sweeping the
    // caret along the whole line.
    //
    // Sweeping was the first attempt and it does not read: eighty key presses
    // at fifty milliseconds is over in four seconds, the caret is a thin bar
    // moving continuously, and the syntax opening and closing behind it is
    // gone before a viewer can look at it. Each span here gets three beats
    // instead — the caret arrives just before it, steps into it, and rests
    // inside while the markers are showing.
    auto *editor = namedItemIn(para, "blockTextArea");
    if (!editor) {
        qWarning("uidriver: no blockTextArea in the intro paragraph");
        return false;
    }
    const QString source = model->getContent(idx);
    const char *spans[] = {"**Kvit**", "*native*", "`.md`", "$e^"};

    for (const char *span : spans) {
        const int at = source.indexOf(QLatin1String(span));
        if (at < 0) {
            qWarning("uidriver: %s is not in the intro paragraph", span);
            continue;
        }
        // Land on the span's opening marker, then step in. Starting a few
        // characters earlier was the first attempt and two of the four spans
        // ended up one short of their own start, because the caret does not
        // rest on a hidden marker and gets nudged; from the marker itself,
        // four steps land inside the word every time.
        editor->setProperty("cursorPosition", at);
        settle(900);
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(window, Qt::Key_Right, Qt::NoModifier, 200);
        qInfo("uidriver: caret at %d for %s (asked %d)",
              editor->property("cursorPosition").toInt(), span, at);
        settle(1700);   // rest inside, markers showing
    }

    // Focus away, and the whole line renders again.
    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 2200);
    return true;
}

bool tourMath(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Calculus.md"))) {
        return false;
    }
    model->insertBlock(model->count(), Block::Paragraph, QString());
    settle(600);
    const int target = model->count() - 1;
    if (QQuickItem *d = delegateAt(window, target))
        clickAt(window, centerOf(d), 400);
    else
        clickEditorBlock(window, target);
    settle(400);

    typeSlowly(window,
               QStringLiteral("Euler's identity, $e^{i\\pi} + 1 = 0"),
               62);
    settle(500);
    // The editor auto-pairs an opening dollar, so the closing one is
    // typed only when it is missing; either way the line ends closed.
    if (!model->getContent(target).trimmed().endsWith(QLatin1Char('$')))
        typeSlowly(window, QStringLiteral("$"), 80);
    settle(700);
    QTest::keyClick(window, Qt::Key_End, Qt::NoModifier, 60);
    typeSlowly(window, QStringLiteral(", which never gets old."), 55);
    settle(600);
    qInfo("uidriver: typed line: [%s]",
          qPrintable(model->getContent(target)));

    // Focus away so the formula renders in its display state.
    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 2000);
    return true;
}

bool tourAsText(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }

    // This segment used to show the ingest repair straightening pasted box
    // art, and that does not film. DiagramRepair is deliberately conservative
    // — every fix is a zero-shift edit, a wall bar swapping with a space or a
    // corner extending through fill, with label text never touched — so the
    // before and the after differ by a character or two, and character art
    // stays text rather than becoming a drawn diagram. The inverse gesture is
    // the one with something to watch: a diagram the application drew, turned
    // into box-drawing text that can be pasted into a commit message or a
    // terminal.
    // The chips are hover controls: the row holding them sits at zero opacity
    // until the pointer is over the block, so it has to be moved there before
    // the chip is on screen to be clicked. Resting on the diagram first also
    // gives the viewer the drawing to hold in mind before its text version
    // appears underneath it.
    auto *canvas = namedItem(window, "diagramReadCanvas");
    if (!canvas) {
        qWarning("uidriver: no rendered diagram in this note");
        return false;
    }
    glide(window, centerOf(canvas));
    settle(1800);

    auto *chip = namedItem(window, "diagramCopyTextChip");
    if (!chip || !chip->isVisible() || chip->width() <= 0) {
        qWarning("uidriver: copy-as-text chip did not appear on hover");
        return false;
    }
    clickAt(window, centerOf(chip), 1200);
    qInfo("uidriver: copied diagram as text (%d chars)",
          int(QGuiApplication::clipboard()->text().size()));

    // Paste into a code block rather than into a paragraph. Multi-line plain
    // text pasted at a paragraph becomes one paragraph per line, which is
    // right for prose and wrong for a drawing; a code block keeps the lines
    // together, and monospaced is what box-drawing characters need anyway.
    //
    // Put it directly under the diagram rather than at the end of the note.
    // Appending was the first attempt and the target landed below the
    // viewport, so the recording showed a copy happening and then nothing:
    // the whole point is the pair, and the pair has to be in frame together.
    const int fence = blockContaining(model, QStringLiteral("flowchart"));
    const int target = fence >= 0 ? fence + 1 : model->count();
    model->insertBlock(target, Block::CodeBlock, QString());
    settle(600);

    // Bring it into view explicitly. A newly inserted block is not scrolled
    // to on its own, and the list virtualises, so an off-screen index has no
    // delegate to click at all.
    if (auto *list = namedItem(window, "blockListView")) {
        QMetaObject::invokeMethod(list, "positionViewAtIndex",
                                  Q_ARG(int, target), Q_ARG(int, 1));
        settle(900);
    }
    if (QQuickItem *d = delegateAt(window, target))
        clickAt(window, centerOf(d), 400);
    else
        clickEditorBlock(window, target);
    settle(400);
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier, 80);
    settle(2000);
    qInfo("uidriver: pasted text diagram (%d blocks total, attrs [%s]):\n%s",
          model->count(), qPrintable(model->getAttributes(target)),
          qPrintable(model->getContent(target)));

    // The caret stays in the pasted block, and that is deliberate. Box-drawing
    // characters are how a diagram fence is recognised, so this block renders
    // as a second drawing the moment it stops being edited — which is exactly
    // the half of "as text" the segment exists to show. Held open, it shows
    // the characters themselves in a monospaced block, with the drawing they
    // came from above. Clicking away here would end the clip on two pictures.
    settle(4200);
    return true;
}
bool tourQuery(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Project board.md"),
                  3200)) {
        return false;
    }
    // Show the question before the answer. A rendered query block is a table,
    // and a table on its own says nothing about where its rows came from —
    // the first version of this segment showed only the result and read as an
    // ordinary table. Clicking the block opens its spec above the results, so
    // the `from:`, `where:` and `sort:` lines are on screen together with the
    // rows they produced.
    const int spec = blockContaining(model, QStringLiteral("from:"));
    if (spec >= 0) {
        // Aim at the block's header strip rather than its middle. The rows of
        // a query result are links: clicking the centre opened one of the
        // notes and the segment sailed off to another page entirely.
        if (QQuickItem *d = delegateAt(window, spec)) {
            const QPoint header =
                d->mapToScene(QPointF(90, 14)).toPoint();
            clickAt(window, header, 800);
        }
        settle(3600);       // long enough to read six lines of query
        // Focus away so the block goes back to being just its answer, which
        // is the state the row is about to appear in.
        if (QQuickItem *heading = delegateAt(window, 0))
            clickAt(window, centerOf(heading), 1200);
    } else {
        qWarning("uidriver: no query spec block in this note");
    }

    // Long enough for the vault scan behind the first evaluation to
    // finish. A change written while that is still in flight is not
    // picked up, and the table then sits on its first answer.
    settle(3000);

    // Change a project's front matter on disk. Gamma is `done`, so it
    // sits outside the query until this makes it active, and then a
    // row appears without the editor being touched.
    //
    // The write has to come from another process. The application
    // ignores changes it made itself, which is what stops a save from
    // being re-read as an outside edit, and a write from this driver
    // is a write from the application: doing it in-process here left
    // the table on its first answer, measured, however long the wait.
    const QString gamma =
        QDir(vault).filePath(QStringLiteral("projects/Gamma.md"));
    if (!QFile::exists(gamma)) {
        qWarning("uidriver: no %s", qPrintable(gamma));
    } else {
#ifdef Q_OS_WIN
        const QString program = QStringLiteral("powershell");
        const QStringList args{
            QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
            QStringLiteral("(Get-Content -Raw '%1') -replace "
                           "'status: done','status: active' | "
                           "Set-Content -NoNewline '%1'")
                .arg(gamma)};
#else
        const QString program = QStringLiteral("sh");
        const QStringList args{
            QStringLiteral("-c"),
            QStringLiteral("sed -i 's/status: done/status: active/' "
                           "'%1'")
                .arg(gamma)};
#endif
        const int rc = QProcess::execute(program, args);
        if (rc == 0)
            qInfo("uidriver: set Gamma active from another process");
        else
            qWarning("uidriver: outside write failed (%s exited %d)",
                     qPrintable(program), rc);
    }
    // Long enough for the watcher to notice, the collection to
    // rescan, and the query's own debounce to expire on top of that.
    settle(7000);
    return true;
}

// The block palette: type "/" on an empty block and pick what it becomes.
bool tourPalette(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    // Open the menu on a block in the middle of the note rather than at its
    // end. The menu is drawn beside the caret, so a caret below the fold gets
    // a menu against the bottom edge with the document it belongs to off
    // screen.
    const int table = blockContaining(model, QStringLiteral("| Feature |"));
    const int target = table >= 0 ? table + 1 : model->count();
    model->insertBlock(target, Block::Paragraph, QString());
    settle(500);
    showBlock(window, target);
    focusBlock(window, target);

    typeSlowly(window, QStringLiteral("/"), 90);
    settle(2400);       // the whole catalogue, grouped by kind
    typeSlowly(window, QStringLiteral("to"), 260);
    settle(2000);       // narrowed to what "to" matches
    QTest::keyClick(window, Qt::Key_Return, Qt::NoModifier, 90);
    settle(1000);
    qInfo("uidriver: block %d is now type %d", target,
          int(model->blockAt(target)->blockType()));

    typeSlowly(window, QStringLiteral("Ship the 1.0 release"), 55);
    settle(700);
    // Tick it, so the block that was just made gets used rather than only
    // made: the checkbox is the whole point of the kind that was chosen.
    if (QQuickItem *d = delegateAt(window, target)) {
        if (QQuickItem *box = namedItemIn(d, "todoCheckbox"))
            clickAt(window, centerOf(box), 1400);
    }
    // Back to the top of the note to finish, which both takes the caret out of
    // the block that was made and puts the whole note in the last frame.
    if (QQuickItem *heading = showBlock(window, 0, 600))
        clickAt(window, centerOf(heading), 2000);
    return true;
}

// A markdown table edited like a grid: the caret moves cell to cell and a row
// is added from the chip under it.
bool tourTables(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    const int table = blockContaining(model, QStringLiteral("| Feature |"));
    if (table < 0) {
        qWarning("uidriver: no feature table in this note");
        return false;
    }
    QQuickItem *delegate = showBlock(window, table);
    if (!delegate) {
        qWarning("uidriver: the table has no delegate on screen");
        return false;
    }
    QQuickItem *frame = namedItemIn(delegate, "tableFrame");
    if (!frame) {
        qWarning("uidriver: no tableFrame in the table delegate");
        return false;
    }

    // Aim at a cell by its row and column rather than by a pixel offset: the
    // grid sizes its columns to their content, so a fixed offset lands in a
    // different cell as soon as the demo note changes. Row 0 is the header.
    // Measured against the frame rather than the column around it, which grows
    // by the height of the add-row chips as soon as a cell is being edited.
    const auto cellPoint = [&](QQuickItem *f, int row, int rows, int col,
                               int cols) {
        return f->mapToScene(QPointF(f->width() * (col + 0.5) / cols,
                                     f->height() * (row + 0.5) / rows))
            .toPoint();
    };
    clickAt(window, cellPoint(frame, 1, 4, 1, 2), 900);   // "shipped"
    settle(900);        // the add-row and add-column chips arrive with it

    // Tab across the grid. Three presses walk the caret to the end of one row
    // and on to the start of the next, which is the behaviour worth showing:
    // the block is a markdown table and it still moves like a spreadsheet.
    for (int i = 0; i < 3; ++i) {
        QTest::keyClick(window, Qt::Key_Tab, Qt::NoModifier, 220);
        settle(700);
    }

    QQuickItem *addRow = namedItemIn(delegate, "tableAddRow");
    if (!addRow || !addRow->isVisible()) {
        qWarning("uidriver: the + Row chip is not on screen");
        return false;
    }
    clickAt(window, centerOf(addRow), 1000);
    settle(600);

    // Fill the row that just appeared. Five rows now, so the new one is the
    // last of them.
    QQuickItem *grown = namedItemIn(delegate, "tableFrame");
    if (grown) {
        clickAt(window, cellPoint(grown, 4, 5, 0, 2), 800);
        typeSlowly(window, QStringLiteral("Kanban boards"), 60);
        settle(500);
        QTest::keyClick(window, Qt::Key_Tab, Qt::NoModifier, 200);
        settle(400);
        typeSlowly(window, QStringLiteral("shipped"), 60);
        settle(700);
    }
    qInfo("uidriver: table is now:\n%s",
          qPrintable(model->getContent(table)));

    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 2200);
    return true;
}

// A kanban board: a card carried into another column, and a card finished.
bool tourKanban(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("Team board.md"))) {
        return false;
    }
    const int board = blockContaining(model, QStringLiteral("## In progress"));
    if (board < 0) {
        qWarning("uidriver: no kanban fence in this note");
        return false;
    }
    // Fold the note list away first. Three columns of cards are wider than a
    // document that shares the window with two side panes, and the right-hand
    // column was running off the edge of the frame.
    if (QQuickItem *collapse = namedItem(window, "noteListCollapseButton"))
        clickAt(window, centerOf(collapse), 900);

    QQuickItem *delegate = showBlock(window, board);
    if (!delegate) {
        qWarning("uidriver: the board has no delegate on screen");
        return false;
    }
    settle(1400);       // a beat to read the three columns before anything moves

    // Which card to carry, and where to. Both are found through the column
    // headers rather than through the card list's order: a card belongs to the
    // column it sits under, and that is a horizontal position on screen.
    const QList<QQuickItem *> headers = namedItemsIn(delegate, "kanbanColName");
    const QList<QQuickItem *> cards = namedItemsIn(delegate, "kanbanCard");
    if (headers.size() < 3 || cards.isEmpty()) {
        qWarning("uidriver: board has %lld columns and %lld cards",
                 qint64(headers.size()), qint64(cards.size()));
        return false;
    }
    const int middleX = centerOf(headers.at(1)).x();
    const int rightX = centerOf(headers.at(2)).x();
    QQuickItem *carried = nullptr;
    int best = INT_MAX;
    for (QQuickItem *card : cards) {
        const int dx = qAbs(centerOf(card).x() - middleX);
        if (dx < best) {
            best = dx;
            carried = card;
        }
    }
    if (!carried) {
        qWarning("uidriver: no card under the middle column");
        return false;
    }

    // Grab near the card's top edge, which is its own drag handle rather than
    // the text editor that opens on a click in the middle of it.
    const QPoint from =
        carried->mapToScene(QPointF(carried->width() / 2, 12)).toPoint();
    const QPoint to(rightX, from.y() + 8);
    qInfo("uidriver: carrying a card from (%d,%d) to (%d,%d)", from.x(),
          from.y(), to.x(), to.y());
    dragFromTo(window, from, to);
    settle(1600);
    qInfo("uidriver: board after the drag:\n%s",
          qPrintable(model->getContent(board)));

    // Then finish a card where it stands, so the board is used and not only
    // rearranged. The delegate is asked for again: the rewrite the drag made
    // goes through the model, and the row it comes back on is not necessarily
    // the item this function was holding.
    QQuickItem *again = delegateAt(window, board);
    for (QQuickItem *card : namedItemsIn(again ? again : delegate,
                                         "kanbanCard")) {
        if (centerOf(card).x() > middleX)
            continue;
        if (QQuickItem *box = namedItemIn(card, "kanbanCardCheckbox")) {
            clickAt(window, centerOf(box), 1600);
            break;
        }
    }
    settle(1200);
    qInfo("uidriver: board at the end:\n%s",
          qPrintable(model->getContent(board)));
    // Off the card, so the last frame is the board rather than one card in
    // its selected state with an empty description prompt on it.
    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 2000);
    settle(1200);
    return true;
}

// Wiki-links: completed as they are typed, followed with a click, and shown
// from the other end in the backlinks pane.
bool tourWikiLinks(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!openNote(ctx, vault, QStringLiteral("projects/Alpha.md"))) {
        return false;
    }
    const int para = blockContaining(model, QStringLiteral("Work notes"));
    if (para < 0) {
        qWarning("uidriver: no prose paragraph in Alpha");
        return false;
    }
    focusBlock(window, para, 700);
    QTest::keyClick(window, Qt::Key_End, Qt::NoModifier, 60);
    settle(400);

    typeSlowly(window, QStringLiteral(" Blocked by [[Bet"), 70);
    settle(2000);       // the picker, narrowed to one note
    QTest::keyClick(window, Qt::Key_Return, Qt::NoModifier, 100);
    settle(1400);
    qInfo("uidriver: paragraph is now [%s]",
          qPrintable(model->getContent(para)));

    // Save, because the link only becomes a backlink once the collection has
    // read it from the file.
    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier, 90);
    settle(1600);
    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 1200);
    settle(800);

    // Follow it with a click on the link itself. Where the link sits is asked
    // of the rendered text rather than guessed: linkAt walks the layout, so
    // this survives a font change or a rewording of the sentence.
    QQuickItem *delegate = delegateAt(window, para);
    if (!delegate) {
        qWarning("uidriver: the paragraph left the viewport");
        return false;
    }
    // The link's position is asked of the layout rather than guessed at in
    // pixels. What the block draws is not what the file holds — the brackets
    // are hidden while the caret is elsewhere — so the word is looked up in
    // the displayed text and the editor is asked where that character sits.
    QQuickItem *editor = namedItemIn(delegate, "blockTextArea");
    if (!editor) {
        qWarning("uidriver: no blockTextArea in the paragraph");
        return false;
    }
    const QString shown = editor->property("text").toString();
    const int at = shown.lastIndexOf(QStringLiteral("Beta"));
    if (at < 0) {
        qWarning("uidriver: the paragraph reads [%s] with no link in it",
                 qPrintable(shown));
        return false;
    }
    QRectF caret;
    QMetaObject::invokeMethod(editor, "positionToRectangle",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QRectF, caret),
                              Q_ARG(int, at + 2));
    const QPoint hit =
        editor->mapToScene(caret.center() + QPointF(0, 0)).toPoint();
    qInfo("uidriver: following the link at (%d,%d) in [%s]", hit.x(), hit.y(),
          qPrintable(shown));
    clickAt(window, hit, 2400);

    // Now the other direction: what points at the note that just opened.
    if (QQuickItem *viewButton = namedItem(window, "toolbarViewButton")) {
        clickAt(window, centerOf(viewButton), 900);
        if (QQuickItem *entry = itemWithLabel(window, QStringLiteral("Backlinks")))
            clickAt(window, centerOf(entry), 1200);
        else
            qWarning("uidriver: no Backlinks entry in the View menu");
    }
    settle(2600);
    if (QQuickItem *list = namedItem(window, "backlinksList"))
        qInfo("uidriver: backlinks pane holds %d rows",
              list->property("count").toInt());
    return true;
}

// Search: the find bar over the open note, then the whole collection from the
// sidebar.
bool tourSearch(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier, 90);
    settle(1000);
    if (!namedItem(window, "findQueryField")) {
        qWarning("uidriver: Ctrl+F did not open the find bar");
        return false;
    }
    typeSlowly(window, QStringLiteral("markdown"), 130);
    settle(2000);       // every match tinted, with the count beside the field
    for (int i = 0; i < 2; ++i) {
        if (QQuickItem *next = namedItem(window, "findNextButton"))
            clickAt(window, centerOf(next), 1100);
    }
    settle(800);
    QTest::keyClick(window, Qt::Key_Escape, Qt::NoModifier, 90);
    settle(900);

    // The same question asked of every note instead of this one.
    QQuickItem *field = namedItem(window, "globalSearchField");
    if (!field) {
        qWarning("uidriver: no global search field (is a vault open?)");
        return false;
    }
    clickAt(window, centerOf(field), 600);
    typeSlowly(window, QStringLiteral("status"), 130);
    settle(3000);       // the collection index answers, grouped by note
    if (QQuickItem *results = namedItem(window, "searchResultsList")) {
        qInfo("uidriver: %d search rows", results->property("count").toInt());
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(results, "itemAtIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(QQuickItem *, row),
                                  Q_ARG(int, 0));
        if (row)
            clickAt(window, centerOf(row), 2600);
    }
    settle(1600);
    return true;
}

// Themes: chosen from the View menu, applied to the whole window at once.
bool tourTheme(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    const auto pick = [&](const QString &name, int holdMs) {
        QQuickItem *viewButton = namedItem(window, "toolbarViewButton");
        if (!viewButton) {
            qWarning("uidriver: no View button on the toolbar");
            return false;
        }
        clickAt(window, centerOf(viewButton), 800);
        QQuickItem *themeRow = itemWithLabel(window, QStringLiteral("Theme"));
        if (!themeRow) {
            qWarning("uidriver: no Theme row in the View menu");
            return false;
        }
        clickAt(window, centerOf(themeRow), 900);
        QQuickItem *entry = itemWithLabel(window, name);
        if (!entry) {
            qWarning("uidriver: no %s theme entry", qPrintable(name));
            return false;
        }
        clickAt(window, centerOf(entry), holdMs);
        return true;
    };
    // Dark and sepia, then back to the theme the clip started in, so the
    // gallery's other clips are not preceded by one that leaves the
    // application looking different.
    if (!pick(QStringLiteral("Dark"), 2600))
        return false;
    if (!pick(QStringLiteral("Sepia"), 2600))
        return false;
    if (!pick(QStringLiteral("Light"), 2000))
        return false;
    return true;
}

// Export: a format and a scope chosen in the dialog, then a real file written
// and named in the status bar.
bool tourExport(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
        return false;
    }
    QQuickItem *fileButton = namedItem(window, "toolbarFileButton");
    if (!fileButton) {
        qWarning("uidriver: no File button on the toolbar");
        return false;
    }
    clickAt(window, centerOf(fileButton), 900);
    QQuickItem *entry = itemWithLabel(window, QStringLiteral("Export…"));
    if (!entry)
        entry = itemWithLabel(window, QStringLiteral("Export..."));
    if (!entry) {
        qWarning("uidriver: no Export entry in the File menu");
        return false;
    }
    clickAt(window, centerOf(entry), 1600);

    QQuickItem *combo = namedItem(window, "exportFormatCombo");
    if (!combo) {
        qWarning("uidriver: the export dialog did not open");
        return false;
    }
    clickAt(window, centerOf(combo), 1200);     // the four formats
    QQuickItem *pdf = itemWithLabel(window, QStringLiteral("PDF (.pdf)"));
    if (!pdf) {
        qWarning("uidriver: no PDF entry in the format list");
        return false;
    }
    clickAt(window, centerOf(pdf), 1600);

    QQuickItem *run = namedItem(window, "exportRunButton");
    if (!run) {
        qWarning("uidriver: no destination button");
        return false;
    }
    clickAt(window, centerOf(run), 1800);
    // The destination chooser. Qt draws its own inside the window wherever the
    // platform has no file chooser to offer, which is the case here, and its
    // name field is the one part of it this has to reach.
    QQuickItem *nameField = namedItem(window, "fileNameTextField");
    if (!nameField) {
        qWarning("uidriver: no destination chooser inside the window; this "
                 "platform put up one of its own, which cannot be filmed");
        return false;
    }
    settle(1400);       // the chooser, long enough to see what it is
    clickAt(window, centerOf(nameField), 500);
    typeSlowly(window, QStringLiteral("Welcome"), 90);
    settle(700);
    // Save is clicked rather than pressed: Return in the name field does not
    // accept this dialog, and the button is what a viewer expects to see used
    // anyway.
    QQuickItem *save = itemWithLabel(window, QStringLiteral("Save"));
    if (!save) {
        qWarning("uidriver: no Save button in the destination chooser");
        return false;
    }
    clickAt(window, centerOf(save), 700);
    // Off the document before the last frames: the pointer landed on the
    // diagram when the dialog closed, and a hovered diagram puts its whole row
    // of chips into the shot. The message the status bar ends on lasts three
    // and a half seconds, so what follows the write is kept short enough that
    // the closing frame still has it.
    glide(window, QPoint(int(window->width() / 2), 60));
    settle(700);
    if (QQuickItem *status = namedItem(window, "transientStatusText"))
        qInfo("uidriver: status bar reads [%s]",
              qPrintable(status->property("text").toString()));
    return true;
}

// Single-file mode: one .md opened on its own, with no vault around it. The
// startup argument is the file, so this scenario opens nothing itself.
bool tourSingleFile(QQuickWindow *window, AppContext *ctx, const QString &vault)
{
    auto *model = ctx->blockModel();
    if (!vault.endsWith(QStringLiteral(".md"))) {
        qWarning("uidriver: tour-singlefile wants a .md file as its startup "
                 "target, not the vault %s", qPrintable(vault));
        return false;
    }
    settle(2600);       // the window as it opens: the note, and nothing else

    const int para = blockContaining(model, QStringLiteral("Welcome to"));
    if (para < 0) {
        qWarning("uidriver: the file did not load");
        return false;
    }
    focusBlock(window, para, 700);
    QTest::keyClick(window, Qt::Key_End, Qt::NoModifier, 60);
    settle(500);
    typeSlowly(window, QStringLiteral(" Opened straight from the filesystem."),
               58);
    settle(900);
    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier, 90);
    settle(1200);
    if (QQuickItem *heading = delegateAt(window, 0))
        clickAt(window, centerOf(heading), 2400);
    settle(1600);       // the status bar: the file's own path, saved
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Driver sessions are ephemeral and often offline; never let them spend
    // the day's update check or hit the network.
    qputenv("KVIT_DISABLE_UPDATE_CHECK", "1");
    KvitApplication::applyPlatformWorkarounds();
    QApplication app(argc, argv);
    KvitApplication kvit(app);

    QString scenario = QStringLiteral("dropcap");
    QString outDir = QStringLiteral(".");
    QString vault;      // opened as the collection root (startup argument)
    QString note;       // absolute .md path opened after startup (still shots)
    QString shotName = QStringLiteral("still");
    QString title;      // caption band text, for the tour recordings
    QString recordDir;  // when set, frames are grabbed while the scenario runs
    int fps = 10;
    int winW = 0, winH = 0;
    int winX = INT_MIN, winY = INT_MIN;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLatin1(argv[i]);
        if (arg.startsWith(QStringLiteral("--scenario=")))
            scenario = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--out=")))
            outDir = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--vault=")))
            vault = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--note=")))
            note = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--name=")))
            shotName = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--title=")))
            title = arg.section(QLatin1Char('='), 1);
        // Grab frames while the scenario plays, for assembling into a GIF.
        // No screen recorder and nobody at the keyboard: the frames come from
        // the window itself, so this runs unattended and identically on any
        // machine.
        else if (arg.startsWith(QStringLiteral("--record=")))
            recordDir = arg.section(QLatin1Char('='), 1);
        else if (arg.startsWith(QStringLiteral("--fps=")))
            fps = qBound(4, arg.section(QLatin1Char('='), 1).toInt(), 30);
        else if (arg.startsWith(QStringLiteral("--size=")))
            (void)sscanf(qPrintable(arg.section(QLatin1Char('='), 1)),
                         "%dx%d", &winW, &winH);
        // A fixed window position, because a screen recorder captures a
        // region rather than a window: without it the region has to be
        // re-selected for every clip.
        else if (arg.startsWith(QStringLiteral("--pos=")))
            (void)sscanf(qPrintable(arg.section(QLatin1Char('='), 1)),
                         "%d,%d", &winX, &winY);
    }

    QStringList startArgs{QString::fromLatin1(argv[0])};
    if (!vault.isEmpty())
        startArgs << vault;
    // The driver runs its own window in-process; it must never forward to (or
    // be pre-empted by) a real running instance.
    kvit.setSingleInstanceEnabled(false);
    if (kvit.start(startArgs) != KvitApplication::StartOutcome::RunEventLoop)
        return -1;

    QTimer::singleShot(0, &app, [&]() {
        QQuickWindow *window = shellWindow(kvit);
        if (!window) {
            qWarning("uidriver: no shell window");
            app.exit(2);
            return;
        }
        if (winW > 0 && winH > 0) {
            window->setWidth(winW);
            window->setHeight(winH);
        }
        if (winX != INT_MIN && winY != INT_MIN)
            window->setPosition(winX, winY);
        window->show();
        window->requestActivate();
        settle(1200);

        AppContext *ctx = activeContext(kvit);
        if (!ctx) {
            qWarning("uidriver: no active context");
            app.exit(2);
            return;
        }
        auto *model = ctx->blockModel();

        // The caption, when one was asked for, goes up before the scenario
        // starts so a recording opens on the feature's name.
        showTitle(window, title);

        // Frame capture, when asked for. Each frame is named with the
        // milliseconds elapsed since capture began, because grabbing is not
        // free and the interval between frames is therefore not the interval
        // asked for: the assembler reads those numbers and gives each frame
        // the duration it actually occupied, so the result plays at the speed
        // the scenario ran at rather than at a nominal frame rate.
        QElapsedTimer clock;
        QTimer frameTimer;
        int frameCount = 0;
        if (!recordDir.isEmpty()) {
            QDir().mkpath(recordDir);
            clock.start();
            QObject::connect(&frameTimer, &QTimer::timeout, window,
                             [&, window]() {
                                 const QImage frame = window->grabWindow();
                                 if (frame.isNull())
                                     return;
                                 const QString path =
                                     QStringLiteral("%1/f_%2.png")
                                         .arg(recordDir)
                                         .arg(clock.elapsed(), 8, 10,
                                              QLatin1Char('0'));
                                 if (frame.save(path))
                                     ++frameCount;
                             });
            frameTimer.start(1000 / fps);
        }

        if (scenario == QStringLiteral("still")) {
            // One staged frame of the real shell: open the requested note in
            // the (already startup-opened) vault, give async renderers
            // (math images, diagram layout) time to land, and grab. Used to
            // produce the curated screenshots/press stills.
            if (!note.isEmpty()) {
                ctx->documentManager()->open(QUrl::fromLocalFile(note));
            }
            settle(2500);
            // These frames are grabbed from a real desktop, so whatever the
            // mouse happens to be resting on is hovered: a block under the
            // cursor grabs with its hover tint and its insert and drag handles
            // showing, in one still and not the next.
            clearHover(window);
            grab(window, outDir + QStringLiteral("/") + shotName
                             + QStringLiteral(".png"));
        } else if (scenario == QStringLiteral("dropcap")) {
            // Start from a single paragraph with prose in it, then apply the
            // drop cap through the slash menu the way a user would.
            while (model->count() > 1)
                model->removeBlock(model->count() - 1);
            model->updateContent(
                0, QStringLiteral("Very early in the morning, while it was "
                                  "still dark, the household stirred and the "
                                  "long day of preparations began in earnest "
                                  "across every room of the old house."));
            settle(600);
            grab(window, outDir + QStringLiteral("/dropcap_before.png"));

            // A second paragraph is where the slash menu gets typed, so the
            // menu has an empty block to open on.
            model->insertBlock(1, Block::Paragraph, QString());
            settle(400);
            clickEditorBlock(window, model->count() - 1);
            settle(300);
            type(window, QStringLiteral("/dropcap"));
            settle(700);
            grab(window, outDir + QStringLiteral("/dropcap_menu.png"));
            QTest::keyClick(window, Qt::Key_Return, Qt::NoModifier, 40);
            settle(700);
            qInfo("uidriver: after apply, block1 content=[%s] attrs=[%s]",
                  qPrintable(model->getContent(1)),
                  qPrintable(model->getAttributes(1)));

            // Type prose into the decorated block, then move focus away: the
            // cap renders in the unfocused (display) state.
            type(window, QStringLiteral("Winter came early that year and the "
                                        "roads out of the valley were closed "
                                        "for weeks on end."));
            settle(500);
            clickEditorBlock(window, 0);
            settle(700);
            grab(window, outDir + QStringLiteral("/dropcap_after.png"));
            qInfo("uidriver: block1 content=[%s] attrs=[%s]",
                  qPrintable(model->getContent(1)),
                  qPrintable(model->getAttributes(1)));
        } else if (scenario == QStringLiteral("settings")) {
            // The settings dialog: one frame per tab, then a drag of the title
            // bar, which is the only handle a Popup gets for moving.
            QMetaObject::invokeMethod(window, "openSettingsDialog");
            settle(900);
            auto *appearance =
                window->findChild<QQuickItem *>(
                    QStringLiteral("appearanceTab"));
            QQuickItem *dialogItem = appearance;
            while (dialogItem && dialogItem->parentItem()
                   && dialogItem->objectName()
                          != QStringLiteral("SettingsDialog"))
                dialogItem = dialogItem->parentItem();
            if (!dialogItem) {
                qWarning("uidriver: settings dialog not on screen");
                app.exit(3);
                return;
            }
            auto *bar = appearance->parentItem();       // ListView content row
            while (bar && !bar->inherits("QQuickTabBar"))
                bar = bar->parentItem();
            qInfo("uidriver: dialog w=%.0f, tab bar w=%.0f (overflow %.0f)",
                  dialogItem->width(), bar ? bar->width() : 0.0,
                  (bar ? bar->width() : 0.0) - dialogItem->width());

            const char *tabs[] = {"appearanceTab", "typographyTab",
                                  "generalTab"};
            const char *shots[] = {"settings_appearance", "settings_typography",
                                   "settings_general"};
            for (int t = 0; t < 3; ++t) {
                auto *tab = window->findChild<QQuickItem *>(
                    QString::fromLatin1(tabs[t]));
                if (!tab) {
                    qWarning("uidriver: no %s", tabs[t]);
                    continue;
                }
                const QPointF c = tab->mapToScene(
                    QPointF(tab->width() / 2, tab->height() / 2));
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                  c.toPoint(), 50);
                settle(400);
                grab(window, outDir + QStringLiteral("/")
                                 + QString::fromLatin1(shots[t])
                                 + QStringLiteral(".png"));
            }

            // Drag the title bar down and to the right, then grab: the dialog
            // should have followed the cursor exactly.
            const QPointF before = dialogItem->mapToScene(QPointF(0, 0));
            const QPoint grip = dialogItem->mapToScene(QPointF(120, 14))
                                    .toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grip, 30);
            for (int step = 1; step <= 6; ++step) {
                QTest::mouseMove(window, grip + QPoint(20 * step, 10 * step),
                                 20);
            }
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                                grip + QPoint(120, 60), 30);
            settle(400);
            const QPointF after = dialogItem->mapToScene(QPointF(0, 0));
            qInfo("uidriver: dialog moved from (%.0f,%.0f) to (%.0f,%.0f), "
                  "expected +120,+60",
                  before.x(), before.y(), after.x(), after.y());
            grab(window, outDir + QStringLiteral("/settings_dragged.png"));
        } else if (scenario == QStringLiteral("inlinemath")) {
            // The same formula inline and as display math, so the two
            // rasterizations can be compared pixel for pixel. Inline images
            // are placed by the overlay layer at whatever sub-pixel offset
            // the text layout puts the span at; display math is centred in
            // its own block.
            while (model->count() > 1)
                model->removeBlock(model->count() - 1);
            model->updateType(0, Block::Paragraph);
            model->updateContent(
                0, QStringLiteral("Let $f$ be given, and note that "
                                  "$\\int_0^\\infty x^2 dx$ converges."));
            model->insertBlock(1, Block::MathBlock,
                               QStringLiteral("\\int_0^\\infty x^2 dx"));
            model->insertBlock(2, Block::Paragraph, QString());
            settle(800);
            clickEditorBlock(window, 2);
            settle(1200);

            QList<QQuickItem *> shots;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
                const QString n = it->objectName();
                if ((n == QStringLiteral("inlineMathImage")
                     || n == QStringLiteral("mathRenderedImage"))
                    && it->isVisible() && it->width() > 0
                    && it->mapToScene(QPointF(0, 0)).x() > 0)
                    shots.append(it);
                for (QQuickItem *k : it->childItems())
                    walk(k);
            };
            walk(window->contentItem());

            const QImage frame = window->grabWindow();
            qInfo("uidriver: frame %dx%d dpr=%.2f", frame.width(),
                  frame.height(), frame.devicePixelRatio());
            int n = 0;
            for (QQuickItem *item : shots) {
                const QPointF p = item->mapToScene(QPointF(0, 0));
                qInfo("uidriver: %s scene=(%.2f,%.2f) size=%.2fx%.2f "
                      "implicit=%.2fx%.2f",
                      qPrintable(item->objectName()), p.x(), p.y(),
                      item->width(), item->height(), item->implicitWidth(),
                      item->implicitHeight());
                const qreal fdpr = frame.devicePixelRatio();
                QRect crop(qRound((p.x() - 3) * fdpr),
                           qRound((p.y() - 3) * fdpr),
                           qRound((item->width() + 6) * fdpr),
                           qRound((item->height() + 6) * fdpr));
                crop = crop.intersected(frame.rect());
                if (crop.isEmpty())
                    continue;
                const QImage zoom = frame.copy(crop).scaled(
                    crop.width() * 6, crop.height() * 6, Qt::IgnoreAspectRatio,
                    Qt::FastTransformation);
                zoom.save(outDir + QStringLiteral("/zoom_")
                          + item->objectName() + QString::number(n++)
                          + QStringLiteral(".png"));
            }
            frame.save(outDir + QStringLiteral("/inlinemath.png"));
            qInfo("uidriver: wrote %s/inlinemath.png", qPrintable(outDir));
        } else if (scenario == QStringLiteral("htmlpaste")) {
            while (model->count() > 1)
                model->removeBlock(model->count() - 1);
            model->updateContent(0, QString());
            settle(400);

            // A browser-shaped payload: the HTML carries the structure and
            // the plain-text flavor has already lost it.
            auto *mime = new QMimeData;
            mime->setText(QStringLiteral("Release notes Faster startup "
                                         "Fixed paste See the changelog"));
            mime->setHtml(QStringLiteral(
                "<h2>Release notes</h2>"
                "<ul><li>Faster <b>startup</b></li>"
                "<li>Fixed <i>paste</i></li></ul>"
                "<p>See the <a href=\"https://example.com/log\">changelog</a>."
                "</p>"));
            QGuiApplication::clipboard()->setMimeData(mime);
            settle(300);

            clickEditorBlock(window, model->count() - 1);
            settle(300);
            QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier, 60);
            settle(900);
            grab(window, outDir + QStringLiteral("/htmlpaste_after.png"));
            for (int i = 0; i < model->count(); ++i) {
                qInfo("uidriver: block %d type=%d [%s]", i,
                      int(model->blockAt(i)->blockType()),
                      qPrintable(model->getContent(i)));
            }
        } else if (scenario == QStringLiteral("urlpaste")) {
            while (model->count() > 1)
                model->removeBlock(model->count() - 1);
            model->updateContent(
                0, QStringLiteral("Read the changelog for details."));
            settle(400);
            QGuiApplication::clipboard()->setText(
                QStringLiteral("https://example.com/log"));
            settle(300);

            clickEditorBlock(window, 0);
            settle(300);
            // Select the word "changelog" (offsets 9..18) and paste the URL.
            QTest::keyClick(window, Qt::Key_Home, Qt::NoModifier, 30);
            for (int i = 0; i < 9; ++i)
                QTest::keyClick(window, Qt::Key_Right, Qt::NoModifier, 8);
            for (int i = 0; i < 9; ++i)
                QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier, 8);
            settle(300);
            QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier, 60);
            settle(700);
            grab(window, outDir + QStringLiteral("/urlpaste_after.png"));
            qInfo("uidriver: block0 [%s]", qPrintable(model->getContent(0)));
        } else if (scenario == QStringLiteral("roundtrip")) {
            // Kvit's own copy pasted back must be byte-identical: the internal
            // arm has to win over the HTML arm it also puts on the clipboard.
            while (model->count() > 1)
                model->removeBlock(model->count() - 1);
            model->updateContent(0, QStringLiteral("# Heading"));
            model->insertBlock(1, Block::BulletList,
                               QStringLiteral("an **item**"));
            model->insertBlock(2, Block::Paragraph, QString());
            settle(500);

            clickEditorBlock(window, 0);
            settle(200);
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier, 40);
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier, 40);
            settle(300);
            QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier, 40);
            settle(400);
            qInfo("uidriver: clipboard structured=%d text=[%s]",
                  int(QGuiApplication::clipboard()->mimeData()->hasFormat(
                      QStringLiteral("application/x-kvit-markdown"))),
                  qPrintable(QGuiApplication::clipboard()->text()));
            grab(window, outDir + QStringLiteral("/roundtrip_after.png"));
        } else if (scenario == QStringLiteral("backuppreview")) {
            // The restore dialog with a stored version drawn in it: the one
            // place a ReadOnlyDocument is on screen, and the only way to see
            // what a document rendered outside the editor pane looks like.
            // Needs --vault and --note, since a version has to be stored
            // against a real file.
            if (!note.isEmpty())
                ctx->documentManager()->open(QUrl::fromLocalFile(note));
            settle(800);

            // Two saves ten minutes apart. The first consumes the rotation
            // window; the second stores the document as the note opened, and
            // that stored copy is what the dialog then draws.
            NoteCollection *collection = ctx->noteCollection();
            ctx->documentManager()->save();
            settle(600);
            collection->setClockOffsetForTesting(11 * 60);
            model->updateContent(0, QStringLiteral("Release notes, revised"));
            ctx->documentManager()->save();
            collection->setClockOffsetForTesting(0);
            settle(1500);

            QObject *dialog =
                window->findChild<QObject *>(QStringLiteral("backupDialog"));
            if (!dialog) {
                qWarning("uidriver: no backup dialog");
                app.exit(3);
                return;
            }
            QMetaObject::invokeMethod(dialog, "openForCurrentNote");
            settle(1500);
            grab(window, outDir + QStringLiteral("/backup_preview.png"));

            // The foot of the same version, so the kinds that sit below the
            // fold — a divider and a code fence — are in a frame too.
            if (QQuickItem *scroll = namedItem(window,
                                               "backupPreviewDocument")) {
                QQuickItem *flick = scroll->parentItem();
                while (flick && !flick->inherits("QQuickFlickable"))
                    flick = flick->parentItem();
                if (flick) {
                    flick->setProperty(
                        "contentY",
                        qMax(0.0, flick->property("contentHeight").toDouble()
                                      - flick->height()));
                    settle(500);
                    grab(window, outDir
                                     + QStringLiteral("/backup_preview_foot.png"));
                }
            }

        // -------------------------------------------------------------
        // Tour scenarios: one per feature, for the demo recordings. Each
        // runs against the checked-in demo vault (screenshots/demo-vault,
        // staged somewhere neutral by the recording script), holds long
        // enough to be watched, and leaves the window up for a beat at the
        // end so a capture does not lose the last frame.
        // -------------------------------------------------------------
        } else if (scenario.startsWith(QStringLiteral("tour-"))) {
            // One segment per feature, and `tour-all`, which plays a chosen
            // few of them in order in one window. Recording them separately is
            // what makes each one re-shootable and gives the gallery its short
            // loops; tour-all is for a single continuous take, where a window
            // closing and reopening between features would show.
            //
            // `continuous` marks the ones tour-all plays. Most segments are
            // left out of it on purpose: a take that ran every feature would
            // be minutes long, and tour-singlefile could not be in it at all,
            // since it needs the process started on a file rather than on a
            // vault.
            struct Segment {
                const char *name;
                const char *caption;
                bool (*run)(QQuickWindow *, AppContext *, const QString &);
                bool continuous;
            };
            static const Segment kSegments[] = {
                {"tour-mermaid",
                 "Drag a node, and the markdown rewrites itself", tourMermaid,
                 true},
                {"tour-livepreview",
                 "Markdown syntax reveals itself around the caret",
                 tourLivePreview, true},
                {"tour-math", "TeX math, rendered as you type", tourMath,
                 true},
                {"tour-astext",
                 "Copy any diagram out as text", tourAsText, true},
                {"tour-query",
                 "A live table built from front matter across the vault",
                 tourQuery, true},
                {"tour-palette",
                 "Type / and choose what the block becomes", tourPalette,
                 false},
                {"tour-tables",
                 "A markdown table, edited like a grid", tourTables, false},
                {"tour-kanban",
                 "A board whose cards are list items in the file", tourKanban,
                 false},
                {"tour-wikilinks",
                 "Link notes by name, and see what links back",
                 tourWikiLinks, false},
                {"tour-search",
                 "Find in the note, then across every note", tourSearch,
                 false},
                {"tour-theme",
                 "One choice repaints the whole window", tourTheme, false},
                {"tour-export",
                 "Export a note to PDF, HTML, markdown or text", tourExport,
                 false},
                {"tour-singlefile",
                 "Open a single file with no vault around it",
                 tourSingleFile, false},
            };

            bool ok = false;
            if (scenario == QStringLiteral("tour-all")) {
                ok = true;
                for (const Segment &segment : kSegments) {
                    if (!segment.continuous)
                        continue;
                    // Each feature announces itself, replacing the caption
                    // the one before it left.
                    showTitle(window, QString::fromUtf8(segment.caption));
                    if (!segment.run(window, ctx, vault)) {
                        qWarning("uidriver: %s failed; stopping the tour",
                                 segment.name);
                        ok = false;
                        break;
                    }
                    settle(1200);   // a breath between features
                }
            } else {
                bool matched = false;
                for (const Segment &segment : kSegments) {
                    if (scenario != QLatin1String(segment.name))
                        continue;
                    matched = true;
                    // A segment run on its own gets its own caption when the
                    // command line did not supply one.
                    if (title.isEmpty())
                        showTitle(window, QString::fromUtf8(segment.caption));
                    ok = segment.run(window, ctx, vault);
                    break;
                }
                // A segment that ran and failed says so itself; this is only
                // for a name that matches nothing, which would otherwise
                // report as a failure of whatever ran last.
                if (!matched)
                    qWarning("uidriver: no such tour segment: %s",
                             qPrintable(scenario));
            }
            if (!ok) {
                app.exit(3);
                return;
            }
        }

        // A closing beat, so a screen recorder stopped by hand does not lose
        // the last thing that happened, and a last frame on disk, so a take
        // can be checked without scrubbing the video.
        if (scenario.startsWith(QStringLiteral("tour-"))) {
            settle(1200);
            grab(window, outDir + QStringLiteral("/") + scenario
                             + QStringLiteral("-final.png"));
        }

        if (frameTimer.isActive()) {
            frameTimer.stop();
            qInfo("uidriver: captured %d frames over %lld ms into %s",
                  frameCount, clock.elapsed(), qPrintable(recordDir));
        }

        app.quit();
    });

    return app.exec();
}
