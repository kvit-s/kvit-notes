// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ignorerules.h"

#include "settingsstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>
#include <QVariantMap>

const QString IgnoreRules::SettingsKey =
    QStringLiteral("vault.ignorePatternsByRoot");

namespace {

QString cleanRelative(QString path)
{
    path = QDir::fromNativeSeparators(path);
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    path = QDir::cleanPath(path);
    return path == QLatin1String(".") ? QString() : path;
}

bool markerIsEscaped(const QString &text, int at)
{
    int slashes = 0;
    for (int i = at - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++slashes;
    return (slashes % 2) != 0;
}

QString globRegularExpression(const QString &glob)
{
    QString result;
    result.reserve(glob.size() * 2);
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('*')) {
                while (i + 1 < glob.size()
                       && glob.at(i + 1) == QLatin1Char('*')) {
                    ++i;
                }
                if (i + 1 < glob.size()
                    && glob.at(i + 1) == QLatin1Char('/')) {
                    ++i;
                    result += QStringLiteral("(?:.*/)?");
                } else {
                    result += QStringLiteral(".*");
                }
            } else {
                result += QStringLiteral("[^/]*");
            }
        } else if (c == QLatin1Char('?')) {
            result += QStringLiteral("[^/]");
        } else if (c == QLatin1Char('[')) {
            const int close = glob.indexOf(QLatin1Char(']'), i + 1);
            if (close > i + 1) {
                QString klass = glob.mid(i + 1, close - i - 1);
                if (klass.startsWith(QLatin1Char('!')))
                    klass[0] = QLatin1Char('^');
                result += QLatin1Char('[') + klass + QLatin1Char(']');
                i = close;
            } else {
                result += QStringLiteral("\\[");
            }
        } else if (c == QLatin1Char('\\') && i + 1 < glob.size()) {
            result += QRegularExpression::escape(QString(glob.at(++i)));
        } else {
            result += QRegularExpression::escape(QString(c));
        }
    }
    return result;
}

IgnoreRules::Rule compileRule(QString pattern, bool *ok)
{
    *ok = false;
    IgnoreRules::Rule rule;

    if (pattern.endsWith(QLatin1Char('\r')))
        pattern.chop(1);
    // Git discards unescaped trailing spaces. Escaped spaces are literal and
    // lose only their escaping backslash.
    while (pattern.endsWith(QLatin1Char(' '))
           && !markerIsEscaped(pattern, pattern.size() - 1)) {
        pattern.chop(1);
    }
    if (pattern.isEmpty())
        return rule;
    if (pattern.startsWith(QLatin1Char('#')))
        return rule;
    if (pattern.startsWith(QStringLiteral("\\#")))
        pattern.remove(0, 1);

    if (pattern.startsWith(QLatin1Char('!'))) {
        rule.negated = true;
        pattern.remove(0, 1);
    } else if (pattern.startsWith(QStringLiteral("\\!"))) {
        pattern.remove(0, 1);
    }
    if (pattern.isEmpty())
        return rule;

    if (pattern.endsWith(QLatin1Char('/'))
        && !markerIsEscaped(pattern, pattern.size() - 1)) {
        rule.directoryOnly = true;
        pattern.chop(1);
    }
    const bool anchored = pattern.startsWith(QLatin1Char('/'));
    if (anchored)
        pattern.remove(0, 1);
    if (pattern.isEmpty())
        return rule;

    const bool pathPattern = anchored || pattern.contains(QLatin1Char('/'));
    const QString body = globRegularExpression(pattern);
    QString expression;
    if (pathPattern) {
        expression = QStringLiteral("^") + body + QStringLiteral("($|/)");
        rule.descendantCapture = 1;
    } else {
        expression = QStringLiteral("(^|/)") + body
            + QStringLiteral("($|/)");
        rule.descendantCapture = 2;
    }
    rule.expression = QRegularExpression(expression);
    *ok = rule.expression.isValid();
    return rule;
}

QString pathBelowBase(const QString &relativePath, const QString &baseDir)
{
    if (baseDir.isEmpty())
        return relativePath;
    if (relativePath == baseDir)
        return QString();
    const QString prefix = baseDir + QLatin1Char('/');
    return relativePath.startsWith(prefix)
        ? relativePath.mid(prefix.size()) : QString();
}

} // namespace

