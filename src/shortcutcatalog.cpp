// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "shortcutcatalog.h"

#include <QVariantMap>

const QList<ShortcutInfo> &ShortcutCatalog::entries()
{
    // Chords are the Windows/Linux column of features.md §13; the app wires the
    // block-scoped ones in EditableBlock's key handler ("engine") and the
    // window-scoped ones as Shortcut elements in main.qml ("window").
    static const QList<ShortcutInfo> kEntries = {
        // §13.1 Text Formatting
        {"Text Formatting", "Bold",              "Ctrl+B",       "engine", false, {}},
        {"Text Formatting", "Italic",            "Ctrl+I",       "engine", false, {}},
        {"Text Formatting", "Underline",         "Ctrl+U",       "engine", false, {}},
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
        {"General", "Save",              "Ctrl+S",       "window", false, {}},
        {"General", "Save As",           "",             "menu",   true,
         "StandardKey.SaveAs resolves to Ctrl+Shift+S, which §13.1 assigns to "
         "strikethrough; Save As yields and is reached from the File menu."},
        {"General", "Undo",              "Ctrl+Z",       "window", false, {}},
        {"General", "Redo",              "Ctrl+Y",       "window", false, {}},
        {"General", "Find",              "Ctrl+F",       "window", false, {}},
        {"General", "Find & Replace",    "Ctrl+H",       "window", false, {}},
        {"General", "Select All",        "Ctrl+A",       "engine", false, {}},
        {"General", "New Note",          "Ctrl+N",       "window", false, {}},
        {"General", "Toggle Sidebar",    "Ctrl+\\",      "window", false, {}},
        {"General", "Distraction-free",  "F11",          "window", false, {}},
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

QVariantList ShortcutCatalog::model() const
{
    QVariantList rows;
    for (const ShortcutInfo &e : entries()) {
        QVariantMap m;
        m.insert("category", e.category);
        m.insert("action", e.action);
        m.insert("chord", e.chord);
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
