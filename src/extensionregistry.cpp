// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "extensionregistry.h"

#include "blockkindregistry.h"

void KvitExtension::registerBlockKinds(BlockKindRegistry &registry)
{
    Q_UNUSED(registry);
}

void KvitExtension::installContextProperties(QQmlContext *context)
{
    Q_UNUSED(context);
}

QString KvitExtension::qmlSlot(const QString &slot) const
{
    Q_UNUSED(slot);
    return QString();
}

ExtensionRegistry &ExtensionRegistry::instance()
{
    static ExtensionRegistry registry;
    return registry;
}

ExtensionRegistry::ExtensionRegistry(QObject *parent)
    : QObject(parent)
{
}

ExtensionRegistry::~ExtensionRegistry() = default;

void ExtensionRegistry::install(std::unique_ptr<KvitExtension> extension)
{
    if (!extension)
        return;
    const QString name = extension->name();
    for (const auto &installed : m_extensions) {
        if (installed->name() == name)
            return;
    }
    m_extensions.push_back(std::move(extension));
    emit extensionsChanged();
}

QStringList ExtensionRegistry::names() const
{
    QStringList result;
    result.reserve(static_cast<int>(m_extensions.size()));
    for (const auto &extension : m_extensions)
        result.append(extension->name());
    return result;
}

QString ExtensionRegistry::slotSource(const QString &slot) const
{
    for (const auto &extension : m_extensions) {
        const QString source = extension->qmlSlot(slot);
        if (!source.isEmpty())
            return source;
    }
    return QString();
}

void ExtensionRegistry::registerBlockKinds(BlockKindRegistry &registry)
{
    for (const auto &extension : m_extensions)
        extension->registerBlockKinds(registry);
}

void ExtensionRegistry::installContextProperties(QQmlContext *context)
{
    if (!context)
        return;
    for (const auto &extension : m_extensions)
        extension->installContextProperties(context);
}

void ExtensionRegistry::clear()
{
    if (m_extensions.empty())
        return;
    m_extensions.clear();
    emit extensionsChanged();
}
