// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "reservedsubtrees.h"

#include <algorithm>

void ReservedSubtrees::add(const ReservedSubtree &subtree)
{
    if (subtree.name.isEmpty() || subtree.admitPattern.isEmpty())
        return;
    const auto taken = [&subtree](const Compiled &c) {
        return c.spec.name == subtree.name;
    };
    if (std::any_of(m_subtrees.cbegin(), m_subtrees.cend(), taken))
        return;

    Compiled compiled;
    compiled.spec = subtree;
    // The default conversion is the file-globbing one, where a wildcard does
    // not cross a path separator. That is what makes `*/report.md` mean "one
    // folder down and no further" rather than "anywhere below".
    compiled.pattern = QRegularExpression(
        QRegularExpression::wildcardToRegularExpression(subtree.admitPattern),
        QRegularExpression::CaseInsensitiveOption);
    m_subtrees.push_back(std::move(compiled));
}

void ReservedSubtrees::clear()
{
    m_subtrees.clear();
}

QStringList ReservedSubtrees::names() const
{
    QStringList result;
    result.reserve(static_cast<int>(m_subtrees.size()));
    for (const Compiled &subtree : m_subtrees)
        result.append(subtree.spec.name);
    return result;
}

QStringList ReservedSubtrees::labels() const
{
    QStringList result;
    result.reserve(static_cast<int>(m_subtrees.size()));
    for (const Compiled &subtree : m_subtrees)
        result.append(subtree.spec.label);
    return result;
}

QString ReservedSubtrees::labelForName(const QString &name) const
{
    for (const Compiled &subtree : m_subtrees) {
        if (subtree.spec.name == name)
            return subtree.spec.label;
    }
    return QString();
}

const ReservedSubtrees::Compiled *
ReservedSubtrees::subtreeFor(const QString &relPath) const
{
    for (const Compiled &subtree : m_subtrees) {
        const QString &name = subtree.spec.name;
        if (relPath.size() > name.size() && relPath.startsWith(name)
            && relPath.at(name.size()) == QLatin1Char('/')) {
            return &subtree;
        }
    }
    return nullptr;
}

bool ReservedSubtrees::isReservedPath(const QString &relPath) const
{
    return subtreeFor(relPath) != nullptr;
}

bool ReservedSubtrees::isReservedDir(const QString &relDir) const
{
    if (relDir.isEmpty())
        return false;
    for (const Compiled &subtree : m_subtrees) {
        if (relDir == subtree.spec.name)
            return true;
    }
    return subtreeFor(relDir) != nullptr;
}

QString ReservedSubtrees::admittedLabel(const QString &relPath) const
{
    const Compiled *subtree = subtreeFor(relPath);
    if (!subtree)
        return QString();
    const QString inside = relPath.mid(subtree->spec.name.size() + 1);
    const QRegularExpressionMatch match = subtree->pattern.match(inside);
    return match.hasMatch() ? subtree->spec.label : QString();
}

QString ReservedSubtrees::requiredTypeFor(const QString &relPath) const
{
    const Compiled *subtree = subtreeFor(relPath);
    return subtree ? subtree->spec.requiredType : QString();
}

bool ReservedSubtrees::typeSatisfies(const QString &relPath,
                                     const QString &type) const
{
    const QString required = requiredTypeFor(relPath);
    if (required.isEmpty())
        return true;
    return type.compare(required, Qt::CaseInsensitive) == 0;
}
