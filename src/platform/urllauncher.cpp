// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "urllauncher.h"

#include <QDesktopServices>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

using FileForm = UrlLauncher::FileForm;

// Add `program` to `openers` if it is on PATH.
void addIfPresent(QList<UrlLauncher::Opener> &openers, const QString &program,
                  const QStringList &args = {}, bool exitCodeIsAVerdict = true,
                  FileForm fileForm = FileForm::Url)
{
    const QString path = QStandardPaths::findExecutable(program);
    if (!path.isEmpty())
        openers.append({path, args, exitCodeIsAVerdict, fileForm});
}

// A WSL session has no browser of its own but reaches the one on the Windows
// side, which is what wslview does when it is installed. explorer.exe is the
// fallback for when it is not: it is always there, and handing it an http URL
// opens the Windows default browser.
bool runningUnderWsl()
{
    QFile version(QStringLiteral("/proc/version"));
    if (!version.open(QIODevice::ReadOnly))
        return false;
    return version.readAll().toLower().contains("microsoft");
}

#endif

} // namespace

UrlLauncher::UrlLauncher(QObject *parent)
    : QObject(parent)
    , m_openers(desktopOpeners())
{
}

QList<UrlLauncher::Opener> UrlLauncher::desktopOpeners()
{
    QList<Opener> openers;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // The reader's own choice first, as every convention on this platform
    // says. BROWSER holds a colon-separated list; a %s placeholder in an
    // entry is a shell-level convention this does not implement, so an entry
    // carrying one is skipped rather than run with the placeholder intact.
    const QString browserEnv = qEnvironmentVariable("BROWSER");
    const QStringList browserList = browserEnv.split(QLatin1Char(':'),
                                                     Qt::SkipEmptyParts);
    for (const QString &entry : browserList) {
        if (entry.contains(QLatin1String("%s")))
            continue;
        addIfPresent(openers, entry);
    }
    // The desktop's own opener, then the two toolkit ones, then the Debian
    // alternatives. Each of these refuses with a non-zero exit when it cannot
    // find a handler, which is exactly the answer wanted here.
    addIfPresent(openers, QStringLiteral("xdg-open"));
    addIfPresent(openers, QStringLiteral("gio"), {QStringLiteral("open")});
    addIfPresent(openers, QStringLiteral("kde-open5"));
    addIfPresent(openers, QStringLiteral("kde-open"));
    addIfPresent(openers, QStringLiteral("gnome-open"));
    addIfPresent(openers, QStringLiteral("wslview"), {},
                 /*exitCodeIsAVerdict=*/true, FileForm::LocalPath);
    addIfPresent(openers, QStringLiteral("x-www-browser"));
    addIfPresent(openers, QStringLiteral("sensible-browser"));
    if (runningUnderWsl()) {
        addIfPresent(openers, QStringLiteral("explorer.exe"), {},
                     /*exitCodeIsAVerdict=*/false, FileForm::WindowsPath);
    }
#endif
    return openers;
}

QString UrlLauncher::windowsPathFor(const QString &localPath)
{
    if (localPath.isEmpty())
        return {};
    const QString wslpath = QStandardPaths::findExecutable(
        QStringLiteral("wslpath"));
    if (wslpath.isEmpty())
        return {};
    QProcess convert;
    convert.start(wslpath, {QStringLiteral("-w"), localPath});
    // A path conversion is a few milliseconds of string work with no I/O
    // beyond reading the mount table, so waiting for it inside the click is
    // cheaper than the machinery to do it asynchronously. The timeout is
    // there for the case where interoperability is switched off and the
    // process never returns at all.
    if (!convert.waitForFinished(2000)
        || convert.exitStatus() != QProcess::NormalExit
        || convert.exitCode() != 0) {
        return {};
    }
    return QString::fromLocal8Bit(convert.readAllStandardOutput()).trimmed();
}

QString UrlLauncher::argumentFor(const Opener &opener, const QString &url)
{
    const QUrl parsed(url);
    if (opener.fileForm == FileForm::Url || !parsed.isLocalFile())
        return url;
    const QString localPath = parsed.toLocalFile();
    if (localPath.isEmpty())
        return {};
    if (opener.fileForm == FileForm::LocalPath)
        return localPath;
    return windowsPathFor(localPath);
}

void UrlLauncher::setOpenersForTests(const QList<Opener> &openers)
{
    m_openers = openers;
    m_testOpenersSet = true;
}

bool UrlLauncher::isOpenableScheme(const QString &url)
{
    const QString scheme = QUrl(url).scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("mailto") || scheme == QLatin1String("file");
}

void UrlLauncher::open(const QString &url)
{
    if (url.isEmpty())
        return;
    if (!isOpenableScheme(url)) {
        emit refused(url);
        return;
    }
    // Tests inject known processes instead of opening the machine's browser.
    // This branch deliberately precedes the native-platform branch: Windows
    // and macOS used to ignore the seam, launching QDesktopServices and making
    // the same unit suite mean something different on every operating system.
    if (m_testOpenersSet) {
        if (m_openers.isEmpty())
            emit failed(url);
        else
            tryOpener(url, 0);
        return;
    }

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    if (QDesktopServices::openUrl(QUrl(url)))
        emit opened(url);
    else
        emit failed(url);
#else
    if (m_openers.isEmpty()) {
        emit failed(url);
        return;
    }
    tryOpener(url, 0);
#endif
}

void UrlLauncher::tryOpener(const QString &url, int index)
{
    if (index >= m_openers.size()) {
        emit failed(url);
        return;
    }
    const Opener opener = m_openers.at(index);

    // Nothing this opener could be handed names the file. Running it anyway
    // is how a Windows-side opener ends up showing an unrelated folder and
    // reporting success, so it is skipped exactly like a refusal.
    const QString argument = argumentFor(opener, url);
    if (argument.isEmpty()) {
        tryOpener(url, index + 1);
        return;
    }

    // The process outlives this call: a browser started in the foreground
    // runs for as long as the reader reads. It is parented to this launcher
    // so a window closing takes its watchers with it, and deletes itself when
    // it does eventually finish.
    auto *process = new QProcess(this);
    QPointer<UrlLauncher> guard(this);
    // Whether this attempt has already been answered, so the verdict timer
    // and the process's own exit cannot both report on it.
    auto settled = std::make_shared<bool>(false);

    QObject::connect(process, &QProcess::finished, process,
                     [this, guard, process, url, index, opener, settled](
                         int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (*settled || !guard)
            return;
        *settled = true;
        const bool worked = !opener.exitCodeIsAVerdict
                          || (status == QProcess::NormalExit && exitCode == 0);
        if (worked)
            emit opened(url);
        else
            tryOpener(url, index + 1);   // this one refused; ask the next
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [this, guard, process, url, index, settled](QProcess::ProcessError) {
        process->deleteLater();
        if (*settled || !guard)
            return;
        *settled = true;
        tryOpener(url, index + 1);
    });

    // Still running after the verdict window: a browser, not an opener that
    // was going to refuse. The process is left alone from here.
    QTimer::singleShot(m_verdictMs, this, [this, guard, url, settled]() {
        if (*settled || !guard)
            return;
        *settled = true;
        emit opened(url);
    });

    process->start(opener.program, opener.args + QStringList{argument});
}
