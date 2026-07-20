// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SHORTCUTCATALOG_H
#define SHORTCUTCATALOG_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantList>
#include <QStringList>

// The §13 keyboard-shortcut catalog: the single source of truth for both the
// discoverable ShortcutReference cheat sheet and the test_shortcutmap audit. Each
// entry names an action, its Windows/Linux chord (empty when the action
// intentionally has no shortcut), where it is wired, and a note. The three
// deviations from the features.md §13 tables are recorded here as intentional,
// with their reason, rather than left as silent gaps.
struct ShortcutInfo {
    QString category;    // "Text Formatting" | "Block Operations" | ...
    QString action;      // "Bold", "Save", …
    QString chord;       // "Ctrl+B", or "" for an intentional no-shortcut action
    QString wiredAt;     // "engine" (block key handler) | "window" | "menu"
    bool intentional;    // true = a documented deviation (no/other shortcut)
    QString note;        // the reason for a deviation, else empty
};

class ShortcutCatalog : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutCatalog(QObject *parent = nullptr) : QObject(parent) {}

    // The full catalog, in the §13.1–§13.4 order.
    static const QList<ShortcutInfo> &entries();

    // Categories, in display order (for the reference's section headers).
    Q_INVOKABLE QStringList categories() const;

    // The catalog as QML-friendly rows: {category, action, chord, note,
    // intentional}. Deviations (empty chord) carry their note so the reference
    // can show "— (reason)" rather than a blank.
    Q_INVOKABLE QVariantList model() const;

    // Lookup helper for the test: the chord for an action, or a sentinel.
    static QString chordFor(const QString &action);
    static bool contains(const QString &action);
};

#endif // SHORTCUTCATALOG_H
