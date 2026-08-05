// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

#include "collectionsearch.h"
#include "collectionsearchindex.h"
#include "documentexporter.h"
#include "notecollection.h"
#include "notelistmodel.h"
#include "querydata.h"
#include "quickswitchermodel.h"
#include "reservedsubtrees.h"

namespace {

// The registration the suite runs against, and the one the first consumer
// will make: a subtree of per-run folders, each holding one
// report, with working copies and control data beside it that must stay
// out of the index entirely.
ReservedSubtree reportSubtree()
{
    ReservedSubtree subtree;
    subtree.name = QStringLiteral(".reports");
    subtree.label = QStringLiteral("Reports");
    subtree.admitPattern = QStringLiteral("*/report.md");
    subtree.requiredType = QStringLiteral("report");
    return subtree;
}

QString report(const QString &body)
{
    return QStringLiteral("---\nkvit-type: report\n---\n") + body;
}

} // namespace

// A subtree the application manages, and the few files in it the note index is
// allowed to see.
//
// The scanner has always skipped dot-prefixed directories, which is right for
// caches, working copies and control data and wrong for the one document in
// each of those folders that a person may later want to find. This suite is
// the whole of that narrow opt-in: what a registration admits, what it leaves
// out, and — the part that matters most — that an admitted file is a realm of
// its own everywhere it meets the user's notes. It is indexed, searchable and
// reachable by a folder-qualified link; it is not in the note counts, the tag
// registry, a bare-name link resolution, a query result or a vault-wide
// export.
//
// The last case is the one to read first if this ever fails in an unexpected
// place: with nothing registered, every one of these behaviours must be what
// it was before any of this existed.
class TestReservedSubtrees : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(CollectionSearchIndex::capabilityAvailable(),
                 "SQLite FTS5 with the trigram tokenizer must be available");
    }

    // ---- the registry itself ------------------------------------------

    void anEmptyRegistryReservesNothing()
    {
        const ReservedSubtrees reserved;
        QVERIFY(reserved.isEmpty());
        QVERIFY(reserved.names().isEmpty());
        QVERIFY(!reserved.isReservedPath(QStringLiteral(".reports/a/report.md")));
        QVERIFY(!reserved.isReservedDir(QStringLiteral(".reports")));
        QVERIFY(reserved.admittedLabel(QStringLiteral("Notes/Idea.md")).isEmpty());
        // Nothing registered means nothing to check, so an ordinary note is
        // never refused for lacking a type it was never asked for.
        QVERIFY(reserved.typeSatisfies(QStringLiteral("Notes/Idea.md"), QString()));
    }

    void aRegistrationAdmitsOnlyWhatItNominates()
    {
        ReservedSubtrees reserved;
        reserved.add(reportSubtree());

        QCOMPARE(reserved.count(), 1);
        QCOMPARE(reserved.names(), QStringList{QStringLiteral(".reports")});
        QCOMPARE(reserved.labelForName(QStringLiteral(".reports")),
                 QStringLiteral("Reports"));

        // Nominated.
        QCOMPARE(reserved.admittedLabel(QStringLiteral(".reports/monday/report.md")),
                 QStringLiteral("Reports"));
        // In the subtree, not nominated: a working copy one level deeper, a
        // file at the subtree's own root, a file of another name beside the
        // report.
        for (const char *path : {".reports/monday/staged/report.md",
                                 ".reports/report.md",
                                 ".reports/monday/notes.md",
                                 ".reports/monday/staged/draft.md"}) {
            const QString relPath = QString::fromLatin1(path);
            QVERIFY2(reserved.isReservedPath(relPath), path);
            QVERIFY2(reserved.admittedLabel(relPath).isEmpty(), path);
        }
        // Outside the subtree entirely.
        QVERIFY(!reserved.isReservedPath(QStringLiteral("report.md")));
        QVERIFY(!reserved.isReservedPath(QStringLiteral("reports/monday/report.md")));
        // A name that merely starts the same is a different directory.
        QVERIFY(!reserved.isReservedPath(QStringLiteral(".reports-old/a/report.md")));
    }

    void theFrontMatterCrossChecksThePath()
    {
        ReservedSubtrees reserved;
        reserved.add(reportSubtree());
        const QString admitted = QStringLiteral(".reports/monday/report.md");

        QCOMPARE(reserved.requiredTypeFor(admitted), QStringLiteral("report"));
        QVERIFY(reserved.typeSatisfies(admitted, QStringLiteral("report")));
        QVERIFY(reserved.typeSatisfies(admitted, QStringLiteral("REPORT")));
        QVERIFY(!reserved.typeSatisfies(admitted, QString()));
        QVERIFY(!reserved.typeSatisfies(admitted, QStringLiteral("note")));

        // A registration that asks for no type admits on the path alone.
        ReservedSubtree typeless = reportSubtree();
        typeless.name = QStringLiteral(".archive");
        typeless.requiredType.clear();
        reserved.add(typeless);
        QVERIFY(reserved.typeSatisfies(QStringLiteral(".archive/q1/report.md"),
                                       QString()));
    }

    void aSecondRegistrationOfOneNameIsIgnored()
    {
        ReservedSubtrees reserved;
        reserved.add(reportSubtree());
        ReservedSubtree again = reportSubtree();
        again.label = QStringLiteral("Something else");
        reserved.add(again);
        QCOMPARE(reserved.count(), 1);
        QCOMPARE(reserved.labelForName(QStringLiteral(".reports")),
                 QStringLiteral("Reports"));

        // A registration missing the two things it cannot do without is not
        // a subtree that admits everything; it is not a subtree at all.
        ReservedSubtree empty;
        empty.label = QStringLiteral("Nothing");
        reserved.add(empty);
        QCOMPARE(reserved.count(), 1);
    }

    // ---- the collection, over a real vault -----------------------------

    // What the walk lets in and what it leaves out.
    void onlyNominatedFilesEnterTheIndex()
    {
        openVault();

        // The user's notes are what they were.
        QCOMPARE(m_collection->noteCount(), 2);
        QCOMPARE(m_collection->noteRelPaths(),
                 (QStringList{QStringLiteral("Ideas/Report.md"),
                              QStringLiteral("Journal.md")}));

        // The reports are indexed, as a realm of their own.
        QCOMPARE(m_collection->realmNoteCount(), 2);
        QCOMPARE(m_collection->realmNoteRelPaths(),
                 (QStringList{QStringLiteral(".reports/monday/report.md"),
                              QStringLiteral(".reports/tuesday/report.md")}));
        QCOMPARE(m_collection->realmOf(QStringLiteral(".reports/monday/report.md")),
                 QStringLiteral("Reports"));
        QVERIFY(m_collection->realmOf(QStringLiteral("Journal.md")).isEmpty());
        QVERIFY(m_collection->note(QStringLiteral(".reports/monday/report.md")));

        // Everything else under the subtree is nowhere: not in the index, not
        // as a folder, and not in any count.
        for (const char *path : {".reports/monday/staged/report.md",
                                 ".reports/monday/.state/control.md",
                                 ".reports/tuesday/staged/report.md"}) {
            QVERIFY2(!m_collection->note(QString::fromLatin1(path)), path);
        }
        const QStringList folders = m_collection->folderRelPaths();
        for (const QString &folder : folders)
            QVERIFY2(!folder.startsWith(QLatin1Char('.')), qPrintable(folder));
        QCOMPARE(folders, QStringList{QStringLiteral("Ideas")});
    }

    // A file the path admitted but the front matter refuses. It belongs to
    // the application that owns the subtree, so it is not a note at all.
    void aFileWithoutTheRegisteredTypeIsNotAdmitted()
    {
        writeFile(QStringLiteral(".reports/wednesday/report.md"),
                  QStringLiteral("Plain text, no front matter\n"));
        openVault();

        QVERIFY(!m_collection->note(QStringLiteral(".reports/wednesday/report.md")));
        QCOMPARE(m_collection->realmNoteCount(), 2);
        QCOMPARE(m_collection->noteCount(), 2);
    }

    // Realm files are not the user's notes, so they are not in the counts the
    // sidebar shows, and their front matter does not join the tag registry.
    void realmFilesStayOutOfCountsAndTags()
    {
        openVault();

        QCOMPARE(m_collection->noteCountInFolder(QString(), true), 2);
        QCOMPARE(m_collection->noteCountInFolder(QStringLiteral("Ideas"), false), 1);
        // The reports carry `tags: [generated]`; the vault's tag registry
        // is the user's filing and does not.
        QVERIFY(!m_collection->allTags().contains(QStringLiteral("generated")));
        QCOMPARE(m_collection->allTags(), QStringList{QStringLiteral("daily")});
    }

    // Every folder in the subtree holds a `report.md`, so a bare [[report]] must
    // never resolve into the realm — it means the user's own note of that
    // name, or nothing.
    void aBareNameNeverResolvesIntoTheRealm()
    {
        openVault();

        QCOMPARE(m_collection->resolveWikiTarget(QStringLiteral("report")),
                 QStringLiteral("Ideas/Report.md"));
        // Folder-qualified resolves normally, with or without the subtree's
        // own name in front.
        QCOMPARE(m_collection->resolveWikiTarget(
                     QStringLiteral(".reports/monday/report")),
                 QStringLiteral(".reports/monday/report.md"));
        QCOMPARE(m_collection->resolveWikiTarget(QStringLiteral("tuesday/report")),
                 QStringLiteral(".reports/tuesday/report.md"));

        // And with the user's own note of that name gone, a bare target
        // resolves to nothing rather than falling into the realm.
        QVERIFY(m_collection->deleteNote(QStringLiteral("Ideas/Report.md")));
        QVERIFY(m_collection->resolveWikiTarget(QStringLiteral("report")).isEmpty());
    }

    // A report cites the notes it discussed, and those citations are
    // backlinks like any other: that is the half of the link graph the realm
    // keeps.
    void backlinksFromARealmFileToANoteWork()
    {
        openVault();

        const QVariantList backlinks =
            m_collection->backlinksTo(QStringLiteral("Journal.md"));
        QCOMPARE(backlinks.size(), 1);
        QCOMPARE(backlinks.first().toMap().value(QStringLiteral("relPath")).toString(),
                 QStringLiteral(".reports/monday/report.md"));
    }

    // Grepping past reports is why they are indexed at all. They are
    // searchable, and each result says which realm it came out of.
    void realmFilesAreSearchableAndSayWhichRealm()
    {
        auto index = std::make_unique<CollectionSearchIndex>();
        m_collection = std::make_unique<NoteCollection>();
        m_collection->reserveSubtree(reportSubtree());
        m_collection->setSearchIndex(index.get());
        QVERIFY(m_collection->openRoot(m_dir->path()));
        auto search = std::make_unique<CollectionSearch>();
        search->setSearchIndex(index.get());
        search->setCollection(m_collection.get());
        QTRY_VERIFY(!search->indexing());

        search->setQuery(QStringLiteral("aubergine"));
        QTRY_COMPARE(search->noteCount(), 1);
        const QVariantMap hit = search->results().first().toMap();
        QCOMPARE(hit.value(QStringLiteral("relPath")).toString(),
                 QStringLiteral(".reports/monday/report.md"));
        QCOMPARE(hit.value(QStringLiteral("realm")).toString(),
                 QStringLiteral("Reports"));

        // A hit in one of the user's notes carries no realm, which is what
        // keeps the two distinguishable in one list. Waiting on the path
        // rather than on the count: a query in flight leaves the previous
        // result set published, and both of these queries match one note.
        search->setQuery(QStringLiteral("porridge"));
        QTRY_COMPARE(search->results().value(0).toMap()
                         .value(QStringLiteral("relPath")).toString(),
                     QStringLiteral("Journal.md"));
        QVERIFY(search->results().first().toMap()
                    .value(QStringLiteral("realm")).toString().isEmpty());

        // What the subtree keeps to itself is not searchable either.
        search->setQuery(QStringLiteral("scaffolding"));
        QTRY_COMPARE(search->noteCount(), 0);

        m_collection->setSearchIndex(nullptr);
    }

    void theSwitcherAndTheNoteListShowTheRealmAsItsOwnSection()
    {
        openVault();

        QuickSwitcherModel switcher;
        switcher.setCollection(m_collection.get());
        const QVariantList rows = switcher.itemsFor(QString());
        QCOMPARE(rows.size(), 4);
        // The user's notes first, with no realm; then the reports, each
        // carrying the label the section is drawn from.
        QVERIFY(rows.at(0).toMap().value(QStringLiteral("realm")).toString().isEmpty());
        QVERIFY(rows.at(1).toMap().value(QStringLiteral("realm")).toString().isEmpty());
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("realm")).toString(),
                 QStringLiteral("Reports"));
        QCOMPARE(rows.at(3).toMap().value(QStringLiteral("realm")).toString(),
                 QStringLiteral("Reports"));

        NoteListModel list;
        list.setCollection(m_collection.get());
        list.setScope(QStringLiteral("all"));
        list.rebuildNow();
        QCOMPARE(list.rowCount(), 4);
        QVERIFY(list.data(list.index(0, 0), NoteListModel::RealmRole)
                    .toString().isEmpty());
        QCOMPARE(list.data(list.index(3, 0), NoteListModel::RealmRole).toString(),
                 QStringLiteral("Reports"));

        // A folder scope names one of the user's folders, and favourites are
        // something a person applies to their own notes: neither draws the
        // realm in.
        list.setScope(QStringLiteral("folder"));
        list.setFolderPath(QStringLiteral("Ideas"));
        list.rebuildNow();
        QCOMPARE(list.rowCount(), 1);
        list.setScope(QStringLiteral("favorites"));
        list.rebuildNow();
        QCOMPARE(list.rowCount(), 0);

        QCOMPARE(m_collection->realmListing().size(), 1);
        const QVariantMap realm = m_collection->realmListing().first().toMap();
        QCOMPARE(realm.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Reports"));
        QCOMPARE(realm.value(QStringLiteral("count")).toInt(), 2);
    }

    // A query block asks about the user's notes. A report is a document
    // the application keeps, not one of them, so it is not an answer.
    void queryBlocksNeverReturnRealmFiles()
    {
        openVault();

        const QueryData::ParseResult parsed =
            QueryData::parse(QStringLiteral("columns: title\nsort: title asc\n"));
        QVERIFY2(parsed.ok, qPrintable(parsed.error));
        const QueryData::Result result =
            QueryData::evaluate(parsed.spec, *m_collection);

        QCOMPARE(result.rows.size(), 2);
        for (const QueryData::Row &row : result.rows)
            QVERIFY2(!row.relPath.startsWith(QLatin1Char('.')),
                     qPrintable(row.relPath));
    }

    // The walk the application actually runs at startup is the asynchronous
    // one, and it is a second implementation of the same rules. It has to
    // reach the same index.
    void theAsynchronousScanAdmitsTheSameFiles()
    {
        m_collection = std::make_unique<NoteCollection>();
        m_collection->reserveSubtree(reportSubtree());
        QVERIFY(m_collection->openRootAsync(m_dir->path()));
        QTRY_VERIFY(!m_collection->scanInProgress());

        QCOMPARE(m_collection->noteCount(), 2);
        QTRY_COMPARE(m_collection->realmNoteCount(), 2);
        QCOMPARE(m_collection->realmNoteRelPaths(),
                 (QStringList{QStringLiteral(".reports/monday/report.md"),
                              QStringLiteral(".reports/tuesday/report.md")}));
        QVERIFY(!m_collection->note(
            QStringLiteral(".reports/monday/staged/report.md")));
        QCOMPARE(m_collection->folderRelPaths(),
                 QStringList{QStringLiteral("Ideas")});
    }

    // Provisional admission, then the front matter's answer: the
    // asynchronous walk lists a nominated file before anything has read it,
    // and the parse that follows takes back the ones that are not what the
    // registration asked for.
    void theAsynchronousScanTakesBackWhatTheFrontMatterRefuses()
    {
        writeFile(QStringLiteral(".reports/wednesday/report.md"),
                  QStringLiteral("No front matter at all\n"));
        m_collection = std::make_unique<NoteCollection>();
        m_collection->reserveSubtree(reportSubtree());
        QVERIFY(m_collection->openRootAsync(m_dir->path()));
        QTRY_VERIFY(!m_collection->scanInProgress());

        QTRY_VERIFY(!m_collection->note(
            QStringLiteral(".reports/wednesday/report.md")));
        QCOMPARE(m_collection->realmNoteCount(), 2);
        QCOMPARE(m_collection->noteCount(), 2);
    }

    void aVaultWideExportLeavesTheRealmWhereItIs()
    {
        openVault();

        QTemporaryDir out;
        QVERIFY(out.isValid());
        DocumentExporter exporter;
        const int written = exporter.exportCollection(
            m_collection.get(), out.path(), QStringLiteral("markdown"), false);

        QCOMPARE(exporter.lastError(), QString());
        QCOMPARE(written, 2);
        QVERIFY(QFileInfo::exists(out.filePath(QStringLiteral("Journal.md"))));
        QVERIFY(QFileInfo::exists(out.filePath(QStringLiteral("Ideas/Report.md"))));
        QVERIFY(!QFileInfo::exists(out.filePath(QStringLiteral(".reports"))));
    }

    // The case to read first when something here fails somewhere unexpected:
    // with nothing registered, the same vault behaves exactly as it did
    // before any of this existed — the subtree is invisible, every one of its
    // files included.
    void withNothingRegisteredTheSubtreeIsInvisible()
    {
        openVault(false);

        QCOMPARE(m_collection->noteCount(), 2);
        QCOMPARE(m_collection->realmNoteCount(), 0);
        QVERIFY(m_collection->realmListing().isEmpty());
        QVERIFY(!m_collection->note(QStringLiteral(".reports/monday/report.md")));
        QVERIFY(m_collection->realmOf(QStringLiteral(".reports/monday/report.md"))
                    .isEmpty());
        QCOMPARE(m_collection->resolveWikiTarget(QStringLiteral("report")),
                 QStringLiteral("Ideas/Report.md"));
        QCOMPARE(m_collection->folderRelPaths(),
                 QStringList{QStringLiteral("Ideas")});

        QuickSwitcherModel switcher;
        switcher.setCollection(m_collection.get());
        QCOMPARE(switcher.itemsFor(QString()).size(), 2);
    }

    // A report written after the vault was opened reaches the index
    // through the same watcher path a note does, and one that stops being a
    // report leaves it again.
    void anAdmittedFileArrivingLaterIsPickedUp()
    {
        openVault();

        writeFile(QStringLiteral(".reports/friday/report.md"),
                  report(QStringLiteral("A later report\n")));
        m_collection->refreshPaths(
            {m_dir->filePath(QStringLiteral(".reports/friday"))});
        QTRY_COMPARE(m_collection->realmNoteCount(), 3);
        QVERIFY(m_collection->note(QStringLiteral(".reports/friday/report.md")));
        QCOMPARE(m_collection->noteCount(), 2);

        // Its working copy arrives in the same folder and is still nothing.
        writeFile(QStringLiteral(".reports/friday/staged/report.md"),
                  report(QStringLiteral("A working copy\n")));
        m_collection->refreshPaths(
            {m_dir->filePath(QStringLiteral(".reports/friday/staged"))});
        QTRY_COMPARE(m_collection->realmNoteCount(), 3);
        QVERIFY(!m_collection->note(
            QStringLiteral(".reports/friday/staged/report.md")));
    }

