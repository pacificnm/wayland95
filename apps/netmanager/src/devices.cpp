#include "devices.h"

#include "secrets.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslCertificate>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QWebEngineCertificateError>

#include <algorithm>

namespace {
Secrets::Attributes secretOf(const QString &id)
{
    return {{QStringLiteral("application"), QStringLiteral("wayland95-netmanager")}, {QStringLiteral("device"), id}};
}

const QStringList kinds = {QStringLiteral("router"), QStringLiteral("switch"), QStringLiteral("accesspoint"),
                           QStringLiteral("printer"), QStringLiteral("storage"), QStringLiteral("camera"),
                           QStringLiteral("server"), QStringLiteral("other")};
} // namespace

Devices::Devices(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Devices::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/devices.json");
}

void Devices::load()
{
    QFile f(filePath());
    QVariantList list;
    if (f.open(QIODevice::ReadOnly))
        list = QJsonDocument::fromJson(f.readAll()).array().toVariantList();
    m_devices.clear();
    for (const QVariant &v : std::as_const(list))
        m_devices << complete(v.toMap());
    std::sort(m_devices.begin(), m_devices.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    emit changed();
}

void Devices::write()
{
    QVariantList list;
    for (QVariant v : std::as_const(m_devices)) {
        QVariantMap d = v.toMap();
        d.remove(QStringLiteral("url")); // (worked out again on loading)
        list << d;
    }
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile f(filePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonArray::fromVariantList(list)).toJson());
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.commit();
    }
    load();
}

// the defaults, and the address
QVariantMap Devices::complete(QVariantMap d) const
{
    auto def = [&d](const char *key, const QVariant &value) {
        if (!d.contains(QLatin1String(key)))
            d.insert(QLatin1String(key), value);
    };
    def("name", QString());
    def("kind", QStringLiteral("other"));
    def("protocol", QStringLiteral("http"));
    def("host", QString());
    def("port", 0);
    def("path", QString());
    def("username", QString());
    def("fillLogin", true);
    def("autoLogin", false);
    def("notes", QString());
    def("fingerprint", QString());
    def("lastUsed", QString());
    if (!kinds.contains(d.value(QStringLiteral("kind")).toString()))
        d.insert(QStringLiteral("kind"), QStringLiteral("other"));
    d.insert(QStringLiteral("url"), urlOf(d));
    return d;
}

QString Devices::check(const QVariantMap &d) const
{
    if (d.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        return QStringLiteral("Give the device a name.");
    const QString host = d.value(QStringLiteral("host")).toString().trimmed();
    if (host.isEmpty())
        return QStringLiteral("Type the device's address: its IP address (192.168.1.1) or its name on the network.");
    // an IP address (v4 or v6), or a host name
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,62})(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,62}))*\\.?$"));
    QHostAddress ip;
    if (!ip.setAddress(host) && !name.match(host).hasMatch())
        return QStringLiteral("\"") + host + QStringLiteral("\" isn't an address. Type an IP address (192.168.1.1) or a name (router.lan), "
                                                           "without http:// or a path.");
    const QString protocol = d.value(QStringLiteral("protocol")).toString();
    if (protocol != QLatin1String("http") && protocol != QLatin1String("https"))
        return QStringLiteral("The connection must be HTTP or HTTPS.");
    const int port = d.value(QStringLiteral("port")).toInt();
    if (port < 0 || port > 65535)
        return QStringLiteral("The port must be between 1 and 65535 (or empty for the usual one).");
    const QString path = d.value(QStringLiteral("path")).toString();
    if (!path.isEmpty() && (!path.startsWith(QLatin1Char('/')) || path.contains(QRegularExpression(QStringLiteral("\\s")))))
        return QStringLiteral("The page must start with / (like /admin), with no spaces.");
    return QString();
}

QString Devices::urlOf(const QVariantMap &d) const
{
    const QString host = d.value(QStringLiteral("host")).toString().trimmed();
    if (host.isEmpty())
        return QString();
    QUrl url;
    url.setScheme(d.value(QStringLiteral("protocol")).toString() == QLatin1String("https") ? QStringLiteral("https") : QStringLiteral("http"));
    url.setHost(host);
    const int port = d.value(QStringLiteral("port")).toInt();
    if (port > 0)
        url.setPort(port);
    const QString path = d.value(QStringLiteral("path")).toString();
    url.setPath(path.isEmpty() ? QStringLiteral("/") : path);
    return url.toString();
}

bool Devices::isDevicePage(const QString &id, const QString &address) const
{
    const QUrl a(urlOf(device(id))), b(address);
    return a.isValid() && b.isValid() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
        && a.port(a.scheme() == QLatin1String("https") ? 443 : 80) == b.port(b.scheme() == QLatin1String("https") ? 443 : 80);
}

QVariantMap Devices::device(const QString &id) const
{
    for (const QVariant &v : m_devices)
        if (v.toMap().value(QStringLiteral("id")) == id)
            return v.toMap();
    return {};
}

