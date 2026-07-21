// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "todometa.h"

#include <QRegularExpression>

namespace {
// Priority emoji, matched and emitted.
const QString kHigh = QString::fromUtf8("\xE2\x8F\xAB");     // ⏫ U+23EB
const QString kMedium = QString::fromUtf8("\xF0\x9F\x94\xBC"); // 🔼 U+1F53C
const QString kLow = QString::fromUtf8("\xF0\x9F\x94\xBD");    // 🔽 U+1F53D
const QString kCalendar = QString::fromUtf8("\xF0\x9F\x93\x85"); // 📅 U+1F4C5

// The trailing metadata block: one or more space-led priority emoji or a
// dated calendar token, anchored to the end of the content.
const QRegularExpression &tailRe()
{
    static const QRegularExpression re(
        QStringLiteral("((?:[ \\t]*(?:") + kCalendar
        + QStringLiteral("[ \\t]*\\d{4}-\\d{2}-\\d{2}|") + kHigh
        + QStringLiteral("|") + kMedium + QStringLiteral("|") + kLow
        + QStringLiteral("))+)$"));
    return re;
}
} // namespace

namespace TodoMeta {

Meta parse(const QString &content)
{
    Meta m;
    const QRegularExpressionMatch match = tailRe().match(content);
    if (!match.hasMatch()) {
        m.text = content;
        return m;
    }
    m.tail = match.captured(1);
    m.text = content.left(match.capturedStart(1)).trimmed();

    // Extract the due date.
    static const QRegularExpression dateRe(
        kCalendar + QStringLiteral("[ \\t]*(\\d{4}-\\d{2}-\\d{2})"));
    const QRegularExpressionMatch dm = dateRe.match(m.tail);
    if (dm.hasMatch())
        m.due = dm.captured(1);

    // Extract the priority (first match wins).
    if (m.tail.contains(kHigh)) m.priority = High;
    else if (m.tail.contains(kMedium)) m.priority = Medium;
    else if (m.tail.contains(kLow)) m.priority = Low;

    return m;
}

QString build(const QString &text, const QString &due, int priority)
{
    QString out = text.trimmed();
    // Canonical order: priority, then due date (Obsidian Tasks convention).
    if (priority == High) out += QLatin1Char(' ') + kHigh;
    else if (priority == Medium) out += QLatin1Char(' ') + kMedium;
    else if (priority == Low) out += QLatin1Char(' ') + kLow;
    if (!due.isEmpty())
        out += QLatin1Char(' ') + kCalendar + QLatin1Char(' ') + due;
    return out;
}

QString displayText(const QString &content)
{
    return parse(content).text;
}

} // namespace TodoMeta

QVariantMap TodoMetaTools::parse(const QString &content) const
{
    const TodoMeta::Meta m = TodoMeta::parse(content);
    return {
        { QStringLiteral("text"), m.text },
        { QStringLiteral("due"), m.due },
        { QStringLiteral("priority"), m.priority },
        { QStringLiteral("tail"), m.tail },
    };
}
