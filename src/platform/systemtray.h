// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QString>

#include <memory>

class QSystemTrayIcon;
class QMenu;
class SettingsStore;
class SystemTrayNotificationBackend;

// System-tray seam (§15.2). Wraps QSystemTrayIcon behind an interface the app
// drives and the tests fake. Under WSLg there is no status-notifier host, so
// available() is false (spike (b)); the tray icon is not shown and the app
// behaves as today, while the menu-action signals and triggerAction() still let
// the in-app path and the gate exercise the routing. notify() posts a native
// message where the platform permits it; the last-notification properties
// record the attempted post for the test and for headless embeddings.
class SystemTray : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(NotificationAuthorization notificationAuthorization
                   READ notificationAuthorization
                   NOTIFY notificationAuthorizationChanged)
    Q_PROPERTY(QString lastNotificationTitle READ lastNotificationTitle
                   NOTIFY notified)
    Q_PROPERTY(QString lastNotification READ lastNotification NOTIFY notified)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray
                   NOTIFY closeToTrayChanged)
public:
    enum NotificationAuthorization {
        Unsupported,
        Unknown,
        Denied,
        Authorized
    };
    Q_ENUM(NotificationAuthorization)

    explicit SystemTray(QObject *parent = nullptr);
    // Backend-injection seam for deterministic lifecycle tests and for hosts
    // that provide notifications through something other than Qt's tray.
    explicit SystemTray(std::unique_ptr<SystemTrayNotificationBackend> backend,
                        QObject *parent = nullptr);
    ~SystemTray() override;

    // Whether a system tray exists on this platform/session.
    bool available() const;
    bool visible() const { return m_visible; }
    NotificationAuthorization notificationAuthorization() const
    {
        return m_notificationAuthorization;
    }
    QString lastNotificationTitle() const { return m_lastNotificationTitle; }
    QString lastNotification() const { return m_lastNotification; }

    // Closing the last window quits by default; staying resident in the
    // tray is opt-in (tray.closeToTray). Persisted through setSettings.
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool on);
    void setSettings(SettingsStore *settings);

    // Show/hide the tray icon (a no-op where no tray is available).
    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();

    // Ask the platform for notification permission. Terminal results are
    // sticky for this instance: repeated calls after authorization or denial
    // never prompt again. Platforms without a prompt start terminal.
    Q_INVOKABLE void requestNotificationAuthorization();

    // Post a native notification (§15.4). The three-argument form associates
    // an opaque id with activation; the two-argument form is retained for
    // existing QML callers. Both record the title and message regardless of
    // whether the platform can deliver them, so the seam remains testable.
    Q_INVOKABLE void notify(const QString &title, const QString &message);
    Q_INVOKABLE void notify(const QString &title, const QString &message,
                            const QString &activationId);

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
    void notificationAuthorizationChanged();
    void notificationActivated(const QString &activationId);
    void closeToTrayChanged();

private:
    void buildMenu();
    void setNotificationAuthorization(NotificationAuthorization authorization);

    QSystemTrayIcon *m_tray = nullptr;   // null where no tray is available
    QMenu *m_menu = nullptr;
    std::unique_ptr<SystemTrayNotificationBackend> m_notificationBackend;
    SettingsStore *m_settings = nullptr;
    bool m_visible = false;
    bool m_closeToTray = false;
    bool m_authorizationRequestInFlight = false;
    NotificationAuthorization m_notificationAuthorization = Unsupported;
    QString m_lastNotificationTitle;
    QString m_lastNotification;
};

// Native-notification half of SystemTray. It is deliberately narrower than
// the public object: menu and tray-icon activation stay owned by SystemTray,
// while a backend reports permission, posts a title/body/id tuple, and emits
// the id belonging to an activated notification. Platform APIs do not escape
// this boundary into QML or extensions.
class SystemTrayNotificationBackend : public QObject
{
    Q_OBJECT
public:
    explicit SystemTrayNotificationBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~SystemTrayNotificationBackend() override = default;

    virtual SystemTray::NotificationAuthorization authorization() const = 0;
    virtual void requestAuthorization() = 0;
    virtual void post(const QString &title, const QString &message,
                      const QString &activationId) = 0;

signals:
    void authorizationChanged(SystemTray::NotificationAuthorization authorization);
    void activated(const QString &activationId);
};

#endif // SYSTEMTRAY_H