QString Devices::save(const QVariantMap &in)
{
    if (!check(in).isEmpty())
        return QString();
    QVariantMap d;
    for (const char *key : {"name", "kind", "protocol", "host", "port", "path", "username", "fillLogin", "autoLogin", "notes"})
        if (in.contains(QLatin1String(key)))
            d.insert(QLatin1String(key), in.value(QLatin1String(key)));
    d.insert(QStringLiteral("name"), d.value(QStringLiteral("name")).toString().trimmed());
    d.insert(QStringLiteral("host"), d.value(QStringLiteral("host")).toString().trimmed());
    QString id = in.value(QStringLiteral("id")).toString();
    for (int i = 0; i < m_devices.size() && !id.isEmpty(); ++i) {
        QVariantMap old = m_devices.at(i).toMap();
        if (old.value(QStringLiteral("id")) != id)
            continue;
        // a different address: the certificate trusted for the old one doesn't count
        if (urlOf(old) != urlOf(d) || old.value(QStringLiteral("host")) != d.value(QStringLiteral("host")))
            old.insert(QStringLiteral("fingerprint"), QString());
        for (auto it = d.cbegin(); it != d.cend(); ++it)
            old.insert(it.key(), it.value());
        m_devices[i] = old;
        write();
        return id;
    }
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d.insert(QStringLiteral("id"), id);
    m_devices << d;
    write();
    return id;
}

void Devices::remove(const QString &id)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).toMap().value(QStringLiteral("id")) == id) {
            m_devices.removeAt(i);
            Secrets::remove(secretOf(id));
            write();
            return;
        }
    }
}

void Devices::setFingerprint(const QString &id, const QString &fingerprint)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        QVariantMap d = m_devices.at(i).toMap();
        if (d.value(QStringLiteral("id")) == id) {
            d.insert(QStringLiteral("fingerprint"), fingerprint);
            m_devices[i] = d;
            write();
            return;
        }
    }
}

void Devices::touch(const QString &id)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        QVariantMap d = m_devices.at(i).toMap();
        if (d.value(QStringLiteral("id")) == id) {
            d.insert(QStringLiteral("lastUsed"), QDateTime::currentDateTime().toString(Qt::ISODate));
            m_devices[i] = d;
            write();
            return;
        }
    }
}

bool Devices::keyringAvailable() const
{
    return Secrets::available();
}

void Devices::password(const QString &id, const QJSValue &done)
{
    QJSValue cb = done;
    Secrets::lookup(secretOf(id), [cb](bool found, const QString &password) mutable {
        const QJSValue r = cb.call({found, password});
        if (r.isError())
            qWarning("Network Manager: %s", qPrintable(r.toString()));
    });
}

void Devices::setPassword(const QString &id, const QString &password, const QJSValue &done)
{
    QJSValue cb = done;
    if (password.isEmpty()) {
        Secrets::remove(secretOf(id));
        cb.call({true, QString()});
        return;
    }
    const QString label = QStringLiteral("Network Manager: ") + device(id).value(QStringLiteral("name")).toString();
    Secrets::store(secretOf(id), label, password, [cb](bool ok, const QString &error) mutable {
        const QJSValue r = cb.call({ok, error});
        if (r.isError())
            qWarning("Network Manager: %s", qPrintable(r.toString()));
    });
}

void Devices::forgetPassword(const QString &id)
{
    Secrets::remove(secretOf(id));
}

void Devices::watch(QObject *view)
{
    // (old-style: the view's class is private to Qt WebEngine)
    if (view)
        connect(view, SIGNAL(certificateError(QWebEngineCertificateError)), this, SLOT(certificateProblem(QWebEngineCertificateError)));
}

void Devices::certificateProblem(const QWebEngineCertificateError &error)
{
    const QList<QSslCertificate> chain = error.certificateChain();
    if (chain.isEmpty())
        return;
    const QByteArray hex = chain.first().digest(QCryptographicHash::Sha256).toHex().toUpper();
    QStringList pairs;
    for (int i = 0; i < hex.size(); i += 2)
        pairs << QString::fromLatin1(hex.mid(i, 2));
    const QSslCertificate &c = chain.first();
    QString who = c.subjectInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
    const QString org = c.subjectInfo(QSslCertificate::Organization).join(QStringLiteral(", "));
    if (!org.isEmpty())
        who += (who.isEmpty() ? QString() : QStringLiteral(" (")) + org + (who.isEmpty() ? QString() : QStringLiteral(")"));
    if (c.isSelfSigned())
        who += QStringLiteral(", signed by itself");
    who += QStringLiteral(", good until ") + c.expiryDate().toLocalTime().toString(QStringLiteral("d MMM yyyy"));
    m_certificates.insert(error.url().toString(), {pairs.join(QLatin1Char(':')), who});
}

QString Devices::fingerprint(const QUrl &url) const
{
    return m_certificates.value(url.toString()).first;
}

QString Devices::certificateSubject(const QUrl &url) const
{
    return m_certificates.value(url.toString()).second;
}

QString Devices::gateway() const
{
    // /proc/net/route: the default route (destination 0) has the gateway,
    // little-endian hex
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> cols = line.simplified().split(' ');
        if (cols.size() < 3 || cols.at(1) != "00000000")
            continue;
        bool ok = false;
        const quint32 g = cols.at(2).toUInt(&ok, 16);
        if (ok && g)
            return QStringLiteral("%1.%2.%3.%4").arg(g & 0xff).arg((g >> 8) & 0xff).arg((g >> 16) & 0xff).arg(g >> 24);
    }
    return QString();
}
