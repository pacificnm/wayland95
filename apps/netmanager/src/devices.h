#pragma once

#include <QHash>
#include <QJSValue>
#include <QUrl>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QWebEngineCertificateError>

// Network Manager's devices: the network equipment whose web pages it logs
// in to (routers, switches, printers...). Their details are kept in
// ~/.config/win95-wayland/win95netmanager/devices.json; their passwords in
// the desktop's keyring (the Secret Service), never in a file. Exposed to
// QML as the singleton `Devices`.
//
// A device: { id, name, kind, protocol ("http"/"https"), host, port (0: the
// protocol's), path, username, fillLogin, autoLogin, notes, fingerprint
// (the SHA-256 of the certificate you chose to trust, for a device whose
// own certificate no one vouches for), lastUsed, url }
class Devices : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)

public:
    explicit Devices(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool keyringAvailable() const;

    Q_INVOKABLE QVariantMap device(const QString &id) const;
    // a new device (no id) or changes to one: its id, or "" if the details
    // won't do (error(): why)
    Q_INVOKABLE QString save(const QVariantMap &device);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void setFingerprint(const QString &id, const QString &fingerprint);
    Q_INVOKABLE void touch(const QString &id);
    // what's wrong with these details: "" if nothing
    Q_INVOKABLE QString check(const QVariantMap &device) const;
    // the address its web page is at
    Q_INVOKABLE QString urlOf(const QVariantMap &device) const;
    // whether a page (an address the view went to) is this device's
    Q_INVOKABLE bool isDevicePage(const QString &id, const QString &url) const;

    // the keyring: done(found, password) / done(ok, error)
    Q_INVOKABLE void password(const QString &id, const QJSValue &done);
    Q_INVOKABLE void setPassword(const QString &id, const QString &password, const QJSValue &done);
    Q_INVOKABLE void forgetPassword(const QString &id);

    // the certificates of the pages a view (a WebEngineView) has a problem
    // with, kept by address: watch(view) once; then, in its certificateError
    // handler (after Qt.callLater: this hears the signal after QML does),
    // the certificate's SHA-256 fingerprint ("AB:CD:...") and who it says it is
    Q_INVOKABLE void watch(QObject *view);
    Q_INVOKABLE QString fingerprint(const QUrl &url) const;
    Q_INVOKABLE QString certificateSubject(const QUrl &url) const;

    // this network's router (the default gateway), for "Add your router"
    Q_INVOKABLE QString gateway() const;

private slots:
    void certificateProblem(const QWebEngineCertificateError &error);

signals:
    void changed();

private:
    void load();
    void write();
    QVariantMap complete(QVariantMap d) const;
    QString filePath() const;

    QVariantList m_devices;
    QHash<QString, QPair<QString, QString>> m_certificates; // url -> fingerprint, subject
};
