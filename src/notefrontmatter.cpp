// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notefrontmatter.h"

namespace {

// One line of the file, without its terminator, plus where its content
// starts and where the NEXT line starts in the original string (so split()
// can cut byte-exactly).
struct Line {
    QString text;      // without '\n'; a trailing '\r' is kept in the bytes
                       // but stripped here for matching
    int start = 0;     // offset of the line's first character
    int nextStart = 0; // offset just past the line's '\n' (or end of string)
};

QList<Line> scanLines(const QString &text)
{
    QList<Line> lines;
    int pos = 0;
    const int n = text.size();
    while (pos < n) {
        int nl = text.indexOf(QLatin1Char('\n'), pos);
        int end = (nl < 0) ? n : nl;
        int next = (nl < 0) ? n : nl + 1;
        QString t = text.mid(pos, end - pos);
        if (t.endsWith(QLatin1Char('\r')))
            t.chop(1);
        lines.append({t, pos, next});
        pos = next;
    }
    return lines;
}

bool isFence(const QString &line)
{
    return line == QLatin1String("---");
}

bool isBlankLine(const QString &line)
{
    return line.trimmed().isEmpty();
}

bool isCommentLine(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char('#'));
}

bool isListItemLine(const QString &line)
{
    const QString t = line.trimmed();
    return t.startsWith(QLatin1String("- ")) || t == QLatin1String("-");
}

bool isContinuationLine(const QString &line)
{
    return !line.isEmpty()
        && (line.at(0) == QLatin1Char(' ') || line.at(0) == QLatin1Char('\t'));
}

// "key: value" or "key:" — key starts at column 0, is non-empty, and
// contains no whitespace before the colon.
bool splitKeyLine(const QString &line, QString *key, QString *value)
{
    if (line.isEmpty() || line.at(0).isSpace())
        return false;
    int colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    QString k = line.left(colon);
    for (const QChar &c : k) {
        if (c.isSpace())
            return false;
    }
    // "key:value" without a space is not a mapping in YAML; require the
    // colon to end the line or be followed by whitespace.
    if (colon + 1 < line.size() && !line.at(colon + 1).isSpace())
        return false;
    *key = k;
    *value = line.mid(colon + 1).trimmed();
    return true;
}

bool isMappingShapedLine(const QString &line)
{
    QString k, v;
    return isBlankLine(line) || isCommentLine(line) || isListItemLine(line)
        || isContinuationLine(line) || splitKeyLine(line, &k, &v);
}

QString stripMatchingQuotes(const QString &s)
{
    if (s.size() >= 2) {
        const QChar first = s.front();
        const QChar last = s.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            return s.mid(1, s.size() - 2);
        }
    }
    return s;
}

bool parseBoolValue(const QString &value, bool *out)
{
    const QString v = value.toLower();
    if (v == QLatin1String("true")) {
        *out = true;
        return true;
    }
    if (v == QLatin1String("false")) {
        *out = false;
        return true;
    }
    return false;
}

bool parseDateTimeValue(const QString &raw, QDateTime *out)
{
    const QString value = stripMatchingQuotes(raw);
    QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
    if (dt.isValid()) {
        *out = dt;
        return true;
    }
    QDate d = QDate::fromString(value, Qt::ISODate);
    if (d.isValid()) {
        *out = d.startOfDay();
        return true;
    }
    return false;
}

} // namespace

