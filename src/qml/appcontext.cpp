// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "appcontext.h"

#include <QQuickStyle>

#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "blockkindregistry.h"
#include "codelanguages.h"
#include "diagrams/diagramcanvas.h"
#include "extensionregistry.h"
#include "windowrouter.h"
#include "perflog.h"
#include "localimageprovider.h"
#include "opendocumentsession.h"
#include "remoteimageprovider.h"

AppContext::AppContext(QObject *parent)
    : AppContext(Options{}, parent)
{
}

AppContext::AppContext(const Options &options, QObject *parent)
    : QObject(parent)
    , m_ownedGlobals(std::make_unique<ProcessServices>(options))
    , m_globals(*m_ownedGlobals)
{
    wire();
}

AppContext::AppContext(ProcessServices &globals, QObject *parent)
    : QObject(parent)
    , m_ownedGlobals(nullptr)
    , m_globals(globals)
{
    wire();
}

void AppContext::setEmbedFetcher(std::unique_ptr<EmbedFetcher> fetcher)
{
    if (!fetcher)
        return;
    // EmbedMetadata borrows its fetcher, so hand it the new one before the
    // previous override is destroyed at the end of this scope. The default it
    // is replacing is the process-global EgressFetcher (ProcessServices), which
    // owns the only QNetworkAccessManager in the tree — so a harness that does
    // not call this reaches the network for real.
    //
    // Only this context's embed wire is swapped. EgressPolicy still sits in
    // front of the transport, and the shared fetcher stays wired to the remote
    // image provider, so consent and address validation behave under test
    // exactly as they ship.
    std::unique_ptr<EmbedFetcher> previous = std::move(m_embedFetcherOverride);
    m_embedFetcherOverride = std::move(fetcher);
    m_embedMetadata.setFetcher(m_embedFetcherOverride.get());
}

AppContext::~AppContext() = default;

// Emitted by qmltyperegistrar from the QML_ELEMENT macros on the types
// themselves; see the generated build/kvit-core_qmltyperegistrations.cpp.
extern void qml_register_types_Kvit();

void AppContext::applyQuickStyle()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
}

void AppContext::registerQmlTypes()
{
    // Calling this by hand is not redundant. The generator also emits a
    // file-scope QQmlModuleRegistration whose constructor would register the
    // module on its own — but kvit-core is a STATIC library, so the linker
    // drops that object file for want of any reference to it, and the types
    // then do not exist at runtime. The symptom is not a link error: QML
    // reports "ReferenceError: <Type> is not defined" and the shell renders
    // wrong. ShellTests catches it, and did so while this was being written.
    //
    // Naming the generated function here is the reference that keeps the
    // translation unit alive. The alternative, letting qt_add_qml_module
    // build a plugin and importing it with Q_IMPORT_QML_PLUGIN, would add a
    // plugin target for a library that is linked directly into every binary
    // that uses it.
    qml_register_types_Kvit();
}

