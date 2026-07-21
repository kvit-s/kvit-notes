// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "vaultlock.h"

#include "vaultpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSysInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcVaultLock, "kvit.vaultlock")

namespace {

#ifdef Q_OS_WIN
using NativeHandle = HANDLE;
const NativeHandle kInvalidHandle = INVALID_HANDLE_VALUE;
#else
using NativeHandle = int;
const NativeHandle kInvalidHandle = -1;
#endif

// One entry per vault this process holds for writing, and at most one holder
// per vault: the descriptor that carries the kernel lock, or an invalid one
// when the filesystem could not provide it. The table is what makes a second
// write-capable collection on the same root refusable, which a POSIX flock
// cannot do on its own -- it is held per process, so this process would
// never contend with itself.
struct Owned {
    NativeHandle handle = kInvalidHandle;
};

QMutex g_mutex;
QHash<QString, Owned> g_owned;
bool g_forcedUnavailable = false;

QString lockFilePath(const QString &vaultRoot)
{
    return QDir(vaultRoot).filePath(QStringLiteral(".kvit/vault.lock"));
}

QByteArray describeThisProcess()
{
    QJsonObject object;
    object.insert(QStringLiteral("host"), QSysInfo::machineHostName());
    object.insert(QStringLiteral("application"),
                  QCoreApplication::applicationName().isEmpty()
                      ? QStringLiteral("Kvit Notes")
                      : QCoreApplication::applicationName());
    object.insert(QStringLiteral("pid"),
                  static_cast<double>(QCoreApplication::applicationPid()));
    object.insert(QStringLiteral("since"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Read the holder's description. Advisory: an unreadable, truncated or
// garbage file just yields empty fields and a vaguer message.
VaultLock::Holder readHolder(const QString &path)
{
    VaultLock::Holder holder;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return holder;
    const QJsonObject object =
        QJsonDocument::fromJson(file.read(4096)).object();
    holder.host = object.value(QStringLiteral("host")).toString();
    holder.application = object.value(QStringLiteral("application")).toString();
    holder.pid = static_cast<qint64>(object.value(QStringLiteral("pid")).toDouble());
    holder.since = QDateTime::fromString(
        object.value(QStringLiteral("since")).toString(), Qt::ISODate);
    return holder;
}

void closeHandle(NativeHandle handle)
{
    if (handle == kInvalidHandle)
        return;
#ifdef Q_OS_WIN
    CloseHandle(handle);
#else
    ::close(handle);
#endif
}

enum class NativeResult { Locked, Contended, Failed };

// Open the lock file and take an exclusive, non-blocking lock on it. The
// descriptor stays open: the lock lives exactly as long as it does, which is
// what makes the kernel release it when the process dies by any means.
NativeResult nativeAcquire(const QString &path, NativeHandle *out)
{
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(
        reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()),
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return NativeResult::Failed;
    OVERLAPPED overlapped = {};
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, 1, 0, &overlapped)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        return (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING)
                   ? NativeResult::Contended
                   : NativeResult::Failed;
    }
    *out = handle;
    return NativeResult::Locked;
#else
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return NativeResult::Failed;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int saved = errno;
        ::close(fd);
        // EWOULDBLOCK is the only answer that means "someone else has it".
        // ENOLCK, EOPNOTSUPP and friends mean this filesystem does not do
        // locking, which must not stop the vault from opening.
        return saved == EWOULDBLOCK ? NativeResult::Contended
                                    : NativeResult::Failed;
    }
    *out = fd;
    return NativeResult::Locked;
#endif
}

// Replace the file's contents with this process's description. Best effort:
// failing to write it costs a good message, never the lock.
void writeHolder(NativeHandle handle, const QByteArray &payload)
{
#ifdef Q_OS_WIN
    SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
    DWORD written = 0;
    WriteFile(handle, payload.constData(), static_cast<DWORD>(payload.size()),
              &written, nullptr);
    SetEndOfFile(handle);
    FlushFileBuffers(handle);
#else
    if (::ftruncate(handle, 0) != 0)
        return;
    if (::lseek(handle, 0, SEEK_SET) != 0)
        return;
    const ssize_t written = ::write(handle, payload.constData(), payload.size());
    Q_UNUSED(written);
#endif
}

} // namespace

QString VaultLock::Holder::describe() const
{
    const QString who = application.isEmpty()
        ? QCoreApplication::translate("VaultLock", "Another Kvit window")
        : application;
    if (!host.isEmpty() && pid > 0) {
        return QCoreApplication::translate(
                   "VaultLock", "%1 on %2 (process %3) has this vault open.")
            .arg(who, host, QString::number(pid));
    }
    return QCoreApplication::translate("VaultLock",
                                       "%1 has this vault open.").arg(who);
}

