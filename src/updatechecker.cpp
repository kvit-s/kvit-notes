// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "updatechecker.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>

#include "settingsstore.h"

namespace {

const QString kEnabledKey = QStringLiteral("updates.checkEnabled");
const QString kLastCheckKey = QStringLiteral("updates.lastCheck");

// One dotted-release + optional prerelease pair, e.g. "1.2.0-rc3".
struct ParsedVersion {
    QList<int> release;
    QStringList prerelease;   // empty = a final release (orders above any rc)
};

ParsedVersion parseVersion(QString v)
{
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    ParsedVersion out;
    const int dash = v.indexOf(QLatin1Char('-'));
    const QString releasePart = dash < 0 ? v : v.left(dash);
    if (dash >= 0)
        out.prerelease = v.mid(dash + 1).split(QLatin1Char('.'),
                                               Qt::SkipEmptyParts);
    const QStringList fields = releasePart.split(QLatin1Char('.'),
                                                 Qt::SkipEmptyParts);
    for (const QString &f : fields) {
        bool okNum = false;
        const int n = f.toInt(&okNum);
        out.release.append(okNum ? n : 0);
    }
    return out;
}

// SemVer-ish identifier comparison that also orders "rc2" < "rc10": split a
// trailing digit run off the alphabetic stem and compare the pieces.
int compareIdentifier(const QString &a, const QString &b)
{
    auto split = [](const QString &s, QString *stem, int *num) {
        int i = s.size();
        while (i > 0 && s.at(i - 1).isDigit())
            --i;
        *stem = s.left(i);
        *num = i < s.size() ? s.mid(i).toInt() : -1;
    };
    QString stemA, stemB;
    int numA = 0, numB = 0;
    split(a, &stemA, &numA);
    split(b, &stemB, &numB);
    const int stemCmp = stemA.compare(stemB, Qt::CaseInsensitive);
    if (stemCmp != 0)
        return stemCmp;
    return numA - numB;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

void UpdateChecker::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    emit enabledChanged();
}

bool UpdateChecker::enabled() const
{
    if (!m_settings)
        return false;
    return m_settings->value(kEnabledKey, true).toBool();
}

void UpdateChecker::setEnabled(bool enabled)
{
    if (!m_settings || enabled == this->enabled())
        return;
    m_settings->setValue(kEnabledKey, enabled);
    emit enabledChanged();
}

void UpdateChecker::maybeCheck()
{
    if (!m_fetcher || !m_settings || !enabled())
        return;
    if (qEnvironmentVariableIsSet("KVIT_DISABLE_UPDATE_CHECK"))
        return;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (m_settings->value(kLastCheckKey, QString()).toString() == today)
        return;
    // Stamp before the request: at most one call per day even if the app
    // exits mid-flight or the reply never lands.
    m_settings->setValue(kLastCheckKey, today);
    m_fetcher->fetch(m_endpoint, [this](bool ok, const QByteArray &body) {
        applyResult(ok, body);
    });
}

void UpdateChecker::applyResult(bool ok, const QByteArray &body)
{
    if (!ok)
        return;
    QString version, url;
    if (!parseLatestPayload(body, &version, &url))
        return;
    if (compareVersions(version, m_currentVersion) <= 0)
        return;
    m_updateAvailable = true;
    m_latestVersion = version;
    m_releaseUrl = url;
    emit stateChanged();
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const ParsedVersion va = parseVersion(a);
    const ParsedVersion vb = parseVersion(b);
    const int fields = qMax(va.release.size(), vb.release.size());
    for (int i = 0; i < fields; ++i) {
        const int fa = i < va.release.size() ? va.release.at(i) : 0;
        const int fb = i < vb.release.size() ? vb.release.at(i) : 0;
        if (fa != fb)
            return fa - fb;
    }
    // Equal release fields: a final release outranks any prerelease.
    if (va.prerelease.isEmpty() != vb.prerelease.isEmpty())
        return va.prerelease.isEmpty() ? 1 : -1;
    const int ids = qMax(va.prerelease.size(), vb.prerelease.size());
    for (int i = 0; i < ids; ++i) {
        if (i >= va.prerelease.size())
            return -1;   // fewer identifiers orders first (semver §11)
        if (i >= vb.prerelease.size())
            return 1;
        const int cmp = compareIdentifier(va.prerelease.at(i),
                                          vb.prerelease.at(i));
        if (cmp != 0)
            return cmp;
    }
    return 0;
}

bool UpdateChecker::parseLatestPayload(const QByteArray &json, QString *version,
                                       QString *url)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    QString tag = obj.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty())
        return false;
    if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V')))
        tag.remove(0, 1);
    if (version)
        *version = tag;
    if (url)
        *url = obj.value(QStringLiteral("html_url")).toString();
    return true;
}
