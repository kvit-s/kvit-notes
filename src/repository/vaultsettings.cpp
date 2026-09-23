// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "vaultsettings.h"

#include "notefileio.h"
#include "vaultpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
const QString kvitDirName = QStringLiteral(".kvit");
const QString settingsFileName = QStringLiteral("settings.json");
const QString siteFolderKey = QStringLiteral("siteFolder");
const QString imageFolderKey = QStringLiteral("imageFolder");
// Far larger than two folder names; a file this size is not ours.
constexpr qint64 maxFileBytes = 64 * 1024;
}

VaultSettings::VaultSettings(QObject *parent) : QObject(parent) {}

void VaultSettings::setWritableCheck(std::function<bool()> check)
{
    m_writable = std::move(check);
}

void VaultSettings::setRoot(const QString &rootPath)
{
    m_rootPath = rootPath;
    m_hasSiteFolder = false;
    m_siteFolder.clear();
    m_hasImageFolder = false;
    m_imageFolder.clear();
    m_detectedFrom.clear();
    m_detectedSiteFolder.clear();

    if (!rootPath.isEmpty()) {
        m_detectedSiteFolder = detectSiteFolder(rootPath, &m_detectedFrom);

        bool ok = false;
        const QByteArray bytes =
            NoteFileIo::readFileBytes(filePath(), &ok, maxFileBytes);
        QJsonParseError error;
        const QJsonDocument doc = ok ? QJsonDocument::fromJson(bytes, &error)
                                     : QJsonDocument();
        const QJsonObject obj = doc.object();
        QString value;
        if (obj.value(siteFolderKey).isString()
            && normalizeFolder(obj.value(siteFolderKey).toString(), &value)) {
            m_hasSiteFolder = true;
            m_siteFolder = value;
        }
        if (obj.value(imageFolderKey).isString()
            && normalizeFolder(obj.value(imageFolderKey).toString(), &value)
            && !value.isEmpty()) {
            m_hasImageFolder = true;
            m_imageFolder = value;
        }
    }
    emit changed();
}

QString VaultSettings::siteFolder() const
{
    return m_hasSiteFolder ? m_siteFolder : m_detectedSiteFolder;
}

QString VaultSettings::siteRootPath() const
{
    if (m_rootPath.isEmpty())
        return QString();
    const QString folder = siteFolder();
    return folder.isEmpty() ? QDir::cleanPath(m_rootPath)
                            : QDir::cleanPath(m_rootPath) + QLatin1Char('/') + folder;
}

QString VaultSettings::imageFolder() const
{
    if (m_hasImageFolder)
        return m_imageFolder;
    const QString site = siteFolder();
    return site.isEmpty() ? QStringLiteral("assets")
                          : site + QStringLiteral("/images");
}

bool VaultSettings::normalizeFolder(const QString &typed, QString *out)
{
    QString value = typed.trimmed();
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (value.startsWith(QLatin1String("./")))
        value.remove(0, 2);
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    if (value == QLatin1String("."))
        value.clear();
    if (!value.isEmpty()) {
        if (!VaultPaths::isPlainRelativePath(value))
            return false;
        // The repository's own folder is never a place for pictures.
        if (value.section(QLatin1Char('/'), 0, 0) == kvitDirName)
            return false;
    }
    if (out)
        *out = value;
    return true;
}

bool VaultSettings::isValidFolder(const QString &typed) const
{
    return normalizeFolder(typed, nullptr);
}

bool VaultSettings::setSiteFolder(const QString &typed)
{
    QString value;
    if (!normalizeFolder(typed, &value))
        return false;
    return store(true, value, m_hasImageFolder, m_imageFolder);
}

bool VaultSettings::resetSiteFolder()
{
    return store(false, QString(), m_hasImageFolder, m_imageFolder);
}

bool VaultSettings::setImageFolder(const QString &typed)
{
    QString value;
    if (!normalizeFolder(typed, &value))
        return false;
    if (value.isEmpty())
        return resetImageFolder();
    return store(m_hasSiteFolder, m_siteFolder, true, value);
}

bool VaultSettings::resetImageFolder()
{
    return store(m_hasSiteFolder, m_siteFolder, false, QString());
}

bool VaultSettings::store(bool hasSite, const QString &site, bool hasImage,
                          const QString &image)
{
    if (m_rootPath.isEmpty() || (m_writable && !m_writable()))
        return false;
    if (hasSite == m_hasSiteFolder && site == m_siteFolder
        && hasImage == m_hasImageFolder && image == m_imageFolder)
        return true;

    QJsonObject obj;
    if (hasSite)
        obj.insert(siteFolderKey, site);
    if (hasImage)
        obj.insert(imageFolderKey, image);
    const QString dir = VaultPaths::ensureOwnedDir(m_rootPath, kvitDirName);
    const QString path = filePath();
    if (dir.isEmpty() || path.isEmpty()
        || !NoteFileIo::writeFileBytesAtomic(
               path, QJsonDocument(obj).toJson(QJsonDocument::Indented))) {
        emit saveFailed(tr("Could not save this vault's settings to %1.")
                            .arg(QDir::toNativeSeparators(
                                path.isEmpty() ? m_rootPath : path)));
        return false;
    }

    m_hasSiteFolder = hasSite;
    m_siteFolder = site;
    m_hasImageFolder = hasImage;
    m_imageFolder = image;
    emit changed();
    return true;
}

QString VaultSettings::filePath() const
{
    return VaultPaths::ownedFile(m_rootPath, kvitDirName, settingsFileName);
}

QString VaultSettings::detectSiteFolder(const QString &rootPath, QString *marker)
{
    const QDir root(rootPath);
    // hugo.* has named a Hugo site since Hugo 0.110. The older config.* name
    // is too common to trust alone, so it also needs Hugo's content folder.
    static const char *const hugoFiles[] = {"hugo.toml", "hugo.yaml",
                                            "hugo.yml", "hugo.json"};
    static const char *const configFiles[] = {"config.toml", "config.yaml",
                                              "config.yml", "config.json"};
    for (const char *name : hugoFiles) {
        if (QFileInfo(root.filePath(QLatin1String(name))).isFile()) {
            if (marker)
                *marker = QLatin1String(name);
            return QStringLiteral("static");
        }
    }
    if (QFileInfo(root.filePath(QStringLiteral("content"))).isDir()) {
        for (const char *name : configFiles) {
            if (QFileInfo(root.filePath(QLatin1String(name))).isFile()) {
                if (marker)
                    *marker = QLatin1String(name);
                return QStringLiteral("static");
            }
        }
    }
    if (marker)
        marker->clear();
    return QString();
}
