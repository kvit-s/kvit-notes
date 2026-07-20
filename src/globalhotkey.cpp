// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "globalhotkey.h"

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
}

bool GlobalHotkey::registerShortcut(const QString &sequence)
{
    if (m_shortcut != sequence) {
        m_shortcut = sequence;
        emit shortcutChanged();
    }
    // A real system-wide grab requires a platform backend (X11 XGrabKey or the
    // GlobalShortcuts portal). Where the platform does not support it — WSLg,
    // spike (b) — registration fails cleanly and the chord is reachable only
    // through the in-app path (the quick-capture window and tray menu).
    const bool ok = m_supported && !sequence.isEmpty();
    if (m_registered != ok) {
        m_registered = ok;
        emit registeredChanged();
    }
    return ok;
}

void GlobalHotkey::unregister()
{
    if (m_registered) {
        m_registered = false;
        emit registeredChanged();
    }
}

void GlobalHotkey::trigger()
{
    emit activated();
}
