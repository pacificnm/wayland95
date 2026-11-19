#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "aiclient.h"
#include "appcompiler.h"
#include "appdatabase.h"
#include "appruntime.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)
Q_IMPORT_QML_PLUGIN(Win95AppPlugin)

// win95apprun - what runs an app made in Wayland Studio.
//
// A program the Studio makes (Make Program) is this file with the compiled
// app on its end; started as that, it runs the app. Otherwise:
//
//   win95apprun PROJECT.w95proj              run a project as it is (the Studio's Start)
//   win95apprun --check PROJECT              compile it; what's wrong, one per line (exit 1 if anything)
//   win95apprun --build PROJECT -o FILE      make the program
//   win95apprun --qml PROJECT [FORM]         the QML a form compiles to
//   win95apprun --shot PROJECT [FORM] -o FILE.png
//                                            a picture of a form as it opens (its load
//                                            code run), off the screen: what's wrong on
//                                            stderr (win95studio-mcp's show_form)
//   win95apprun --eval SCRIPT.js PROJECT     run a script with the project's database as Db and
//                                            Test.out(text) to print (tests/studio/run.sh)
//
// A project's SQLite file is kept beside the project; a program's in
// ~/.local/share/<its name> (W95APP_DATA_DIR puts it elsewhere).

namespace {

class TestHelper : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    AppDatabase *db = nullptr;
    Q_INVOKABLE void out(const QString &text) { QTextStream(stdout) << text << '\n'; }
    // SQL as it is, on the project's database ("" or what went wrong):
    // only here, for a test to make a database the Studio didn't
    Q_INVOKABLE QString sql(const QString &statement) { return db ? db->execForTest(statement) : QStringLiteral("no database"); }
};

void say(const QString &text)
{
    QTextStream(stderr) << text << '\n';
}

QJsonObject readProject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 can't be read: %2").arg(path, file.errorString());
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!doc.isObject()) {
        *error = QStringLiteral("%1 isn't a Wayland Studio project (%2).").arg(path, parse.errorString());
        return {};
    }
    return doc.object();
}

