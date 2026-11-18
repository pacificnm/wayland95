#include "apiclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString base64url(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString randomText(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    for (char &c : data)
        c = char(QRandomGenerator::system()->bounded(256));
    return base64url(data);
}

// what an error answer says about itself
QString messageIn(const QVariant &data)
{
    const QVariantMap map = data.toMap();
    for (const char *key : { "error_description", "message", "detail", "error", "title" }) {
        const QVariant v = map.value(QLatin1String(key));
        if (v.typeId() == QMetaType::QString && !v.toString().isEmpty())
            return v.toString();
        if (v.typeId() == QMetaType::QVariantMap)
            return messageIn(v);
    }
    return QString();
}

// every object key in a JSON text, in the order they first come (a
// QJsonObject has them sorted; a table's fields want the API's own order)
QStringList keysInOrder(const QByteArray &json)
{
    QStringList keys;
    int depth = 0;
    for (int i = 0; i < json.size(); ++i) {
        const char c = json.at(i);
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
        } else if (c == '"') {
            int j = i + 1;
            QByteArray s;
            for (; j < json.size() && json.at(j) != '"'; ++j) {
                if (json.at(j) == '\\' && j + 1 < json.size())
                    ++j;
                s += json.at(j);
            }
            int k = j + 1;
            while (k < json.size() && (json.at(k) == ' ' || json.at(k) == '\n' || json.at(k) == '\r' || json.at(k) == '\t'))
                ++k;
            if (k < json.size() && json.at(k) == ':') {
                const QString key = QString::fromUtf8(s);
                if (!keys.contains(key))
                    keys << key;
            }
            i = j;
        }
    }
    return keys;
}

} // namespace

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
{
}

void ApiClient::setApis(const QVariantList &apis)
{
    m_apis = apis;
}

QVariantMap ApiClient::api(const QString &name) const
{
    for (const QVariant &a : m_apis) {
        if (a.toMap().value(QStringLiteral("name")).toString() == name)
            return a.toMap();
    }
    return {};
}

// ${env:NAME}: that environment variable
QString ApiClient::resolved(const QString &text) const
{
    static const QRegularExpression env(QStringLiteral("\\$\\{env:([A-Za-z_][A-Za-z0-9_]*)\\}"));
    QString out = text;
    QRegularExpressionMatch m;
    while ((m = env.match(out)).hasMatch())
        out.replace(m.capturedStart(), m.capturedLength(), qEnvironmentVariable(m.captured(1).toLatin1().constData()));
    return out;
}

