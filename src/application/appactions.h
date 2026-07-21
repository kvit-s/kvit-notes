// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef APPACTIONS_H
#define APPACTIONS_H

#include <QObject>
#include <QString>
#include <QRectF>
#include <QUrl>

// The shell-level actions a block delegate can ask for.
//
// Delegates used to reach the application window directly:
//
//     var win = Window.window
//     if (win && win.insertImageIntoBlock)
//         win.insertImageIntoBlock(idx)
//
// `Window.window` is typed QQuickWindow, which declares none of these, so the
// call could not be checked and the `if` was a probe standing in for a
// contract nobody had written. There were 113 such calls across 13 files.
//
// Signals cannot be forwarded up a delegate tree cheaply here:
// EditableBlock.qml is instantiated by seven other delegate files, so a signal
// it emitted would have to be re-declared and re-emitted at every level, and
// the intermediate declarations would explain nothing to anyone reading them.
// Routing through one object instead means nesting depth stops mattering.
//
// The trade this makes, stated rather than glossed: this is a global mediator,
// and a mediator is real coupling. What it buys is that the coupling is typed,
// enumerable and greppable — thirteen named signals with signatures qmllint
// checks — where the coupling it replaces was an ambient probe into whatever
// `Window.window` happened to return. It is deliberately NOT a general
// dispatcher: a single `request(name, args)` would be the same duck-typing
// wearing a different hat, and would check nothing.
//
// Each request() method emits the matching signal and nothing else. A signal
// with no connection does nothing at all, which is exactly what the old
// `if (win && win.x)` guard achieved when the window did not implement x — so
// the call sites keep their behaviour by construction rather than by care.
class AppActions : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Navigation and scrolling.
    Q_INVOKABLE void requestScrollToBlock(int index) { emit scrollToBlockRequested(index); }
    Q_INVOKABLE void requestOpenNoteByPath(const QString &relPath) { emit openNoteByPathRequested(relPath); }
    Q_INVOKABLE void requestCenterCaretLine(QObject *item) { emit centerCaretLineRequested(item); }

    // Menus the shell owns and a delegate asks it to raise.
    Q_INVOKABLE void requestTextContextMenu(QObject *target) { emit textContextMenuRequested(target); }
    Q_INVOKABLE void requestLinkContextMenu(QObject *target) { emit linkContextMenuRequested(target); }
    Q_INVOKABLE void requestBlockHandleMenu(QObject *target) { emit blockHandleMenuRequested(target); }

    // Insert flows that need a dialog, so they belong to the window.
    Q_INVOKABLE void requestInsertImage(int index) { emit insertImageRequested(index); }
    Q_INVOKABLE void requestInsertEmbed(int index) { emit insertEmbedRequested(index); }
    Q_INVOKABLE void requestInsertTable(int index) { emit insertTableRequested(index); }

    // Chrome.
    Q_INVOKABLE void requestLightbox(const QString &source, const QString &alt)
    {
        emit lightboxRequested(source, alt);
    }
    Q_INVOKABLE void requestTransientStatus(const QString &message)
    {
        emit transientStatusRequested(message);
    }

    // Objects the window owns that a delegate asks it to drive. These were
    // reached as `win.selectionKeyHandler.forceActiveFocus()` and the like —
    // a delegate reaching through the window into one of its children, two
    // layers past anything it should know about. Asking for the effect
    // instead means the child stays private to the shell.
    Q_INVOKABLE void requestSelectionFocus() { emit selectionFocusRequested(); }
    Q_INVOKABLE void requestOpenLink(const QUrl &url) { emit openLinkRequested(url); }
    Q_INVOKABLE void requestBlockMenu(int index, const QString &mode, const QRectF &area)
    {
        emit blockMenuRequested(index, mode, area);
    }
    Q_INVOKABLE void requestMathCommandMenu(QObject *host, const QRectF &area, bool displayMath)
    {
        emit mathCommandMenuRequested(host, area, displayMath);
    }
    Q_INVOKABLE void requestWikiLinkMenu(QObject *host, const QRectF &area)
    {
        emit wikiLinkMenuRequested(host, area);
    }
    Q_INVOKABLE void requestEditLink(int index, int start, int end, const QString &text,
                                     const QString &url, bool removable)
    {
        emit editLinkRequested(index, start, end, text, url, removable);
    }
    Q_INVOKABLE void requestInsertLink(int index, int start, int end, const QString &text)
    {
        emit insertLinkRequested(index, start, end, text);
    }

signals:
    void scrollToBlockRequested(int index);
    void openNoteByPathRequested(const QString &relPath);
    void centerCaretLineRequested(QObject *item);
    void textContextMenuRequested(QObject *target);
    void linkContextMenuRequested(QObject *target);
    void blockHandleMenuRequested(QObject *target);
    void insertImageRequested(int index);
    void insertEmbedRequested(int index);
    void insertTableRequested(int index);
    void lightboxRequested(const QString &source, const QString &alt);
    void transientStatusRequested(const QString &message);
    void selectionFocusRequested();
    void openLinkRequested(const QUrl &url);
    void blockMenuRequested(int index, const QString &mode, const QRectF &area);
    void mathCommandMenuRequested(QObject *host, const QRectF &area, bool displayMath);
    void wikiLinkMenuRequested(QObject *host, const QRectF &area);
    void editLinkRequested(int index, int start, int end, const QString &text,
                           const QString &url, bool removable);
    void insertLinkRequested(int index, int start, int end, const QString &text);
};

#endif // APPACTIONS_H