// open the app's database and bring it up to the project's tables
// (`always`: even a project with no tables yet, to read what's in it: --eval)
bool openDatabase(AppDatabase &db, const QJsonObject &compiled, const QString &dataDir, QString *error, bool always = false)
{
    const QVariantList tables = compiled.value(QLatin1String("tables")).toVariant().toList();
    db.setApis(compiled.value(QLatin1String("apis")).toVariant().toList());
    db.setSchema(tables);
    if (tables.isEmpty() && !always)
        return true;
    if (!db.open(compiled.value(QLatin1String("database")).toObject().toVariantMap(), dataDir)) {
        *error = QStringLiteral("The database didn't open.\n\n") + db.lastError();
        return false;
    }
    const QVariantMap synced = db.syncSchema();
    if (!synced.value(QStringLiteral("ok")).toBool()) {
        *error = QStringLiteral("The database's tables couldn't be set up.\n\n") + synced.value(QStringLiteral("error")).toString();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // --shot draws off the screen, with Qt Quick's software renderer
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--shot") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        }
    }
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();

    // a program: the app on the end of this file
    QJsonObject compiled;
    QString dataDir;
    const QByteArray payload = AppCompiler::payloadOf(QCoreApplication::applicationFilePath());
    const bool program = !payload.isEmpty();
    QString mode;
    QString projectPath;
    QString shotForm, shotPath;

    if (program) {
        compiled = AppCompiler::unpack(payload);
        if (compiled.isEmpty()) {
            say(QStringLiteral("This program's app can't be read (the file is damaged)."));
            return 1;
        }
    } else {
        QString outPath, script, formName;
        for (int i = 0; i < args.size(); ++i) {
            const QString a = args.at(i);
            if (a == QLatin1String("--check") || a == QLatin1String("--build") || a == QLatin1String("--qml")
                || a == QLatin1String("--shot")) {
                mode = a.mid(2);
            } else if (a == QLatin1String("--eval") && i + 1 < args.size()) {
                mode = QStringLiteral("eval");
                script = args.at(++i);
            } else if (a == QLatin1String("-o") && i + 1 < args.size()) {
                outPath = args.at(++i);
            } else if (projectPath.isEmpty()) {
                projectPath = a;
            } else {
                formName = a;
            }
        }
        if (projectPath.isEmpty()) {
            say(QStringLiteral("usage: win95apprun [--check | --build -o FILE | --qml | --shot -o FILE.png | --eval SCRIPT.js] PROJECT.w95proj"));
            return 2;
        }
        QString error;
        const QJsonObject project = readProject(projectPath, &error);
        if (project.isEmpty()) {
            say(error);
            return 1;
        }
        const QString projectDir = QFileInfo(projectPath).absolutePath();
        dataDir = projectDir;

        if (mode == QLatin1String("qml")) {
            const QJsonArray forms = project.value(QLatin1String("forms")).toArray();
            for (const QJsonValue &f : forms) {
                const QJsonObject form = f.toObject();
                if (formName.isEmpty() || form.value(QLatin1String("name")).toString() == formName)
                    QTextStream(stdout) << AppCompiler::formQml(form, project, projectDir);
            }
            return 0;
        }

        QStringList problems;
        compiled = AppCompiler::compile(project, projectDir, &problems);
        if (mode == QLatin1String("check") || mode == QLatin1String("build")) {
            // the forms' code too: each one has to compile as QML
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.setOutputWarningsToStandardError(false);
            AppDatabase db;
            AppRuntime runtime(compiled, &engine, &db);
            engine.rootContext()->setContextProperty(QStringLiteral("App"), &runtime);
            engine.rootContext()->setContextProperty(QStringLiteral("Db"), &db);
            problems += runtime.check();
            for (const QString &p : std::as_const(problems))
                QTextStream(stdout) << p << '\n';
            if (!problems.isEmpty())
                return 1;
            if (mode == QLatin1String("check"))
                return 0;
            if (outPath.isEmpty()) {
                say(QStringLiteral("--build needs -o FILE"));
                return 2;
            }
            if (!AppCompiler::writeProgram(QCoreApplication::applicationFilePath(), compiled, outPath, &error)) {
                say(error);
                return 1;
            }
            return 0;
        }
        if (mode == QLatin1String("eval")) {
            AppDatabase db;
            if (!openDatabase(db, compiled, dataDir, &error, true)) {
                say(error);
                return 1;
            }
            QFile file(script);
            if (!file.open(QIODevice::ReadOnly)) {
                say(QStringLiteral("%1 can't be read").arg(script));
                return 1;
            }
            QQmlEngine engine;
            TestHelper helper;
            helper.db = &db;
            engine.globalObject().setProperty(QStringLiteral("Db"), engine.newQObject(&db));
            engine.globalObject().setProperty(QStringLiteral("Test"), engine.newQObject(&helper));
            QQmlEngine::setObjectOwnership(&db, QQmlEngine::CppOwnership);
            QQmlEngine::setObjectOwnership(&helper, QQmlEngine::CppOwnership);
            const QJSValue result = engine.evaluate(QString::fromUtf8(file.readAll()), script);
            if (result.isError()) {
                say(QStringLiteral("%1:%2: %3").arg(script).arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString()));
                return 1;
            }
            return 0;
        }
        if (mode == QLatin1String("shot")) {
            if (outPath.isEmpty()) {
                say(QStringLiteral("--shot needs -o FILE.png"));
                return 2;
            }
            // (what's wrong is said, and the form drawn as far as it goes)
            for (const QString &p : std::as_const(problems))
                say(p);
            shotForm = formName;
            shotPath = outPath;
        } else if (!problems.isEmpty()) {
            for (const QString &p : std::as_const(problems))
                say(p);
            return 1;
        }
    }

    // --- run the app ----------------------------------------------------------
    const QJsonObject info = compiled.value(QLatin1String("app")).toObject();
    QString name = info.value(QLatin1String("name")).toString();
    if (name.isEmpty())
        name = program ? QFileInfo(QCoreApplication::applicationFilePath()).baseName() : QFileInfo(projectPath).baseName();
    app.setApplicationName(name);
    app.setApplicationDisplayName(QString());
    app.setDesktopFileName(name);
    if (program) {
        dataDir = qEnvironmentVariable("W95APP_DATA_DIR");
        if (dataDir.isEmpty())
            dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + name;
    }

    // the project's key, for its AI Chats (Project > Properties)
    AiClient::setProjectKey(compiled.value(QLatin1String("ai")).toObject().value(QLatin1String("apiKey")).toString());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.addImageProvider(QStringLiteral("app"), new AppImages(compiled.value(QLatin1String("assets")).toObject()));
    AppDatabase db;
    AppRuntime runtime(compiled, &engine, &db);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &runtime);
    engine.rootContext()->setContextProperty(QStringLiteral("Db"), &db);
    // what goes wrong in the app's code, by form, event and the event's own line
    engine.setOutputWarningsToStandardError(false);
    QObject::connect(&engine, &QQmlEngine::warnings, &runtime, [&runtime](const QList<QQmlError> &warnings) {
        for (const QQmlError &w : warnings)
            say(runtime.located(w.toString()));
    });

    QString error;
    if (!shotPath.isEmpty()) {
        // one form, drawn off the screen once its load code has run; its
        // records too if the database opens (it's the project's own)
        if (!openDatabase(db, compiled, dataDir, &error))
            say(error);
        if (shotForm.isEmpty())
            shotForm = info.value(QLatin1String("startForm")).toString();
        auto *window = qobject_cast<QQuickWindow *>(runtime.openForm(shotForm));
        if (!window) {
            // (openForm has said why)
            say(QStringLiteral("The form %1 didn't open.").arg(shotForm));
            return 1;
        }
        int status = 1;
        QTimer::singleShot(700, &app, [&]() {
            const QImage image = window->grabWindow();
            if (image.isNull())
                say(QStringLiteral("The form couldn't be drawn."));
            else if (!image.save(shotPath, "PNG"))
                say(QStringLiteral("%1 can't be written.").arg(shotPath));
            else
                status = 0;
            app.exit(status);
        });
        app.exec();
        return status;
    }
    if (!openDatabase(db, compiled, dataDir, &error) || !runtime.start(&error)) {
        say(error);
        runtime.msgBox(error, runtime.title(), QStringLiteral("error"));
        // (the message box is the only window: closing it ends the program)
        app.exec();
        return 1;
    }
    return app.exec();
}

#include "main.moc"