VaultLock::~VaultLock()
{
    release();
}

void VaultLock::setForcedUnavailableForTests(bool forced)
{
    QMutexLocker locker(&g_mutex);
    g_forcedUnavailable = forced;
}

VaultLock::Result VaultLock::acquire(const QString &vaultRoot, Access access)
{
    m_blockingHolder = Holder{};

    const QString canonical =
        QFileInfo(vaultRoot).canonicalFilePath().isEmpty()
            ? QDir(vaultRoot).absolutePath()
            : QFileInfo(vaultRoot).canonicalFilePath();

    if (!m_root.isEmpty() && m_root == canonical && m_access == access)
        return m_lastResult; // already ours, on the same terms

    // A reader states it will not write, so there is nothing for the lock to
    // protect and nothing to contend for: it opens a vault another process
    // holds, and a vault this one is writing. Any lock this object held is
    // dropped, because the vault it belonged to is being left.
    if (access == Access::Read) {
        release();
        m_root = canonical;
        m_access = Access::Read;
        m_holdsNativeLock = false;
        m_lastResult = Result::Acquired;
        return Result::Acquired;
    }

    QMutexLocker locker(&g_mutex);

    // Somebody in this process is already writing this vault. Admitting a
    // second writer here would recreate the lost update the lock prevents
    // between processes, and nothing else can catch it: the kernel lock is
    // held per process, so this process never contends with itself. Like the
    // cross-process refusal below, it changes nothing -- a caller whose
    // switch is refused is left holding exactly what it had.
    if (g_owned.contains(canonical)) {
        m_blockingHolder = Holder{QSysInfo::machineHostName(),
                                  QCoreApplication::applicationName(),
                                  QCoreApplication::applicationPid(),
                                  QDateTime::currentDateTime()};
        return Result::HeldInThisProcess;
    }

    // The three ways a write acquisition can succeed differ only in whether
    // a kernel lock came with it. All three register the vault as held by
    // this process, because that answer never depended on the filesystem.
    const auto take = [this, &canonical](NativeHandle handle, Result result) {
        releaseLocked();
        g_owned.insert(canonical, Owned{handle});
        m_root = canonical;
        m_access = Access::Write;
        m_holdsNativeLock = handle != kInvalidHandle;
        m_ownsRegistration = true;
        m_lastResult = result;
        return result;
    };

    if (g_forcedUnavailable)
        return take(kInvalidHandle, Result::Unavailable);

    const QString path = lockFilePath(canonical);
    if (VaultPaths::ensureOwnedDir(canonical, QStringLiteral(".kvit")).isEmpty()) {
        qCWarning(lcVaultLock, "cannot create .kvit for %s; opening unlocked",
                  qPrintable(canonical));
        return take(kInvalidHandle, Result::Unavailable);
    }

    NativeHandle handle = kInvalidHandle;
    switch (nativeAcquire(path, &handle)) {
    case NativeResult::Locked:
        break;
    case NativeResult::Contended:
        // The one case that keeps the previous vault: this call changes
        // nothing, so a caller that refuses to switch is left exactly as it
        // was, still holding whatever it had.
        m_blockingHolder = readHolder(path);
        return Result::HeldByAnother;
    case NativeResult::Failed:
        qCWarning(lcVaultLock,
                  "cannot lock %s; opening unlocked (a second session on this "
                  "vault will not be detected)", qPrintable(path));
        return take(kInvalidHandle, Result::Unavailable);
    }

    writeHolder(handle, describeThisProcess());
    return take(handle, Result::Acquired);
}

void VaultLock::release()
{
    if (m_root.isEmpty())
        return;
    QMutexLocker locker(&g_mutex);
    releaseLocked();
}

void VaultLock::releaseLocked()
{
    if (m_root.isEmpty())
        return;
    if (m_ownsRegistration) {
        auto it = g_owned.find(m_root);
        if (it != g_owned.end()) {
            // Closing the descriptor drops the kernel lock. The file itself is
            // left in place: removing it would race another process that has
            // already opened it and is about to lock it, and an unlocked file
            // lying around costs nothing.
            closeHandle(it->handle);
            g_owned.erase(it);
        }
    }
    m_root.clear();
    m_holdsNativeLock = false;
    m_ownsRegistration = false;
    m_lastResult = Result::Acquired;
}
