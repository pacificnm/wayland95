#include "appruntime.h"

#include "appdatabase.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUrl>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QQuickWindow>
#include <QRegularExpression>

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
