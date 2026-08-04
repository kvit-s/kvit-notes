// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MENUACCESSKEYS_H
#define MENUACCESSKEYS_H

#include <QObject>
#include <QString>

// How a menu label is written, and what the running platform does with it.
//
// Windows and Linux menus give each command an access key: one letter of the
// label, drawn underlined, that opens the menu or runs the command when it is
// typed. The label carries that key as an `&` before the letter, so "&Copy"
// means the underlined C, and "&&" is how a label writes a literal ampersand.
//
// Qt Quick already understands the spelling. A menu label is rendered through
// QQuickMnemonicLabel, which draws the underline, and QQuickAbstractButton
// registers Alt+<letter> from the same text, active only while the item is
// shown. So neither the drawing nor the key binding needs code here; what
// needs code is the two things the spelling gets wrong on its own.
//
// The first is macOS, which has no access keys at all: its menus show no
// underlines, and Alt is the Option key, which types characters rather than
// reaching menus. label() removes the markers there, so one set of QML sources
// serves all three platforms.
//
// The second is text nobody wrote as a label — a folder name, a vault path, a
// template's file name. An `&` in one of those is part of the name and must
// not be read as a marker, or a folder called "R&D" loses its ampersand and
// shows an underlined D. plain() escapes it.
//
// QML reaches both as the `MenuText` singleton:
//
//     MenuItem { text: MenuText.label(qsTr("&Copy")) }
//     MenuItem { text: MenuText.plain(folderName) }
class MenuAccessKeys : public QObject
{
    Q_OBJECT
public:
    explicit MenuAccessKeys(QObject *parent = nullptr) : QObject(parent) {}

    // A hand-written label, as this platform should show it: unchanged where
    // access keys are a convention, with the markers taken out where they are
    // not.
    Q_INVOKABLE static QString label(const QString &markedLabel);

    // Arbitrary text used as a menu label, escaped so that nothing in it can
    // be read as an access-key marker. Independent of the platform: the
    // escaped form means the same thing to Qt Quick's own menus and to the
    // native menus macOS builds from them.
    Q_INVOKABLE static QString plain(const QString &text);

    // Whether this platform shows access keys at all.
    static bool platformShowsAccessKeys();

    // What label() does, for a platform the caller names rather than the one
    // the binary was built for. This is the whole rule; label() is it applied
    // to the running platform. Both platforms are therefore testable from
    // whichever one the suite happens to run on, which matters because the
    // macOS half of the rule is the half no suite here can reach.
    static QString labelFor(const QString &markedLabel, bool showAccessKeys);

    // The access key `markedLabel` declares, or a null QChar when it declares
    // none. Used by the guard test that no two commands in one menu claim the
    // same letter.
    static QChar accessKeyOf(const QString &markedLabel);
};

#endif // MENUACCESSKEYS_H
