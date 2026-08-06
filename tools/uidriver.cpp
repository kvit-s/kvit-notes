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
#include "kvitapplication.h"
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
void showTitle(QQuickWindow *window, const QString &text, int holdMs = 2400)
{
    if (text.isEmpty())
        return;
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

        // -------------------------------------------------------------
        // Tour scenarios: one per feature, for the demo recordings. Each
        // runs against the checked-in demo vault (screenshots/demo-vault,
        // staged somewhere neutral by the recording script), holds long
        // enough to be watched, and leaves the window up for a beat at the
        // end so a capture does not lose the last frame.
        // -------------------------------------------------------------
        } else if (scenario == QStringLiteral("tour-mermaid")) {
            if (!openNote(ctx, vault, QStringLiteral("Release pipeline.md"))) {
                app.exit(3);
                return;
            }
            auto *canvas = namedItem(window, "diagramReadCanvas");
            if (!canvas) {
                qWarning("uidriver: no diagramReadCanvas in this note");
                app.exit(3);
                return;
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
                app.exit(3);
                return;
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
        } else if (scenario == QStringLiteral("tour-livepreview")) {
            if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
                app.exit(3);
                return;
            }
            const int idx = blockContaining(model, QStringLiteral("native"));
            QQuickItem *para = idx >= 0 ? delegateAt(window, idx) : nullptr;
            if (!para) {
                qWarning("uidriver: intro paragraph not on screen (idx=%d)",
                         idx);
                app.exit(3);
                return;
            }
            clickAt(window, centerOf(para), 700);

            // Walk the caret across the line. Each span reveals its own
            // syntax as the caret enters it and closes up again on the way
            // out, which is the whole point of the hybrid engine and is
            // invisible in a still.
            QTest::keyClick(window, Qt::Key_Home, Qt::NoModifier, 60);
            settle(600);
            for (int i = 0; i < 80; ++i)
                QTest::keyClick(window, Qt::Key_Right, Qt::NoModifier, 52);
            settle(800);

            // Focus away, and the whole line renders again.
            if (QQuickItem *heading = delegateAt(window, 0))
                clickAt(window, centerOf(heading), 1800);
        } else if (scenario == QStringLiteral("tour-math")) {
            if (!openNote(ctx, vault, QStringLiteral("Calculus.md"))) {
                app.exit(3);
                return;
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
        } else if (scenario == QStringLiteral("tour-repair")) {
            if (!openNote(ctx, vault, QStringLiteral("Welcome.md"))) {
                app.exit(3);
                return;
            }
            // Box art of the kind a language model emits, with the columns
            // not quite meeting. It arrives fenced, because that is the shape
            // a model's answer actually has and it is the paste path that
            // produces a block rather than a run of paragraphs.
            QGuiApplication::clipboard()->setText(QStringLiteral(
                "```\n"
                "+--------+       +---------+\n"
                "| Commit |------>|  CI run |\n"
                "+--------+       +---------+\n"
                "     |                 |\n"
                "     v                 v\n"
                "  +---------+    +----------+\n"
                "  | Package |    | Release |\n"
                "  +---------+    +----------+\n"
                "```\n"));
            settle(400);

            model->insertBlock(model->count(), Block::Paragraph, QString());
            settle(500);
            const int target = model->count() - 1;
            if (QQuickItem *d = delegateAt(window, target))
                clickAt(window, centerOf(d), 400);
            else
                clickEditorBlock(window, target);
            settle(400);
            QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier, 80);
            settle(2200);
            qInfo("uidriver: pasted block: [%s]",
                  qPrintable(model->getContent(model->count() - 1)));

            // The pasted block holds focus, so it shows its source. Focus
            // away and the straightened art renders as a diagram, which is
            // the half of this that a viewer is here for.
            if (QQuickItem *heading = delegateAt(window, 0))
                clickAt(window, centerOf(heading), 1800);

            // The inverse gesture, on the note's own flowchart: copy a
            // rendered diagram back out as text.
            if (auto *chip = namedItem(window, "diagramCopyTextChip")) {
                if (chip->isVisible() && chip->width() > 0)
                    clickAt(window, centerOf(chip), 1600);
            }
            settle(1600);
        } else if (scenario == QStringLiteral("tour-query")) {
            if (!openNote(ctx, vault, QStringLiteral("Project board.md"),
                          3200)) {
                app.exit(3);
                return;
            }
            // Long enough for the vault scan behind the first evaluation to
            // finish. A change written while that is still in flight is not
            // picked up, and the table then sits on its first answer.
            settle(5000);

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
        }

        // A closing beat, so a screen recorder stopped by hand does not lose
        // the last thing that happened, and a last frame on disk, so a take
        // can be checked without scrubbing the video.
        if (scenario.startsWith(QStringLiteral("tour-"))) {
            settle(1200);
            grab(window, outDir + QStringLiteral("/") + scenario
                             + QStringLiteral("-final.png"));
        }

        app.quit();
    });

    return app.exec();
}
