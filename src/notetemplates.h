// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTETEMPLATES_H
#define NOTETEMPLATES_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVariantMap>
#include <functional>

class NoteCollection;

// Note templates (features.md §18; phase11 decision 6): markdown files under a
// collection-level `.kvit/templates/` directory — files-as-truth, editable by
// hand — that seed new notes. Three built-ins (Meeting Notes, Project Plan,
// Daily Journal) are written on first use. Exposed as the `noteTemplates`
// context property.
//
// A template is just a note, so its front-matter (tags, favorite) carries into
// the note created from it. Instantiation expands {{date}}, {{time}},
// {{title}}, and {{date:FORMAT}} through a pure expander with an injected clock
// (mirroring the backup clock), so expansion is hermetic in tests.
class NoteTemplates : public QObject
{
    Q_OBJECT
    // Bumped when the template set changes (write/delete/seed), so the
    // management dialog's list re-reads.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit NoteTemplates(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);

    int revision() const { return m_revision; }

    // <root>/.kvit/templates, or empty when no collection is open. Created on
    // demand by the write/seed paths, not by this getter.
    Q_INVOKABLE QString templatesDir() const;

    // Template base names (without ".md"), sorted case-insensitively.
    Q_INVOKABLE QStringList templateNames() const;
    Q_INVOKABLE QString readTemplate(const QString &name) const;
    // Write (creating the directory as needed); an empty/invalid name fails.
    Q_INVOKABLE bool writeTemplate(const QString &name, const QString &content);
    Q_INVOKABLE bool deleteTemplate(const QString &name);

    // Seed the three built-ins if the directory holds no templates yet.
    Q_INVOKABLE void seedBuiltinsIfEmpty();

    // Read template `name`, expand its variables against `title` and the
    // clock, split off its front-matter, and return
    // {"body", "tags"(QStringList), "favorite"(bool)} for the create flow to
    // apply. An unknown template returns an empty map.
    Q_INVOKABLE QVariantMap instantiate(const QString &name,
                                        const QString &title) const;

    // Expand with the current (or injected) clock — for previews/tests.
    Q_INVOKABLE QString expandNow(const QString &content,
                                  const QString &title) const;

    // The pure expander (unit-tested without a GUI): replaces {{date}},
    // {{time}}, {{title}}, {{date:FORMAT}}. An unknown {{token}} is left
    // verbatim so a stray brace pair never silently vanishes.
    static QString expand(const QString &content, const QString &title,
                          const QDateTime &now);

    // Injected clock, mirroring NoteCollection's backup clock.
    void setClockForTesting(std::function<QDateTime()> clock);

signals:
    void revisionChanged();

private:
    QDateTime now() const;
    void bump();
    QString pathFor(const QString &name) const;

    NoteCollection *m_collection = nullptr;
    std::function<QDateTime()> m_clock; // null = QDateTime::currentDateTime
    int m_revision = 0;
};

#endif // NOTETEMPLATES_H
