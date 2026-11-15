#pragma once

#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantMap>

// a portal file filter: a name and patterns, each (0 = glob, 1 = MIME type)
struct PortalFilter
{
    QString name;
    QList<std::pair<uint, QString>> patterns;
};
Q_DECLARE_METATYPE(PortalFilter)
QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &f);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &f);

class FileChooserPortal;

// org.freedesktop.impl.portal.Request at a request's handle: the portal
// calls Close() when the program gives up on the dialog
class PortalRequest : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    PortalRequest(FileChooserPortal *portal, const QString &handle);

public slots:
    void Close();

private:
    FileChooserPortal *m_portal;
    QString m_handle;
};

// org.freedesktop.impl.portal.FileChooser: xdg-desktop-portal asks here
// when a program (VS Code, a browser, any Flatpak app) wants an Open or
// Save dialog, and the Wayland 95 one appears: Open (one file or several),
// Save As, or choosing a folder.
class FileChooserPortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")

public:
    FileChooserPortal(QQmlEngine *engine, QObject *parent = nullptr);

    void close(const QString &handle);
    // from the dialog window
    Q_INVOKABLE void done(const QString &key, bool ok, const QStringList &paths, int filterIndex);
    bool idle() const { return m_requests.isEmpty(); }

public slots:
    uint OpenFile(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFile(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFiles(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                   const QString &title, const QVariantMap &options, QVariantMap &results);

signals:
    void idleChanged();

private:
    struct Request {
        QDBusMessage call;
        QPointer<QObject> window;
        QList<PortalFilter> filters;
        PortalRequest *object = nullptr;
    };
    void open(const QString &mode, const QDBusObjectPath &handle, const QString &title, const QVariantMap &options);
    void finished(const QString &handle, bool ok, const QStringList &paths, int filterIndex);

    QQmlEngine *m_engine;
    QHash<QString, Request> m_requests;
};