QVariantMap ApiClient::request(const QString &apiName, const QString &method, const QString &path,
                               const QVariantMap &query, const QVariant &body)
{
    auto failed = [](const QString &error, int status = 0) {
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("status"), status },
                            { QStringLiteral("error"), error } };
    };
    const QVariantMap api = this->api(apiName);
    if (api.isEmpty())
        return failed(QStringLiteral("There is no API named %1 in the project.").arg(apiName));
    const QVariantMap auth = api.value(QStringLiteral("auth")).toMap();
    const QString type = auth.value(QStringLiteral("type")).toString();

    for (int attempt = 0; attempt < 2; ++attempt) {
        // the URL: the base's, and this path
        static const QRegularExpression whole(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
        QString address = resolved(path);
        if (!whole.match(address).hasMatch()) {
            QString base = resolved(api.value(QStringLiteral("baseUrl")).toString());
            while (base.endsWith(QLatin1Char('/')))
                base.chop(1);
            address = base + (address.startsWith(QLatin1Char('/')) || address.isEmpty() ? address : QLatin1Char('/') + address);
        }
        QUrl url(address);
        QUrlQuery q(url);
        for (auto it = query.cbegin(); it != query.cend(); ++it)
            q.addQueryItem(it.key(), it.value().toString());
        if (type == QLatin1String("apikey") && auth.value(QStringLiteral("keyIn")).toString() == QLatin1String("query"))
            q.addQueryItem(resolved(auth.value(QStringLiteral("keyName")).toString()), resolved(auth.value(QStringLiteral("keyValue")).toString()));
        url.setQuery(q);
        if (!url.isValid() || url.scheme().isEmpty())
            return failed(QStringLiteral("%1 isn't a web address.").arg(address));

        QNetworkRequest req(url);
        req.setRawHeader("Accept", "application/json");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        for (const QVariant &h : api.value(QStringLiteral("headers")).toList()) {
            const QString line = resolved(h.toString());
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0)
                req.setRawHeader(line.left(colon).trimmed().toUtf8(), line.mid(colon + 1).trimmed().toUtf8());
        }
        if (type == QLatin1String("apikey") && auth.value(QStringLiteral("keyIn")).toString() != QLatin1String("query")) {
            req.setRawHeader(resolved(auth.value(QStringLiteral("keyName"), QStringLiteral("X-Api-Key")).toString()).toUtf8(),
                             resolved(auth.value(QStringLiteral("keyValue")).toString()).toUtf8());
        } else if (type == QLatin1String("basic")) {
            const QString pair = resolved(auth.value(QStringLiteral("user")).toString()) + QLatin1Char(':')
                               + resolved(auth.value(QStringLiteral("password")).toString());
            req.setRawHeader("Authorization", "Basic " + pair.toUtf8().toBase64());
        } else if (type == QLatin1String("bearer")) {
            req.setRawHeader("Authorization", "Bearer " + resolved(auth.value(QStringLiteral("token")).toString()).toUtf8());
        } else if (type == QLatin1String("oauth2")) {
            QString error;
            const QVariantMap t = token(api, attempt > 0, &error);
            if (!error.isEmpty())
                return failed(error);
            req.setRawHeader("Authorization", "Bearer " + t.value(QStringLiteral("access")).toString().toUtf8());
        }

        QByteArray data;
        // (a JavaScript object, from an app's code, comes as a QJSValue)
        const QVariant sent = body.metaType() == QMetaType::fromType<QJSValue>() ? body.value<QJSValue>().toVariant() : body;
        if (sent.isValid() && !sent.isNull()) {
            data = QJsonDocument::fromVariant(sent).toJson(QJsonDocument::Compact);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        }
        QNetworkReply *reply = m_net.sendCustomRequest(req, method.toUpper().toUtf8(), data);
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(30000);
        loop.exec();
        if (!reply->isFinished()) {
            reply->abort();
            reply->deleteLater();
            return failed(QStringLiteral("%1 didn't answer (30 seconds).").arg(url.host()));
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const QString netError = reply->errorString();
        const bool networkFailed = status == 0 && reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        // a token that ran out: once more, with a new one
        if (status == 401 && type == QLatin1String("oauth2") && attempt == 0)
            continue;
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parse);
        const QVariant answer = parse.error == QJsonParseError::NoError ? doc.toVariant() : QVariant(QString::fromUtf8(raw));
        if (networkFailed)
            return failed(netError);
        if (status < 200 || status >= 300) {
            const QString said = messageIn(answer);
            return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("status"), status }, { QStringLiteral("data"), answer },
                                { QStringLiteral("error"), QStringLiteral("%1 said %2%3").arg(url.host()).arg(status)
                                                               .arg(said.isEmpty() ? QString() : QStringLiteral(": ") + said) } };
        }
        return { { QStringLiteral("ok"), true }, { QStringLiteral("status"), status }, { QStringLiteral("data"), answer },
                 { QStringLiteral("keyOrder"), keysInOrder(raw.left(65536)) } };
    }
    return failed(QStringLiteral("%1 didn't take the sign-in.").arg(apiName), 401);
}

QVariantMap ApiClient::signIn(const QString &apiName)
{
    const QVariantMap api = this->api(apiName);
    if (api.isEmpty())
        return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("There is no API named %1.").arg(apiName) } };
    if (api.value(QStringLiteral("auth")).toMap().value(QStringLiteral("type")).toString() != QLatin1String("oauth2"))
        return { { QStringLiteral("ok"), true } };
    QString error;
    token(api, true, &error);
    return { { QStringLiteral("ok"), error.isEmpty() }, { QStringLiteral("error"), error } };
}

void ApiClient::signOut(const QString &apiName)
{
    const QVariantMap api = this->api(apiName);
    const QString key = apiName + QLatin1Char(' ') + api.value(QStringLiteral("baseUrl")).toString();
    m_tokens.remove(key);
    QVariantMap all = loadTokens();
    all.remove(key);
    saveTokens(all);
}

