#include "appruntime.h"

#include "appdatabase.h"
#include "appmpris.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUrl>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QCollator>
#include <QDirIterator>
#include <QGuiApplication>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QTimer>
#include <QImage>
#include <QDateTime>

AppRuntime::AppRuntime(const QJsonObject &compiled, QQmlEngine *engine, AppDatabase *db, QObject *parent)
    : QObject(parent)
    , m_compiled(compiled)
    , m_engine(engine)
    , m_db(db)
{
}

QString AppRuntime::name() const
{
    return m_compiled.value(QLatin1String("app")).toObject().value(QLatin1String("name")).toString();
}

QString AppRuntime::title() const
{
    const QString t = m_compiled.value(QLatin1String("app")).toObject().value(QLatin1String("title")).toString();
    return t.isEmpty() ? name() : t;
}

QString AppRuntime::version() const
{
    return m_compiled.value(QLatin1String("app")).toObject().value(QLatin1String("version")).toString();
}

QStringList AppRuntime::formNames() const
{
    QStringList names;
    const QJsonArray order = m_compiled.value(QLatin1String("order")).toArray();
    for (const QJsonValue &n : order)
        names << n.toString();
    return names;
}

QQmlComponent *AppRuntime::component(const QString &name)
{
    if (QQmlComponent *made = m_components.value(name))
        return made;
    const QJsonObject forms = m_compiled.value(QLatin1String("forms")).toObject();
    if (!forms.contains(name))
        return nullptr;
    auto *c = new QQmlComponent(m_engine, this);
    c->setData(forms.value(name).toString().toUtf8(), QUrl(QStringLiteral("qrc:/w95app/%1.qml").arg(name)));
    m_components.insert(name, c);
    return c;
}

// "Form1, button1.clicked, line 2" for a line of Form1's QML
QString AppRuntime::describe(const QString &form, int line) const
{
    const QJsonArray lines = m_compiled.value(QLatin1String("lines")).toObject().value(form).toArray();
    for (const QJsonValue &l : lines) {
        const QJsonObject range = l.toObject();
        const int from = range.value(QLatin1String("from")).toInt();
        if (line >= from && line <= range.value(QLatin1String("to")).toInt())
            return QStringLiteral("%1, %2, line %3").arg(form, range.value(QLatin1String("what")).toString()).arg(line - from + 1);
    }
    return form;
}

QString AppRuntime::located(const QString &message) const
{
    static const QRegularExpression where(QStringLiteral("qrc:/w95app/([A-Za-z0-9_]+)\\.qml(?::(\\d+))?(?::\\d+)?"));
    QString out = message;
    int from = 0;
    for (;;) {
        const QRegularExpressionMatch match = where.match(out, from);
        if (!match.hasMatch())
            break;
        const QString with = describe(match.captured(1), match.captured(2).toInt());
        out.replace(match.capturedStart(), match.capturedLength(), with);
        from = match.capturedStart() + with.size();
    }
    return out;
}

QStringList AppRuntime::check()
{
    QStringList problems;
    for (const QString &name : formNames()) {
        QQmlComponent *c = component(name);
        if (!c || !c->isError())
            continue;
        const QList<QQmlError> errors = c->errors();
        for (const QQmlError &e : errors)
            problems << describe(name, e.line()) + QStringLiteral(": ") + e.description();
    }
    return problems;
}

QObject *AppRuntime::openForm(const QString &name, const QVariantMap &args)
{
    QQmlComponent *c = component(name);
    if (!c) {
        qWarning("App.openForm: there is no form named %s", qPrintable(name));
        return nullptr;
    }
    if (c->isError()) {
        for (const QQmlError &e : c->errors())
            qWarning("%s", qPrintable(describe(name, e.line()) + QStringLiteral(": ") + e.description()));
        return nullptr;
    }
    QObject *form = c->createWithInitialProperties({ { QStringLiteral("args"), args } }, m_engine->rootContext());
    if (!form) {
        for (const QQmlError &e : c->errors())
            qWarning("%s", qPrintable(describe(name, e.line()) + QStringLiteral(": ") + e.description()));
        return nullptr;
    }
    form->setParent(this);
    QQmlEngine::setObjectOwnership(form, QQmlEngine::CppOwnership);
    m_open.append(form);
    form->setProperty("visible", true);
    if (auto *window = qobject_cast<QQuickWindow *>(form))
        window->requestActivate();
    return form;
}

