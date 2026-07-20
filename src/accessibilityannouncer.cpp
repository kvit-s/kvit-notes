// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "accessibilityannouncer.h"

#include <QGuiApplication>
#include <QWindow>
#include <QAccessible>

void AccessibilityAnnouncer::announce(const QString &message)
{
    if (message.isEmpty())
        return;
    m_lastMessage = message;
    emit announced(message);

    // Post a real announcement to any attached assistive technology. This is a
    // no-op where the accessibility bridge is not up (e.g. headless or the WSLg
    // session in spike (c)); the signal above is what the gate asserts.
#if QT_CONFIG(accessibility)
    if (QWindow *w = QGuiApplication::focusWindow()) {
        if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(w)) {
            QAccessibleAnnouncementEvent event(iface->object(), message);
            QAccessible::updateAccessibility(&event);
        }
    }
#endif
}

void AccessibilityAnnouncer::announceSaveState(bool dirty)
{
    announce(dirty ? tr("Unsaved changes") : tr("Saved"));
}

void AccessibilityAnnouncer::announceMatchCount(int count)
{
    announce(count == 0 ? tr("No matches")
           : count == 1 ? tr("1 match")
                        : tr("%1 matches").arg(count));
}

void AccessibilityAnnouncer::announceMode(const QString &mode, bool on)
{
    announce(on ? tr("%1 on").arg(mode) : tr("%1 off").arg(mode));
}

void AccessibilityAnnouncer::announceConversion(const QString &typeName)
{
    announce(tr("Converted to %1").arg(typeName));
}
