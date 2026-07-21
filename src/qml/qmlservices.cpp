// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "qmlservices.h"

#include "qmlsingletons.h"

#include <QQmlEngine>
#include <QVariant>

namespace {
// The engine property the table hangs on. Underscored because it is not part
// of anything QML is meant to reach; only the create() functions read it, and
// they go through services() below.
const char *kServiceTableProperty = "_kvitServiceTable";
}

namespace KvitQml {

QStringList singletonNames()
{
    // Expanded from the one list that also declares the wrappers, so the
    // reserved set cannot fall behind the registrations.
    static const QStringList names = {
#define KVIT_QML_SINGLETON_NAME(Type, Name) QStringLiteral(#Name),
        KVIT_QML_SINGLETONS(KVIT_QML_SINGLETON_NAME)
#undef KVIT_QML_SINGLETON_NAME
    };
    return names;
}


void attachServices(QQmlEngine *engine, ServiceTable *table)
{
    if (!engine)
        return;
    engine->setProperty(kServiceTableProperty,
                        QVariant::fromValue(static_cast<void *>(table)));
}

const ServiceTable *services(const QQmlEngine *engine)
{
    if (!engine)
        return nullptr;
    return static_cast<const ServiceTable *>(
        engine->property(kServiceTableProperty).value<void *>());
}

}   // namespace KvitQml
