#include "locales.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLocale>
#include <QProcess>
#include <QSet>

#include <algorithm>

namespace {
// "de_DE.UTF-8@euro" -> "de_DE"
QString base(const QString &name) { return name.section(QLatin1Char('.'), 0, 0).section(QLatin1Char('@'), 0, 0); }
}

Locales::Locales(QObject *parent)
    : QObject(parent)
{
    // what glibc has (locale -a: "en_US.utf8")
    QProcess p;
    p.start(QStringLiteral("locale"), { QStringLiteral("-a") });
    if (p.waitForFinished(3000)) {
        for (const QByteArray &line : p.readAllStandardOutput().split('\n')) {
            const QString n = base(QString::fromLocal8Bit(line.trimmed()));
            if (n.contains(QLatin1Char('_')) && !m_installed.contains(n))
                m_installed << n;
        }
    }

    QSet<QString> seen;
    for (const QLocale &l : QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory)) {
        const QString n = l.name();
        if (!n.contains(QLatin1Char('_')) || seen.contains(n) || l.language() == QLocale::C)
            continue;
        seen.insert(n);
        m_all << QVariantMap{
            { QStringLiteral("name"), n + QStringLiteral(".UTF-8") },
            { QStringLiteral("display"), displayName(n) },
            { QStringLiteral("installed"), m_installed.contains(n) },
        };
    }
    std::sort(m_all.begin(), m_all.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("display")).toString().compare(b.toMap().value(QStringLiteral("display")).toString(), Qt::CaseInsensitive) < 0;
    });
    readSystem();
}

QString Locales::session() const
{
    QString lang = qEnvironmentVariable("LC_ALL");
    if (lang.isEmpty())
        lang = qEnvironmentVariable("LANG");
    return lang;
}

QString Locales::displayName(const QString &name) const
{
    const QLocale l(base(name));
    if (l.language() == QLocale::C)
        return name;
    return QLocale::languageToString(l.language()) + QStringLiteral(" (") + QLocale::territoryToString(l.territory()) + QLatin1Char(')');
}

bool Locales::isInstalled(const QString &name) const { return m_installed.contains(base(name)); }

void Locales::readSystem()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.locale1"), QStringLiteral("/org/freedesktop/locale1"),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << QStringLiteral("org.freedesktop.locale1") << QStringLiteral("Locale");
    const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
    QString lang;
    if (reply.isValid())
        for (const QString &kv : reply.value().variant().toStringList())
            if (kv.startsWith(QLatin1String("LANG=")))
                lang = kv.mid(5);
    if (lang != m_system) {
        m_system = lang;
        emit systemChanged();
    }
}

void Locales::setSystem(const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.locale1"), QStringLiteral("/org/freedesktop/locale1"),
                                                       QStringLiteral("org.freedesktop.locale1"), QStringLiteral("SetLocale"));
    call << QStringList{ QStringLiteral("LANG=") + name } << true; // interactive: polkit may ask for a password
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        readSystem();
        emit systemSet(!watcher->isError(), watcher->isError() ? watcher->error().message() : QString());
    });
}
