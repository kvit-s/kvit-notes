// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef VAULTSETTINGS_H
#define VAULTSETTINGS_H

#include <QObject>
#include <QString>

#include <functional>

// Settings that belong to one vault rather than to the user:
// <root>/.kvit/settings.json.
//
// Both of them are about how the notes in this folder refer to pictures,
// because that is decided by whatever else reads the folder, not by the
// reader. A Hugo site writes `![](/images/a.png)` and serves it from
// `static/images/a.png`; an Obsidian vault writes `assets/a.png` relative to
// the vault. The same person can keep both.
//
//   siteFolder   The folder, relative to the vault, that a picture path
//                starting with "/" is looked up in. "" is the vault itself.
//   imageFolder  The folder, relative to the vault, that a pasted or dropped
//                picture is saved in. When it lies inside a non-empty
//                siteFolder, the path written into the note starts with "/"
//                and is relative to siteFolder, as the site expects.
//
// A value the file does not have is a default rather than a stored choice.
// siteFolder's default is detected: "static" in a folder that is a Hugo site
// (hugo.toml, or config.toml beside a content/ folder), "" otherwise.
// imageFolder's default follows it: "<siteFolder>/images" when siteFolder is
// set, else "assets", which is where pictures have always gone.
//
// The file is shared and editable by other tools, so what it holds is
// untrusted input: a value that is not a plain relative path inside the vault
// is ignored as though it were absent.
class VaultSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString siteFolder READ siteFolder NOTIFY changed)
    Q_PROPERTY(bool siteFolderIsDefault READ siteFolderIsDefault NOTIFY changed)
    Q_PROPERTY(QString detectedFrom READ detectedFrom NOTIFY changed)
    Q_PROPERTY(QString siteRootPath READ siteRootPath NOTIFY changed)
    Q_PROPERTY(QString imageFolder READ imageFolder NOTIFY changed)
    Q_PROPERTY(bool imageFolderIsDefault READ imageFolderIsDefault NOTIFY changed)

public:
    explicit VaultSettings(QObject *parent = nullptr);

    // Read the vault's file, or return to defaults with an empty root. An
    // absent, unreadable or corrupt file gives defaults and writes nothing.
    void setRoot(const QString &rootPath);

    // Asked before every write; false refuses it. The collection answers
    // from its read-only state, which can change while a root stays open.
    void setWritableCheck(std::function<bool()> check);

    QString siteFolder() const;
    bool siteFolderIsDefault() const { return !m_hasSiteFolder; }
    // The file that made the default "static" ("hugo.toml"), or "".
    QString detectedFrom() const { return m_detectedFrom; }
    // siteFolder as an absolute path; the vault root when siteFolder is "",
    // and "" when no vault is open.
    QString siteRootPath() const;
    QString imageFolder() const;
    bool imageFolderIsDefault() const { return !m_hasImageFolder; }

    // A folder as the reader typed it, cleaned to the form the file stores:
    // backslashes turned round, "./" and trailing slashes dropped. Returns
    // false, leaving `out` alone, for anything that is not "" or a plain
    // relative path inside the vault, or that names the .kvit folder.
    static bool normalizeFolder(const QString &typed, QString *out);
    Q_INVOKABLE bool isValidFolder(const QString &typed) const;

    // Store a choice, or with the reset forms drop it and return to the
    // default. Each writes the file at once. False when the value is not
    // valid, no vault is open, the vault is read-only or the write failed,
    // and nothing changes then. An empty imageFolder is not a folder, so
    // setImageFolder("") resets it.
    Q_INVOKABLE bool setSiteFolder(const QString &typed);
    Q_INVOKABLE bool resetSiteFolder();
    Q_INVOKABLE bool setImageFolder(const QString &typed);
    Q_INVOKABLE bool resetImageFolder();

    // The folder a Hugo site keeps its served files in when `rootPath` is
    // one, else "". `marker` receives the file that identified it.
    static QString detectSiteFolder(const QString &rootPath, QString *marker);

signals:
    void changed();
    // A write that failed, with a message the reader can act on. Wired to
    // the collection's operationFailed.
    void saveFailed(const QString &message);

private:
    bool store(bool hasSite, const QString &site, bool hasImage,
               const QString &image);
    QString filePath() const;

    QString m_rootPath;
    std::function<bool()> m_writable;
    bool m_hasSiteFolder = false;
    QString m_siteFolder;
    bool m_hasImageFolder = false;
    QString m_imageFolder;
    QString m_detectedSiteFolder;
    QString m_detectedFrom;
};

#endif // VAULTSETTINGS_H
