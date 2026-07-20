// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef GLOBALHOTKEY_H
#define GLOBALHOTKEY_H

#include <QObject>
#include <QString>

// Configurable system-wide hotkey seam (§15.1, phase12 decision 6). registering
// a real global shortcut needs a platform mechanism (an X11 XGrabKey or the
// org.freedesktop.portal.GlobalShortcuts portal) that WSLg does not grant
// (spike (b)); this class is the seam. registerShortcut() reports whether the
// platform accepted the grab, activated() fires when the chord is pressed
// system-wide, and trigger() simulates that activation for the in-app path and
// the tests — so quick capture is reachable regardless of whether the OS
// delivers the chord. supported() records the platform verdict for the UI.
class GlobalHotkey : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY shortcutChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    Q_PROPERTY(bool supported READ supported CONSTANT)
public:
    explicit GlobalHotkey(QObject *parent = nullptr);

    QString shortcut() const { return m_shortcut; }
    bool registered() const { return m_registered; }
    // Whether this platform can register a system-wide hotkey at all. False
    // under WSLg (a documented environment limit); the in-app path still works.
    bool supported() const { return m_supported; }

    // Register (or re-register) the chord as a system-wide hotkey. Returns true
    // when the platform accepted it. A no-op success is impossible: on an
    // unsupported platform it stores the chord and returns false.
    Q_INVOKABLE bool registerShortcut(const QString &sequence);
    Q_INVOKABLE void unregister();

    // Simulate a system-wide activation (the in-app trigger and the test seam).
    Q_INVOKABLE void trigger();

    // Test/main seam: let the host declare platform support explicitly (a fake
    // desktop in tests, or main.cpp after probing).
    void setSupported(bool supported) { m_supported = supported; }

signals:
    void activated();
    void shortcutChanged();
    void registeredChanged();

private:
    QString m_shortcut;
    bool m_registered = false;
    bool m_supported = false;
};

#endif // GLOBALHOTKEY_H
