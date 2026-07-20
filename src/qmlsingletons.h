// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QMLSINGLETONS_H
#define QMLSINGLETONS_H

#include <QtQml/qqmlregistration.h>

#include "filewatcher.h"
#include "foldertreemodel.h"
#include "globalhotkey.h"
#include "qmlservices.h"
#include "querytools.h"
#include "quickswitchermodel.h"
#include "shortcutcatalog.h"

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

#define KVIT_QML_SINGLETON(Type)                                              \
    struct Type##Foreign                                                      \
    {                                                                         \
        Q_GADGET                                                              \
        QML_FOREIGN(Type)                                                     \
        QML_NAMED_ELEMENT(Type)                                               \
        QML_SINGLETON                                                         \
    public:                                                                   \
        static Type *create(QQmlEngine *engine, QJSEngine *)                  \
        {                                                                     \
            return KvitQml::singleton<Type>(engine);                          \
        }                                                                     \
    };

KVIT_QML_SINGLETON(QueryTools)
KVIT_QML_SINGLETON(GlobalHotkey)
KVIT_QML_SINGLETON(FileWatcher)
KVIT_QML_SINGLETON(ShortcutCatalog)
KVIT_QML_SINGLETON(QuickSwitcherModel)
KVIT_QML_SINGLETON(FolderTreeModel)

#endif // QMLSINGLETONS_H
