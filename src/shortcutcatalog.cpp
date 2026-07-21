// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "shortcutcatalog.h"

#include <QKeySequence>
#include <QVariantMap>

namespace {
#ifdef Q_OS_MACOS
constexpr auto kFindReplaceChord = "Meta+Alt+F";
constexpr auto kDistractionFreeChord = "Meta+Ctrl+F";
#else
constexpr auto kFindReplaceChord = "Ctrl+H";
constexpr auto kDistractionFreeChord = "F11";
#endif
}

const QList<ShortcutInfo> &ShortcutCatalog::entries()
{
    // Chords are the Windows/Linux column of features.md §13; the app wires the
    // block-scoped ones in EditableBlock's key handler ("engine") and the
    // window-scoped ones as Shortcut elements in main.qml ("window").
    static const QList<ShortcutInfo> kEntries = {
        // §13.1 Text Formatting
        {"Text Formatting", "Bold",              "Ctrl+B",       "engine", false, {},
         QKeySequence::Bold},
        {"Text Formatting", "Italic",            "Ctrl+I",       "engine", false, {},
         QKeySequence::Italic},
        {"Text Formatting", "Underline",         "Ctrl+U",       "engine", false, {},
         QKeySequence::Underline},
        {"Text Formatting", "Strikethrough",     "Ctrl+Shift+S", "engine", false, {}},
        {"Text Formatting", "Inline Code",       "Ctrl+E",       "engine", false, {}},
        {"Text Formatting", "Link",              "Ctrl+K",       "engine", false, {}},
        {"Text Formatting", "Superscript",       "",             "menu",   true,
         "No shortcut in the §13.1 table; reached from the toolbar, formatting "
         "bar, and text context menu."},
        {"Text Formatting", "Subscript",         "",             "menu",   true,
         "No shortcut in the §13.1 table; reached from the toolbar, formatting "
         "bar, and text context menu."},

        // §13.2 Block Operations
        {"Block Operations", "Move block up",    "Alt+Up",       "engine", false, {}},
        {"Block Operations", "Move block down",  "Alt+Down",     "engine", false, {}},
        {"Block Operations", "Duplicate block",  "Ctrl+D",       "engine", false, {}},
        {"Block Operations", "Delete block",     "Ctrl+Shift+D", "engine", false, {}},
        {"Block Operations", "Indent",           "Tab",          "engine", false, {}},
        {"Block Operations", "Outdent",          "Shift+Tab",    "engine", false, {}},

        // §13.3 Block Conversion
        {"Block Conversion", "Paragraph",        "Ctrl+0",       "engine", false, {}},
        {"Block Conversion", "Heading 1",        "Ctrl+1",       "engine", false, {}},
        {"Block Conversion", "Heading 2",        "Ctrl+2",       "engine", false, {}},
        {"Block Conversion", "Heading 3",        "Ctrl+3",       "engine", false, {}},
        {"Block Conversion", "Heading 4",        "",             "menu",   true,
         "The §13.3 conversion table stops at Ctrl+3; Heading 4 is reached from "
         "the block menu, the turn-into menus, and the toolbar."},
        {"Block Conversion", "Todo",             "Ctrl+T",       "engine", false, {}},
        {"Block Conversion", "Quote",            "Ctrl+Shift+T", "engine", false, {}},

        // Math entry: wired in the math editors' key
        // handlers -- the MathBlock source editor and the inline $...$ span
        // path in EditableBlock. "\\" opens the command menu (browse, then
        // completion as letters follow); "$" auto-pairs in prose.
        {"Math Editing", "Math command menu",      "\\",         "engine", false, {}},
        {"Math Editing", "Complete math command",  "Ctrl+Space", "engine", false, {}},
        {"Math Editing", "Next equation slot",     "Tab",        "engine", false, {}},
        {"Math Editing", "Previous equation slot", "Shift+Tab",  "engine", false, {}},
        {"Math Editing", "Inline math pair",       "$",          "engine", false, {}},

        // §13.4 General
        {"General", "Save",              "Ctrl+S",       "window", false, {},
         QKeySequence::Save},
        {"General", "Save As",           "",             "menu",   true,
         "StandardKey.SaveAs resolves to Ctrl+Shift+S, which §13.1 assigns to "
         "strikethrough; Save As yields and is reached from the File menu."},
        {"General", "Undo",              "Ctrl+Z",       "window", false, {},
         QKeySequence::Undo},
        {"General", "Redo",              "Ctrl+Y",       "window", false, {},
         QKeySequence::Redo},
        {"General", "Find",              "Ctrl+F",       "window", false, {},
         QKeySequence::Find},
        {"General", "Find & Replace",    kFindReplaceChord, "window", false, {}},
        {"General", "Select All",        "Ctrl+A",       "engine", false, {},
         QKeySequence::SelectAll},
        {"General", "New Note",          "Ctrl+N",       "window", false, {},
         QKeySequence::New},
        {"General", "Toggle Sidebar",    "Ctrl+\\",      "window", false, {}},
        {"General", "Distraction-free",  kDistractionFreeChord,
         "window", false, {}},
        // Wiki-link navigation. Ctrl+P rather
        // than Obsidian's Ctrl+O, which §13.4-adjacent behavior already
        // assigns to Open File.
        {"General", "Quick Switcher",    "Ctrl+P",       "window", false, {}},
        {"General", "Back",              "Alt+Left",     "window", false, {}},
        {"General", "Forward",           "Alt+Right",    "window", false, {}},
        {"General", "Backlinks",         "Ctrl+Shift+B", "window", false, {}},
    };
    return kEntries;
}

