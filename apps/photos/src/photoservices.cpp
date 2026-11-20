#include "photoservices.h"

#include <QVariantMap>

void PhotoService::fail(PhotoServiceJob *job)
{
    job->fail(QStringLiteral("This service can't do that."));
}

// ---- PhotoServiceJob --------------------------------------------------------

PhotoServiceJob::PhotoServiceJob(const QString &serviceId, const QString &action, QObject *parent)
    : QObject(parent)
    , m_serviceId(serviceId)
    , m_action(action)
{
}

void PhotoServiceJob::progress(int percent, const QString &message)
{
    if (!m_done)
        emit progressed(qBound(0, percent, 100), message);
}

void PhotoServiceJob::imported(const QStringList &paths)
{
    if (!m_done && !paths.isEmpty())
        emit importedPaths(paths);
}

void PhotoServiceJob::finish(const QString &message)
{
    if (m_done)
        return;
    m_done = true;
    emit done(true, message);
    deleteLater();
}

void PhotoServiceJob::fail(const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit done(false, error);
    deleteLater();
}

// ---- PhotoServices ----------------------------------------------------------

PhotoServices *PhotoServices::s_instance = nullptr;

QList<PhotoService *> &PhotoServices::registry()
{
    static QList<PhotoService *> list;
    return list;
}

PhotoServices::PhotoServices(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

PhotoServices::~PhotoServices()
{
    if (s_instance == this)
        s_instance = nullptr;
}

PhotoServices *PhotoServices::instance()
{
    if (!s_instance)
        s_instance = new PhotoServices;
    return s_instance;
}

PhotoServices *PhotoServices::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void PhotoServices::registerService(PhotoService *service)
{
    if (!service)
        return;
    registry().append(service);
    if (s_instance)
        emit s_instance->servicesChanged();
}

QVariantList PhotoServices::services() const
{
    QVariantList list;
    for (const PhotoService *s : std::as_const(registry())) {
        const int caps = s->capabilities();
        list.append(QVariantMap{
            {QStringLiteral("id"), s->id()},
            {QStringLiteral("name"), s->name()},
            {QStringLiteral("canImport"), bool(caps & PhotoService::Import)},
            {QStringLiteral("canExport"), bool(caps & PhotoService::Export)},
            {QStringLiteral("canShare"), bool(caps & PhotoService::Share)},
        });
    }
    return list;
}

PhotoService *PhotoServices::find(const QString &id) const
{
    for (PhotoService *s : std::as_const(registry()))
        if (s->id() == id)
            return s;
    return nullptr;
}

bool PhotoServices::run(const QString &serviceId, const QString &action, const QStringList &paths,
                        const QString &folder)
{
    PhotoService *service = find(serviceId);
    if (!service)
        return false;
    const int caps = service->capabilities();
    const PhotoService::Capability need = action == QLatin1String("import") ? PhotoService::Import
            : action == QLatin1String("export") ? PhotoService::Export : PhotoService::Share;
    if (!(caps & need) || (need != PhotoService::Import && paths.isEmpty()))
        return false;

    const QString name = service->name();
    auto *job = new PhotoServiceJob(serviceId, action, this);
    connect(job, &PhotoServiceJob::progressed, this, [this, name](int percent, const QString &message) {
        emit progress(name, percent, message);
    });
    connect(job, &PhotoServiceJob::importedPaths, this, &PhotoServices::imported);
    connect(job, &PhotoServiceJob::done, this, [this, name, action](bool ok, const QString &message) {
        --m_running;
        emit busyChanged();
        emit finished(name, action, ok, message);
    });
    ++m_running;
    emit busyChanged();
    if (need == PhotoService::Import)
        service->importPhotos(folder, job);
    else if (need == PhotoService::Export)
        service->exportPhotos(paths, job);
    else
        service->sharePhotos(paths, job);
    return true;
}
