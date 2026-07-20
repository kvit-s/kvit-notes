// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QMLSERVICES_H
#define QMLSERVICES_H

#include <QHash>
#include <QObject>
#include <QQmlEngine>

class QJSEngine;

// How a QML singleton finds the composition it belongs to.
//
// The editor's services are members of an AppContext, and a process can hold
// more than one: the shell has its own, and tests build their own to stay
// isolated. A QML singleton registered with qmlRegisterSingletonInstance
// would defeat that, because it binds one object pointer for the whole
// process.
//
// Qt resolves a QML_SINGLETON through a static create(QQmlEngine *,
// QJSEngine *) once per engine, which is the seam this uses. AppContext fills
// a table with its own services and hangs it on the engine; each service's
// create() reads its instance back out. One engine, one composition, and a
// second AppContext in the same process is unaffected.
//
// The table is keyed by QMetaObject rather than by name, so the registering
// side and the reading side are tied together by the type itself and a
// mismatch cannot compile.
namespace KvitQml {

class ServiceTable
{
public:
    template <typename T>
    void add(T *instance)
    {
        m_services.insert(&T::staticMetaObject, instance);
    }

    QObject *lookup(const QMetaObject *type) const
    {
        return m_services.value(type, nullptr);
    }

private:
    QHash<const QMetaObject *, QObject *> m_services;
};

// Publish `table` on `engine`. The table must outlive the engine; AppContext
// owns both ends of that.
void attachServices(QQmlEngine *engine, ServiceTable *table);

// The table attached to `engine`, or nullptr when nothing attached one —
// which is what an engine built by something other than AppContext looks
// like.
const ServiceTable *services(const QQmlEngine *engine);

// The instance of T this engine's composition owns, or nullptr.
//
// A null return is what QML reports as a singleton whose members are all
// undefined, so it shows up as a load warning rather than a crash, and
// ShellTests fails on any load warning.
template <typename T>
T *singleton(QQmlEngine *engine)
{
    const ServiceTable *table = services(engine);
    if (!table)
        return nullptr;
    T *instance = qobject_cast<T *>(table->lookup(&T::staticMetaObject));
    if (instance) {
        // The composition owns these, not the engine. Without this the
        // garbage collector would take a singleton it believes it created and
        // leave AppContext holding a dangling member.
        QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    }
    return instance;
}

}   // namespace KvitQml

#endif // QMLSERVICES_H
