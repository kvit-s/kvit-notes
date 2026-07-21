// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "extensionregistry.h"

#include <QLoggingCategory>
#include <QQmlContext>
#include <QRegularExpression>

#include <algorithm>

#include "blockkindregistry.h"

Q_LOGGING_CATEGORY(lcExtensions, "kvit.extensions")

void KvitExtension::registerBlockKinds(BlockKindRegistry &registry)
{
    Q_UNUSED(registry);
}

QVariantMap KvitExtension::contextObjects()
{
    return {};
}

QString KvitExtension::qmlSlot(const QString &slot) const
{
    Q_UNUSED(slot);
    return QString();
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

void ExtensionRegistry::installContextProperties(QQmlContext *context,
                                                 const QStringList &reservedNames)
{
    m_publishedNamespaces.clear();
    if (!context)
        return;

    // A QML identifier, so `agent.session` resolves rather than parsing as
    // something else.
    static const QRegularExpression identifier(
        QStringLiteral("^[a-z_][A-Za-z0-9_]*$"));

    QStringList taken = reservedNames;
    for (const auto &extension : m_extensions) {
        const QString ns = extension->qmlNamespace();
        if (!identifier.match(ns).hasMatch()) {
            qCWarning(lcExtensions,
                      "module '%s' asked for QML namespace '%s', which is not a "
                      "valid identifier; it will publish nothing",
                      qPrintable(extension->name()), qPrintable(ns));
            continue;
        }
        // Case-insensitive on purpose, and the reason is worth the two lines
        // it takes to say. The core's own objects are QML singletons with
        // capitalised names, while a module namespace must start lowercase,
        // so `theme` and `Theme` can never collide as identifiers — they
        // would simply coexist, one character apart, meaning entirely
        // unrelated objects in the same file. That is a debugging trap rather
        // than a style preference, and the module most likely to hit it is
        // written in another repository by someone who cannot see this rule
        // unless it is enforced.
        const auto clashes = [&ns](const QString &name) {
            return name.compare(ns, Qt::CaseInsensitive) == 0;
        };
        const auto hit = std::find_if(taken.cbegin(), taken.cend(), clashes);
        if (hit != taken.cend()) {
            if (*hit == ns) {
                qCWarning(lcExtensions,
                          "module '%s' asked for QML namespace '%s', which is "
                          "already taken; it will publish nothing",
                          qPrintable(extension->name()), qPrintable(ns));
            } else {
                qCWarning(lcExtensions,
                          "module '%s' asked for QML namespace '%s', but the "
                          "editor already publishes '%s'. QML would then have "
                          "two names one character apart standing for "
                          "unrelated objects, so the namespace is refused and "
                          "the module will publish nothing. Choose a name of "
                          "your own, such as '%s%s'.",
                          qPrintable(extension->name()), qPrintable(ns),
                          qPrintable(*hit), qPrintable(extension->name()),
                          qPrintable(ns));
            }
            continue;
        }

        // One property per module, holding everything it contributes. The map
        // reaches QML as a JavaScript object, so `agent.session` works and no
        // module can occupy a bare global name.
        context->setContextProperty(ns, extension->contextObjects());
        taken.append(ns);
        m_publishedNamespaces.append(ns);
    }
}

void ExtensionRegistry::clear()
{
    if (m_extensions.empty())
        return;
    m_extensions.clear();
    emit extensionsChanged();
}
