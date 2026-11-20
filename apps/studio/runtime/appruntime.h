#pragma once

#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQueue>
#include <QVariantMap>

class AppDatabase;
class QMediaPlayer;
class QTimer;

// A running app: `App` in its code.
//
//   App.openForm("Orders", { customer: 3 })   open a form (the form sees args)
//   App.showForm("Orders")                     the open one to the front, or open it
//   App.form("Orders")                         an open one (or null)
//   App.msgBox("Saved.")                       App.msgBox(text, title, icon, buttons, callback)
//   App.asset("song.mp3")                      a file put inside the program, as a file:// URL
//   App.quit()
//   App.name, App.title, App.version, App.formNames
//
// Files and folders on the computer:
//
//   App.chooseFile(options, function (path) {})       the Open box ("" if cancelled)
//   App.chooseFiles(options, function (paths) {})     several at once ([] if cancelled)
//   App.chooseFolder(options, function (path) {})     Select Folder
//   App.chooseSaveFile(options, function (path) {})   Save As (asks before replacing)
//       options: { title, folder, name, filters: ["Music (*.mp3 *.flac)", "All Files (*.*)"], suffix }
//   App.listFiles(folder, "*.mp3 *.flac", recursive)  the files' paths, in order
//   App.fileInfo(path)                                 { exists, isFile, isDir, path, name, baseName, suffix, folder, size, modified }
//   App.readTags(path, function (tags) {})            a sound's or video's own title, artist,
//                                                     album, cover art... (without playing it)
class AppRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QStringList formNames READ formNames CONSTANT)
    // a test run (win95apprun --test): message and file boxes are answered
    // by the test, said as they come; MediaPlayers are muted
    Q_PROPERTY(bool testing READ testing CONSTANT)

public:
    AppRuntime(const QJsonObject &compiled, QQmlEngine *engine, AppDatabase *db, QObject *parent = nullptr);

    QString name() const;
    QString title() const;
    QString version() const;
    QStringList formNames() const;
    bool testing() const { return m_testing; }
    void setTesting(bool on) { m_testing = on; }
    // (a test's) the next message box's answer (a button's index or text),
    // the next file box's (paths; [] cancels it)
    Q_INVOKABLE void testAnswer(const QVariant &answer) { m_answers.enqueue(answer); }
    Q_INVOKABLE void testChoose(const QVariant &paths) { m_chosen.enqueue(paths); }
    Q_INVOKABLE QStringList testMessages() const { return m_messages; }

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

    // the Win95 Open, Save As and Select Folder boxes, in a window of their
    // own; the answer goes to the callback. They return the box's window.
    Q_INVOKABLE QObject *chooseFile(const QVariantMap &options = QVariantMap(), const QJSValue &callback = QJSValue());
    Q_INVOKABLE QObject *chooseFiles(const QVariantMap &options = QVariantMap(), const QJSValue &callback = QJSValue());
    Q_INVOKABLE QObject *chooseFolder(const QVariantMap &options = QVariantMap(), const QJSValue &callback = QJSValue());
    Q_INVOKABLE QObject *chooseSaveFile(const QVariantMap &options = QVariantMap(), const QJSValue &callback = QJSValue());
    // the files in a folder whose names match (a pattern or several: "*.mp3 *.flac",
    // or a list), in its folders too with recursive; sorted the way people
    // count ("2" before "10"); at most 20000
    Q_INVOKABLE QStringList listFiles(const QString &folder, const QJSValue &patterns = QJSValue(),
                                      bool recursive = false) const;
    Q_INVOKABLE QVariantMap fileInfo(const QString &path) const;
    // a media file's tags, read one file at a time (a list of paths waits its turn)
    Q_INVOKABLE void readTags(const QString &path, const QJSValue &callback);

    // each form's QML compiled: what's wrong, as "Form1, button1.clicked,
    // line 2: ..." (empty if all of them are fine)
    QStringList check();
    // a message naming a line of a form's QML, with the event's name and
    // its own line number instead
    QString located(const QString &message) const;
    // open the startup form; false (and why) if it can't be
    bool start(QString *error);

signals:
    // (a test run's) a message box or a file box, as it was answered
    void testSaid(const QString &what);

private:
    QQmlComponent *component(const QString &name);
    QString describe(const QString &form, int line) const;
    QObject *choose(const QString &mode, bool multiple, const QVariantMap &options, const QJSValue &callback);
    void nextTags();
    void tagsDone(const QVariantMap &tags);

    QJsonObject m_compiled;
    QQmlEngine *m_engine;
    AppDatabase *m_db;
    QHash<QString, QQmlComponent *> m_components;
    QList<QPointer<QObject>> m_open;
    QPointer<QQmlComponent> m_fileWindow;
    bool m_testing = false;
    QQueue<QVariant> m_answers, m_chosen;
    QStringList m_messages;
    // readTags: what's waiting, and the player that reads one at a time
    struct TagRequest { QString path; QJSValue callback; };
    QQueue<TagRequest> m_tagQueue;
    QMediaPlayer *m_tagPlayer = nullptr;
    QTimer *m_tagTimeout = nullptr;
    bool m_tagBusy = false;
};

// (a file box's answer on its way to the callback)
class AppChooseReply : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    QJSValue callback;
    bool multiple = false;
    QPointer<QQmlEngine> engine;
public slots:
    void answered(bool ok, const QVariant &paths);
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
