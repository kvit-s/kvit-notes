// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TODOMETA_H
#define TODOMETA_H

#include <QObject>
#include <QString>
#include <QVariantMap>

// Todo due date and priority metadata (features.md §1.2.3). Stored
// inline in the todo's content as trailing Obsidian
// Tasks tokens — 📅 <date> for the due date; ⏫ / 🔼 / 🔽 for high / medium /
// low priority — the largest existing ecosystem for todo metadata in
// markdown. The engine treats the tail as a non-editable chrome segment
// (rendered as chips); this component splits content into the editable text
// and its metadata, and rebuilds it. displayText() is what word counts and
// search run over, so the emoji tail never leaks into either.
namespace TodoMeta {

enum Priority { None = 0, Low = -1, Medium = 1, High = 2 };

struct Meta {
    QString text;          // the todo body, metadata stripped
    QString due;           // ISO "yyyy-MM-dd", or ""
    int priority = None;
    QString tail;          // the raw metadata suffix (with its leading space)
};

Meta parse(const QString &content);
QString build(const QString &text, const QString &due, int priority);
// The body without its metadata tail (for counts/search exclusion).
QString displayText(const QString &content);

} // namespace TodoMeta

// QML context object (todoMeta).
class TodoMetaTools : public QObject
{
    Q_OBJECT
public:
    explicit TodoMetaTools(QObject *parent = nullptr) : QObject(parent) {}

    // {text, due, priority, tail}.
    Q_INVOKABLE QVariantMap parse(const QString &content) const;
    Q_INVOKABLE QString build(const QString &text, const QString &due,
                              int priority) const
    { return TodoMeta::build(text, due, priority); }
    Q_INVOKABLE QString tail(const QString &content) const
    { return TodoMeta::parse(content).tail; }
    Q_INVOKABLE QString displayText(const QString &content) const
    { return TodoMeta::displayText(content); }
};

#endif // TODOMETA_H