void AppContext::wire()
{
    m_blockModel.setUndoStack(&m_undoStack);
    // The model resolves fence kinds against the process-global registry, so a
    // module's kinds are visible in every window and every window agrees on the
    // one kind numbering.
    m_blockModel.setBlockKindRegistry(m_globals.blockKinds());
    // The same registry behind the block menu, so a kind a module registered
    // is offered in the slash menu as well as drawn on screen.
    m_blockMenuModel.setBlockKindRegistry(m_globals.blockKinds());
    // And behind the export, so a module's kind writes its own markup rather
    // than falling back to the plain code block it looks like without it.
    m_documentExporter.setBlockKindRegistry(m_globals.blockKinds());

    m_documentManager.setBlockModel(&m_blockModel);
    m_documentManager.setUndoStack(&m_undoStack);


    m_documentSelection.setModel(&m_blockModel);
    m_documentSearch.setModel(&m_blockModel);
    // The document outline: a heading-tree projection feeding the outline
    // panel, the TOC block, and internal links.
    m_documentOutline.setModel(&m_blockModel);
    // Document statistics (features.md §19.1).
    m_documentStats.setModel(&m_blockModel);
    // Export (features.md §12.5). Theme is process-global (ProcessServices).
    // The collection is what a query block in an exported note is evaluated
    // against, and the embed cache supplies the title an embed card carries
    // into the output; neither is ever fetched or written by an export.
    m_documentExporter.setTheme(m_globals.theme());
    m_documentExporter.setCollection(&m_noteCollection);
    m_documentExporter.setEmbedMetadata(&m_embedMetadata);

    // Disk-backed global search: one SQLite FTS5 index the collection feeds
    // and the search facade queries, off the GUI thread.
    // A capability probe guards packaging: a release build without FTS5 and the
    // trigram tokenizer cannot serve global search.
    if (!CollectionSearchIndex::capabilityAvailable()) {
        qWarning("Global search disabled: the SQLite driver lacks FTS5 with the "
                 "trigram tokenizer. Packaged builds must ship it.");
    }
    m_noteCollection.setSearchIndex(&m_searchIndex);
    m_noteCollection.setOpenDocument(&m_documentManager);
    m_ignoreRules.setSettings(m_globals.settings());
    m_noteCollection.setIgnoreRules(&m_ignoreRules);
    m_fileWatcher.setIgnoreRules(&m_ignoreRules);
    m_fileSystemTreeModel.setCollection(&m_noteCollection);
    m_fileSystemTreeModel.setIgnoreRules(&m_ignoreRules);
    m_folderTreeModel.setCollection(&m_noteCollection);
    m_noteListModel.setCollection(&m_noteCollection);
    m_collectionSearch.setSearchIndex(&m_searchIndex);
    m_collectionSearch.setCollection(&m_noteCollection);

    // Note templates (features.md §18).
    m_noteTemplates.setCollection(&m_noteCollection);
    // Import into the collection (features.md §12.6).
    m_documentImporter.setCollection(&m_noteCollection);
    // The one transport and the one policy are process-global (ProcessServices);
    // the embed cache borrows them. Embed preview cards (features.md §1.2.14)
    // stay inert until the reader approves the origin, so a note cannot fetch by
    // being opened. Remote images/media and the update check share the same
    // transport.
    m_embedMetadata.setFetcher(m_globals.egressFetcher());
    m_embedMetadata.setPolicy(m_globals.egressPolicy());
    m_embedMetadata.setCollection(&m_noteCollection);
    // The update check shares this transport, but the launcher hands it over
    // (KvitApplication::start), so composing an AppContext in a test still
    // yields an update checker with no fetcher and no way to reach the wire.

    m_startupController.setCollection(&m_noteCollection);
    m_startupController.setDocumentManager(&m_documentManager);
    m_startupController.setBlockModel(&m_blockModel);
    m_startupController.setUndoStack(&m_undoStack);
    m_startupController.setNavigationHistory(&m_navigationHistory);

    // System integration seams (the tray and the system-wide hotkey) are
    // process-global and wired in ProcessServices, not here.

    // External file watching (features.md §12.1). Debounced outside
    // changes refresh the affected note paths when possible; directory-level
    // changes still fall back to a full collection refresh. The own-write guard
    // (hooked to the save path) keeps the app's own writes from self-triggering.
    // The open note is watched closely for the conflict case, which main.qml
    // turns into a keep-mine/load-theirs banner.
    connect(&m_fileWatcher, &FileWatcher::externalChangePaths,
            &m_noteCollection, &NoteCollection::refreshPaths);
    connect(&m_documentManager, &DocumentManager::aboutToSave,
            &m_fileWatcher, [this](const QString &path) {
                m_fileWatcher.noteOwnWrite(path);
            });
    connect(&m_documentManager, &DocumentManager::aboutToSave,
            &m_noteCollection, [this](const QString &path) {
                if (!m_noteCollection.relativePath(path).isEmpty())
                    m_noteCollection.backupBeforeOverwrite(path);
            });
    connect(&m_documentManager, &DocumentManager::saveSucceededWithText,
            &m_noteCollection,
            [this](const QString &path, const QString &fileText) {
                if (!m_noteCollection.relativePath(path).isEmpty())
                    m_noteCollection.noteSaved(path, fileText);
            });
    connect(&m_noteCollection, &NoteCollection::aboutToWrite,
            &m_fileWatcher, [this](const QString &path) {
                m_fileWatcher.noteOwnWrite(path);
            });
    // Directory mutations the app performs that never go through a file
    // write, so aboutToWrite above never sees them: moving a note between
    // folders, and deleting one. Both change the parent directory's listing,
    // which the watcher would otherwise report as an outside edit and answer
    // with a full collection refresh.
    connect(&m_noteCollection, &NoteCollection::noteMoved, &m_fileWatcher,
            [this](const QString &oldRel, const QString &newRel) {
                m_fileWatcher.noteOwnDirectoryChange(
                    QFileInfo(m_noteCollection.absolutePath(oldRel)).absolutePath());
                m_fileWatcher.noteOwnDirectoryChange(
                    QFileInfo(m_noteCollection.absolutePath(newRel)).absolutePath());
            });
    connect(&m_noteCollection, &NoteCollection::noteRemoved, &m_fileWatcher,
            [this](const QString &relPath) {
                m_fileWatcher.noteOwnDirectoryChange(
                    QFileInfo(m_noteCollection.absolutePath(relPath)).absolutePath());
            });
    connect(&m_noteCollection, &NoteCollection::rootChanged,
            &m_fileWatcher, [this]() {
                m_fileWatcher.watchRoot(m_noteCollection.rootPath());
            });
    connect(&m_documentManager, &DocumentManager::currentFilePathChanged,
            &m_fileWatcher, [this]() {
                m_fileWatcher.watchFile(m_documentManager.currentFilePath());
            });
    // A save replaces the file rather than editing it in place, so the kernel
    // watch is left pointing at the inode that was just discarded. FileWatcher
    // renews it when the guarded change event arrives, but that event is not
    // guaranteed — a same-path save on some platforms delivers nothing at all —
    // so confirm the registration here too. Both paths are idempotent.
    connect(&m_documentManager, &DocumentManager::saveSucceeded,
            &m_fileWatcher, [this](const QString &path) {
                if (path == m_documentManager.currentFilePath())
                    m_fileWatcher.rewatchCurrentFile();
            });
    // Collection metadata is reflected into the live document session in
    // C++, alongside the exclusive open-note writer. QML no longer orders
    // repository revisions and document snapshots itself.
    //
    // The gate is hasParsedMetadata(), not frontMatterFor().isEmpty(). Opening
    // or upgrading a cold vault publishes a placeholder entry for a note the
    // background scan has not read yet, and frontMatterFor() answers "" for
    // that placeholder exactly as it does for a note that genuinely has no
    // metadata. Projecting the first case replaced the live document's real
    // front matter — tags, favourite state, foreign keys — with nothing, and
    // marked it dirty, so a save, an autosave or a shutdown before the scan
    // caught up wrote the note back without its metadata. A missing or
    // placeholder entry means "not authoritative yet"; the block loaded with
    // the document stays until the same note has actually been parsed.
    const auto projectFrontMatter = [this](const QString &relPath) {
        if (relPath.isEmpty())
            return;
        if (m_noteCollection.relativePath(m_documentManager.currentFilePath())
            != relPath)
            return;
        if (!m_noteCollection.hasParsedMetadata(relPath))
            return;
        m_documentManager.setFrontMatter(
            m_noteCollection.frontMatterFor(relPath));
    };
    // The targeted trigger: this note's entry now holds metadata read from
    // its file. One path comparison and nothing else, as the signal asks.
    connect(&m_noteCollection, &NoteCollection::noteMetadataReady,
            &m_documentManager, projectFrontMatter);
    // Metadata the user edits from outside the document — a tag added from the
    // note list, a favourite toggled — moves the revision without a scan, so
    // that path still needs the generic signal. It is gated identically.
    connect(&m_noteCollection, &NoteCollection::revisionChanged,
            &m_documentManager, [this, projectFrontMatter]() {
                projectFrontMatter(m_noteCollection.relativePath(
                    m_documentManager.currentFilePath()));
            });

    // Wiki-link navigation: back/forward history and the quick switcher's
    // filter. History entries follow collection renames/deletions and clear
    // with the root.
    connect(&m_noteCollection, &NoteCollection::noteMoved,
            &m_navigationHistory, &NavigationHistory::renamePath);
    connect(&m_noteCollection, &NoteCollection::noteRemoved,
            &m_navigationHistory, &NavigationHistory::dropPath);
    connect(&m_noteCollection, &NoteCollection::rootChanged,
            &m_navigationHistory, &NavigationHistory::clear);
    m_quickSwitcherModel.setCollection(&m_noteCollection);

    // Collection query block: the QML seam over the pure QueryData
    // parse/evaluate module.
    m_queryTools.setCollection(&m_noteCollection);

    // Open actions route through the window registry when one is installed, so
    // an already-open vault raises its window instead of opening a duplicate.
    // Without a registry (single-composition tests) they fall back to the
    // in-place behaviour this context had before the multi-window split.
    connect(&m_appActions, &AppActions::openVaultRequested, this,
            [this](const QString &path) {
                if (m_documentManager.isDirty()) {
                    emit m_appActions.vaultSwitchConfirmationRequested(path);
                    return;
                }
                if (m_router)
                    m_router->openVaultInWindow(this, path);
                else
                    openVaultRoot(path);
            });
    connect(&m_appActions, &AppActions::openVaultConfirmed, this,
            [this](const QString &path) {
                if (m_documentManager.isDirty())
                    return;
                if (m_router)
                    m_router->openVaultInWindow(this, path);
                else
                    openVaultRoot(path);
            });
    connect(&m_appActions, &AppActions::openVaultInNewWindowRequested, this,
            [this](const QString &path) {
                if (m_router)
                    m_router->openVaultInNewWindow(path);
                else
                    openVaultRoot(path);
            });
    connect(&m_appActions, &AppActions::openFileInNewWindowRequested, this,
            [this](const QString &path) {
                if (m_router)
                    m_router->openFileInNewWindow(path);
                else
                    m_documentManager.open(QUrl::fromLocalFile(path));
            });
    connect(&m_appActions, &AppActions::closeVaultRequested, this,
            [this](const QString &path) {
                if (m_router)
                    m_router->closeVault(path);
            });

    // Repository conditions that the user has to hear about. Each was raised
    // by the repository and heard by nothing, so the only record was the log.
    // They arrive as transient status because that is the channel the shell
    // already renders; the external-change one deserves the persistent
    // conflict surface instead, which lives in QML.
    connect(&m_noteCollection, &NoteCollection::vaultUnprotected,
            &m_appActions, [this](const QString &path, const QString &detail) {
                const QString where = QFileInfo(path).fileName();
                m_appActions.requestTransientStatus(
                    detail.isEmpty()
                        ? tr("\"%1\" is open without a lock: this filesystem "
                             "does not support one, so another session could "
                             "write to it at the same time.").arg(where)
                        : tr("\"%1\" is open without a lock (%2), so another "
                             "session could write to it at the same time.")
                              .arg(where, detail));
            });
    connect(&m_noteCollection, &NoteCollection::noteChangedExternally,
            &m_appActions, [this](const QString &relPath) {
                m_appActions.requestTransientStatus(
                    tr("\"%1\" changed outside Kvit, so the change being "
                       "written was abandoned rather than discarding it.")
                        .arg(relPath));
                // The abandoned write already registered itself as an own
                // write with the watcher, so the change that caused it to be
                // abandoned may be swallowed as ours. Re-read the note here
                // rather than waiting for a watcher event that has been
                // spoken for.
                m_noteCollection.refreshPaths(
                    QStringList{m_noteCollection.absolutePath(relPath)});
            });
    connect(&m_noteCollection, &NoteCollection::operationIncomplete,
            &m_appActions, [this](const QStringList &relPaths) {
                if (relPaths.isEmpty())
                    return;
                m_appActions.requestTransientStatus(
                    tr("An operation interrupted by the last session could not "
                       "be finished. These notes may still hold their earlier "
                       "text: %1").arg(relPaths.join(QStringLiteral(", "))));
            });
}