private:
    void writeFile(const QString &relPath, const QString &content)
    {
        const QString absPath = m_dir->filePath(relPath);
        QVERIFY(QDir().mkpath(QFileInfo(absPath).absolutePath()));
        QFile file(absPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content.toUtf8());
    }

    // A vault holding two of the user's notes and a managed subtree of two
    // runs, each with a working copy and control data beside its
    // report. `registered` false opens the same vault with nothing
    // reserved, which is the open editor.
    void openVault(bool registered = true)
    {
        m_collection = std::make_unique<NoteCollection>();
        if (registered)
            m_collection->reserveSubtree(reportSubtree());
        QVERIFY(m_collection->openRoot(m_dir->path()));
    }

    void initVault()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());

        writeFile(QStringLiteral("Journal.md"),
                  QStringLiteral("---\ntags: [daily]\n---\n"
                                 "Porridge for breakfast\n"));
        writeFile(QStringLiteral("Ideas/Report.md"),
                  QStringLiteral("A note the user called Report\n"));

        // Front matter carrying both the type the registration asks for and
        // a tag, so the tag registry can be checked for what it leaves out.
        writeFile(QStringLiteral(".reports/monday/report.md"),
                  QStringLiteral("---\nkvit-type: report\ntags: [generated]\n---\n"
                                 "We discussed [[Journal]] and aubergine\n"));
        writeFile(QStringLiteral(".reports/monday/staged/report.md"),
                  report(QStringLiteral("Working copy scaffolding\n")));
        writeFile(QStringLiteral(".reports/monday/.state/control.md"),
                  report(QStringLiteral("Control data scaffolding\n")));
        writeFile(QStringLiteral(".reports/tuesday/report.md"),
                  report(QStringLiteral("Another run\n")));
        writeFile(QStringLiteral(".reports/tuesday/staged/report.md"),
                  report(QStringLiteral("More scaffolding\n")));
    }

private slots:
    void init() { initVault(); }
    void cleanup()
    {
        m_collection.reset();
        m_dir.reset();
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<NoteCollection> m_collection;
};

QTEST_MAIN(TestReservedSubtrees)
#include "test_reservedsubtrees.moc"