QString AppRuntime::asset(const QString &name)
{
    const QJsonObject assets = m_compiled.value(QLatin1String("assets")).toObject();
    if (!assets.contains(name))
        return QString();
    const QByteArray data = QByteArray::fromBase64(assets.value(name).toString().toLatin1());
    // (one folder per app; a file of another size is a new one)
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/media");
    const QString path = dir + QLatin1Char('/') + QFileInfo(name).fileName();
    if (QFileInfo(path).size() != data.size()) {
        QDir().mkpath(dir);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            qWarning("App.asset: %s can't be written", qPrintable(path));
            return QString();
        }
    }
    return QUrl::fromLocalFile(path).toString();
}

QObject *AppRuntime::showForm(const QString &name, const QVariantMap &args)
{
    QObject *open = form(name);
    if (!open)
        return openForm(name, args);
    open->setProperty("visible", true);
    if (auto *window = qobject_cast<QQuickWindow *>(open)) {
        if (window->visibility() == QWindow::Minimized)
            window->showNormal();
        window->requestActivate();
    }
    return open;
}

QObject *AppRuntime::form(const QString &name) const
{
    for (const QPointer<QObject> &f : m_open) {
        if (f && f->property("formName").toString() == name)
            return f;
    }
    return nullptr;
}

void AppRuntime::formClosed(QObject *form)
{
    m_open.removeAll(form);
    if (form)
        form->deleteLater();
}

void AppMsgBoxReply::finished(int index)
{
    if (callback.isCallable()) {
        const QJSValue result = callback.call({ QJSValue(index) });
        if (result.isError())
            qWarning("%s", qPrintable(result.toString()));
    }
    if (sender())
        sender()->deleteLater();
    deleteLater();
}

