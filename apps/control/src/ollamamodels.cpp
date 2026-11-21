#include "ollamamodels.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {
const QString base = QStringLiteral("http://127.0.0.1:11434");

QNetworkRequest request(const QString &path)
{
    QNetworkRequest r(QUrl(base + path));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return r;
}

QString errorOf(QNetworkReply *reply, const QByteArray &body)
{
    const QString e = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toString();
    if (!e.isEmpty())
        return e;
    if (reply->error() == QNetworkReply::ConnectionRefusedError)
        return QStringLiteral("Ollama isn't running. Turn it on in Services.");
    return reply->errorString();
}
} // namespace

OllamaModels::OllamaModels(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    refresh();
}

QNetworkReply *OllamaModels::get(const QString &path)
{
    QNetworkRequest r = request(path);
    r.setTransferTimeout(5000);
    return m_net->get(r);
}

void OllamaModels::refresh()
{
    if (m_pending > 0)
        return;
    m_pending = 3;
    auto done = [this] {
        if (--m_pending == 0) {
            m_checked = true;
            emit changed();
        }
    };

    QNetworkReply *v = get(QStringLiteral("/api/version"));
    connect(v, &QNetworkReply::finished, this, [this, v, done] {
        v->deleteLater();
        m_reachable = v->error() == QNetworkReply::NoError;
        m_version = m_reachable ? QJsonDocument::fromJson(v->readAll()).object().value(QStringLiteral("version")).toString() : QString();
        done();
    });

    QNetworkReply *t = get(QStringLiteral("/api/tags"));
    connect(t, &QNetworkReply::finished, this, [this, t, done] {
        t->deleteLater();
        QVariantList list;
        if (t->error() == QNetworkReply::NoError) {
            const QJsonArray models = QJsonDocument::fromJson(t->readAll()).object().value(QStringLiteral("models")).toArray();
            for (const QJsonValue &m : models) {
                const QJsonObject o = m.toObject();
                const QJsonObject d = o.value(QStringLiteral("details")).toObject();
                list << QVariantMap{
                    { QStringLiteral("name"), o.value(QStringLiteral("name")).toString() },
                    { QStringLiteral("size"), o.value(QStringLiteral("size")).toDouble() },
                    { QStringLiteral("modified"), QDateTime::fromString(o.value(QStringLiteral("modified_at")).toString(), Qt::ISODateWithMs) },
                    { QStringLiteral("family"), d.value(QStringLiteral("family")).toString() },
                    { QStringLiteral("parameters"), d.value(QStringLiteral("parameter_size")).toString() },
                    { QStringLiteral("quantization"), d.value(QStringLiteral("quantization_level")).toString() },
                };
            }
        }
        m_models = list;
        done();
    });

    QNetworkReply *p = get(QStringLiteral("/api/ps"));
    connect(p, &QNetworkReply::finished, this, [this, p, done] {
        p->deleteLater();
        QVariantList list;
        if (p->error() == QNetworkReply::NoError) {
            const QJsonArray models = QJsonDocument::fromJson(p->readAll()).object().value(QStringLiteral("models")).toArray();
            for (const QJsonValue &m : models) {
                const QJsonObject o = m.toObject();
                list << QVariantMap{
                    { QStringLiteral("name"), o.value(QStringLiteral("name")).toString() },
                    { QStringLiteral("size"), o.value(QStringLiteral("size")).toDouble() },
                    { QStringLiteral("vram"), o.value(QStringLiteral("size_vram")).toDouble() },
                    { QStringLiteral("until"), QDateTime::fromString(o.value(QStringLiteral("expires_at")).toString(), Qt::ISODateWithMs) },
                };
            }
        }
        m_loaded = list;
        done();
    });
}

bool OllamaModels::validName(const QString &name) const
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._\\-/]*(:[A-Za-z0-9._\\-]+)?$"));
    return name.size() <= 200 && re.match(name).hasMatch();
}

double OllamaModels::pullDone() const
{
    double n = 0;
    for (const auto &l : m_layers)
        n += l.first;
    return n;
}

double OllamaModels::pullTotal() const
{
    double n = 0;
    for (const auto &l : m_layers)
        n += l.second;
    return n;
}