NoteFrontMatter::Split NoteFrontMatter::split(const QString &fileText)
{
    Split result;
    result.body = fileText;

    if (!fileText.startsWith(QLatin1String("---")))
        return result;

    const int n = fileText.size();
    int lineEnd = fileText.indexOf(QLatin1Char('\n'));
    int nextLine = lineEnd < 0 ? n : lineEnd + 1;
    if (lineEnd < 0)
        lineEnd = n;

    QString firstLine = fileText.left(lineEnd);
    if (firstLine.endsWith(QLatin1Char('\r')))
        firstLine.chop(1);
    if (!isFence(firstLine))
        return result;

    bool hasKeyLine = false;
    int bodyStart = -1;
    int pos = nextLine;
    while (pos < n) {
        int nl = fileText.indexOf(QLatin1Char('\n'), pos);
        int end = nl < 0 ? n : nl;
        int next = nl < 0 ? n : nl + 1;
        QString line = fileText.mid(pos, end - pos);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        if (isFence(line)) {
            bodyStart = next;
            break;
        }

        if (!isMappingShapedLine(line))
            return result;
        QString k, v;
        if (splitKeyLine(line, &k, &v))
            hasKeyLine = true;

        pos = next;
    }
    if (bodyStart < 0 || !hasKeyLine)
        return result;

    result.block = fileText.left(bodyStart);
    result.body = fileText.mid(bodyStart);
    result.present = true;
    return result;
}

bool NoteFrontMatter::parseTagsValue(const QString &value, QStringList *tags)
{
    tags->clear();
    if (value.isEmpty())
        return true; // "tags:" alone — empty or block list follows
    if (value.startsWith(QLatin1Char('[')) && value.endsWith(QLatin1Char(']'))) {
        // Split on commas outside quotes, so a quoted entry may itself
        // contain a comma (serializeTag() quotes exactly these).
        const QString inner = value.mid(1, value.size() - 2);
        QStringList parts;
        QString part;
        QChar quote; // null when outside a quoted run
        for (const QChar &c : inner) {
            if (!quote.isNull()) {
                if (c == quote)
                    quote = QChar();
                part.append(c);
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                quote = c;
                part.append(c);
            } else if (c == QLatin1Char(',')) {
                parts.append(part);
                part.clear();
            } else {
                part.append(c);
            }
        }
        parts.append(part);
        for (const QString &p : parts) {
            const QString tag = stripMatchingQuotes(p.trimmed());
            if (!tag.isEmpty())
                tags->append(tag);
        }
        return true;
    }
    if (value.startsWith(QLatin1Char('[')))
        return false; // unterminated flow list — not understood
    const QString tag = stripMatchingQuotes(value);
    if (!tag.isEmpty())
        tags->append(tag);
    return true;
}

NoteFrontMatter::Metadata NoteFrontMatter::parse(const QString &block)
{
    Metadata meta;
    if (block.isEmpty())
        return meta;

    QList<Line> lines = scanLines(block);
    // Drop the fences; parse() accepts exactly what split() produced.
    if (!lines.isEmpty() && isFence(lines.first().text))
        lines.removeFirst();
    if (!lines.isEmpty() && isFence(lines.last().text))
        lines.removeLast();

    for (int i = 0; i < lines.size(); ++i) {
        const QString &text = lines.at(i).text;
        QString key, value;
        if (!splitKeyLine(text, &key, &value)) {
            // Blank, comment, or stray continuation with no owner key.
            meta.unknownLines.append(text);
            continue;
        }

        // The general key map: every key line,
        // known keys included so queries address tags/created uniformly.
        // Raw scalar only; a block list leaves "". Last occurrence wins,
        // matching the structured fields' rule.
        meta.fields.insert(key, value);

        if (key == QLatin1String("tags")) {
            QStringList tags;
            if (!parseTagsValue(value, &tags)) {
                meta.unknownLines.append(text);
                continue;
            }
            // Block-list form: "tags:" followed by "- item" lines.
            while (value.isEmpty() && i + 1 < lines.size()
                   && isListItemLine(lines.at(i + 1).text)) {
                const QString item =
                    lines.at(i + 1).text.trimmed().mid(1).trimmed();
                const QString tag = stripMatchingQuotes(item);
                if (!tag.isEmpty())
                    tags.append(tag);
                ++i;
            }
            meta.tags = tags;
        } else if (key == QLatin1String("created")) {
            QDateTime dt;
            if (parseDateTimeValue(value, &dt))
                meta.created = dt;
            else
                meta.unknownLines.append(text);
        } else if (key == QLatin1String("pinned")) {
            bool b = false;
            if (parseBoolValue(value, &b))
                meta.pinned = b;
            else
                meta.unknownLines.append(text);
        } else if (key == QLatin1String("favorite")) {
            bool b = false;
            if (parseBoolValue(value, &b))
                meta.favorite = b;
            else
                meta.unknownLines.append(text);
        } else if (key == QLatin1String("goal")) {
            // Per-note writing goal: a positive integer word target. A
            // non-integer or non-positive value is not understood, so it is
            // preserved verbatim like any foreign value.
            bool ok = false;
            const int g = value.trimmed().toInt(&ok);
            if (ok && g > 0)
                meta.goal = g;
            else
                meta.unknownLines.append(text);
        } else {
            // Foreign key: keep it and every line that belongs to it.
            meta.unknownLines.append(text);
            while (i + 1 < lines.size()
                   && (isListItemLine(lines.at(i + 1).text)
                       || isContinuationLine(lines.at(i + 1).text))) {
                meta.unknownLines.append(lines.at(i + 1).text);
                ++i;
            }
        }
    }
    return meta;
}

