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
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest/QTest>

#include <functional>

#include "blockmodel.h"
#include "extensionregistry.h"
#include "kvitapplication.h"

namespace {

QQuickWindow *shellWindow(KvitApplication &kvit)
{
    const auto roots = kvit.engine().rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
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
    int winW = 0, winH = 0;
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
        else if (arg.startsWith(QStringLiteral("--size=")))
            (void)sscanf(qPrintable(arg.section(QLatin1Char('='), 1)),
                         "%dx%d", &winW, &winH);
    }

    QStringList startArgs{QString::fromLatin1(argv[0])};
    if (!vault.isEmpty())
        startArgs << vault;
    if (!kvit.start(startArgs))
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
        window->show();
        window->requestActivate();
        settle(1200);

        auto *model = kvit.context().blockModel();

        if (scenario == QStringLiteral("still")) {
            // One staged frame of the real shell: open the requested note in
            // the (already startup-opened) vault, give async renderers
            // (math images, diagram layout) time to land, and grab. Used to
            // produce the curated screenshots/press stills.
            if (!note.isEmpty()) {
                kvit.context().documentManager()->open(
                    QUrl::fromLocalFile(note));
            }
            settle(2500);
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
        }

        app.quit();
    });

    return app.exec();
}