bool IgnoreRules::Rule::matches(const QString &path, bool isDirectory) const
{
    const QRegularExpressionMatch match = expression.match(path);
    if (!match.hasMatch())
        return false;
    if (!directoryOnly)
        return true;
    // A slash in the suffix means the matched directory is an ancestor of
    // this path. An exact match is directory-only only when the caller says
    // the entry itself is a directory.
    return match.captured(descendantCapture) == QLatin1String("/")
        || isDirectory;
}

bool IgnoreRules::Snapshot::isExcluded(const QString &relativePath,
                                       bool isDirectory) const
{
    const QString cleaned = cleanRelative(relativePath);
    if (cleaned.isEmpty() || cleaned == QLatin1String("..")
        || cleaned.startsWith(QStringLiteral("../"))) {
        return false;
    }

    bool excluded = false;
    const auto apply = [&](const RuleGroup &group) {
        const QString below = pathBelowBase(cleaned, group.baseDir);
        if (below.isEmpty())
            return;
        for (const Rule &rule : group.rules) {
            if (rule.matches(below, isDirectory))
                excluded = !rule.negated;
        }
    };
    for (const RuleGroup &group : m_groups)
        apply(group);
    // Settings are explicitly additional exclusions and therefore have the
    // final say after every project-owned ignore file, including nested ones.
    apply(m_settings);
    return excluded;
}

IgnoreRules::Snapshot
IgnoreRules::Snapshot::withDirectory(const QString &relativeDir) const
{
    Snapshot result = *this;
    const QString cleaned = cleanRelative(relativeDir);
    if (cleaned.isEmpty())
        return result; // the root .gitignore is already in the snapshot
    const QString path = ignoreFileForDirectory(cleaned);
    const RuleGroup group = IgnoreRules::readRuleFile(path, cleaned);
    if (!group.rules.isEmpty())
        result.m_groups.append(group);
    return result;
}

IgnoreRules::Snapshot
IgnoreRules::Snapshot::throughDirectory(const QString &relativeDir) const
{
    Snapshot result = *this;
    QString accumulated;
    const QStringList parts = cleanRelative(relativeDir).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        accumulated = accumulated.isEmpty()
            ? part : accumulated + QLatin1Char('/') + part;
        result = result.withDirectory(accumulated);
    }
    return result;
}

QString IgnoreRules::Snapshot::ignoreFileForDirectory(
    const QString &relativeDir) const
{
    if (m_rootPath.isEmpty())
        return QString();
    const QString cleaned = cleanRelative(relativeDir);
    return QDir(cleaned.isEmpty() ? m_rootPath
                                  : QDir(m_rootPath).filePath(cleaned))
        .filePath(QStringLiteral(".gitignore"));
}

IgnoreRules::IgnoreRules(QObject *parent)
    : QObject(parent)
{
}

void IgnoreRules::setSettings(SettingsStore *settings)
{
    if (m_settings == settings)
        return;
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &SettingsStore::valueChanged, this,
                [this](const QString &key) {
                    if (key == SettingsKey)
                        loadAdditionalPatterns(true);
                });
        connect(m_settings, &SettingsStore::revisionChanged, this,
                [this]() { loadAdditionalPatterns(true); });
    }
    loadAdditionalPatterns(true);
}

QString IgnoreRules::normalizedRoot(const QString &rootPath)
{
    return rootPath.isEmpty()
        ? QString() : QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
}

void IgnoreRules::setRootPath(const QString &rootPath, bool notify)
{
    const QString normalized = normalizedRoot(rootPath);
    if (m_rootPath == normalized)
        return;
    m_rootPath = normalized;
    loadAdditionalPatterns(false);
    emit rootPathChanged();
    if (notify)
        bump();
}

QStringList IgnoreRules::normalizedPatterns(const QStringList &patterns)
{
    QStringList result;
    for (const QString &pattern : patterns) {
        if (!pattern.trimmed().isEmpty() && !result.contains(pattern))
            result.append(pattern);
    }
    return result;
}

void IgnoreRules::setAdditionalPatterns(const QStringList &patterns)
{
    const QStringList normalized = normalizedPatterns(patterns);
    if (m_additionalPatterns == normalized)
        return;
    m_additionalPatterns = normalized;
    storeAdditionalPatterns();
    bump();
}

