// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notetemplates.h"
#include "notecollection.h"
#include "notefrontmatter.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// The characters a template name may not contain: path separators and the
// rest of the set Windows rejects in a file name. They used to be stripped,
// which silently aliased distinct names onto one file — "A/B" and "AB" both
// became AB.md, so saving one overwrote the other and deleting one deleted
// the other. A name carrying any of them is now refused outright, which
// keeps the file name and the user-visible name the same string.
const QLatin1String kForbiddenNameChars("/\\:*?\"<>|");

bool isValidTemplateName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    for (const QChar &ch : trimmed) {
        if (kForbiddenNameChars.contains(ch))
            return false;
        // Control characters are equally unusable in a file name.
        if (ch.category() == QChar::Other_Control)
            return false;
    }
    // "." and ".." name directories, not templates.
    if (trimmed == QLatin1String(".") || trimmed == QLatin1String(".."))
        return false;
    return true;
}

// The file base name for a valid template name. Names are no longer
// rewritten, so this is just the trimmed name; callers must have checked
// isValidTemplateName() first.
QString sanitize(const QString &name)
{
    return isValidTemplateName(name) ? name.trimmed() : QString();
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

bool writeAll(const QString &path, const QString &content)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    return f.commit();
}

} // namespace

NoteTemplates::NoteTemplates(QObject *parent)
    : QObject(parent)
{
}

void NoteTemplates::setCollection(NoteCollection *collection)
{
    m_collection = collection;
    bump();
}

void NoteTemplates::setClockForTesting(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime NoteTemplates::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void NoteTemplates::bump()
{
    ++m_revision;
    emit revisionChanged();
}

QString NoteTemplates::templatesDir() const
{
    if (!m_collection || !m_collection->isOpen())
        return QString();
    return QDir(m_collection->rootPath())
        .filePath(QStringLiteral(".kvit/templates"));
}

QString NoteTemplates::pathFor(const QString &name) const
{
    const QString dir = templatesDir();
    if (dir.isEmpty())
        return QString();
    const QString base = sanitize(name);
    if (base.isEmpty())
        return QString();
    return QDir(dir).filePath(base + QStringLiteral(".md"));
}

QStringList NoteTemplates::templateNames() const
{
    const QString dir = templatesDir();
    if (dir.isEmpty())
        return {};
    QDir d(dir);
    QStringList names;
    const QStringList files =
        d.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QString &f : files)
        names.append(f.left(f.size() - 3)); // drop ".md"
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return names;
}

QString NoteTemplates::readTemplate(const QString &name) const
{
    const QString path = pathFor(name);
    return path.isEmpty() ? QString() : readAll(path);
}

bool NoteTemplates::writeTemplate(const QString &name, const QString &content)
{
    const QString dir = templatesDir();
    if (dir.isEmpty())
        return false;
    if (sanitize(name).isEmpty())
        return false;
    QDir().mkpath(dir);
    if (!writeAll(pathFor(name), content))
        return false;
    bump();
    return true;
}

bool NoteTemplates::deleteTemplate(const QString &name)
{
    const QString path = pathFor(name);
    if (path.isEmpty() || !QFile::exists(path))
        return false;
    if (!QFile::remove(path))
        return false;
    bump();
    return true;
}

void NoteTemplates::seedBuiltinsIfEmpty()
{
    const QString dir = templatesDir();
    if (dir.isEmpty())
        return;
    if (!templateNames().isEmpty())
        return; // the user already has templates; never overwrite

    writeTemplate(QStringLiteral("Meeting Notes"),
        QStringLiteral(
            "---\ntags: [meeting]\n---\n"
            "# {{title}}\n\n"
            "**Date:** {{date}}  \n**Time:** {{time}}\n\n"
            "## Attendees\n\n- \n\n"
            "## Agenda\n\n1. \n\n"
            "## Notes\n\n\n\n"
            "## Action items\n\n- [ ] \n"));

    writeTemplate(QStringLiteral("Project Plan"),
        QStringLiteral(
            "# {{title}}\n\n"
            "*Started {{date}}.*\n\n"
            "## Goal\n\n\n\n"
            "## Milestones\n\n- [ ] \n\n"
            "## Risks\n\n| Risk | Mitigation |\n| --- | --- |\n|  |  |\n"));

    writeTemplate(QStringLiteral("Daily Journal"),
        QStringLiteral(
            "---\ntags: [journal]\n---\n"
            "# {{date:dddd, MMMM d, yyyy}}\n\n"
            "## Three good things\n\n1. \n2. \n3. \n\n"
            "## Today\n\n\n\n"
            "## Notes\n\n\n"));
}

QString NoteTemplates::expand(const QString &content, const QString &title,
                              const QDateTime &now)
{
    // {{token}} with an optional :FORMAT argument. A trailing whitespace-only
    // difference is tolerated inside the braces.
    static const QRegularExpression re(
        QStringLiteral("\\{\\{\\s*([a-zA-Z]+)(?::([^}]*))?\\s*\\}\\}"));
    QString out;
    out.reserve(content.size());
    int last = 0;
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += content.mid(last, m.capturedStart() - last);
        const QString token = m.captured(1).toLower();
        const QString arg = m.captured(2);
        QString replacement;
        bool known = true;
        if (token == QLatin1String("date")) {
            replacement = arg.isEmpty()
                ? now.date().toString(QStringLiteral("yyyy-MM-dd"))
                : now.toString(arg);
        } else if (token == QLatin1String("time")) {
            replacement = arg.isEmpty()
                ? now.time().toString(QStringLiteral("HH:mm"))
                : now.toString(arg);
        } else if (token == QLatin1String("title")) {
            replacement = title;
        } else {
            known = false;
        }
        // An unknown token is left verbatim (never silently dropped).
        out += known ? replacement : m.captured(0);
        last = m.capturedEnd();
    }
    out += content.mid(last);
    return out;
}

QString NoteTemplates::expandNow(const QString &content,
                                 const QString &title) const
{
    return expand(content, title, now());
}

QVariantMap NoteTemplates::instantiate(const QString &name,
                                       const QString &title) const
{
    const QString raw = readTemplate(name);
    if (raw.isEmpty() && !QFile::exists(pathFor(name)))
        return {};
    const QString expanded = expand(raw, title, now());
    const NoteFrontMatter::Split split = NoteFrontMatter::split(expanded);
    const NoteFrontMatter::Metadata meta = NoteFrontMatter::parse(split.block);
    return QVariantMap{
        {QStringLiteral("body"), split.present ? split.body : expanded},
        {QStringLiteral("tags"), meta.tags},
        {QStringLiteral("favorite"), meta.favorite},
    };
}
