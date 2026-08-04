// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "menuaccesskeys.h"

// The `&` spelling has exactly two forms: a single `&` marks the character
// after it as the access key, and `&&` is a literal ampersand. Both functions
// below are that one rule read in opposite directions, which is why they are
// written out here rather than taken from QPlatformTheme::removeMnemonics —
// that lives behind Qt's private QPA headers, and the rule is four lines.

QString MenuAccessKeys::label(const QString &markedLabel)
{
    return labelFor(markedLabel, platformShowsAccessKeys());
}

bool MenuAccessKeys::platformShowsAccessKeys()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}

QString MenuAccessKeys::labelFor(const QString &markedLabel, bool showAccessKeys)
{
    if (showAccessKeys)
        return markedLabel;

    QString shown;
    shown.reserve(markedLabel.size());
    for (int i = 0; i < markedLabel.size(); ++i) {
        const QChar c = markedLabel.at(i);
        if (c != u'&') {
            shown.append(c);
            continue;
        }
        // `&&` is an ampersand the label meant to show; a lone `&` is the
        // marker itself and leaves nothing behind.
        if (i + 1 < markedLabel.size() && markedLabel.at(i + 1) == u'&') {
            shown.append(u'&');
            ++i;
        }
    }
    return shown;
}

QString MenuAccessKeys::plain(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'&', QLatin1String("&&"));
    return escaped;
}

QChar MenuAccessKeys::accessKeyOf(const QString &markedLabel)
{
    for (int i = 0; i < markedLabel.size(); ++i) {
        if (markedLabel.at(i) != u'&')
            continue;
        if (i + 1 >= markedLabel.size())
            break;
        const QChar next = markedLabel.at(i + 1);
        if (next == u'&') {
            ++i;   // an escaped ampersand, not a marker
            continue;
        }
        return next.toUpper();
    }
    return QChar();
}