void AppRuntime::msgBox(const QString &text, const QString &title, const QString &icon,
                        const QStringList &buttons, const QJSValue &callback, QObject *owner)
{
    if (m_testing) {
        // answered by the test (the first button if it said nothing), as a
        // person would: after the code that showed it has gone on
        const QStringList shown = buttons.isEmpty() ? QStringList{ QStringLiteral("OK") } : buttons;
        int index = 0;
        if (!m_answers.isEmpty()) {
            const QVariant a = m_answers.dequeue();
            if (a.metaType().id() == QMetaType::QString) {
                index = qMax(0, shown.indexOf(a.toString()));
            } else {
                index = qBound(0, a.toInt(), int(shown.size()) - 1);
            }
        }
        m_messages << text;
        emit testSaid(QStringLiteral("[message %1] \"%2\": %3  (%4) -> %5")
                          .arg(icon, title.isEmpty() ? this->title() : title, QString(text).replace(QLatin1Char('\n'), QStringLiteral(" / ")),
                               shown.join(QStringLiteral(" / ")), shown.at(index)));
        QJSValue cb = callback;
        QMetaObject::invokeMethod(this, [this, cb, index]() mutable {
            if (cb.isCallable()) {
                const QJSValue result = cb.call({ QJSValue(index) });
                if (result.isError())
                    qWarning("%s", qPrintable(located(result.property(QStringLiteral("fileName")).toString() + QLatin1Char(':')
                                                      + result.property(QStringLiteral("lineNumber")).toString() + QStringLiteral(": ")
                                                      + result.toString())));
            }
        }, Qt::QueuedConnection);
        return;
    }
    static QPointer<QQmlComponent> component;
    if (!component)
        component = new QQmlComponent(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Win95App/AppMessageWindow.qml")), this);
    QObject *box = component->createWithInitialProperties({
        { QStringLiteral("message"), text },
        { QStringLiteral("title"), title.isEmpty() ? this->title() : title },
        { QStringLiteral("icon"), icon },
        { QStringLiteral("buttons"), buttons.isEmpty() ? QStringList{ QStringLiteral("OK") } : buttons },
    }, m_engine->rootContext());
    if (!box) {
        qWarning("%s\n%s", qPrintable(text), qPrintable(component->errorString()));
        return;
    }
    QQmlEngine::setObjectOwnership(box, QQmlEngine::CppOwnership);
    auto *reply = new AppMsgBoxReply(this);
    reply->callback = callback;
    connect(box, SIGNAL(finished(int)), reply, SLOT(finished(int)));
    if (auto *window = qobject_cast<QQuickWindow *>(box)) {
        if (auto *parent = qobject_cast<QWindow *>(owner))
            window->setTransientParent(parent);
        window->setVisible(true);
    }
}

// --- files and folders ------------------------------------------------------

namespace {

QString expandHome(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    return path;
}

// "Music (*.mp3 *.flac)" or { label, patterns } -> { label, patterns }
QVariantList fileFilters(const QVariant &given)
{
    QVariantList out;
    const QVariantList list = given.toList();
    static const QRegularExpression inParens(QStringLiteral("\\(([^)]*)\\)\\s*$"));
    for (const QVariant &f : list) {
        QVariantMap filter;
        if (f.metaType().id() == QMetaType::QVariantMap) {
            filter = f.toMap();
            if (filter.value(QStringLiteral("patterns")).metaType().id() == QMetaType::QString)
                filter.insert(QStringLiteral("patterns"),
                              filter.value(QStringLiteral("patterns")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts));
        } else {
            const QString text = f.toString().trimmed();
            if (text.isEmpty())
                continue;
            const QRegularExpressionMatch m = inParens.match(text);
            const QString patterns = m.hasMatch() ? m.captured(1) : text;
            QStringList p = patterns.split(QRegularExpression(QStringLiteral("[\\s;,]+")), Qt::SkipEmptyParts);
            for (QString &one : p) {
                if (one == QLatin1String("*.*"))
                    one = QStringLiteral("*");
            }
            filter.insert(QStringLiteral("label"), m.hasMatch() ? text : text + QStringLiteral(" (") + patterns + QLatin1Char(')'));
            filter.insert(QStringLiteral("patterns"), p);
        }
        if (filter.value(QStringLiteral("patterns")).toStringList().isEmpty())
            filter.insert(QStringLiteral("patterns"), QStringList{ QStringLiteral("*") });
        out << filter;
    }
    if (out.isEmpty())
        out << QVariantMap{ { QStringLiteral("label"), QStringLiteral("All Files (*.*)") },
                            { QStringLiteral("patterns"), QStringList{ QStringLiteral("*") } } };
    return out;
}

} // namespace

void AppChooseReply::answered(bool ok, const QVariant &paths)
{
    const QStringList list = ok ? paths.toStringList() : QStringList();
    if (callback.isCallable()) {
        QJSValue arg;
        if (multiple) {
            arg = engine ? engine->toScriptValue(list) : QJSValue();
        } else {
            arg = QJSValue(list.isEmpty() ? QString() : list.first());
        }
        const QJSValue result = callback.call({ arg });
        if (result.isError())
            qWarning("%s", qPrintable(result.toString()));
    }
    if (sender())
        sender()->deleteLater();
    deleteLater();
}

QObject *AppRuntime::choose(const QString &mode, bool multiple, const QVariantMap &options, const QJSValue &callback)
{
    if (m_testing) {
        // answered by the test (Test.choose), or cancelled
        QStringList paths;
        if (!m_chosen.isEmpty()) {
            const QVariant a = m_chosen.dequeue();
            paths = a.metaType().id() == QMetaType::QString ? QStringList{ a.toString() } : a.toStringList();
        }
        for (QString &p : paths)
            p = expandHome(p);
        if (!multiple && paths.size() > 1)
            paths = paths.mid(0, 1);
        const QString box = mode == QLatin1String("folder") ? QStringLiteral("Select Folder")
                          : mode == QLatin1String("save") ? QStringLiteral("Save As") : QStringLiteral("Open");
        const QString title = options.value(QStringLiteral("title")).toString();
        emit testSaid(QStringLiteral("[file box] %1%2 -> %3").arg(box, title.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(title),
                                                               paths.isEmpty() ? QStringLiteral("cancelled") : paths.join(QStringLiteral(", "))));
        auto *reply = new AppChooseReply(this);
        reply->callback = callback;
        reply->multiple = multiple;
        reply->engine = m_engine;
        QMetaObject::invokeMethod(reply, [reply, paths]() { reply->answered(!paths.isEmpty(), paths); }, Qt::QueuedConnection);
        return nullptr;
    }
    if (!m_fileWindow)
        m_fileWindow = new QQmlComponent(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Win95App/AppFileWindow.qml")), this);
    QString folder = expandHome(options.value(QStringLiteral("folder")).toString());
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QDir::homePath();
    QObject *box = m_fileWindow->createWithInitialProperties({
        { QStringLiteral("mode"), mode },
        { QStringLiteral("multiple"), multiple },
        { QStringLiteral("dialogTitle"), options.value(QStringLiteral("title")).toString() },
        { QStringLiteral("startFolder"), folder },
        { QStringLiteral("startName"), options.value(QStringLiteral("name")).toString() },
        { QStringLiteral("filters"), mode == QLatin1String("folder") ? QVariantList() : fileFilters(options.value(QStringLiteral("filters"))) },
        { QStringLiteral("suffix"), options.value(QStringLiteral("suffix")).toString().remove(QLatin1Char('.')) },
    }, m_engine->rootContext());
    if (!box) {
        qWarning("App.choose: %s", qPrintable(m_fileWindow->errorString()));
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(box, QQmlEngine::CppOwnership);
    auto *reply = new AppChooseReply(this);
    reply->callback = callback;
    reply->multiple = multiple;
    reply->engine = m_engine;
    connect(box, SIGNAL(answered(bool,QVariant)), reply, SLOT(answered(bool,QVariant)));
    if (auto *window = qobject_cast<QQuickWindow *>(box)) {
        if (QWindow *owner = QGuiApplication::focusWindow())
            window->setTransientParent(owner);
        window->setVisible(true);
        window->requestActivate();
    }
    return box;
}

QObject *AppRuntime::chooseFile(const QVariantMap &options, const QJSValue &callback)
{
    return choose(QStringLiteral("open"), false, options, callback);
}

QObject *AppRuntime::chooseFiles(const QVariantMap &options, const QJSValue &callback)
{
    return choose(QStringLiteral("open"), true, options, callback);
}

QObject *AppRuntime::chooseFolder(const QVariantMap &options, const QJSValue &callback)
{
    return choose(QStringLiteral("folder"), false, options, callback);
}

QObject *AppRuntime::chooseSaveFile(const QVariantMap &options, const QJSValue &callback)
{
    return choose(QStringLiteral("save"), false, options, callback);
}

QStringList AppRuntime::listFiles(const QString &folder, const QJSValue &patterns, bool recursive) const
{
    const QString dir = expandHome(folder);
    QStringList filters;
    if (patterns.isArray()) {
        const int n = patterns.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < n; ++i)
            filters += patterns.property(quint32(i)).toString().split(QRegularExpression(QStringLiteral("[\\s;,]+")), Qt::SkipEmptyParts);
    } else if (patterns.isString()) {
        filters = patterns.toString().split(QRegularExpression(QStringLiteral("[\\s;,]+")), Qt::SkipEmptyParts);
    }
    if (filters.isEmpty() || filters.contains(QStringLiteral("*.*")))
        filters = { QStringLiteral("*") };
    QStringList out;
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return out;
    QDirIterator it(dir, filters, QDir::Files | QDir::Readable,
                    recursive ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks : QDirIterator::NoIteratorFlags);
    while (it.hasNext() && out.size() < 20000)
        out << it.next();
    // (each folder's files before its folders' would be Windows' order;
    // a path's own order keeps an album's songs together and in order)
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(out.begin(), out.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return out;
}

QVariantMap AppRuntime::fileInfo(const QString &path) const
{
    const QFileInfo info(expandHome(path));
    return {
        { QStringLiteral("exists"), info.exists() },
        { QStringLiteral("isFile"), info.isFile() },
        { QStringLiteral("isDir"), info.isDir() },
        { QStringLiteral("path"), info.exists() ? info.absoluteFilePath() : info.filePath() },
        { QStringLiteral("name"), info.fileName() },
        { QStringLiteral("baseName"), info.completeBaseName() },
        { QStringLiteral("suffix"), info.suffix().toLower() },
        { QStringLiteral("folder"), info.absolutePath() },
        { QStringLiteral("size"), info.exists() ? QVariant(info.size()) : QVariant(0) },
        { QStringLiteral("modified"), info.exists() ? QVariant(info.lastModified().toString(Qt::ISODate)) : QVariant(QString()) },
    };
}

// --- a media file's tags -------------------------------------------------------

void AppRuntime::readTags(const QString &path, const QJSValue &callback)
{
    m_tagQueue.enqueue({ path, callback });
    if (!m_tagBusy)
        QMetaObject::invokeMethod(this, &AppRuntime::nextTags, Qt::QueuedConnection);
}

void AppRuntime::nextTags()
{
    if (m_tagBusy || m_tagQueue.isEmpty())
        return;
    m_tagBusy = true;
    if (!m_tagPlayer) {
        m_tagPlayer = new QMediaPlayer(this);
        m_tagTimeout = new QTimer(this);
        m_tagTimeout->setSingleShot(true);
        m_tagTimeout->setInterval(8000);
        connect(m_tagTimeout, &QTimer::timeout, this, [this] {
            tagsDone({ { QStringLiteral("error"), QStringLiteral("It took too long to read.") } });
        });
        connect(m_tagPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (!m_tagBusy || !m_tagTimeout->isActive())
                return;
            if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
                const QMediaMetaData m = m_tagPlayer->metaData();
                auto text = [&](QMediaMetaData::Key key) { return m.stringValue(key).trimmed(); };
                QString artist = text(QMediaMetaData::ContributingArtist);
                if (artist.isEmpty())
                    artist = text(QMediaMetaData::Author);
                const QDateTime date = m.value(QMediaMetaData::Date).toDateTime();
                int year = date.isValid() ? date.date().year() : 0;
                if (!year)
                    year = text(QMediaMetaData::Date).left(4).toInt();
                tagsDone({
                    { QStringLiteral("title"), text(QMediaMetaData::Title) },
                    { QStringLiteral("artist"), artist },
                    { QStringLiteral("album"), text(QMediaMetaData::AlbumTitle) },
                    { QStringLiteral("albumArtist"), text(QMediaMetaData::AlbumArtist) },
                    { QStringLiteral("genre"), text(QMediaMetaData::Genre) },
                    // ("3", or "3/8": the third of eight)
                    { QStringLiteral("trackNumber"), m.stringValue(QMediaMetaData::TrackNumber).section(QLatin1Char('/'), 0, 0).trimmed().toInt() },
                    { QStringLiteral("year"), year },
                    { QStringLiteral("comment"), text(QMediaMetaData::Comment) },
                    { QStringLiteral("duration"), m_tagPlayer->duration() },
                    { QStringLiteral("hasVideo"), m_tagPlayer->hasVideo() },
                    { QStringLiteral("hasAudio"), m_tagPlayer->hasAudio() },
                    // (its own picture, saved in the program's cache: an Image's picture)
                    { QStringLiteral("coverArt"), [&]() {
                          QVariant art = m.value(QMediaMetaData::CoverArtImage);
                          if (art.value<QImage>().isNull())
                              art = m.value(QMediaMetaData::ThumbnailImage);
                          return MprisService::imageUrl(art);
                      }() },
                    { QStringLiteral("error"), QString() },
                });
            } else if (status == QMediaPlayer::InvalidMedia) {
                const QString why = m_tagPlayer->errorString();
                tagsDone({ { QStringLiteral("error"), why.isEmpty() ? QStringLiteral("It can't be read.") : why } });
            }
        });
    }
    const QString path = m_tagQueue.head().path.trimmed();
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    const QUrl url = scheme.match(path).hasMatch() && !path.startsWith(QLatin1String("file://"))
        ? QUrl(path) : QUrl::fromLocalFile(expandHome(path));
    if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile()) {
        tagsDone({ { QStringLiteral("error"), QStringLiteral("There's no such file.") } });
        return;
    }
    m_tagTimeout->start();
    m_tagPlayer->setSource(QUrl());
    m_tagPlayer->setSource(url);
}