bool AppContext::openVaultRoot(const QString &path)
{
    // A window switching vaults in place is leaving one vault for another, and
    // the note on screen belongs to the one being left. Three things follow,
    // and none of them used to happen: the editor's unsaved work has to reach
    // disk while this window is still the vault's one writer, because
    // NoteCollection releases that vault's lock as it takes the next one; the
    // document then has to be let go, or the editor keeps a file open — and
    // saveable — in a vault this process no longer holds, where a second
    // session is now free to edit the same file; and the new vault needs its
    // own note opened, which is the startup flow again rather than anything
    // new.
    // A loose-file window opening a folder is the same departure: it has no
    // collection to leave, but the file on screen is not in the vault it is
    // about to show either.
    const bool switching = m_noteCollection.isOpen()
                               ? m_noteCollection.rootPath() != path
                               : m_documentManager.hasFile();

    if (switching) {
        // Through the repository's own seam for this, rather than by calling
        // the document manager's save directly: "make sure the file holds what
        // is on screen, and tell me if it does not" is exactly what
        // OpenDocumentSession states, and the session remains the only writer
        // of the open note.
        OpenDocumentSession &openNote = m_documentManager;
        openNote.flushPendingEdits();
        if (openNote.hasUnsavedChanges()) {
            if (openNote.openFilePath().isEmpty()) {
                // A document that has never been saved has nowhere to go, and
                // the switch is about to replace the model it lives in. Say so
                // and stay put; the alternative is discarding work the reader
                // never agreed to lose in order to honour a menu item.
                m_appActions.requestTransientStatus(
                    tr("This document has never been saved. Save it before "
                       "opening another folder."));
                return false;
            }
            // The same refusal NoteSession makes before any note switch: an
            // unwritable file or a full disk means the edits exist only in the
            // model that is about to be replaced, so stay where we are and let
            // the error stand.
            if (!openNote.persistCurrentRevision())
                return false;
        }
    }

    // The index is released inside openRootAsync, once the new vault has
    // actually been taken; a switch refused because another process holds it
    // leaves this vault's index open and searchable.
    if (!m_noteCollection.openRootAsync(path))
        return false;

    if (switching) {
        // Closes the departed note (and calls off any write still running
        // against it). The document that replaces it comes from the new vault,
        // through the same start-note selection a cold launch uses.
        m_documentManager.newDocument();
        m_navigationHistory.clear();
        m_startupController.restartForOpenRoot();
    }
    return true;
}

