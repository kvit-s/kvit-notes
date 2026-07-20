// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockattributes.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

// The trailing tag, anchored to end of line: optional separating whitespace,
// "<!--kvit ", a non-greedy payload, "-->", optional trailing whitespace. The
// payload cannot contain "-->", so the non-greedy capture is unambiguous.
const QRegularExpression &tagRe()
{
    static const QRegularExpression re(
        QStringLiteral("\\s*<!--kvit (.*?)-->\\s*$"));
    return re;
}

} // namespace

QString BlockAttributes::stripTag(const QString &line, QString *payload)
{
    const QRegularExpressionMatch m = tagRe().match(line);
    if (!m.hasMatch())
        return line;  // no kvit tag — byte-identical
    if (payload)
        *payload = canonical(m.captured(1).trimmed());
    return line.left(m.capturedStart(0));
}

QString BlockAttributes::attachTag(const QString &content, const QString &payload)
{
    if (payload.isEmpty())
        return content;  // no attributes — byte-identical fast path

    const QString canon = canonical(payload);
    if (canon.isEmpty())
        return content;  // no attributes — byte-identical
    return content + QStringLiteral("  <!--kvit ") + canon + QStringLiteral("-->");
}

QMap<QString, QString> BlockAttributes::parseMap(const QString &payload)
{
    // QMap keeps keys sorted, which gives the canonical order for free.
    QMap<QString, QString> map;
    const QString trimmed = payload.trimmed();
    if (trimmed.isEmpty())
        return map;
    const QStringList tokens =
        trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                      Qt::SkipEmptyParts);
    for (const QString &tok : tokens) {
        const int eq = tok.indexOf(QLatin1Char('='));
        if (eq < 0) {
            map.insert(tok, QString());          // bare flag
        } else {
            const QString key = tok.left(eq);
            if (!key.isEmpty())
                map.insert(key, tok.mid(eq + 1)); // key=value (value may be "")
        }
    }
    return map;
}

QString BlockAttributes::serializeMap(const QMap<QString, QString> &map)
{
    QStringList out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (it.value().isEmpty())
            out << it.key();
        else
            out << it.key() + QLatin1Char('=') + it.value();
    }
    return out.join(QLatin1Char(' '));
}

QString BlockAttributes::canonical(const QString &payload)
{
    return serializeMap(parseMap(payload));
}

bool BlockAttributes::has(const QString &payload, const QString &key) const
{
    return parseMap(payload).contains(key);
}

QString BlockAttributes::str(const QString &payload, const QString &key,
                             const QString &def) const
{
    const QMap<QString, QString> map = parseMap(payload);
    const auto it = map.constFind(key);
    return it == map.constEnd() ? def : it.value();
}

int BlockAttributes::num(const QString &payload, const QString &key,
                         int def) const
{
    const QMap<QString, QString> map = parseMap(payload);
    const auto it = map.constFind(key);
    if (it == map.constEnd())
        return def;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : def;
}

QString BlockAttributes::withValue(const QString &payload, const QString &key,
                                   const QString &value) const
{
    QMap<QString, QString> map = parseMap(payload);
    map.insert(key, value);
    return serializeMap(map);
}

QString BlockAttributes::withFlag(const QString &payload, const QString &key,
                                  bool on) const
{
    QMap<QString, QString> map = parseMap(payload);
    if (on)
        map.insert(key, QString());
    else
        map.remove(key);
    return serializeMap(map);
}

QString BlockAttributes::without(const QString &payload, const QString &key) const
{
    QMap<QString, QString> map = parseMap(payload);
    map.remove(key);
    return serializeMap(map);
}
