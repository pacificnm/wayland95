#pragma once

#include <QHash>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QNetworkAccessManager;


// Services > Ollama > Settings: the models of the Ollama running on this
// computer, through its HTTP API (http://127.0.0.1:11434, as the user: no
// password needed). Getting a model (pull) shows its progress; one at a
// time. Exposed to QML as the singleton `Ollama`.
class OllamaModels : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Ollama)
    QML_SINGLETON

    Q_PROPERTY(bool reachable READ reachable NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    // [{ name, size, modified, family, parameters, quantization }]
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
    // [{ name, size, vram, until }]: the ones in memory now
    Q_PROPERTY(QVariantList loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool pulling READ pulling NOTIFY pullChanged)
    Q_PROPERTY(QString pullModel READ pullModel NOTIFY pullChanged)
    Q_PROPERTY(QString pullStatus READ pullStatus NOTIFY pullChanged)
    Q_PROPERTY(int pullPercent READ pullPercent NOTIFY pullChanged)    // -1: not known
    Q_PROPERTY(double pullDone READ pullDone NOTIFY pullChanged)       // bytes
    Q_PROPERTY(double pullTotal READ pullTotal NOTIFY pullChanged)

public:
    explicit OllamaModels(QObject *parent = nullptr);

    bool reachable() const { return m_reachable; }
    bool checked() const { return m_checked; }
    QString version() const { return m_version; }
    QVariantList models() const { return m_models; }
    QVariantList loaded() const { return m_loaded; }
    bool pulling() const { return m_pull; }
    QString pullModel() const { return m_pullModel; }
    QString pullStatus() const { return m_pullStatus; }
    int pullPercent() const { return m_pullPercent; }
    double pullDone() const;
    double pullTotal() const;

    Q_INVOKABLE void refresh();
    // gets a model ("qwen3.5:2b"); pullFinished(ok, message) when it's done
    Q_INVOKABLE void pull(const QString &name);
    Q_INVOKABLE void cancelPull();
    // removed(ok, message)
    Q_INVOKABLE void remove(const QString &name);
    // a name Ollama would take: letters, digits and ._-:/ , no spaces
    Q_INVOKABLE bool validName(const QString &name) const;

signals:
    void changed();
    void pullChanged();
    void pullFinished(bool ok, const QString &message);
    void removed(bool ok, const QString &message);

private:
    QNetworkReply *get(const QString &path);
    void readPull();
    void finishPull(bool ok, const QString &message);

    QNetworkAccessManager *m_net;
    bool m_reachable = false, m_checked = false;
    int m_pending = 0;
    QString m_version;
    QVariantList m_models, m_loaded;
    QPointer<QNetworkReply> m_pull;
    QByteArray m_pullBuffer;
    QString m_pullModel, m_pullStatus, m_pullError;
    int m_pullPercent = -1;
    QHash<QString, QPair<double, double>> m_layers; // digest -> completed, total
};
