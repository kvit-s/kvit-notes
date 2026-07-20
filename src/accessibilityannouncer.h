// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef ACCESSIBILITYANNOUNCER_H
#define ACCESSIBILITYANNOUNCER_H

#include <QObject>
#include <QString>

// The live-region announcement seam (phase12 decision 4, §14.2). Dynamic
// changes that a sighted user sees as chrome updates — save state, search match
// counts, mode toggles, block conversions — are spoken to assistive technology
// through this one object. It always records the last message and emits a signal
// (so the gate can assert an announcement fired without a live screen reader,
// which the WSLg AT-SPI bridge does not provide — spike (c)), and, when a
// window and the accessibility bridge are present, posts a real
// QAccessibleAnnouncementEvent so an attached reader speaks it.
class AccessibilityAnnouncer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY announced)
public:
    explicit AccessibilityAnnouncer(QObject *parent = nullptr) : QObject(parent) {}

    QString lastMessage() const { return m_lastMessage; }

    // Speak an arbitrary message.
    Q_INVOKABLE void announce(const QString &message);

    // Convenience wrappers for the four dynamic-change categories §14.2 names,
    // so the call sites read intently and the phrasing stays consistent.
    Q_INVOKABLE void announceSaveState(bool dirty);
    Q_INVOKABLE void announceMatchCount(int count);
    Q_INVOKABLE void announceMode(const QString &mode, bool on);
    Q_INVOKABLE void announceConversion(const QString &typeName);

signals:
    void announced(const QString &message);

private:
    QString m_lastMessage;
};

#endif // ACCESSIBILITYANNOUNCER_H
