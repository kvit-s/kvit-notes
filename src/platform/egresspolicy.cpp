// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "egresspolicy.h"

#include "settingsstore.h"

namespace {

const QString kAutoLoadKey = QStringLiteral("network.autoLoadRemoteContent");
const QString kOriginsKey = QStringLiteral("network.allowedOrigins");

} // namespace

EgressPolicy::EgressPolicy(QObject *parent)
    : QObject(parent)
{
}

void EgressPolicy::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    load();
}

void EgressPolicy::load()
{
    m_allowedOrigins.clear();
    if (m_settings) {
        const QStringList stored =
            m_settings->value(kOriginsKey, QStringList()).toStringList();
        for (const QString &origin : stored) {
            const QString normalized = originOf(origin);
            if (!normalized.isEmpty())
                m_allowedOrigins.insert(normalized);
        }
    }
    ++m_revision;
    emit policyChanged();
}

bool EgressPolicy::autoLoadRemoteContent() const
{
    return m_settings && m_settings->value(kAutoLoadKey, false).toBool();
}

void EgressPolicy::setAutoLoadRemoteContent(bool on)
{
    if (!m_settings || on == autoLoadRemoteContent())
        return;
    m_settings->setValue(kAutoLoadKey, on);
    ++m_revision;
    emit policyChanged();
}

bool EgressPolicy::isFetchableScheme(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

QString EgressPolicy::originOf(const QString &url)
{
    const QUrl u(url);
    if (!u.isValid() || !isFetchableScheme(u) || u.host().isEmpty())
        return QString();
    QString origin = u.scheme().toLower() + QStringLiteral("://")
                   + u.host().toLower();
    // A non-default port is part of the origin: approving a site's web
    // server is not approving whatever else listens on the same name.
    const int port = u.port(-1);
    const int defaultPort = u.scheme().compare(QLatin1String("https"),
                                               Qt::CaseInsensitive) == 0 ? 443 : 80;
    if (port != -1 && port != defaultPort)
        origin += QLatin1Char(':') + QString::number(port);
    return origin;
}

bool EgressPolicy::canRequestConsent(const QString &url) const
{
    return refusalReason(url).isEmpty() && !originOf(url).isEmpty();
}

QString EgressPolicy::refusalReason(const QString &url) const
{
    const QUrl u(url);
    if (url.trimmed().isEmpty() || !u.isValid())
        return tr("not a valid address");
    if (!isFetchableScheme(u))
        return tr("only http and https addresses are loaded");
    if (u.host().isEmpty())
        return tr("the address names no host");
    // Credentials in a URL are a phishing and credential-leak shape, and no
    // legitimate embed needs them.
    if (!u.userName().isEmpty() || !u.password().isEmpty())
        return tr("the address embeds credentials");
    return QString();
}

QString EgressPolicy::imageSourceFor(const QString &url) const
{
    if (url.isEmpty())
        return QString();
    if (!isFetchableScheme(QUrl(url)))
        return url;             // local file, data: or qrc: — no egress
    if (!isAllowed(url))
        return QString();       // inert placeholder, no request
    return QStringLiteral("image://remote/")
         + QString::fromUtf8(QUrl::toPercentEncoding(url));
}

bool EgressPolicy::isAllowed(const QString &url) const
{
    if (!refusalReason(url).isEmpty())
        return false;
    if (autoLoadRemoteContent())
        return true;
    return isOriginAllowed(url);
}

bool EgressPolicy::isOriginAllowed(const QString &url) const
{
    const QString origin = originOf(url);
    return !origin.isEmpty() && m_allowedOrigins.contains(origin);
}

void EgressPolicy::allowOrigin(const QString &url)
{
    const QString origin = originOf(url);
    if (origin.isEmpty() || m_allowedOrigins.contains(origin))
        return;
    m_allowedOrigins.insert(origin);
    if (m_settings) {
        QStringList stored = m_allowedOrigins.values();
        stored.sort();
        m_settings->setValue(kOriginsKey, stored);
    }
    ++m_revision;
    emit policyChanged();
}

void EgressPolicy::forgetOrigin(const QString &url)
{
    const QString origin = originOf(url);
    if (origin.isEmpty() || !m_allowedOrigins.remove(origin))
        return;
    if (m_settings) {
        QStringList stored = m_allowedOrigins.values();
        stored.sort();
        m_settings->setValue(kOriginsKey, stored);
    }
    ++m_revision;
    emit policyChanged();
}

QStringList EgressPolicy::allowedOrigins() const
{
    QStringList out = m_allowedOrigins.values();
    out.sort();
    return out;
}

void EgressPolicy::forgetAllOrigins()
{
    if (m_allowedOrigins.isEmpty())
        return;
    m_allowedOrigins.clear();
    if (m_settings)
        m_settings->setValue(kOriginsKey, QStringList());
    ++m_revision;
    emit policyChanged();
}

bool EgressPolicy::addressIsBlocked(const QHostAddress &address) const
{
    if (address.isNull())
        return true;

    // An IPv4-mapped IPv6 address (::ffff:127.0.0.1) is the same host as the
    // IPv4 form, so classify the unwrapped address rather than the wrapper.
    QHostAddress addr = address;
    bool mapped = false;
    const quint32 v4 = addr.toIPv4Address(&mapped);
    if (mapped && addr.protocol() == QAbstractSocket::IPv6Protocol)
        addr = QHostAddress(v4);

    if (addr.isLoopback())
        return !m_allowLoopbackForTests;
    if (addr.isNull() || addr.isBroadcast() || addr.isMulticast())
        return true;
    if (addr == QHostAddress(QHostAddress::AnyIPv4)
        || addr == QHostAddress(QHostAddress::AnyIPv6))
        return true;
    // Link-local covers 169.254.0.0/16, and with it 169.254.169.254 -- the
    // cloud instance metadata endpoint that turns a blind GET into a
    // credential disclosure on AWS, GCP and Azure.
    if (addr.isLinkLocal())
        return true;
    if (addr.isUniqueLocalUnicast())   // IPv6 fc00::/7
        return true;

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = addr.toIPv4Address();
        const auto inNet = [ip](quint32 net, int bits) {
            const quint32 mask = bits == 0 ? 0u : (0xffffffffu << (32 - bits));
            return (ip & mask) == (net & mask);
        };
        if (inNet(0x0a000000, 8))    return true;   // 10.0.0.0/8
        if (inNet(0xac100000, 12))   return true;   // 172.16.0.0/12
        if (inNet(0xc0a80000, 16))   return true;   // 192.168.0.0/16
        if (inNet(0x64400000, 10))   return true;   // 100.64.0.0/10 (CGNAT)
        if (inNet(0x00000000, 8))    return true;   // 0.0.0.0/8
        if (inNet(0xc0000000, 24))   return true;   // 192.0.0.0/24 (IETF)
        if (inNet(0xf0000000, 4))    return true;   // 240.0.0.0/4 (reserved)
    }
    return false;
}
