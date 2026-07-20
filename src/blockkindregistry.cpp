// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockkindregistry.h"

BlockKindRegistry &BlockKindRegistry::instance()
{
    static BlockKindRegistry registry;
    return registry;
}

BlockKindRegistry::BlockKindRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltins();
}

void BlockKindRegistry::registerBuiltins()
{
    // The built-ins carry no delegate URL: main.qml declares a DelegateChoice
    // for each of them statically, which keeps the common rendering path free
    // of any registry lookup.
    const struct { const char *language; int kind; } builtins[] = {
        { "kanban",  BlockKinds::Kanban },
        { "toc",     BlockKinds::Toc },
        { "mermaid", BlockKinds::Mermaid },
        { "query",   BlockKinds::Query },
    };
    for (const auto &builtin : builtins) {
        const QString language = QString::fromLatin1(builtin.language);
        m_byLanguage.insert(language, Entry{language, builtin.kind, QString()});
    }
}

int BlockKindRegistry::registerFenceLanguage(const QString &language,
                                             const QString &delegateUrl)
{
    if (language.isEmpty())
        return 0;

    const auto existing = m_byLanguage.constFind(language);
    if (existing != m_byLanguage.constEnd())
        return existing->kind;

    const int kind = m_nextKind++;
    m_byLanguage.insert(language, Entry{language, kind, delegateUrl});
    if (!delegateUrl.isEmpty())
        m_delegateByKind.insert(kind, delegateUrl);
    return kind;
}

int BlockKindRegistry::kindForLanguage(const QString &language) const
{
    const auto entry = m_byLanguage.constFind(language);
    return entry == m_byLanguage.constEnd() ? 0 : entry->kind;
}

QString BlockKindRegistry::delegateUrl(int kind) const
{
    return m_delegateByKind.value(kind);
}

QVariantList BlockKindRegistry::registeredDelegates() const
{
    QVariantList result;
    for (auto it = m_byLanguage.constBegin(); it != m_byLanguage.constEnd(); ++it) {
        if (it->delegateUrl.isEmpty())
            continue;
        result.append(QVariantMap{
            { QStringLiteral("kind"), it->kind },
            { QStringLiteral("language"), it->language },
            { QStringLiteral("delegateUrl"), it->delegateUrl },
        });
    }
    return result;
}

QStringList BlockKindRegistry::languages() const
{
    return m_byLanguage.keys();
}

void BlockKindRegistry::reset()
{
    m_byLanguage.clear();
    m_delegateByKind.clear();
    m_nextKind = FirstRegisteredKind;
    registerBuiltins();
}