void AppContext::openSettings(const QString &settingsPath)
{
    // Opening the settings store and attaching everything that reads it is a
    // process-level action that lives on ProcessServices. A context that owns
    // its ProcessServices (the single-composition tests) reaches it through
    // here; the application opens settings on the shared ProcessServices once,
    // directly, before any window is built.
    m_globals.openSettings(settingsPath);
}

void AppContext::applyStartupArguments(const QStringList &arguments)
{
    QString fileArg;
    QString rootArg;
    if (arguments.size() > 1) {
        QFileInfo argInfo(arguments.at(1));
        if (argInfo.exists() && argInfo.isFile())
            fileArg = argInfo.absoluteFilePath();
        else if (argInfo.exists() && argInfo.isDir())
            rootArg = argInfo.absoluteFilePath();
    }

    if (!fileArg.isEmpty()) {
        m_documentManager.open(QUrl::fromLocalFile(fileArg));
    } else {
        if (rootArg.isEmpty()) {
            rootArg = QDir(QStandardPaths::writableLocation(
                               QStandardPaths::DocumentsLocation))
                          .filePath(QStringLiteral("Kvit"));
        }
        m_startupController.setRootPath(rootArg);
    }

    if (m_noteCollection.isOpen())
        m_fileWatcher.watchRoot(m_noteCollection.rootPath());
}

