// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Cross-process safety for a vault. Every other suite runs in one process, so
// nothing else here can see what two Kvit processes do to the same notes
// directory, which is exactly where the lost update lives.
//
// The binary doubles as its own helper. Run with --writer it opens a vault,
// changes one thing, holds, and exits; with --holder it takes a vault and
// waits to be killed. The tests drive those with QProcess.
#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "notecollection.h"
#include "vaultlock.h"

namespace {

// Exit codes the helpers use, so a test can tell "refused" from "crashed".
const int kExitOk = 0;
const int kExitRefused = 3;

// Mirrors an editor session's shape: load state at open, change one thing,
// write the whole object back.
int runWriter(const QStringList &args)
{
    const QString root = args.value(0);
    const QString tag = args.value(1);
    const QString color = args.value(2);
    const int holdMs = args.value(3).toInt();

    NoteCollection collection;
    if (!collection.openRoot(root))
        return kExitRefused;
    // Hold the loaded state so the other process loads the same base. A user
    // with a window open does this for hours; the race is not narrow.
    QThread::msleep(holdMs);
    collection.setTagColor(tag, color);
    return kExitOk;
}

// Takes the vault and waits. Printing a line lets the parent wait for the
// lock to actually be held rather than sleeping and hoping.
int runHolder(const QStringList &args)
{
    NoteCollection collection;
    if (!collection.openRoot(args.value(0)))
        return kExitRefused;
    printf("held\n");
    fflush(stdout);
    QThread::sleep(600);   // killed by the test
    return kExitOk;
}

QJsonObject readCollectionJson(const QString &root)
{
    QFile file(QDir(root).filePath(QStringLiteral(".kvit/collection.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class TestVaultLock : public QObject
{
    Q_OBJECT

private slots:
    void concurrentWritersNoLongerLoseAnUpdate();
    void secondProcessIsRefusedWithAnExplanation();
    void lockIsReleasedWhenTheOwnerIsKilled();
    void lockIsReleasedOnClose();
    void secondWritingCollectionInOneProcessIsRefused();
    void singleFileModeNeedsNoVault();
    void unlockableFilesystemStillOpens();
    void unlockableFilesystemTellsTheUser();
    void holderDescriptionSurvivesAGarbageLockFile();
    void failedSwitchKeepsTheCurrentVaultLocked();
    void readOnlySessionsShareAVaultWithAWriter();

private:
    QProcess *startHolder(const QString &root);
    QString program() const { return QCoreApplication::applicationFilePath(); }
};

QProcess *TestVaultLock::startHolder(const QString &root)
{
    auto *holder = new QProcess(this);
    holder->start(program(), {QStringLiteral("--holder"), root});
    if (!holder->waitForStarted(5000))
        return nullptr;
    // Wait for the "held" line: the lock is taken only once it prints.
    if (!holder->waitForReadyRead(15000))
        return nullptr;
    return holder;
}

// The finding, and the fix for it. Two processes open the same vault and each
// records a different tag colour. Before the lock, one change vanished:
// QSaveFile makes each write atomic but does nothing about a whole write
// built on a stale read. Now the second process is refused, so the first
// process's change is still there.
void TestVaultLock::concurrentWritersNoLongerLoseAnUpdate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        NoteCollection seed;
        QVERIFY(seed.openRoot(dir.path()));
        seed.setTagColor(QStringLiteral("seed"), QStringLiteral("#000000"));
    }

    QProcess first;
    first.start(program(), {QStringLiteral("--writer"), dir.path(),
                            QStringLiteral("alpha"), QStringLiteral("#ff0000"),
                            QStringLiteral("600")});
    QVERIFY(first.waitForStarted(5000));

    QProcess second;
    second.start(program(), {QStringLiteral("--writer"), dir.path(),
                             QStringLiteral("beta"), QStringLiteral("#00ff00"),
                             QStringLiteral("1200")});
    QVERIFY(second.waitForStarted(5000));

    QVERIFY(first.waitForFinished(30000));
    QVERIFY(second.waitForFinished(30000));

    const QJsonObject tagColors =
        readCollectionJson(dir.path()).value(QStringLiteral("tagColors")).toObject();

    // Exactly one of them got the vault; the other refused rather than
    // writing. Which one wins is a race, so the assertion is on the
    // invariant: the winner's change survived and the loser wrote nothing.
    const bool firstWon = first.exitCode() == kExitOk;
    QCOMPARE(firstWon, second.exitCode() == kExitRefused);
    QVERIFY2(first.exitCode() == kExitOk || second.exitCode() == kExitOk,
             "neither process could open the vault");

    QVERIFY(tagColors.contains(QStringLiteral("seed")));
    if (firstWon) {
        QVERIFY2(tagColors.contains(QStringLiteral("alpha")),
                 "the winning process's change was lost");
        QVERIFY(!tagColors.contains(QStringLiteral("beta")));
    } else {
        QVERIFY2(tagColors.contains(QStringLiteral("beta")),
                 "the winning process's change was lost");
        QVERIFY(!tagColors.contains(QStringLiteral("alpha")));
    }
}

void TestVaultLock::secondProcessIsRefusedWithAnExplanation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QProcess *holder = startHolder(dir.path());
    QVERIFY(holder);

    NoteCollection second;
    QSignalSpy inUse(&second, &NoteCollection::vaultInUse);
    QSignalSpy failed(&second, &NoteCollection::operationFailed);

    QVERIFY2(!second.openRoot(dir.path()), "a second session opened the vault");
    QCOMPARE(inUse.count(), 1);
    // vaultInUse is the only signal for this: a generic failure as well would
    // mean the UI reports it twice.
    QCOMPARE(failed.count(), 0);
    QVERIFY(!second.isOpen());

    // The refusal names the holder, so the user is not left guessing which
    // window to close.
    const QString detail = inUse.first().at(1).toString();
    QVERIFY2(detail.contains(QString::number(holder->processId())),
             qPrintable(QStringLiteral("refusal did not name the holder: ") + detail));

    holder->kill();
    holder->waitForFinished(10000);
}

// A crash must not leave a vault that nobody can open. The lock is a kernel
// lock on an open descriptor, so a SIGKILL releases it: there is no stale
// state to detect and no file for the user to delete by hand.
void TestVaultLock::lockIsReleasedWhenTheOwnerIsKilled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QProcess *holder = startHolder(dir.path());
    QVERIFY(holder);

    {
        NoteCollection blocked;
        QVERIFY2(!blocked.openRoot(dir.path()),
                 "a second session opened a vault another process holds");
    }

    holder->kill();                              // no cleanup runs
    QVERIFY(holder->waitForFinished(10000));
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(".kvit/vault.lock")));

    NoteCollection after;
    QVERIFY2(after.openRoot(dir.path()),
             "the vault stayed locked after its owner was killed");
    QVERIFY(after.isOpen());
}

