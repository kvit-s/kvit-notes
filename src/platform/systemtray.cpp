// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "systemtray.h"
#include "settingsstore.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QApplication>
#include <QIcon>
#include <QPointer>
#include <QTimer>

#include <utility>

namespace {
const QString kSettingsCloseToTray = QStringLiteral("tray.closeToTray");

#ifndef Q_OS_MACOS
// Windows and freedesktop notification hosts do not put an authorization
// prompt in front of QSystemTrayIcon. Their effective answer is available
// synchronously: a live tray that supports messages is authorized, and every
// other session is unsupported.
class QtTrayNotificationBackend final : public SystemTrayNotificationBackend
{
public:
    explicit QtTrayNotificationBackend(QSystemTrayIcon *tray)
        : m_tray(tray)
        , m_authorization(tray && QSystemTrayIcon::supportsMessages()
                              ? SystemTray::Authorized
                              : SystemTray::Unsupported)
    {
        if (!m_tray)
            return;

        // QSystemTrayIcon has no id in messageClicked(), and each call to
        // showMessage replaces the tray's current balloon. Preserve the id
        // paired with that current message and emit it when Qt reports the
        // click.
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
            const QString activationId = m_currentActivationId;
            // On Windows Qt also reports messageClicked when the tray icon
            // itself is clicked while a balloon is visible. Defer briefly so
            // the accompanying Trigger can suppress that false activation.
            QTimer::singleShot(50, this, [this, activationId]() {
                if (!m_trayIconTriggered)
                    emit activated(activationId);
            });
        });
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason != QSystemTrayIcon::Trigger)
                        return;
                    m_trayIconTriggered = true;
                    QTimer::singleShot(75, this, [this]() {
                        m_trayIconTriggered = false;
                    });
                });
    }

    SystemTray::NotificationAuthorization authorization() const override
    {
        return m_authorization;
    }

    void requestAuthorization() override
    {
        // There is no prompt on these hosts; the constructor reported the
        // effective terminal state directly.
    }

    void post(const QString &title, const QString &message,
              const QString &activationId) override
    {
        if (m_authorization != SystemTray::Authorized || !m_tray
            || !m_tray->isVisible()) {
            return;
        }
        m_currentActivationId = activationId;
        m_tray->showMessage(title, message, QSystemTrayIcon::Information, 4000);
    }

private:
    QPointer<QSystemTrayIcon> m_tray;
    SystemTray::NotificationAuthorization m_authorization;
    QString m_currentActivationId;
    bool m_trayIconTriggered = false;
};
#endif
} // namespace

#ifdef Q_OS_MACOS
// Implemented with UNUserNotificationCenter in systemtray_mac.mm. macOS is
// the one supported desktop where authorization is asynchronous and may show
// a prompt, and native requests retain the activation id per notification.
std::unique_ptr<SystemTrayNotificationBackend>
createMacSystemTrayNotificationBackend(QSystemTrayIcon *tray);
#endif

SystemTray::SystemTray(QObject *parent)
    : SystemTray(std::unique_ptr<SystemTrayNotificationBackend>(), parent)
{
}