void AppContext::installContextProperties(QQmlEngine *engine)
{
    if (!engine)
        return;
    QQmlContext *context = engine->rootContext();

    // The services this composition offers QML as singletons. Each one's
    // create() reads its instance back out of here, so an engine gets the
    // AppContext that installed on it and no other. Registered before the
    // shell loads, because the first binding that touches a singleton
    // resolves it.
    m_services.add(&m_queryTools);
    m_services.add(&m_documentDecorations);
    m_services.add(&m_urlLauncher);
    m_services.add(m_globals.globalHotkey());
    m_services.add(&m_fileWatcher);
    m_services.add(&m_shortcutCatalog);
    m_services.add(&m_menuAccessKeys);
    m_services.add(&m_quickSwitcherModel);
    m_services.add(&m_fileSystemTreeModel);
    m_services.add(&m_textFileViewModel);
    m_services.add(&m_folderTreeModel);
    m_services.add(&m_markdownFormatter);
    m_services.add(&m_blockMenuModel);
    m_services.add(&m_mathCommandModel);
    m_services.add(&m_documentStats);
    m_services.add(&m_documentExporter);
    m_services.add(&m_documentSerializer);
    m_services.add(&m_documentImporter);
    m_services.add(&m_embedMetadata);
    m_services.add(m_globals.systemTray());
    m_services.add(&m_navigationHistory);
    m_services.add(&m_startupController);
    m_services.add(m_globals.updateChecker());
    m_services.add(&m_tableTools);
    m_services.add(&m_kanbanTools);
    m_services.add(&m_todoMeta);
    m_services.add(&m_mathTools);
    m_services.add(&m_undoStack);
    m_services.add(&m_documentOutline);
    m_services.add(&m_collectionSearch);
    m_services.add(&m_noteTemplates);
    m_services.add(m_globals.egressPolicy());
    m_services.add(m_globals.remoteMediaCache());
    m_services.add(m_globals.typography());
    m_services.add(m_globals.interfaceMetrics());
    m_services.add(&m_imageAssets);
    m_services.add(&m_assetStore);
    m_services.add(&m_blockAttributes);
    m_services.add(&m_clipboardHelper);
    m_services.add(&m_a11y);
    m_services.add(m_globals.extensions());
    m_services.add(m_globals.blockKinds());
    m_services.add(&m_documentSearch);
    m_services.add(&m_noteListModel);
    m_services.add(m_globals.settings());
    m_services.add(&m_documentManager);
    m_services.add(&m_noteCollection);
    m_services.add(&m_blockModel);
    m_services.add(&m_documentSelection);
    m_services.add(m_globals.theme());
    m_services.add(m_globals.systemAppearance());
    m_services.add(&m_appActions);
    KvitQml::attachServices(engine, &m_services);

    // The core installs no context properties of its own any more: every one
    // of them became a QML singleton resolved through the service table
    // above. The list stays, empty, because it is still what a module's
    // namespace is checked against alongside the singleton names — and a
    // future core context property must land on it to be checked at all.
    m_installedProperties.clear();

    // The per-block attribute reader/editor: delegates read typed
    // presentation values off a block's `attributes` payload, and the
    // attribute editors compute a new payload to hand to setBlockAttributes.
    // The live-region announcer: dynamic changes speak
    // through this seam to assistive technology.
    // Math: the MicroTeX seam. The provider owns rendering under
    // image://math/...; mathRenderer is the parse-check + encoder the
    // delegates use. The engine takes ownership of the provider.
    engine->addImageProvider(QStringLiteral("math"), new MathImageProvider);
    // The only way a remote image reaches QML: image://remote/<url> fetches
    // through the egress fetcher, so consent, address validation, redirect
    // revalidation and the byte cap all apply. Binding a remote URL straight
    // to an Image's `source` would bypass every one of them.
    engine->addImageProvider(QStringLiteral("remote"),
                             new RemoteImageProvider(m_globals.egressFetcher()));
    // The same treatment for a file on disk: image://local/<path> checks the
    // decoded size against the same budget before allocating. QML's own file
    // loader would allocate whatever the header claimed.
    engine->addImageProvider(QStringLiteral("local"), new LocalImageProvider);

    // The two extension seams: block-kind registration and QML slot
    // injection. Both are inert in the open build: no module is installed,
    // so `blockKinds` reports only the built-in fence kinds and every
    // `extensions` slot resolves to an empty source.
    // Modules publish last and under their own namespace, and every name the
    // core just took is refused to them.
    // Modules publish last, under their own namespace, and every name the
    // core occupies is refused to them — both what it just put on the context
    // and the QML names of its module singletons. The singleton half is what
    // stops a module taking `theme` while the core owns `Theme`; see
    // ExtensionRegistry::installContextProperties.
    m_globals.extensions()->installContextProperties(
        context, m_installedProperties + KvitQml::singletonNames());
}
