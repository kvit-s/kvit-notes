// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FAULTINJECTION_H
#define FAULTINJECTION_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

// Making the filesystem fail, from a test.
//
// Several defects this codebase has shipped were invisible to the suite for
// want of a way to make I/O fail: a save that ignored a short write, a
// capture that dropped the user's text when the vault was read-only, an
// import that counted a truncated copy as a success. Each was eventually
// caught by a test that reached for the OS directly, and each did so
// differently — one probed for the root bypass and skipped, one did not; one
// handled Windows, one did not; all restored state by hand, so an assertion
// that failed early left the filesystem modified for whatever ran next.
//
// These are those techniques, written once. Each guard restores what it
// changed in its destructor, so an early return or a failed QVERIFY cannot
// leak the condition into the next test.
//
// Every guard reports `supported()`. A denial that the platform or the
// current user ignores — root bypasses directory permissions, and NTFS
// ignores the read-only bit on a directory when creating a file inside it —
// must become a QSKIP, because the alternative is a test that passes without
// having tested anything:
//
//     FaultInjection::DeniedWrites denied(dir.path());
//     if (!denied.supported())
//         QSKIP(qPrintable(denied.skipReason()));
//
// These deliberately do not reach into production code. An earlier decision
// (tests/test_systemintegration.cpp) declined to add a write-failure seam to
// NoteCollection on the grounds that it was a wider change than the fix it
// served, and that reasoning still holds: injecting at the OS boundary tests
// the real write path, including the parts of it that Qt implements.
namespace FaultInjection {

namespace detail {

// Can this process still create a file under `dirPath`? The permission bits
// are advisory for root, and mean something different on NTFS, so the only
// reliable answer is to try.
inline bool canCreateFileIn(const QString &dirPath)
{
    const QString probe = QDir(dirPath).filePath(
        QStringLiteral(".kvit-faultinjection-probe"));
    QFile file(probe);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.close();
    QFile::remove(probe);
    return true;
}

} // namespace detail

// Denies creation of new files in a directory for the guard's lifetime.
class DeniedWrites
{
public:
    explicit DeniedWrites(const QString &dirPath)
        : m_dirPath(dirPath)
    {
        m_original = QFile::permissions(dirPath);
        m_applied = QFile::setPermissions(
            dirPath,
            m_original & ~(QFile::WriteOwner | QFile::WriteUser
                           | QFile::WriteGroup | QFile::WriteOther));
        m_effective = m_applied && !detail::canCreateFileIn(dirPath);
    }

    ~DeniedWrites()
    {
        if (m_applied)
            QFile::setPermissions(m_dirPath, m_original);
    }

    DeniedWrites(const DeniedWrites &) = delete;
    DeniedWrites &operator=(const DeniedWrites &) = delete;

    bool supported() const { return m_effective; }

    QString skipReason() const
    {
        if (!m_applied)
            return QStringLiteral("could not change permissions on %1")
                .arg(m_dirPath);
        return QStringLiteral(
            "this platform or user bypasses directory write permissions "
            "(running as root, or a filesystem that ignores the bit)");
    }

private:
    QString m_dirPath;
    QFile::Permissions m_original;
    bool m_applied = false;
    bool m_effective = false;
};

// Denies writes to one existing file. This is the form that works on
// Windows, where the read-only bit on a directory does not stop creation
// inside it but does stop a write to the file itself.
class DeniedFileWrites
{
public:
    explicit DeniedFileWrites(const QString &filePath)
        : m_filePath(filePath)
    {
        if (!QFileInfo::exists(filePath))
            return;
        m_original = QFile::permissions(filePath);
        m_applied = QFile::setPermissions(filePath, QFile::ReadOwner
                                                        | QFile::ReadUser);
        if (m_applied) {
            QFile probe(filePath);
            m_effective = !probe.open(QIODevice::WriteOnly);
        }
    }

    ~DeniedFileWrites()
    {
        if (m_applied)
            QFile::setPermissions(m_filePath, m_original);
    }

    DeniedFileWrites(const DeniedFileWrites &) = delete;
    DeniedFileWrites &operator=(const DeniedFileWrites &) = delete;

    bool supported() const { return m_effective; }

    QString skipReason() const
    {
        if (!m_applied)
            return QStringLiteral("could not change permissions on %1")
                .arg(m_filePath);
        return QStringLiteral(
            "this user bypasses file write permissions (running as root?)");
    }

private:
    QString m_filePath;
    QFile::Permissions m_original;
    bool m_applied = false;
    bool m_effective = false;
};

// Caps how many bytes this process may write to any single file, so a write
// past the cap fails partway through — the shape a full disk or an exceeded
// quota produces, without needing either. Unix only; `supported()` is false
// elsewhere.
class FileSizeLimit
{
public:
    explicit FileSizeLimit(qint64 bytes)
    {
#ifdef Q_OS_UNIX
        if (getrlimit(RLIMIT_FSIZE, &m_original) != 0)
            return;
        // Writing past the cap raises SIGXFSZ, whose default disposition
        // terminates the process; ignoring it turns the write into an
        // ordinary error return.
        m_previousHandler = signal(SIGXFSZ, SIG_IGN);
        struct rlimit capped = m_original;
        capped.rlim_cur = static_cast<rlim_t>(bytes);
        m_applied = setrlimit(RLIMIT_FSIZE, &capped) == 0;
        if (!m_applied)
            signal(SIGXFSZ, m_previousHandler);
#else
        Q_UNUSED(bytes);
#endif
    }

    ~FileSizeLimit()
    {
#ifdef Q_OS_UNIX
        if (m_applied) {
            setrlimit(RLIMIT_FSIZE, &m_original);
            signal(SIGXFSZ, m_previousHandler);
        }
#endif
    }

    FileSizeLimit(const FileSizeLimit &) = delete;
    FileSizeLimit &operator=(const FileSizeLimit &) = delete;

    bool supported() const { return m_applied; }

    QString skipReason() const
    {
        return QStringLiteral(
            "forcing a short write needs RLIMIT_FSIZE, which this platform "
            "does not provide");
    }

private:
    bool m_applied = false;
#ifdef Q_OS_UNIX
    struct rlimit m_original;
    void (*m_previousHandler)(int) = SIG_DFL;
#endif
};

} // namespace FaultInjection

#endif // FAULTINJECTION_H
