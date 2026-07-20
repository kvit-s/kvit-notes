// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "settingsstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QJSValue>
#include <QSaveFile>

// QML passes JS arrays/objects into QVariant parameters as wrapped
// QJSValues; QJsonValue::fromVariant would silently store those as
// null. Unwrap to plain variants at the boundary.
static QVariant unwrapped(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QJSValue>())
        return value.value<QJSValue>().toVariant();
    return value;
}

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
{
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(WriteDelayMs);
    connect(&m_writeTimer, &QTimer::timeout, this, &SettingsStore::writeFile);
}

SettingsStore::~SettingsStore()
{
    if (m_dirty)
        writeFile();
}

bool SettingsStore::open(const QString &filePath)
{
    // A pending write belongs to the previous path; land it there first.
    if (m_dirty)
        writeFile();
    m_writeTimer.stop();

    m_filePath = filePath;
    m_values = QJsonObject();
    m_dirty = false;

    const QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        bumpRevision();
        return false;
    }

    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc =
            QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
            m_values = doc.object();
        // Corrupt or non-object: defaults; the file is rewritten on the
        // first setValue.
    }

    bumpRevision();
    return true;
}

QVariant SettingsStore::value(const QString &key, const QVariant &fallback) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
        return unwrapped(fallback);
    return it->toVariant();
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    const QJsonValue jsonValue = QJsonValue::fromVariant(unwrapped(value));
    const auto it = m_values.constFind(key);
    if (it != m_values.constEnd() && *it == jsonValue)
        return;

    m_values.insert(key, jsonValue);
    scheduleWrite();
    emit valueChanged(key);
    bumpRevision();
}

void SettingsStore::remove(const QString &key)
{
    if (!m_values.contains(key))
        return;

    m_values.remove(key);
    scheduleWrite();
    emit valueChanged(key);
    bumpRevision();
}

bool SettingsStore::contains(const QString &key) const
{
    return m_values.contains(key);
}

void SettingsStore::flush()
{
    if (m_dirty)
        writeFile();
}

void SettingsStore::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}

void SettingsStore::scheduleWrite()
{
    m_dirty = true;
    m_writeTimer.start();  // restarts on every change: bursts coalesce
}

void SettingsStore::writeFile()
{
    m_writeTimer.stop();
    m_dirty = false;
    if (m_filePath.isEmpty())
        return;

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_values).toJson(QJsonDocument::Indented));
    file.commit();
}