void OllamaModels::pull(const QString &name)
{
    if (m_pull || !validName(name))
        return;
    m_pullModel = name;
    m_pullStatus = QStringLiteral("Starting...");
    m_pullError.clear();
    m_pullPercent = -1;
    m_layers.clear();
    m_pullBuffer.clear();
    const QByteArray body = QJsonDocument(QJsonObject{ { QStringLiteral("model"), name }, { QStringLiteral("stream"), true } }).toJson(QJsonDocument::Compact);
    m_pull = m_net->post(request(QStringLiteral("/api/pull")), body);
    connect(m_pull, &QNetworkReply::readyRead, this, &OllamaModels::readPull);
    connect(m_pull, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_pull;
        readPull();
        const QByteArray rest = m_pullBuffer;
        if (r->error() == QNetworkReply::OperationCanceledError)
            finishPull(false, QString());
        else if (r->error() != QNetworkReply::NoError)
            finishPull(false, m_pullError.isEmpty() ? errorOf(r, rest) : m_pullError);
        else if (!m_pullError.isEmpty())
            finishPull(false, m_pullError);
        else
            finishPull(true, m_pullModel + QStringLiteral(" is ready."));
        r->deleteLater();
    });
    emit pullChanged();
}

// one JSON object a line: {"status":"pulling 6a0746a1ec1a","digest":...,"total":N,"completed":N}
void OllamaModels::readPull()
{
    if (!m_pull)
        return;
    m_pullBuffer += m_pull->readAll();
    int nl;
    while ((nl = m_pullBuffer.indexOf('\n')) >= 0) {
        const QJsonObject o = QJsonDocument::fromJson(m_pullBuffer.left(nl)).object();
        m_pullBuffer.remove(0, nl + 1);
        if (o.contains(QStringLiteral("error"))) {
            m_pullError = o.value(QStringLiteral("error")).toString();
            continue;
        }
        const QString status = o.value(QStringLiteral("status")).toString();
        const QString digest = o.value(QStringLiteral("digest")).toString();
        if (!digest.isEmpty() && o.contains(QStringLiteral("total"))) {
            m_layers[digest] = { o.value(QStringLiteral("completed")).toDouble(), o.value(QStringLiteral("total")).toDouble() };
            m_pullStatus = QStringLiteral("Downloading...");
        } else if (status == QLatin1String("pulling manifest")) {
            m_pullStatus = QStringLiteral("Looking it up...");
        } else if (status.startsWith(QLatin1String("verifying"))) {
            m_pullStatus = QStringLiteral("Checking the download...");
        } else if (status == QLatin1String("writing manifest") || status.startsWith(QLatin1String("removing"))) {
            m_pullStatus = QStringLiteral("Finishing...");
        } else if (!status.isEmpty()) {
            m_pullStatus = status;
        }
        const double total = pullTotal();
        m_pullPercent = total > 0 ? int(100 * pullDone() / total) : -1;
    }
    emit pullChanged();
}

void OllamaModels::finishPull(bool ok, const QString &message)
{
    const QString name = m_pullModel;
    m_pull = nullptr;
    m_pullStatus.clear();
    m_pullPercent = -1;
    emit pullChanged();
    refresh();
    // (a stopped download has no message)
    if (!message.isEmpty() || ok)
        emit pullFinished(ok, ok ? message : QStringLiteral("Couldn't get ") + name + QStringLiteral(": ") + message);
}

void OllamaModels::cancelPull()
{
    if (m_pull)
        m_pull->abort();
}

void OllamaModels::remove(const QString &name)
{
    const QByteArray body = QJsonDocument(QJsonObject{ { QStringLiteral("model"), name } }).toJson(QJsonDocument::Compact);
    QNetworkReply *r = m_net->sendCustomRequest(request(QStringLiteral("/api/delete")), "DELETE", body);
    connect(r, &QNetworkReply::finished, this, [this, r, name] {
        r->deleteLater();
        const bool ok = r->error() == QNetworkReply::NoError;
        emit removed(ok, ok ? name + QStringLiteral(" has been removed.") : errorOf(r, r->readAll()));
        refresh();
    });
}
