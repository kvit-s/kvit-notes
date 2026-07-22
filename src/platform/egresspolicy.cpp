// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "egresspolicy.h"

#include "settingsstore.h"

namespace {

const QString kAutoLoadKey = QStringLiteral("network.autoLoadRemoteContent");
const QString kOriginsKey = QStringLiteral("network.allowedOrigins");

// One special-use range, in the CIDR form IANA registers it under. Loopback
// is deliberately absent: it is checked separately because the test suite can
// only serve from it and turns it back on through a seam.
struct SpecialUsePrefix {
    const char *network;
    int bits;
};

// Everything IANA lists as other than ordinary public unicast, for both
// families in one table so a range cannot be fixed for IPv4 and forgotten for
// IPv6. QHostAddress::isInSubnet() answers false when the families differ, so
// a single pass covers both.
constexpr SpecialUsePrefix kSpecialUsePrefixes[] = {
    // ---- IPv4 ----
    {"0.0.0.0",        8},   // "this network"
    {"10.0.0.0",       8},   // RFC1918 private
    {"100.64.0.0",    10},   // carrier-grade NAT
    {"169.254.0.0",   16},   // link-local, incl. 169.254.169.254 metadata
    {"172.16.0.0",    12},   // RFC1918 private
    {"192.0.0.0",     24},   // IETF protocol assignments
    {"192.0.2.0",     24},   // TEST-NET-1
    {"192.168.0.0",   16},   // RFC1918 private
    {"198.18.0.0",    15},   // benchmarking (routed inside some networks)
    {"198.51.100.0",  24},   // TEST-NET-2
    {"203.0.113.0",   24},   // TEST-NET-3
    {"224.0.0.0",      4},   // multicast
    {"240.0.0.0",      4},   // reserved, incl. 255.255.255.255 broadcast
    // ---- IPv6 ----
    {"::",           128},   // unspecified
    {"100::",         64},   // discard-only
    {"2001:db8::",    32},   // documentation
    {"fc00::",         7},   // unique-local
    {"fe80::",        10},   // link-local
    {"fec0::",        10},   // site-local (deprecated, still routed in places)
    {"ff00::",         8},   // multicast
};

// A v6 address that carries a v4 address inside it reaches whatever that v4
// address names, so the v4 has to be classified rather than the wrapper.
// Returns a null address when `addr` is not one of these encapsulations.
QHostAddress embeddedIPv4(const QHostAddress &addr)
{
    if (addr.protocol() != QAbstractSocket::IPv6Protocol)
        return QHostAddress();
    const Q_IPV6ADDR raw = addr.toIPv6Address();
    const auto word32 = [&raw](int byte) {
        return (quint32(raw[byte]) << 24) | (quint32(raw[byte + 1]) << 16)
             | (quint32(raw[byte + 2]) << 8) | quint32(raw[byte + 3]);
    };
    // 6to4 (2002::/16): the v4 address sits in bytes 2..5.
    if (raw[0] == 0x20 && raw[1] == 0x02)
        return QHostAddress(word32(2));
    // Teredo (2001:0000::/32): the client's v4 address is the last 32 bits,
    // stored inverted.
    if (raw[0] == 0x20 && raw[1] == 0x01 && raw[2] == 0x00 && raw[3] == 0x00)
        return QHostAddress(word32(12) ^ 0xffffffffu);
    // NAT64 well-known prefix (64:ff9b::/96): the v4 address is the last 32
    // bits, and the translator forwards to it.
    if (addr.isInSubnet(QHostAddress(QStringLiteral("64:ff9b::")), 96))
        return QHostAddress(word32(12));
    return QHostAddress();
}

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

bool EgressPolicy::isNonEgressSource(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme.isEmpty()) {
        // A relative path, resolved by QML against the document it appears
        // in. "//host/x" also parses with an empty scheme but names an
        // authority, and is a protocol-relative URL, so an authority
        // disqualifies it.
        return url.authority().isEmpty();
    }
    if (scheme == QLatin1String("file") || scheme == QLatin1String("qrc")
        || scheme == QLatin1String("data")) {
        return true;
    }
    // The app's own providers, which are objects in this process: the MicroTeX
    // renderer and the remote provider that fetches through EgressFetcher.
    // image://<anything else> would be a provider the app did not install.
    if (scheme == QLatin1String("image")) {
        const QString provider = url.host().toLower();
        return provider == QLatin1String("math")
            || provider == QLatin1String("remote")
            || provider == QLatin1String("local");
    }
    return false;
}

QString EgressPolicy::originOf(const QString &url)
{
    const QUrl u(url);
    if (!u.isValid() || !isFetchableScheme(u) || u.host().isEmpty())
        return QString();
    // An IPv6 literal has to keep its brackets: "http://::1:8080" is not a
    // URL, so an origin serialized without them cannot be parsed back and the
    // reader's consent is silently lost on the next launch.
    QString host = u.host().toLower();
    if (host.contains(QLatin1Char(':')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    QString origin = u.scheme().toLower() + QStringLiteral("://") + host;
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
    const QUrl parsed(url);
    if (!isFetchableScheme(parsed)) {
        // Not http(s): pass through only what cannot open a connection. A
        // scheme outside the allowlist yields "" rather than being handed to
        // QML, because QML's Image would give it to QNetworkAccessManager,
        // which speaks more schemes than this class checks -- `local+http:`
        // reaches a unix socket, and the next Qt release may add another.
        if (!isNonEgressSource(parsed))
            return QString();
        // A file on disk goes through the app's own provider, which refuses
        // an image whose header says it would decode larger than the same
        // budget a remote one is held to. QML's own file loader has no such
        // check, so a note naming a 20000x20000 PNG used to be a way to
        // exhaust memory just by opening it. qrc: and data: are the app's own
        // bytes and a bounded literal, and pass through unchanged.
        if (parsed.isLocalFile()) {
            return QStringLiteral("image://local/")
                 + QString::fromUtf8(
                       QUrl::toPercentEncoding(parsed.toLocalFile()));
        }
        return url;
    }
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
    // The flag forms Qt already computes, kept alongside the table so a Qt
    // classification and the prefix list would both have to be wrong for a
    // range to slip through.
    if (addr.isLinkLocal() || addr.isSiteLocal() || addr.isUniqueLocalUnicast())
        return true;

    // 6to4, Teredo and NAT64 all name an IPv4 address inside an IPv6 one, and
    // a packet sent there arrives at that IPv4 host. Classify what it carries.
    const QHostAddress inner = embeddedIPv4(addr);
    if (!inner.isNull())
        return addressIsBlocked(inner);

    static const QList<QPair<QHostAddress, int>> prefixes = []() {
        QList<QPair<QHostAddress, int>> out;
        for (const SpecialUsePrefix &p : kSpecialUsePrefixes)
            out.append({QHostAddress(QLatin1String(p.network)), p.bits});
        return out;
    }();
    for (const auto &prefix : prefixes) {
        if (addr.isInSubnet(prefix.first, prefix.second))
            return true;
    }
    return false;
}
