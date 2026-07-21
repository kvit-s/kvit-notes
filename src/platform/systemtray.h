// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QString>

class QSystemTrayIcon;
class QMenu;
class SettingsStore;

// System-tray seam (§15.2). Wraps QSystemTrayIcon behind an interface the app
// drives and the tests fake. Under WSLg there is no status-notifier host, so
// available() is false (spike (b)); the tray icon is not shown and the app
// behaves as today, while the menu-action signals and triggerAction() still let
// the in-app path and the gate exercise the routing. notify() posts a balloon
// message where a tray exists; lastNotification records it for the test.
class SystemTray : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(QString lastNotification READ lastNotification NOTIFY notified)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray
                   NOTIFY closeToTrayChanged)
public:
    explicit SystemTray(QObject *parent = nullptr);
    ~SystemTray() override;

    // Whether a system tray exists on this platform/session.
    bool available() const;
    bool visible() const { return m_visible; }
    QString lastNotification() const { return m_lastNotification; }

    // Closing the last window quits by default; staying resident in the
    // tray is opt-in (tray.closeToTray). Persisted through setSettings.
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool on);
    void setSettings(SettingsStore *settings);

    // Show/hide the tray icon (a no-op where no tray is available).
    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();

    // Post a balloon notification (§15.4). Records lastNotification regardless
    // of whether a tray delivers it, so the seam is testable.
    Q_INVOKABLE void notify(const QString &title, const QString &message);

    // Simulate a menu action firing — the test/in-app seam. name is one of
    // "newNote", "quickCapture", "show", "quit".
    Q_INVOKABLE void triggerAction(const QString &name);

signals:
    void newNoteRequested();
    void quickCaptureRequested();
    void showWindowRequested();
    void quitRequested();
    void visibleChanged();
    void notified(const QString &message);
    void closeToTrayChanged();

private:
    void buildMenu();

    QSystemTrayIcon *m_tray = nullptr;   // null where no tray is available
    QMenu *m_menu = nullptr;
    SettingsStore *m_settings = nullptr;
    bool m_visible = false;
    bool m_closeToTray = false;
    QString m_lastNotification;
};

#endif // SYSTEMTRAY_H
