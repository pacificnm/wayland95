#pragma once

// The two API keys (FMP and Anthropic) in the desktop keyring (apps/common/secrets.h), never in a file: upstream kept
// them in its settings table encrypted with Electron's safeStorage. The keyring answers asynchronously on the GUI
// thread, so the keys are read once at start-up and kept in memory for the worker threads.

#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantMap>

class KeyStore : public QObject
{
    Q_OBJECT
public:
    explicit KeyStore(QObject *parent = nullptr);

    // starts reading both keys; `ready` is emitted when both lookups have answered
    void load();
    bool isLoaded() const;

    // thread-safe; the stored key, else the environment variable upstream also read (FMP_API_KEY, ANTHROPIC_API_KEY)
    QString fmpKey() const;
    QString anthropicKey() const;
    void setFmpKey(const QString &key);
    void setAnthropicKey(const QString &key);
    void clearFmpKey();
    void clearAnthropicKey();
    // {source: 'stored' | 'env' | 'none', encrypted}
    QVariantMap fmpStatus() const;
    QVariantMap anthropicStatus() const;

signals:
    void ready();

private:
    void store(const QString &service, const QString &key);
    void forget(const QString &service);
    QVariantMap status(const QString &stored, const char *env) const;
    mutable QMutex m_mutex;
    QString m_fmp, m_anthropic;
    int m_pending = 0;
    bool m_loaded = false;
};