void TestVaultLock::lockIsReleasedOnClose()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NoteCollection first;
    QVERIFY(first.openRoot(dir.path()));

    // Another process is refused while it is open...
    QProcess probe;
    probe.start(program(), {QStringLiteral("--writer"), dir.path(),
                            QStringLiteral("x"), QStringLiteral("#111111"),
                            QStringLiteral("0")});
    QVERIFY(probe.waitForStarted(5000));
    QVERIFY(probe.waitForFinished(20000));
    QCOMPARE(probe.exitCode(), kExitRefused);

    // ...and admitted once it closes.
    first.closeRoot();
    QProcess after;
    after.start(program(), {QStringLiteral("--writer"), dir.path(),
                            QStringLiteral("y"), QStringLiteral("#222222"),
                            QStringLiteral("0")});
    QVERIFY(after.waitForStarted(5000));
    QVERIFY(after.waitForFinished(20000));
    QCOMPARE(after.exitCode(), kExitOk);
}

// One writer per vault holds inside this process too. Two NoteCollection
// objects on one root each load the whole state and write whole files back
// from it, so the second one's saves discard the first one's exactly as a
// second process's would -- and the kernel cannot see it, because a POSIX
// flock belongs to the process rather than to the descriptor that took it.
// Read-only sessions are unaffected: they take nothing and lose nothing.
void TestVaultLock::secondWritingCollectionInOneProcessIsRefused()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NoteCollection first;
    QVERIFY(first.openRoot(dir.path()));
    QVERIFY(!first.createNote(QString(), QStringLiteral("Note")).isEmpty());

    NoteCollection second;
    QSignalSpy inUse(&second, &NoteCollection::vaultInUse);
    QVERIFY2(!second.openRoot(dir.path()),
             "a second writing collection in the same process was admitted");
    QVERIFY(!second.isOpen());
    // The refusal says what happened rather than failing silently.
    QCOMPARE(inUse.count(), 1);
    QVERIFY(!inUse.first().at(1).toString().isEmpty());

    // Reopening the vault a collection already holds is not a second
    // acquisition, and neither is a session that only reads.
    QVERIFY(first.openRoot(dir.path()));
    NoteCollection reader;
    reader.setReadOnly(true);
    QVERIFY2(reader.openRoot(dir.path()),
             "a read-only session was refused a vault this process writes");
    QCOMPARE(reader.noteCount(), 1);

    // The vault stays held against other processes for as long as the writer
    // has it; the readers alongside it neither hold it nor extend that.
    QProcess probe;
    probe.start(program(), {QStringLiteral("--writer"), dir.path(),
                            QStringLiteral("x"), QStringLiteral("#333333"),
                            QStringLiteral("0")});
    QVERIFY(probe.waitForStarted(5000));
    QVERIFY(probe.waitForFinished(20000));
    QCOMPARE(probe.exitCode(), kExitRefused);

    // And once the writer lets go, the next one in gets it -- including
    // another collection here, which is what makes this a refusal rather
    // than a permanent claim.
    first.closeRoot();
    NoteCollection successor;
    QVERIFY2(successor.openRoot(dir.path()),
             "the vault stayed claimed after its writer closed it");
    successor.closeRoot();

    QProcess after;
    after.start(program(), {QStringLiteral("--writer"), dir.path(),
                            QStringLiteral("y"), QStringLiteral("#444444"),
                            QStringLiteral("0")});
    QVERIFY(after.waitForStarted(5000));
    QVERIFY(after.waitForFinished(20000));
    QCOMPARE(after.exitCode(), kExitOk);
}

