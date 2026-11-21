#include "keystore.h"

#include "secrets.h"

#include <QMetaObject>

namespace {
Secrets::Attributes attributes(const QString &service)
{
    return {{QStringLiteral("application"), QStringLiteral("wayland95-tradinglab")}, {QStringLiteral("service"), service}};
}
const QString FMP = QStringLiteral("fmp");
const QString ANTHROPIC = QStringLiteral("anthropic");
}

KeyStore::KeyStore(QObject *parent)
    : QObject(parent)
{
}

void KeyStore::load()
{
    if (!Secrets::available()) {
        m_loaded = true;
        emit ready();
        return;
    }
    m_pending = 2;
    auto done = [this] {
        if (--m_pending == 0) {
            m_loaded = true;
            emit ready();
        }
    };
    Secrets::lookup(attributes(FMP), [this, done](bool found, const QString &key) {
        if (found) { QMutexLocker g(&m_mutex); m_fmp = key; }
        done();
    });
    Secrets::lookup(attributes(ANTHROPIC), [this, done](bool found, const QString &key) {
        if (found) { QMutexLocker g(&m_mutex); m_anthropic = key; }
        done();
    });
}

bool KeyStore::isLoaded() const { return m_loaded; }

QString KeyStore::fmpKey() const
{
    QMutexLocker g(&m_mutex);
    return m_fmp.isEmpty() ? qEnvironmentVariable("FMP_API_KEY") : m_fmp;
}

QString KeyStore::anthropicKey() const
{
    QMutexLocker g(&m_mutex);
    return m_anthropic.isEmpty() ? qEnvironmentVariable("ANTHROPIC_API_KEY") : m_anthropic;
}

// the keyring is talked to on the GUI thread (D-Bus replies arrive there); callers may be worker threads
void KeyStore::store(const QString &service, const QString &key)
{
    QMetaObject::invokeMethod(this, [service, key] {
        Secrets::store(attributes(service), QStringLiteral("Trading Lab %1 API key").arg(service == FMP ? QStringLiteral("FMP") : QStringLiteral("Anthropic")),
                       key, [](bool, const QString &) {});
    }, Qt::QueuedConnection);
}

void KeyStore::forget(const QString &service)
{
    QMetaObject::invokeMethod(this, [service] { Secrets::remove(attributes(service)); }, Qt::QueuedConnection);
}

void KeyStore::setFmpKey(const QString &key) { { QMutexLocker g(&m_mutex); m_fmp = key; } store(FMP, key); }
void KeyStore::setAnthropicKey(const QString &key) { { QMutexLocker g(&m_mutex); m_anthropic = key; } store(ANTHROPIC, key); }
void KeyStore::clearFmpKey() { { QMutexLocker g(&m_mutex); m_fmp.clear(); } forget(FMP); }
void KeyStore::clearAnthropicKey() { { QMutexLocker g(&m_mutex); m_anthropic.clear(); } forget(ANTHROPIC); }

QVariantMap KeyStore::status(const QString &stored, const char *env) const
{
    if (!stored.isEmpty())
        return {{QStringLiteral("source"), QStringLiteral("stored")}, {QStringLiteral("encrypted"), true}};
    return {{QStringLiteral("source"), qEnvironmentVariableIsEmpty(env) ? QStringLiteral("none") : QStringLiteral("env")}, {QStringLiteral("encrypted"), false}};
}

QVariantMap KeyStore::fmpStatus() const { QMutexLocker g(&m_mutex); const QString s = m_fmp; g.unlock(); return status(s, "FMP_API_KEY"); }
QVariantMap KeyStore::anthropicStatus() const { QMutexLocker g(&m_mutex); const QString s = m_anthropic; g.unlock(); return status(s, "ANTHROPIC_API_KEY"); }