void AppRuntime::tagsDone(const QVariantMap &given)
{
    if (m_tagTimeout)
        m_tagTimeout->stop();
    const TagRequest request = m_tagQueue.dequeue();
    QVariantMap tags = given;
    tags.insert(QStringLiteral("path"), request.path);
    tags.insert(QStringLiteral("ok"), tags.value(QStringLiteral("error")).toString().isEmpty());
    for (const char *key : { "title", "artist", "album", "albumArtist", "genre", "comment", "coverArt" }) {
        if (!tags.contains(QLatin1String(key)))
            tags.insert(QLatin1String(key), QString());
    }
    for (const char *key : { "trackNumber", "year", "duration" }) {
        if (!tags.contains(QLatin1String(key)))
            tags.insert(QLatin1String(key), 0);
    }
    if (m_tagPlayer)
        m_tagPlayer->setSource(QUrl());
    m_tagBusy = false;
    if (request.callback.isCallable()) {
        const QJSValue result = request.callback.call({ m_engine->toScriptValue(tags) });
        if (result.isError())
            qWarning("%s", qPrintable(located(result.property(QStringLiteral("fileName")).toString() + QLatin1Char(':')
                                              + result.property(QStringLiteral("lineNumber")).toString() + QStringLiteral(": ")
                                              + result.toString())));
    }
    if (!m_tagQueue.isEmpty())
        QMetaObject::invokeMethod(this, &AppRuntime::nextTags, Qt::QueuedConnection);
}

void AppRuntime::quit()
{
    // (queued: from a form's load event the program's event loop hasn't started yet)
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
}

bool AppRuntime::start(QString *error)
{
    const QString first = m_compiled.value(QLatin1String("app")).toObject().value(QLatin1String("startForm")).toString();
    if (first.isEmpty()) {
        *error = QStringLiteral("This app has no forms.");
        return false;
    }
    if (!openForm(first)) {
        QQmlComponent *c = component(first);
        *error = c && c->isError() ? located(c->errorString()) : QStringLiteral("The form %1 didn't open.").arg(first);
        return false;
    }
    return true;
}

AppImages::AppImages(const QJsonObject &assets)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_assets(assets)
{
}

QImage AppImages::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = QImage::fromData(QByteArray::fromBase64(m_assets.value(id).toString().toLatin1()));
    if (size)
        *size = image.size();
    if (requestedSize.isValid() && !image.isNull())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