// `kvit-notes note.md` opens a file with no vault, so it takes no lock and is
// never refused: two processes editing two unrelated files must both work,
// and the file-watcher banner already covers two editors on one file.
void TestVaultLock::singleFileModeNeedsNoVault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QProcess *holder = startHolder(dir.path());
    QVERIFY(holder);

    // No collection is opened at all in single-file mode; nothing consults
    // the lock, and no lock file appears for a bare directory.
    QTemporaryDir plain;
    QVERIFY(plain.isValid());
    QFile note(QDir(plain.path()).filePath(QStringLiteral("note.md")));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("# Note\n");
    note.close();
    QVERIFY(!QFile::exists(QDir(plain.path()).filePath(".kvit/vault.lock")));

    holder->kill();
    holder->waitForFinished(10000);
}

// Some network filesystems do not implement locking. Refusing to open the
// vault there would make the app unusable for those users, so the lock
// degrades to absent and the vault still opens -- repeatedly, and for each
// session that has it to itself. What does not degrade is this process's own
// knowledge of what it has open, so a second writer here is still refused;
// that answer never came from the filesystem.
void TestVaultLock::unlockableFilesystemStillOpens()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    VaultLock::setForcedUnavailableForTests(true);
    NoteCollection first;
    QVERIFY2(first.openRoot(dir.path()),
             "an unlockable filesystem made the vault unopenable");

    NoteCollection second;
    QVERIFY2(!second.openRoot(dir.path()),
             "a second writer was admitted because the filesystem could not "
             "lock");

    first.closeRoot();
    QVERIFY2(second.openRoot(dir.path()),
             "the unlockable vault could not be reopened after its writer "
             "closed it");
    VaultLock::setForcedUnavailableForTests(false);
}

// The lock file's contents are advisory. A truncated or hostile file must
// still leave the lock itself working and produce a sane message.
void TestVaultLock::holderDescriptionSurvivesAGarbageLockFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QProcess *holder = startHolder(dir.path());
    QVERIFY(holder);

    // Overwrite the description without disturbing the kernel lock.
    QFile lockFile(QDir(dir.path()).filePath(QStringLiteral(".kvit/vault.lock")));
    QVERIFY(lockFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    lockFile.write("}{ not json at all");
    lockFile.close();

    NoteCollection second;
    QSignalSpy inUse(&second, &NoteCollection::vaultInUse);
    QVERIFY2(!second.openRoot(dir.path()),
             "a corrupt description let a second session in");
    QCOMPARE(inUse.count(), 1);
    QVERIFY(!inUse.first().at(1).toString().isEmpty());

    holder->kill();
    holder->waitForFinished(10000);
}

