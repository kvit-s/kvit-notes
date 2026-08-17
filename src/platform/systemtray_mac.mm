// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "systemtray.h"

#include <QMetaObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QUuid>

#import <UserNotifications/UserNotifications.h>

#include <memory>

namespace {

NSString *const kActivationIdKey = @"kvit-activation-id";

SystemTray::NotificationAuthorization authorizationFor(
    UNAuthorizationStatus status)
{
    switch (status) {
    case UNAuthorizationStatusNotDetermined:
        return SystemTray::Unknown;
    case UNAuthorizationStatusDenied:
        return SystemTray::Denied;
    case UNAuthorizationStatusAuthorized:
    case UNAuthorizationStatusProvisional:
    case UNAuthorizationStatusEphemeral:
        return SystemTray::Authorized;
    }
    return SystemTray::Unknown;
}

} // namespace

class MacNotificationBackend;

@interface KvitNotificationCenterDelegate
    : NSObject <UNUserNotificationCenterDelegate> {
@public
    MacNotificationBackend *backend;
}
@end

// macOS owns notification authorization independently of the menu-bar item.
// UNUserNotificationCenter also carries userInfo on each request, avoiding
// QSystemTrayIcon::messageClicked()'s single implicit "current message" id.
class MacNotificationBackend final : public SystemTrayNotificationBackend
{
public:
    explicit MacNotificationBackend(QSystemTrayIcon *tray)
        : m_tray(tray)
    {
        if (!m_tray)
            return;

        m_center = [UNUserNotificationCenter currentNotificationCenter];
        m_authorization = SystemTray::Unknown;
        m_delegate = [[KvitNotificationCenterDelegate alloc] init];
        m_delegate->backend = this;
        m_center.delegate = m_delegate;
        refreshAuthorization();
    }

    ~MacNotificationBackend() override
    {
        if (m_delegate) {
            m_delegate->backend = nullptr;
            if (m_center.delegate == m_delegate)
                m_center.delegate = nil;
        }
    }

    SystemTray::NotificationAuthorization authorization() const override
    {
        return m_authorization;
    }

    void requestAuthorization() override
    {
        if (m_authorization != SystemTray::Unknown || m_requestPending
            || !m_tray) {
            return;
        }

        m_requestPending = true;
        QPointer<MacNotificationBackend> guard(this);
        const UNAuthorizationOptions options = UNAuthorizationOptionAlert;
        [m_center requestAuthorizationWithOptions:options
                                completionHandler:^(BOOL granted,
                                                    NSError *error) {
            Q_UNUSED(granted);
            if (!guard)
                return;
            // A false result can mean denied or still undetermined. Query the
            // stored platform verdict instead of guessing which one it was.
            if (!error) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard]() {
                        if (guard)
                            guard->refreshAuthorization(true);
                    },
                    Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(
                guard.data(),
                [guard]() {
                    if (!guard)
                        return;
                    guard->m_requestPending = false;
                    guard->setAuthorization(SystemTray::Unknown, true);
                },
                Qt::QueuedConnection);
        }];
    }

    void post(const QString &title, const QString &message,
              const QString &activationId) override
    {
        if (m_authorization != SystemTray::Authorized || !m_tray
            || !m_tray->isVisible()) {
            return;
        }

        UNMutableNotificationContent *content =
            [[UNMutableNotificationContent alloc] init];
        content.title = title.toNSString();
        content.body = message.toNSString();
        content.userInfo = @{kActivationIdKey: activationId.toNSString()};

        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        UNNotificationRequest *request =
            [UNNotificationRequest requestWithIdentifier:requestId.toNSString()
                                                  content:content
                                                  trigger:nil];
        [m_center addNotificationRequest:request withCompletionHandler:nil];
    }

    void publishActivation(const QString &activationId)
    {
        QPointer<MacNotificationBackend> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard, activationId]() {
                if (guard)
                    emit guard->activated(activationId);
            },
            Qt::QueuedConnection);
    }

private:
    void refreshAuthorization(bool completesRequest = false)
    {
        QPointer<MacNotificationBackend> guard(this);
        [m_center getNotificationSettingsWithCompletionHandler:
            ^(UNNotificationSettings *settings) {
                const SystemTray::NotificationAuthorization result =
                    authorizationFor(settings.authorizationStatus);
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, result, completesRequest]() {
                        if (!guard)
                            return;
                        if (completesRequest)
                            guard->m_requestPending = false;
                        guard->setAuthorization(result, completesRequest);
                    },
                    Qt::QueuedConnection);
            }];
    }

    void setAuthorization(SystemTray::NotificationAuthorization authorization,
                          bool forceSignal)
    {
        if (!forceSignal && authorization == m_authorization)
            return;
        m_authorization = authorization;
        emit authorizationChanged(authorization);
    }

    QPointer<QSystemTrayIcon> m_tray;
    UNUserNotificationCenter *m_center = nil;
    KvitNotificationCenterDelegate *m_delegate = nil;
    SystemTray::NotificationAuthorization m_authorization =
        SystemTray::Unsupported;
    bool m_requestPending = false;
};

@implementation KvitNotificationCenterDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion
{
    Q_UNUSED(center);
    Q_UNUSED(notification);
    completion(UNNotificationPresentationOptionBanner);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
didReceiveNotificationResponse:(UNNotificationResponse *)response
         withCompletionHandler:(void (^)(void))completion
{
    Q_UNUSED(center);
    if (backend
        && [response.actionIdentifier
            isEqualToString:UNNotificationDefaultActionIdentifier]) {
        const id value = response.notification.request.content.userInfo[
            kActivationIdKey];
        if ([value isKindOfClass:[NSString class]])
            backend->publishActivation(
                QString::fromNSString((NSString *)value));
    }
    completion();
}

@end

std::unique_ptr<SystemTrayNotificationBackend>
createMacSystemTrayNotificationBackend(QSystemTrayIcon *tray)
{
    return std::make_unique<MacNotificationBackend>(tray);
}
