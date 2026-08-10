// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef IGNORERULES_H
#define IGNORERULES_H

#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

class SettingsStore;

// One root's exclusion policy, shared by every filesystem walk over that
// root. Git's exclude file and each directory's .gitignore supply the project
// rules; SettingsStore supplies an additional, root-specific list for folders
// that are not repositories or need local exclusions beyond git's.
//
// Filesystem walks take a Snapshot. It is an immutable value, so a worker can
// carry the exact same policy down a tree without reading this QObject from a
// background thread. withDirectory() adds that directory's .gitignore to the
// inherited stack; ignored directories are rejected before it is called and
// therefore are never opened merely to discover more rules.
class IgnoreRules : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QStringList additionalPatterns READ additionalPatterns
               WRITE setAdditionalPatterns NOTIFY rulesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY rulesChanged)

public:
    static const QString SettingsKey;

    struct Rule {
        QRegularExpression expression;
        bool negated = false;
        bool directoryOnly = false;
        int descendantCapture = 0;

        bool matches(const QString &path, bool isDirectory) const;
    };

    struct RuleGroup {
        QString baseDir;
        QVector<Rule> rules;
    };

    class Snapshot
    {
    public:
        Snapshot() = default;

        QString rootPath() const { return m_rootPath; }
        bool isExcluded(const QString &relativePath, bool isDirectory) const;

        // Add the .gitignore owned by relativeDir. The caller enters folders
        // one at a time, so the returned value is the policy inherited by
        // that folder's children.
        Snapshot withDirectory(const QString &relativeDir) const;

        // Build the inherited stack for a walk that starts below the root,
        // such as a watcher refresh of one changed directory.
        Snapshot throughDirectory(const QString &relativeDir) const;

        QString ignoreFileForDirectory(const QString &relativeDir) const;
        QString gitInfoExcludePath() const { return m_gitInfoExcludePath; }

    private:
        friend class IgnoreRules;
        QString m_rootPath;
        QString m_gitInfoExcludePath;
        QVector<RuleGroup> m_groups;
        RuleGroup m_settings;
    };

    explicit IgnoreRules(QObject *parent = nullptr);

    void setSettings(SettingsStore *settings);
    SettingsStore *settings() const { return m_settings; }

    // notify=false is for a collection changing roots: the collection applies
    // the new policy to its first scan itself, before rootChanged is emitted.
    void setRootPath(const QString &rootPath, bool notify = true);
    QString rootPath() const { return m_rootPath; }

    QStringList additionalPatterns() const { return m_additionalPatterns; }
    Q_INVOKABLE void setAdditionalPatterns(const QStringList &patterns);
    Q_INVOKABLE bool isExcluded(const QString &relativePath,
                                bool isDirectory = false) const;

    Snapshot snapshot() const;
    bool isRulesFile(const QString &absolutePath) const;
    int revision() const { return m_revision; }

    // A watched .gitignore or .git/info/exclude changed. Snapshots read files
    // afresh, so invalidation is just a revision and a signal to the owners of
    // the scan and watch registrations.
    Q_INVOKABLE void reload();

signals:
    void rootPathChanged();
    void rulesChanged();

private:
    static RuleGroup readRuleFile(const QString &path,
                                  const QString &baseDir);
    static RuleGroup compilePatterns(const QStringList &patterns,
                                     const QString &baseDir);
    static QString gitInfoExcludeForRoot(const QString &rootPath);
    static QString normalizedRoot(const QString &rootPath);
    static QStringList normalizedPatterns(const QStringList &patterns);

    void loadAdditionalPatterns(bool notify);
    void storeAdditionalPatterns();
    void bump();

    SettingsStore *m_settings = nullptr;
    QString m_rootPath;
    QStringList m_additionalPatterns;
    int m_revision = 0;
};

#endif // IGNORERULES_H
