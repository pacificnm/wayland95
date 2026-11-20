#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;
class PhotoServiceJob;

// Photo services: online places photos come from or go to (a sharing site,
// a cloud photo library, a sync folder...). None comes with Photo Gallery
// yet; this is where one plugs in.
//
// A service is a PhotoService subclass, registered once at start-up:
//
//   PhotoServices::registerService(new MyCloudPhotos);   // (main.cpp)
//
// It says what it can do (capabilities()), and Photo Gallery puts it in its
// menus: File > Import From, File > Export To and File > Share, each
// listing the services that can. Choosing one calls the matching method
// with a PhotoServiceJob, which the service reports back through, whenever
// it's done (sign-ins, uploads and downloads take their time: never block):
//
//   job->progress(40, "Uploading beach.jpg...");
//   job->finish("Shared 3 photos.");         // or job->fail("Not signed in.")
//   job->imported({"/home/me/Pictures/Cloud/a.jpg"})  // (Import: the new files)
//
// The window shows the progress in its status bar and the result in a
// message box. A service keeps its own settings and sign-in (the Keyring
// is the place for passwords and tokens).
class PhotoService
{
public:
    enum Capability {
        Import = 0x1, // brings photos into a folder here
        Export = 0x2, // copies photos there (a library, an album)
        Share = 0x4,  // sends photos to people (a link, a post)
    };

    virtual ~PhotoService() = default;
    // a short, unique name ("examplecloud") and what menus call it
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual int capabilities() const = 0;

    // into `folder` (the gallery's)
    virtual void importPhotos(const QString &folder, PhotoServiceJob *job) { Q_UNUSED(folder); fail(job); }
    virtual void exportPhotos(const QStringList &paths, PhotoServiceJob *job) { Q_UNUSED(paths); fail(job); }
    virtual void sharePhotos(const QStringList &paths, PhotoServiceJob *job) { Q_UNUSED(paths); fail(job); }

private:
    static void fail(PhotoServiceJob *job);
};

// One request to a service, reported back on. Lives until it's finished
// or failed (then it deletes itself); a service may keep a QPointer to it.
class PhotoServiceJob : public QObject
{
    Q_OBJECT
public:
    PhotoServiceJob(const QString &serviceId, const QString &action, QObject *parent);
    QString serviceId() const { return m_serviceId; }
    QString action() const { return m_action; }

public slots:
    void progress(int percent, const QString &message);
    void imported(const QStringList &paths);
    void finish(const QString &message = QString());
    void fail(const QString &error);

signals:
    void progressed(int percent, const QString &message);
    void importedPaths(const QStringList &paths);
    void done(bool ok, const QString &message);

private:
    QString m_serviceId;
    QString m_action;
    bool m_done = false;
};

// The registered services, for QML as the singleton `PhotoServices`.
class PhotoServices : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // [{ id, name, canImport, canExport, canShare }]
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit PhotoServices(QObject *parent = nullptr);
    ~PhotoServices() override;
    static PhotoServices *instance();
    static PhotoServices *create(QQmlEngine *, QJSEngine *);

    // takes ownership
    static void registerService(PhotoService *service);

    QVariantList services() const;
    bool busy() const { return m_running > 0; }

    // action: "import" (into `folder`), "export" or "share" (`paths`);
    // false if there's no such service, or it can't
    Q_INVOKABLE bool run(const QString &serviceId, const QString &action, const QStringList &paths,
                         const QString &folder);

signals:
    void servicesChanged();
    void busyChanged();
    void progress(const QString &serviceName, int percent, const QString &message);
    void imported(const QStringList &paths);
    void finished(const QString &serviceName, const QString &action, bool ok, const QString &message);

private:
    PhotoService *find(const QString &id) const;

    static QList<PhotoService *> &registry();
    static PhotoServices *s_instance;
    int m_running = 0;
};
