#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Wayland Studio's web APIs: the project's connections ("apis") and the
// requests made through them, with their sign-in. A connection:
//
//   { name: "shop", baseUrl: "https://api.example.com/v1",
//     headers: ["Accept: application/json"],
//     auth: { type: "none" | "apikey" | "basic" | "bearer" | "oauth2",
//             keyName: "X-Api-Key", keyValue: "...", keyIn: "header" | "query",   (apikey)
//             user: "...", password: "...",                                          (basic)
//             token: "...",                                                          (bearer)
//             grant: "client_credentials" | "password" | "authorization_code",       (oauth2)
//             tokenUrl, authUrl, clientId, clientSecret, scope, username, password,
//             redirectPort: 8765 } }
//
// Any of these can be `${env:NAME}`: that environment variable, when the
// app runs (a secret then isn't kept in the project, or in a program made
// from it). OAuth's tokens are kept for the user (the app's data folder,
// readable by them alone), refreshed when they run out, and asked for again
// (a sign-in in the browser, for authorization_code) when they can't be.
//
// Requests wait for their answer (as the database's do): an app's code is
// one line after another.
class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setApis(const QVariantList &apis);
    QVariantList apis() const { return m_apis; }
    QVariantMap api(const QString &name) const;

    // { ok, status, data (the JSON answer), error }. `path` is the base
    // URL's, or a whole URL; `query` adds to its query string.
    QVariantMap request(const QString &apiName, const QString &method, const QString &path,
                        const QVariantMap &query = QVariantMap(), const QVariant &body = QVariant());
    // sign in now (OAuth: get a token): { ok, error }
    QVariantMap signIn(const QString &apiName);
    // forget the connection's tokens (the next request signs in again)
    void signOut(const QString &apiName);

private:
    QString resolved(const QString &text) const;
    QVariantMap token(const QVariantMap &api, bool fresh, QString *error);
    QVariantMap tokenRequest(const QVariantMap &auth, const QList<QPair<QString, QString>> &form, QString *error);
    QVariantMap authorizationCode(const QVariantMap &api, QString *error);
    QString tokensFile() const;
    QVariantMap loadTokens() const;
    void saveTokens(const QVariantMap &tokens) const;

    QNetworkAccessManager m_net;
    QVariantList m_apis;
    QHash<QString, QVariantMap> m_tokens; // api name -> { access, refresh, expires }
};