// --- OAuth 2.0 -------------------------------------------------------------------

QString ApiClient::tokensFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/api-tokens.json");
}

QVariantMap ApiClient::loadTokens() const
{
    QFile file(tokensFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void ApiClient::saveTokens(const QVariantMap &tokens) const
{
    QDir().mkpath(QFileInfo(tokensFile()).absolutePath());
    QSaveFile file(tokensFile());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject::fromVariantMap(tokens)).toJson());
    file.commit();
}

// a token the API takes: the one kept, a refreshed one, or a new one
QVariantMap ApiClient::token(const QVariantMap &api, bool fresh, QString *error)
{
    const QString key = api.value(QStringLiteral("name")).toString() + QLatin1Char(' ') + api.value(QStringLiteral("baseUrl")).toString();
    if (!m_tokens.contains(key)) {
        const QVariantMap kept = loadTokens().value(key).toMap();
        if (!kept.isEmpty())
            m_tokens.insert(key, kept);
    }
    QVariantMap t = m_tokens.value(key);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 expires = t.value(QStringLiteral("expires")).toLongLong();
    // (a new one a little before it runs out: 30 seconds, or a quarter of a short one's life)
    const qint64 margin = qMin<qint64>(30, t.value(QStringLiteral("lasts")).toLongLong() / 4);
    if (!fresh && !t.value(QStringLiteral("access")).toString().isEmpty() && (expires == 0 || now < expires - margin))
        return t;

    const QVariantMap auth = api.value(QStringLiteral("auth")).toMap();
    auto r = [&](const char *k) { return resolved(auth.value(QLatin1String(k)).toString()); };
    QVariantMap got;
    QString why;
    if (!t.value(QStringLiteral("refresh")).toString().isEmpty()) {
        got = tokenRequest(auth, { { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
                                   { QStringLiteral("refresh_token"), t.value(QStringLiteral("refresh")).toString() } }, &why);
    }
    if (got.isEmpty()) {
        const QString grant = auth.value(QStringLiteral("grant"), QStringLiteral("client_credentials")).toString();
        why.clear();
        if (grant == QLatin1String("client_credentials")) {
            got = tokenRequest(auth, { { QStringLiteral("grant_type"), QStringLiteral("client_credentials") } }, &why);
        } else if (grant == QLatin1String("password")) {
            got = tokenRequest(auth, { { QStringLiteral("grant_type"), QStringLiteral("password") },
                                       { QStringLiteral("username"), r("username") },
                                       { QStringLiteral("password"), r("password") } }, &why);
        } else {
            got = authorizationCode(api, &why);
        }
    }
    if (got.isEmpty()) {
        *error = QStringLiteral("Signing in to %1 didn't work: %2").arg(api.value(QStringLiteral("name")).toString(), why);
        return {};
    }
    if (got.value(QStringLiteral("refresh")).toString().isEmpty())
        got.insert(QStringLiteral("refresh"), t.value(QStringLiteral("refresh")));
    m_tokens.insert(key, got);
    QVariantMap all = loadTokens();
    all.insert(key, got);
    saveTokens(all);
    return got;
}

// POST the token URL: { access, refresh, expires } ({} and why, if not)
QVariantMap ApiClient::tokenRequest(const QVariantMap &auth, const QList<QPair<QString, QString>> &form, QString *error)
{
    QUrlQuery body;
    for (const auto &[k, v] : form)
        body.addQueryItem(k, v);
    auto r = [&](const char *k) { return resolved(auth.value(QLatin1String(k)).toString()); };
    body.addQueryItem(QStringLiteral("client_id"), r("clientId"));
    if (!r("clientSecret").isEmpty())
        body.addQueryItem(QStringLiteral("client_secret"), r("clientSecret"));
    if (!r("scope").isEmpty())
        body.addQueryItem(QStringLiteral("scope"), r("scope"));
    QNetworkRequest req(QUrl(r("tokenUrl")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_net.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        *error = QStringLiteral("its token URL didn't answer");
        return {};
    }
    const QVariantMap answer = QJsonDocument::fromJson(reply->readAll()).object().toVariantMap();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString netError = reply->errorString();
    reply->deleteLater();
    const QString access = answer.value(QStringLiteral("access_token")).toString();
    if (access.isEmpty()) {
        const QString said = messageIn(answer);
        *error = !said.isEmpty() ? said : status ? QStringLiteral("its token URL said %1").arg(status) : netError;
        return {};
    }
    const qint64 lasts = answer.value(QStringLiteral("expires_in")).toLongLong();
    return { { QStringLiteral("access"), access },
             { QStringLiteral("refresh"), answer.value(QStringLiteral("refresh_token")) },
             { QStringLiteral("expires"), lasts > 0 ? QDateTime::currentSecsSinceEpoch() + lasts : 0 },
             { QStringLiteral("lasts"), lasts } };
}

// the browser signs in, and comes back to us with a code (PKCE)
QVariantMap ApiClient::authorizationCode(const QVariantMap &api, QString *error)
{
    const QVariantMap auth = api.value(QStringLiteral("auth")).toMap();
    auto r = [&](const char *k) { return resolved(auth.value(QLatin1String(k)).toString()); };
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, quint16(auth.value(QStringLiteral("redirectPort")).toInt()))) {
        *error = QStringLiteral("no port to come back to (%1)").arg(server.errorString());
        return {};
    }
    const QString redirect = QStringLiteral("http://127.0.0.1:%1/callback").arg(server.serverPort());
    const QString verifier = randomText(48);
    const QString state = randomText(16);
    QUrl url(r("authUrl"));
    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("client_id"), r("clientId"));
    q.addQueryItem(QStringLiteral("redirect_uri"), redirect);
    if (!r("scope").isEmpty())
        q.addQueryItem(QStringLiteral("scope"), r("scope"));
    q.addQueryItem(QStringLiteral("state"), state);
    q.addQueryItem(QStringLiteral("code_challenge"), base64url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)));
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    url.setQuery(q);

    QString code, why;
    QEventLoop loop;
    connect(&server, &QTcpServer::newConnection, &loop, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, &loop, [&, socket, buffer]() {
            *buffer += socket->readAll();
            if (!buffer->contains("\r\n\r\n"))
                return;
            // GET /callback?code=...&state=... HTTP/1.1
            const QList<QByteArray> line = buffer->left(buffer->indexOf("\r\n")).split(' ');
            const QUrl back(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(line.value(1)));
            const QUrlQuery answer(back);
            if (back.path() != QLatin1String("/callback")) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            if (answer.queryItemValue(QStringLiteral("state")) != state)
                why = QStringLiteral("the answer wasn't to this sign-in");
            else if (answer.hasQueryItem(QStringLiteral("error")))
                why = answer.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded).isEmpty()
                    ? answer.queryItemValue(QStringLiteral("error")) : answer.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded);
            else
                code = answer.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
            const QByteArray page = code.isEmpty()
                ? "<html><body><h3>Signing in didn't work.</h3><p>You can close this window.</p></body></html>"
                : "<html><body><h3>Signed in.</h3><p>You can close this window and go back to the program.</p></body></html>";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                          + QByteArray::number(page.size()) + "\r\n\r\n" + page);
            socket->disconnectFromHost();
            loop.quit();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    // the browser (W95_OAUTH_BROWSER: a command to give the address to instead, for tests)
    const QString browser = qEnvironmentVariable("W95_OAUTH_BROWSER");
    if (!browser.isEmpty()) {
        QStringList args = QProcess::splitCommand(browser);
        const QString program = args.takeFirst();
        QProcess::startDetached(program, args << url.toString(QUrl::FullyEncoded));
    } else if (!QDesktopServices::openUrl(url)) {
        *error = QStringLiteral("the browser didn't open %1").arg(url.toString());
        return {};
    }
    QTimer::singleShot(300000, &loop, &QEventLoop::quit);
    loop.exec();
    if (code.isEmpty()) {
        *error = why.isEmpty() ? QStringLiteral("no answer from the browser (5 minutes)") : why;
        return {};
    }
    return tokenRequest(auth, { { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
                                { QStringLiteral("code"), code },
                                { QStringLiteral("redirect_uri"), redirect },
                                { QStringLiteral("code_verifier"), verifier } }, error);
}