QStringList ShortcutCatalog::categories() const
{
    QStringList cats;
    for (const ShortcutInfo &e : entries()) {
        if (!cats.contains(e.category))
            cats.append(e.category);
    }
    return cats;
}

QString ShortcutCatalog::displayChord(QKeySequence::StandardKey standardKey,
                                     const QString &chord)
{
    // A standard key usually binds SEVERAL sequences, and main.qml's
    // `sequences: [StandardKey.X]` arms all of them — Redo answers to both
    // Ctrl+Y and Ctrl+Shift+Z on Linux. Which one Qt lists first depends on
    // the platform theme and is not stable between processes, so picking the
    // first would make the reference show a different chord than the one the
    // §13 table promises, at random.
    //
    // Prefer the documented chord whenever the platform really does bind it,
    // and fall back to Qt's own answer when it does not — which is what
    // happens on macOS, where Redo is Command-Shift-Z and the Windows/Linux
    // Ctrl+Y is simply not among the bindings.
    if (standardKey != QKeySequence::UnknownKey) {
        const QList<QKeySequence> bound = QKeySequence::keyBindings(standardKey);
        const QKeySequence documented =
            QKeySequence::fromString(chord, QKeySequence::PortableText);
        if (!documented.isEmpty() && bound.contains(documented))
            return documented.toString(QKeySequence::NativeText);
        if (!bound.isEmpty())
            return bound.first().toString(QKeySequence::NativeText);
    }
    return displayChord(chord);
}

QString ShortcutCatalog::displayChord(const QString &chord)
{
    // Qt binds "Ctrl+B" to Command-B on macOS (Ctrl and Meta are swapped
    // unless AA_MacDontSwapCtrlAndMeta is set, and this app does not set it),
    // so the chords above are already correct there. What was wrong was
    // showing them: the reference printed the stored "Ctrl+B" while the key
    // that worked was Command-B. NativeText resolves each chord the same way
    // the binding does, giving the platform's own glyphs on macOS and leaving
    // the Windows/Linux spelling untouched.
    if (chord.isEmpty())
        return QString();
    const QKeySequence seq = QKeySequence::fromString(chord, QKeySequence::PortableText);
    if (seq.isEmpty())
        return chord;   // a literal trigger like "\\" or "$", not a chord
    return seq.toString(QKeySequence::NativeText);
}

QVariantList ShortcutCatalog::model() const
{
    QVariantList rows;
    for (const ShortcutInfo &e : entries()) {
        QVariantMap m;
        m.insert("category", e.category);
        m.insert("action", e.action);
        // The portable chord stays the catalog's identity (the audit compares
        // against it); displayChord is what a human should be shown.
        m.insert("chord", e.chord);
        m.insert("displayChord", displayChord(e.standardKey, e.chord));
        m.insert("note", e.note);
        m.insert("intentional", e.intentional);
        rows.append(m);
    }
    return rows;
}

QString ShortcutCatalog::chordFor(const QString &action)
{
    for (const ShortcutInfo &e : entries()) {
        if (e.action == action)
            return e.chord;
    }
    return QStringLiteral("\x01<absent>");  // a sentinel no real chord equals
}

bool ShortcutCatalog::contains(const QString &action)
{
    for (const ShortcutInfo &e : entries()) {
        if (e.action == action)
            return true;
    }
    return false;
}
