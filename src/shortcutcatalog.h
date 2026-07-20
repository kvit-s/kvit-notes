// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SHORTCUTCATALOG_H
#define SHORTCUTCATALOG_H

#include <QKeySequence>
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
    // Set where the behavior is genuinely driven by a Qt standard key — the
    // Shortcut elements in main.qml that use `sequences: [StandardKey.X]`, and
    // the editor's Ctrl checks that Qt's macOS Ctrl/Meta swap makes equivalent
    // to one. The chord above stays the §13 spelling and remains this entry's
    // identity; the standard key is what lets the reference show the chord the
    // running platform actually binds. Entries deliberately NOT driven by a
    // standard key (Find & Replace, Back, Forward, F11) leave this unset, so
    // what is displayed never claims more than what is wired.
    QKeySequence::StandardKey standardKey = QKeySequence::UnknownKey;
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

    // A chord rendered the way the running platform spells it: Command and
    // Option glyphs on macOS, "Ctrl+B" on Windows and Linux. The stored chord
    // is the portable form and stays the catalog's identity; this is only for
    // display, so the cheat sheet cannot disagree with the keys that work.
    Q_INVOKABLE static QString displayChord(const QString &chord);
    // The same, for an entry that a Qt standard key drives.
    static QString displayChord(QKeySequence::StandardKey standardKey,
                                const QString &chord);

    // Lookup helper for the test: the chord for an action, or a sentinel.
    static QString chordFor(const QString &action);
    static bool contains(const QString &action);
};

#endif // SHORTCUTCATALOG_H