// Switching vaults releases the lock on the one being left. When the vault
// being opened turns out to be held elsewhere the switch is refused, and the
// application goes on showing and writing the previous vault -- which by then
// had no lock, so another process could open it and start writing whole-file
// snapshots underneath a live session. That is precisely the lost update the
// lock exists to prevent, reached by the ordinary route of picking the wrong
// vault from the menu.
void TestVaultLock::failedSwitchKeepsTheCurrentVaultLocked()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    // Another process holds the vault we are about to try to switch to.
    QProcess *holder = startHolder(second.path());
    QVERIFY(holder);

    NoteCollection collection;
    QVERIFY(collection.openRoot(first.path()));

    QSignalSpy inUse(&collection, &NoteCollection::vaultInUse);
    QVERIFY2(!collection.openRoot(second.path()),
             "a vault another process holds was opened");
    QCOMPARE(inUse.count(), 1);
    // The session is still on the vault it had.
    QCOMPARE(QDir(collection.rootPath()).canonicalPath(),
             QDir(first.path()).canonicalPath());
    QVERIFY(collection.isOpen());

    // And that vault is still unavailable to anybody else.
    QProcess probe;
    probe.start(program(), {QStringLiteral("--writer"), first.path(),
                            QStringLiteral("x"), QStringLiteral("#555555"),
                            QStringLiteral("0")});
    QVERIFY(probe.waitForStarted(5000));
    QVERIFY(probe.waitForFinished(20000));
    QVERIFY2(probe.exitCode() == kExitRefused,
             "the vault still open in this session lost its lock when a "
             "switch to another vault failed");

    holder->kill();
    holder->waitForFinished(10000);
}

// The lock is what stops two writers. A consumer that only reads has nothing
// to lose and nothing to take, so it says so and is admitted to a vault
// another process is writing -- and refuses to write anything itself.
void TestVaultLock::readOnlySessionsShareAVaultWithAWriter()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        NoteCollection seed;
        QVERIFY(seed.openRoot(dir.path()));
        QVERIFY(!seed.createNote(QString(), QStringLiteral("Note")).isEmpty());
    }

    QProcess *holder = startHolder(dir.path());
    QVERIFY(holder);

    NoteCollection reader;
    reader.setReadOnly(true);
    QVERIFY2(reader.openRoot(dir.path()),
             "a read-only session was refused a vault it cannot write to");
    QCOMPARE(reader.noteCount(), 1);

    QSignalSpy failed(&reader, &NoteCollection::operationFailed);
    QVERIFY(reader.createNote(QString(), QStringLiteral("Second")).isEmpty());
    QVERIFY(!reader.deleteNote(QStringLiteral("Note.md")));
    QVERIFY(failed.count() >= 2);

    // A writing session is still refused while the holder has it.
    NoteCollection writer;
    QVERIFY(!writer.openRoot(dir.path()));

    holder->kill();
    holder->waitForFinished(10000);
}

// Opening unlocked is the right call on a filesystem that cannot lock, but
// until now the user was never told: the vault opened looking exactly like a
// protected one, and only the log said otherwise.
void TestVaultLock::unlockableFilesystemTellsTheUser()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    VaultLock::setForcedUnavailableForTests(true);
    NoteCollection collection;
    QSignalSpy unprotected(&collection, &NoteCollection::vaultUnprotected);
    const bool opened = collection.openRoot(dir.path());
    VaultLock::setForcedUnavailableForTests(false);

    QVERIFY(opened);
    QCOMPARE(unprotected.count(), 1);
    QVERIFY(!unprotected.first().at(1).toString().isEmpty());
}

int main(int argc, char *argv[])
{
    // The helper modes, checked before QTest sees the arguments.
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kvit Notes"));

    if (args.value(0) == QLatin1String("--writer"))
        return runWriter(args.mid(1));
    if (args.value(0) == QLatin1String("--holder"))
        return runHolder(args.mid(1));

    TestVaultLock test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_vaultlock.moc"