// ---- Typed field accessors ---------------------------------------------

QString NoteFrontMatter::Metadata::fieldString(const QString &key) const
{
    return stripMatchingQuotes(fields.value(key).trimmed());
}

QDateTime NoteFrontMatter::Metadata::fieldDate(const QString &key) const
{
    QDateTime dt;
    if (parseDateTimeValue(fields.value(key).trimmed(), &dt))
        return dt;
    return QDateTime();
}

double NoteFrontMatter::Metadata::fieldNumber(const QString &key,
                                              bool *ok) const
{
    bool parsed = false;
    // QString::toDouble is locale-independent (C locale) by contract.
    const double value = fieldString(key).toDouble(&parsed);
    if (ok)
        *ok = parsed;
    return parsed ? value : 0.0;
}

QStringList NoteFrontMatter::Metadata::fieldList(const QString &key) const
{
    const QString raw = fields.value(key).trimmed();
    if (raw.isEmpty())
        return {};
    // A YAML inline list goes through the tags parser (quote-aware, so a
    // quoted item may contain a comma); a plain scalar splits on commas.
    if (raw.startsWith(QLatin1Char('['))) {
        QStringList items;
        if (NoteFrontMatter::parseTagsValue(raw, &items))
            return items;
        return {};
    }
    QStringList out;
    const QStringList parts = raw.split(QLatin1Char(','));
    for (const QString &part : parts) {
        const QString item = stripMatchingQuotes(part.trimmed());
        if (!item.isEmpty())
            out.append(item);
    }
    return out;
}

QString NoteFrontMatter::serializeTag(const QString &tag)
{
    // Quote when the flow-list syntax or the quote stripping could
    // misread the tag; the parser strips exactly one matching outer pair.
    static const QString needsQuoting = QStringLiteral(",[]#:'\"");
    bool quote = tag != tag.trimmed();
    for (const QChar &c : tag) {
        if (needsQuoting.contains(c)) {
            quote = true;
            break;
        }
    }
    if (!quote)
        return tag;
    return QLatin1Char('"') + tag + QLatin1Char('"');
}

QString NoteFrontMatter::serialize(const Metadata &meta)
{
    QStringList lines;
    if (!meta.tags.isEmpty()) {
        QStringList quoted;
        for (const QString &tag : meta.tags)
            quoted.append(serializeTag(tag));
        lines.append(QStringLiteral("tags: [%1]")
                         .arg(quoted.join(QLatin1String(", "))));
    }
    if (meta.created.isValid()) {
        lines.append(QStringLiteral("created: %1")
                         .arg(meta.created.toString(Qt::ISODate)));
    }
    if (meta.pinned)
        lines.append(QStringLiteral("pinned: true"));
    if (meta.favorite)
        lines.append(QStringLiteral("favorite: true"));
    if (meta.goal > 0)
        lines.append(QStringLiteral("goal: %1").arg(meta.goal));
    lines.append(meta.unknownLines);

    if (lines.isEmpty())
        return QString();
    return QStringLiteral("---\n") + lines.join(QLatin1Char('\n'))
        + QStringLiteral("\n---\n");
}
