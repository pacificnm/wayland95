#pragma once

#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QVariantMap>

class AppDatabase;

// A running app: `App` in its code.
//
//   App.openForm("Orders", { customer: 3 })   open a form (the form sees args)
//   App.showForm("Orders")                     the open one to the front, or open it
//   App.form("Orders")                         an open one (or null)
//   App.msgBox("Saved.")                       App.msgBox(text, title, icon, buttons, callback)
//   App.asset("song.mp3")                      a file put inside the program, as a file:// URL
//   App.quit()
//   App.name, App.title, App.version, App.formNames
class AppRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QStringList formNames READ formNames CONSTANT)

public:
    AppRuntime(const QJsonObject &compiled, QQmlEngine *engine, AppDatabase *db, QObject *parent = nullptr);

    QString name() const;
    QString title() const;
    QString version() const;
    QStringList formNames() const;

    Q_INVOKABLE QObject *openForm(const QString &name, const QVariantMap &args = QVariantMap());
    Q_INVOKABLE QObject *showForm(const QString &name, const QVariantMap &args = QVariantMap());
    Q_INVOKABLE QObject *form(const QString &name) const;
    Q_INVOKABLE void msgBox(const QString &text, const QString &title = QString(),
                            const QString &icon = QStringLiteral("info"),
                            const QStringList &buttons = { QStringLiteral("OK") },
                            const QJSValue &callback = QJSValue(), QObject *owner = nullptr);
    Q_INVOKABLE void quit();
    // a file the compiler put inside the app (a sound, a video), unpacked
    // to the program's cache the first time: its file:// URL ("" if there's none)
    Q_INVOKABLE QString asset(const QString &name);
    // (AppForm, as its window closes)
    Q_INVOKABLE void formClosed(QObject *form);

    // each form's QML compiled: what's wrong, as "Form1, button1.clicked,
    // line 2: ..." (empty if all of them are fine)
    QStringList check();
    // a message naming a line of a form's QML, with the event's name and
    // its own line number instead
    QString located(const QString &message) const;
    // open the startup form; false (and why) if it can't be
    bool start(QString *error);

private:
    QQmlComponent *component(const QString &name);
    QString describe(const QString &form, int line) const;

    QJsonObject m_compiled;
    QQmlEngine *m_engine;
    AppDatabase *m_db;
    QHash<QString, QQmlComponent *> m_components;
    QList<QPointer<QObject>> m_open;
};

// (a message box's answer on its way to the callback)
class AppMsgBoxReply : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    QJSValue callback;
public slots:
    void finished(int index);
};

// image://app/<name>: the pictures compiled into the app
class AppImages : public QQuickImageProvider
{
public:
    explicit AppImages(const QJsonObject &assets);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QJsonObject m_assets;
};