bool IgnoreRules::isExcluded(const QString &relativePath,
                             bool isDirectory) const
{
    const QString parent = QFileInfo(cleanRelative(relativePath)).path();
    const Snapshot rules = parent == QLatin1String(".")
        ? snapshot() : snapshot().throughDirectory(parent);
    return rules.isExcluded(relativePath, isDirectory);
}

IgnoreRules::Snapshot IgnoreRules::snapshot() const
{
    Snapshot result;
    result.m_rootPath = m_rootPath;
    result.m_gitInfoExcludePath = gitInfoExcludeForRoot(m_rootPath);
    if (!result.m_gitInfoExcludePath.isEmpty()) {
        const RuleGroup info = readRuleFile(result.m_gitInfoExcludePath,
                                            QString());
        if (!info.rules.isEmpty())
            result.m_groups.append(info);
    }
    const RuleGroup rootIgnore = readRuleFile(
        result.ignoreFileForDirectory(QString()), QString());
    if (!rootIgnore.rules.isEmpty())
        result.m_groups.append(rootIgnore);
    result.m_settings = compilePatterns(m_additionalPatterns, QString());
    return result;
}

bool IgnoreRules::isRulesFile(const QString &absolutePath) const
{
    if (m_rootPath.isEmpty() || absolutePath.isEmpty())
        return false;
    const QString clean = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath());
    const QString infoExclude = gitInfoExcludeForRoot(m_rootPath);
    if (!infoExclude.isEmpty() && clean == QDir::cleanPath(infoExclude))
        return true;
    const QString rootPrefix = m_rootPath + QLatin1Char('/');
    return clean.startsWith(rootPrefix)
        && QFileInfo(clean).fileName() == QLatin1String(".gitignore");
}

void IgnoreRules::reload()
{
    bump();
}

IgnoreRules::RuleGroup IgnoreRules::readRuleFile(const QString &path,
                                                 const QString &baseDir)
{
    if (path.isEmpty())
        return RuleGroup{baseDir, {}};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return RuleGroup{baseDir, {}};
    QStringList lines;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd())
        lines.append(stream.readLine());
    return compilePatterns(lines, baseDir);
}

IgnoreRules::RuleGroup
IgnoreRules::compilePatterns(const QStringList &patterns,
                             const QString &baseDir)
{
    RuleGroup group;
    group.baseDir = cleanRelative(baseDir);
    for (const QString &pattern : patterns) {
        bool ok = false;
        Rule rule = compileRule(pattern, &ok);
        if (ok)
            group.rules.append(std::move(rule));
    }
    return group;
}

QString IgnoreRules::gitInfoExcludeForRoot(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return QString();
    const QString dotGit = QDir(rootPath).filePath(QStringLiteral(".git"));
    QFileInfo info(dotGit);
    QString gitDir;
    if (info.isDir()) {
        gitDir = info.absoluteFilePath();
    } else if (info.isFile()) {
        QFile file(dotGit);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString first = QString::fromUtf8(file.readLine()).trimmed();
            if (first.startsWith(QStringLiteral("gitdir:"),
                                 Qt::CaseInsensitive)) {
                const QString named = first.mid(7).trimmed();
                gitDir = QFileInfo(named.startsWith(QLatin1Char('/'))
                        ? named : QDir(rootPath).filePath(named)).absoluteFilePath();
            }
        }
    }
    return gitDir.isEmpty()
        ? QString() : QDir(gitDir).filePath(QStringLiteral("info/exclude"));
}

void IgnoreRules::loadAdditionalPatterns(bool notify)
{
    QStringList loaded;
    if (m_settings && !m_rootPath.isEmpty()) {
        const QVariantMap roots = m_settings->value(SettingsKey).toMap();
        loaded = normalizedPatterns(roots.value(m_rootPath).toStringList());
    }
    if (loaded == m_additionalPatterns)
        return;
    m_additionalPatterns = loaded;
    if (notify)
        bump();
}

void IgnoreRules::storeAdditionalPatterns()
{
    if (!m_settings || m_rootPath.isEmpty())
        return;
    QVariantMap roots = m_settings->value(SettingsKey).toMap();
    if (m_additionalPatterns.isEmpty())
        roots.remove(m_rootPath);
    else
        roots.insert(m_rootPath, m_additionalPatterns);
    m_settings->setValue(SettingsKey, roots);
}

void IgnoreRules::bump()
{
    ++m_revision;
    emit rulesChanged();
}
