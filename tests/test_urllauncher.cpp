// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QSignalSpy>

#include "urllauncher.h"

// What a click on a link is answered with.
//
// The openers are stubbed with programs whose behaviour is known -- /bin/false
// refuses, /bin/true accepts, sleep keeps running -- because the alternative
// is launching whatever browser the machine running this suite happens to
// have, on the desk of whoever is running it.
class TestUrlLauncher : public QObject
{
    Q_OBJECT

private:
    static UrlLauncher::Opener at(const QString &program,
                                  const QStringList &args = {},
                                  bool exitCodeIsAVerdict = true)
    {
        const QString path = QStandardPaths::findExecutable(program);
        return {path, args, exitCodeIsAVerdict};
    }

private slots:
    void initTestCase()
    {
        // Every case below rests on these three existing.
        for (const char *tool : {"true", "false", "sh"}) {
            QVERIFY2(!QStandardPaths::findExecutable(QLatin1String(tool)).isEmpty(),
                     tool);
        }
    }

    void nothingToOpenWithFails()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({});
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        launcher.open(QStringLiteral("https://example.com"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(),
                 QStringLiteral("https://example.com"));
        QCOMPARE(opened.count(), 0);
    }

    void anOpenerThatExitsZeroOpenedIt()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("true"))});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 5000);
        QCOMPARE(failed.count(), 0);
    }

    // The case this class exists for: every opener on the machine refuses,
    // which is what xdg-open, `gio open` and sensible-browser all do when no
    // browser is installed. Qt's own call reports success here.
    void everyOpenerRefusingIsAFailure()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("false")),
                                     at(QStringLiteral("false"))});
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(opened.count(), 0);
    }

    // A refusal moves on to the next candidate rather than ending the
    // attempt: a desktop commonly offers several openers and only the last
    // knows how to answer.
    void aRefusalFallsThroughToTheNextOpener()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("false")),
                                     at(QStringLiteral("true"))});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 5000);
        QCOMPARE(failed.count(), 0);
    }

    // A browser started in the foreground never exits while the reader is
    // reading, so "has not refused yet" is the only success it can offer.
    void anOpenerStillRunningCountsAsOpened()
    {
        UrlLauncher launcher;
        launcher.setVerdictMsForTests(200);
        // Through sh, so the URL this launcher appends lands harmlessly in $0
        // rather than being read as a second argument.
        launcher.setOpenersForTests({at(QStringLiteral("sh"),
                                        {QStringLiteral("-c"),
                                         QStringLiteral("sleep 30")})});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 5000);
        QCOMPARE(failed.count(), 0);
    }

    // An opener whose exit code says nothing -- explorer.exe, the way a WSL
    // session reaches the browser on the Windows side, exits 1 on success --
    // is taken at the launch rather than at the code.
    void anOpenerWithoutAMeaningfulExitCodeIsTrusted()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("false"), {},
                                        /*exitCodeIsAVerdict=*/false)});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 5000);
        QCOMPARE(failed.count(), 0);
    }

    // ---- Which spelling of a file each opener is handed ----------------
    //
    // The reader clicked a file in this Linux session and the program that
    // will open it runs on the Windows side. It cannot follow a Linux path:
    // handed file:///home/sk/tools/check.py, explorer.exe opens the reader's
    // Documents folder, and because its exit code is no verdict the click
    // reports success. That is what "Open with desktop" did under WSL, and
    // converting the path first is the fix.
    void aWindowsSideOpenerIsHandedAWindowsPath()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("wslpath")).isEmpty())
            QSKIP("wslpath is a WSL tool; nothing here can do the conversion");
        const UrlLauncher::Opener explorer{
            QStringLiteral("/mnt/c/WINDOWS/explorer.exe"), {},
            /*exitCodeIsAVerdict=*/false, UrlLauncher::FileForm::WindowsPath};
        // A directory every WSL distribution has, so the case rests on the
        // conversion rather than on the fixture.
        const QString argument =
            UrlLauncher::argumentFor(explorer, QStringLiteral("file:///home"));
        QVERIFY2(!argument.isEmpty(), "wslpath converted nothing");
        QVERIFY2(argument.contains(QLatin1Char('\\')), qPrintable(argument));
        QVERIFY2(!argument.startsWith(QLatin1String("file:")),
                 qPrintable(argument));
    }

    // wslview converts the path itself on the way through, and a path is
    // what its documented interface takes.
    void wslviewIsHandedThePathWithoutTheScheme()
    {
        const UrlLauncher::Opener wslview{
            QStringLiteral("/usr/bin/wslview"), {}, true,
            UrlLauncher::FileForm::LocalPath};
        QCOMPARE(UrlLauncher::argumentFor(
                     wslview, QStringLiteral("file:///home/reader/report.pdf")),
                 QStringLiteral("/home/reader/report.pdf"));
    }

    // Only files are rewritten. There is no path in a web address for any of
    // this to apply to, and the Windows shell opens one perfectly well.
    void aWebUrlReachesEveryOpenerUnchanged()
    {
        const QString url = QStringLiteral("https://example.com/a?b=c#d");
        for (const UrlLauncher::FileForm form :
             {UrlLauncher::FileForm::Url, UrlLauncher::FileForm::LocalPath,
              UrlLauncher::FileForm::WindowsPath}) {
            const UrlLauncher::Opener opener{QStringLiteral("/bin/true"), {},
                                             true, form};
            QCOMPARE(UrlLauncher::argumentFor(opener, url), url);
        }
    }

    // A file URL naming no path cannot be spelled for a Windows-side opener.
    // Running it anyway is how an unrelated folder opens over a click that
    // then says it worked, so the candidate is skipped exactly like a
    // refusal -- and with nothing behind it the click fails honestly.
    void anOpenerThatCannotSpellTheFileIsSkipped()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({{QStringLiteral("/bin/true"), {},
                                      /*exitCodeIsAVerdict=*/false,
                                      UrlLauncher::FileForm::WindowsPath}});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QStringLiteral("file:"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(opened.count(), 0);
    }

    // The wiring on whichever machine this is running on: where the Windows
    // shell is the last resort, it has to be the one asking for a Windows
    // path, or the conversion above never happens in the application.
    void theWindowsShellFallbackAsksForAWindowsPath()
    {
        bool found = false;
        const QList<UrlLauncher::Opener> openers = UrlLauncher::desktopOpeners();
        for (const UrlLauncher::Opener &opener : openers) {
            if (!opener.program.endsWith(QLatin1String("explorer.exe")))
                continue;
            found = true;
            QVERIFY(opener.fileForm == UrlLauncher::FileForm::WindowsPath);
            QVERIFY(!opener.exitCodeIsAVerdict);
        }
        if (!found)
            QSKIP("not a WSL session: no Windows shell among the openers");
    }

    // A program that is not there at all is a refusal, not a crash: a
    // candidate list is built from what was on PATH when the window opened,
    // and PATH outlives that.
    void anOpenerThatCannotStartIsARefusal()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({{QStringLiteral("/nonexistent/opener"), {}, true},
                                     at(QStringLiteral("true"))});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        launcher.open(QStringLiteral("https://example.com"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 5000);
    }

    // What a note may hand to the desktop. The list is short because the URL
    // is written by whoever wrote the note, and on WSL the openers include
    // the Windows shell and every handler registered with it.
    void schemesTheDesktopIsAskedAbout_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("openable");
        QTest::newRow("https") << "https://example.com/a" << true;
        QTest::newRow("http") << "http://example.com" << true;
        QTest::newRow("uppercase") << "HTTPS://example.com" << true;
        QTest::newRow("mailto") << "mailto:someone@example.com" << true;
        QTest::newRow("file") << "file:///home/reader/report.pdf" << true;
        QTest::newRow("application scheme") << "ms-msdt:/id PCWDiagnostic" << false;
        QTest::newRow("windows search") << "search-ms:query=x" << false;
        QTest::newRow("javascript") << "javascript:alert(1)" << false;
        QTest::newRow("data") << "data:text/html,<h1>x</h1>" << false;
        QTest::newRow("smb share") << "smb://host/share" << false;
        QTest::newRow("no scheme") << "example.com/a" << false;
        QTest::newRow("relative path") << "docs/readme.md" << false;
    }
    void schemesTheDesktopIsAskedAbout()
    {
        QFETCH(QString, url);
        QFETCH(bool, openable);
        QCOMPARE(UrlLauncher::isOpenableScheme(url), openable);

        // A refused scheme runs nothing at all, and says so as its own answer
        // rather than as "no browser", which would be a different fix.
        if (openable)
            return;
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("true"))});
        QSignalSpy refused(&launcher, &UrlLauncher::refused);
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(url);
        QCOMPARE(refused.count(), 1);
        QTest::qWait(200);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void anEmptyUrlAsksNothingOfTheDesktop()
    {
        UrlLauncher launcher;
        launcher.setOpenersForTests({at(QStringLiteral("true"))});
        QSignalSpy opened(&launcher, &UrlLauncher::opened);
        QSignalSpy failed(&launcher, &UrlLauncher::failed);
        launcher.open(QString());
        QTest::qWait(300);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(failed.count(), 0);
    }
};

QTEST_MAIN(TestUrlLauncher)
#include "test_urllauncher.moc"
