// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "systemtray.h"
#include "settingsstore.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QApplication>
#include <QIcon>

namespace {
const QString kSettingsCloseToTray = QStringLiteral("tray.closeToTray");
} // namespace

SystemTray::SystemTray(QObject *parent)
    : QObject(parent)
{
    // Build the real tray icon only where the platform provides one; otherwise
    // the seam still routes actions through its signals (documented WSLg gap).
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
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

void SystemTray::notify(const QString &title, const QString &message)
{
    m_lastNotification = message;
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(title, message, QSystemTrayIcon::Information, 4000);
    emit notified(message);
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
