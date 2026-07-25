// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef URLLAUNCHER_H
#define URLLAUNCHER_H

#include <QObject>
#include <QString>
#include <QStringList>

// Handing a link to the desktop, and knowing whether that worked.
//
// QDesktopServices::openUrl() (and Qt.openUrlExternally, which is the same
// call) cannot answer the second half on Unix. On a machine with no browser
// registered it prints
//
//     qt.qpa.services: Unable to detect a web browser to launch '<url>'
//
// to the terminal and then returns TRUE anyway, measured on Qt 6.10 under
// WSL. A caller that trusts the return value reports success for a click that
// did nothing, and the reader is left with a link that silently fails and an
// application that says nothing -- which is the state this class exists to
// end.
//
// So the handoff happens here instead: the openers a desktop offers are tried
// in order, as child processes whose exit status is the answer. An opener
// that exits non-zero (xdg-open with no handler, `gio open` with none,
// sensible-browser with no browser installed) is a refusal and the next
// candidate is tried; one that exits zero has opened the link; one that is
// still running after a moment is a browser started in the foreground, which
// never exits while the reader is reading, and counts as opened. When every
// candidate refuses, failed() says so and the window can tell the reader
// rather than leaving the click looking ignored.
//
// What this gives up against Qt's own implementation is its XDG portal path,
// used when a sandboxed application cannot see the host's handlers. A desktop
// with a portal has xdg-open as well, so the first candidate covers it.
//
// Windows and macOS keep QDesktopServices::openUrl: both have a real system
// handler, and there the return value means what it says.
class UrlLauncher : public QObject
{
    Q_OBJECT

public:
    explicit UrlLauncher(QObject *parent = nullptr);

    // One program that can be asked to open a URL.
    struct Opener {
        QString program;        // absolute path
        QStringList args;       // fixed arguments, before the URL
        // False for a program whose exit code says nothing about whether the
        // URL opened. Windows' explorer.exe, which is how a WSL session
        // reaches the browser on the Windows side, exits 1 on success.
        bool exitCodeIsAVerdict = true;
    };

    // Hand a URL to the desktop. The answer arrives as opened(), failed() or
    // refused(), not as a return value: on Unix it takes as long as an opener
    // takes to refuse, and refusing may run through several of them.
    Q_INVOKABLE void open(const QString &url);

    // The schemes a link in a note may be opened with: the web, mail, and a
    // local file, which is what people put in notes. Everything else is
    // refused before any program is run.
    //
    // The URL comes from a document, and a document is written by whoever
    // handed it to the reader. A scheme outside this list is how that writer
    // reaches an application-specific handler on the reader's machine -- the
    // shape behind the ms-msdt: class of attack -- and under WSL the openers
    // below include the Windows shell, which knows a great many such
    // handlers. An allowlist rather than a list of dangerous schemes: the
    // dangerous ones are whatever the reader's machine has registered, which
    // this process cannot enumerate and has no business guessing at.
    static bool isOpenableScheme(const QString &url);

    // The openers this desktop offers, most specific first. Empty when
    // nothing on this machine can open a URL.
    static QList<Opener> desktopOpeners();

    // ---- Test seams ----
    // Programs whose behaviour the test controls (/bin/false to refuse,
    // /bin/true to accept, sleep to stay running) rather than whatever
    // browser the machine running the suite happens to have, which a test
    // must never launch. Deliberately not Q_INVOKABLE: nothing reachable from
    // QML may choose which program this class runs.
    void setOpenersForTests(const QList<Opener> &openers);
    // How long an opener is given to refuse before it is taken to have
    // worked. Shortened so a test does not wait out the real interval.
    void setVerdictMsForTests(int ms) { m_verdictMs = ms; }

signals:
    void opened(const QString &url);
    // Every opener this desktop has refused it, or it has none.
    void failed(const QString &url);
    // Nothing was asked of the desktop: the URL names a scheme this
    // application does not open (see isOpenableScheme).
    void refused(const QString &url);

private:
    void tryOpener(const QString &url, int index);

    QList<Opener> m_openers;
    int m_verdictMs = 1500;
};

#endif // URLLAUNCHER_H
