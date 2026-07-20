// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QMLSINGLETONS_H
#define QMLSINGLETONS_H

#include <QtQml/qqmlregistration.h>

#include "blockmenumodel.h"
#include "documentexporter.h"
#include "documentimporter.h"
#include "documentserializer.h"
#include "documentstats.h"
#include "embedmetadata.h"
#include "filewatcher.h"
#include "foldertreemodel.h"
#include "globalhotkey.h"
#include "kanbandata.h"
#include "markdownformatter.h"
#include "mathcommandmodel.h"
#include "mathrenderer.h"
#include "navigationhistory.h"
#include "qmlservices.h"
#include "querytools.h"
#include "quickswitchermodel.h"
#include "shortcutcatalog.h"
#include "systemtray.h"
#include "tabledata.h"
#include "todometa.h"
#include "updatechecker.h"

// Which services QML reaches as `Kvit` module singletons, and how each one
// finds the composition it belongs to.
//
// Every declaration here is a QML_FOREIGN wrapper rather than a macro on the
// service itself, for two reasons.
//
// The first is layering: the services stay free of QML registration, so the
// list of what the shell can reach is one file rather than a macro scattered
// across forty headers.
//
// The second is a trap. Qt chooses how to construct a singleton in
// QQmlPrivate::singletonConstructionMode(), and it tests
// std::is_default_constructible<T> BEFORE it looks for a create() factory.
// Every service here takes `QObject *parent = nullptr`, so putting
// QML_SINGLETON and a create() on the class itself gets the factory silently
// ignored: Qt default-constructs its own instance, QML gets a valid object
// wired to nothing, and there is no warning anywhere. A sidebar bound to such
// a FolderTreeModel is simply always empty.
//
// The FactoryWrapper branch is tested first, before default-constructibility,
// so a foreign wrapper carrying the create() is honoured. That is what these
// are. The test that pins this down is
// everySingletonResolvesWithinItsOwnComposition in tests/test_shell.cpp; it
// compares addresses, because an instance that merely exists proves nothing.

// The QML name is given separately because a few services are published under
// a name that is not their class name — MathTools has always been
// `mathRenderer` to QML, and renaming it here would be a second change riding
// on this one. Keeping the old name, capitalised, makes every call site a
// one-character edit and keeps the diff reviewable.
#define KVIT_QML_SINGLETON_NAMED(Type, Name)                                  \
    struct Name##Foreign                                                      \
    {                                                                         \
        Q_GADGET                                                              \
        QML_FOREIGN(Type)                                                     \
        QML_NAMED_ELEMENT(Name)                                               \
        QML_SINGLETON                                                         \
    public:                                                                   \
        static Type *create(QQmlEngine *engine, QJSEngine *)                  \
        {                                                                     \
            return KvitQml::singleton<Type>(engine);                          \
        }                                                                     \
    };

#define KVIT_QML_SINGLETON(Type) KVIT_QML_SINGLETON_NAMED(Type, Type)

KVIT_QML_SINGLETON(QueryTools)
KVIT_QML_SINGLETON(GlobalHotkey)
KVIT_QML_SINGLETON(FileWatcher)
KVIT_QML_SINGLETON(ShortcutCatalog)
KVIT_QML_SINGLETON(QuickSwitcherModel)
KVIT_QML_SINGLETON(FolderTreeModel)

KVIT_QML_SINGLETON(MarkdownFormatter)
KVIT_QML_SINGLETON(BlockMenuModel)
KVIT_QML_SINGLETON(MathCommandModel)
KVIT_QML_SINGLETON(DocumentStats)
KVIT_QML_SINGLETON(DocumentExporter)
KVIT_QML_SINGLETON(DocumentSerializer)
KVIT_QML_SINGLETON(DocumentImporter)
KVIT_QML_SINGLETON(EmbedMetadata)
KVIT_QML_SINGLETON(SystemTray)
KVIT_QML_SINGLETON(NavigationHistory)
KVIT_QML_SINGLETON(UpdateChecker)
KVIT_QML_SINGLETON(TableTools)
KVIT_QML_SINGLETON(KanbanTools)
KVIT_QML_SINGLETON_NAMED(TodoMetaTools, TodoMeta)
KVIT_QML_SINGLETON_NAMED(MathTools, MathRenderer)

#endif // QMLSINGLETONS_H