SystemTray::SystemTray(std::unique_ptr<SystemTrayNotificationBackend> backend,
                       QObject *parent)
    : QObject(parent)
{
    // Build the real tray icon only where the platform provides one; otherwise
    // the seam still routes actions through its signals (documented WSLg gap).
    //
    // A tray being available is not enough: the menu below is a QWidget, and a
    // QWidget cannot be constructed unless the application object is a
    // QApplication. Windows reports a tray as available even to a process
    // running under QCoreApplication or QGuiApplication - every test binary
    // that builds an AppContext, and any headless embedding of the core - and
    // there the menu is a fatal error rather than a degraded tray. Linux never
    // showed it because the WSL and offscreen sessions report no tray at all.
    if (QSystemTrayIcon::isSystemTrayAvailable()
        && qobject_cast<QApplication *>(qApp) != nullptr) {
        m_tray = new QSystemTrayIcon(this);
        m_tray->setIcon(QIcon::fromTheme(QStringLiteral("accessories-text-editor"),
                                         QApplication::windowIcon()));
        m_tray->setToolTip(QStringLiteral("Kvit Notes"));
        buildMenu();
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger)
                        emit showWindowRequested();
                });
    }

    if (backend) {
        m_notificationBackend = std::move(backend);
    } else {
#ifdef Q_OS_MACOS
        m_notificationBackend = createMacSystemTrayNotificationBackend(m_tray);
#else
        m_notificationBackend =
            std::make_unique<QtTrayNotificationBackend>(m_tray);
#endif
    }

    m_notificationAuthorization = m_notificationBackend->authorization();
    connect(m_notificationBackend.get(),
            &SystemTrayNotificationBackend::authorizationChanged,
            this, [this](NotificationAuthorization authorization) {
                m_authorizationRequestInFlight = false;
                setNotificationAuthorization(authorization);
            });
    connect(m_notificationBackend.get(),
            &SystemTrayNotificationBackend::activated,
            this, &SystemTray::notificationActivated);
}

SystemTray::~SystemTray() = default;

void SystemTray::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    const bool on =
        m_settings->value(kSettingsCloseToTray, false).toBool();
    if (on != m_closeToTray) {
        m_closeToTray = on;
        emit closeToTrayChanged();
    }
}

void SystemTray::setCloseToTray(bool on)
{
    if (on == m_closeToTray)
        return;
    m_closeToTray = on;
    if (m_settings)
        m_settings->setValue(kSettingsCloseToTray, on);
    emit closeToTrayChanged();
}

void SystemTray::buildMenu()
{
    m_menu = new QMenu();
    QAction *newNote = m_menu->addAction(tr("New Note"));
    connect(newNote, &QAction::triggered, this, &SystemTray::newNoteRequested);
    QAction *capture = m_menu->addAction(tr("Quick Capture…"));
    connect(capture, &QAction::triggered, this, &SystemTray::quickCaptureRequested);
    m_menu->addSeparator();
    QAction *show = m_menu->addAction(tr("Show Kvit"));
    connect(show, &QAction::triggered, this, &SystemTray::showWindowRequested);
    m_menu->addSeparator();
    QAction *quit = m_menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &SystemTray::quitRequested);
    if (m_tray)
        m_tray->setContextMenu(m_menu);
}

bool SystemTray::available() const
{
    return m_tray != nullptr;
}

void SystemTray::show()
{
    if (m_tray)
        m_tray->show();
    if (!m_visible) {
        m_visible = true;
        emit visibleChanged();
    }
}

void SystemTray::hide()
{
    if (m_tray)
        m_tray->hide();
    if (m_visible) {
        m_visible = false;
        emit visibleChanged();
    }
}

void SystemTray::requestNotificationAuthorization()
{
    if (m_notificationAuthorization != Unknown
        || m_authorizationRequestInFlight) {
        return;
    }
    m_authorizationRequestInFlight = true;
    m_notificationBackend->requestAuthorization();
}

void SystemTray::notify(const QString &title, const QString &message)
{
    notify(title, message, QString());
}

void SystemTray::notify(const QString &title, const QString &message,
                        const QString &activationId)
{
    m_lastNotificationTitle = title;
    m_lastNotification = message;
    if (m_notificationAuthorization == Authorized)
        m_notificationBackend->post(title, message, activationId);
    emit notified(message);
}

void SystemTray::setNotificationAuthorization(
    NotificationAuthorization authorization)
{
    if (authorization == m_notificationAuthorization)
        return;
    m_notificationAuthorization = authorization;
    emit notificationAuthorizationChanged();
}

void SystemTray::triggerAction(const QString &name)
{
    if (name == QLatin1String("newNote"))
        emit newNoteRequested();
    else if (name == QLatin1String("quickCapture"))
        emit quickCaptureRequested();
    else if (name == QLatin1String("show"))
        emit showWindowRequested();
    else if (name == QLatin1String("quit"))
        emit quitRequested();
}
