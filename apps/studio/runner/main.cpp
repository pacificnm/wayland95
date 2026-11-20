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
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <functional>
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
//   win95apprun --test SCRIPT.js PROJECT [--data DIR] [--timeout SECONDS] [--no-start]
//                                            run the app off the screen, muted, with a test
//                                            script driving it (Test.click, Test.type, Test.check,
//                                            Test.wait...): what happened, on stdout; exit 1 if a
//                                            check failed, the code went wrong or it timed out
//                                            (win95studio-mcp's run_test). --data: where its
//                                            SQLite file is (a copy, say)
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

// --test: the script's own `Test` is made in JavaScript (testPrelude) on
// this: what it says goes to stdout, in order with what the app says
class TestDriver : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    int passed = 0, failed = 0, errors = 0, pending = 0;
    bool finished = false;
    QElapsedTimer clock;
    std::function<void()> finish;

    static void line(const QString &text) { QTextStream out(stdout); out << text << '\n'; out.flush(); }
    Q_INVOKABLE void log(const QString &text) { line(QStringLiteral("[log] ") + text); }
    Q_INVOKABLE void check(bool ok, const QString &what)
    {
        if (ok)
            ++passed;
        else
            ++failed;
        line((ok ? QStringLiteral("[pass] ") : QStringLiteral("[FAIL] ")) + what);
    }
    Q_INVOKABLE void error(const QString &what) { ++errors; line(QStringLiteral("[error] ") + what); }
    // run `fn` in `ms` milliseconds (the app goes on meanwhile)
    Q_INVOKABLE void wait(int ms, const QJSValue &fn)
    {
        ++pending;
        QJSValue f = fn;
        QTimer::singleShot(qMax(0, ms), this, [this, f]() mutable {
            --pending;
            if (finished)
                return;
            if (f.isCallable()) {
                const QJSValue r = f.call();
                if (r.isError())
                    error(QStringLiteral("test script, line %1: %2").arg(scriptLine(r)).arg(r.toString()));
            }
            settle();
        });
    }
    Q_INVOKABLE void done() { if (!finished && finish) finish(); }
    // where in the script an error came from: the innermost frame that's
    // the script's own, not the Test helper it went through
    static int scriptLine(const QJSValue &error)
    {
        const QStringList frames = error.property(QStringLiteral("stack")).toString().split(QLatin1Char('\n'));
        static const QRegularExpression at(QStringLiteral("test\\.qml:(\\d+)"));
        for (const QString &f : frames) {
            const QRegularExpressionMatch m = at.match(f);
            if (m.hasMatch() && m.captured(1).toInt() > kPreludeLines)
                return m.captured(1).toInt() - kPreludeLines;
        }
        return error.property(QStringLiteral("lineNumber")).toInt() - kPreludeLines;
    }
    // nothing more to wait for: it's over (once what's queued has run)
    void settle()
    {
        QTimer::singleShot(150, this, [this]() { if (pending == 0 && !finished && finish) finish(); });
    }
    static int kPreludeLines;
};
int TestDriver::kPreludeLines = 0;

// what a test script can use, beside App and Db
const char *testPrelude = R"JS(var Test = (function () {
    var D = TestDriver;
    function form(f) {
        var o = typeof f === "string" ? App.form(f) : f;
        if (!o) throw new Error("the form " + f + " isn't open (Test.open(\"" + f + "\") opens it)");
        return o;
    }
    function element(f, name) {
        var o = form(f), e = o.element(name);
        if (!e) throw new Error(o.formName + " has no element named " + name);
        return e;
    }
    function kind(e) { return String(e).replace(/^App/, "").replace(/(_QMLTYPE)?_\d+.*$/, "").replace(/\(.*$/, ""); }
    function usable(e, what) {
        if (!e.enabled || !e.visible)
            throw new Error(e.form.formName + ", " + e.objectName + ": can't be " + what + " (" + (!e.visible ? "not Visible" : "not Enabled") + ")");
    }
    function str(v) { return typeof v === "string" ? v : JSON.stringify(v); }
    return {
        log: function () { D.log(Array.prototype.map.call(arguments, str).join(" ")); },
        check: function (ok, what) { D.check(!!ok, what || "(a check)"); return !!ok; },
        equal: function (got, want, what) {
            var ok = str(got) === str(want);
            D.check(ok, (what || "equal") + (ok ? ": " + str(got) : ": got " + str(got) + ", wanted " + str(want)));
            return ok;
        },
        wait: function (ms, fn) { D.wait(ms, fn); },
        done: function () { D.done(); },
        form: form,
        element: element,
        open: function (name, args) { var f = App.openForm(name, args || {}); if (!f) throw new Error("the form " + name + " didn't open"); return f; },
        // a click as a person would: a Button, CheckBox, OptionButton, Label,
        // Image, Visualizer; a Menu's or ToolBar's item by its Name; a
        // DataGrid's row (from 0)
        click: function (f, name, item) {
            var e = element(f, name), k = kind(e);
            usable(e, "clicked");
            if (k === "MenuBar" || k === "ToolBar") {
                var it = e.itemNamed(item);
                if (!it) throw new Error(e.objectName + " has no item named " + item);
                if (it.enabled === false) throw new Error(e.objectName + ": " + item + " isn't enabled");
                e.choose(it);
            } else if (k === "CheckBox") {
                e.toggle();
            } else if (k === "OptionButton") {
                e.choose();
            } else if (k === "DataGrid") {
                if (item === undefined || item < 0 || item >= e.rows.length) throw new Error(e.objectName + " has no row " + item + " (it has " + e.rows.length + ")");
                e.select(item, false);
            } else if (typeof e.clicked === "function") {
                e.clicked();
            } else {
                throw new Error(e.objectName + " (" + k + ") isn't something to click");
            }
        },
        doubleClick: function (f, name, row) {
            var e = element(f, name), k = kind(e);
            usable(e, "double-clicked");
            if (k === "DataGrid") {
                if (row === undefined || row < 0 || row >= e.rows.length) throw new Error(e.objectName + " has no row " + row);
                e.select(row, true);
            } else if (k === "ListBox") {
                e.choose(row);
                e.doubleClicked();
            } else {
                throw new Error(e.objectName + " (" + k + ") has nothing to double-click");
            }
        },
        // typing into a TextBox (all of what's there replaced), then Enter if `enter`
        type: function (f, name, text, enter) {
            var e = element(f, name);
            usable(e, "typed in");
            if (e.readOnly) throw new Error(e.objectName + " is Locked");
            e.text = String(text);
            e.changed();
            if (enter) e.accepted();
        },
        // a ComboBox's or ListBox's item (its index from 0, or its text); a Slider's value
        pick: function (f, name, what) {
            var e = element(f, name), k = kind(e);
            usable(e, "changed");
            if (k === "Slider") {
                e.moved(Number(what));
                return;
            }
            var i = typeof what === "number" ? what : (e.items || []).indexOf(String(what));
            if (i < 0) throw new Error(e.objectName + " has no item " + str(what) + " (" + str(e.items) + ")");
            e.choose(i);
        },
        // a Timer's timer event, now
        tick: function (f, name) { var e = element(f, name); e.count += 1; e.timer(); },
        // the next message box's answer (a button's text or index; else the first),
        // and the next file box's (a path or a list; else it's cancelled)
        answer: function (a) { App.testAnswer(a); },
        choose: function (paths) { App.testChoose(paths); },
        messages: function () { return App.testMessages(); },
        lastMessage: function () { var m = App.testMessages(); return m.length ? m[m.length - 1] : ""; }
    };
})();
)JS";

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
        if (qstrcmp(argv[i], "--shot") == 0 || qstrcmp(argv[i], "--test") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        }
        // (a picture or a test run never shows on the desktop's sound menu)
        if (qstrcmp(argv[i], "--shot") == 0 || qstrcmp(argv[i], "--test") == 0 || qstrcmp(argv[i], "--check") == 0)
            qputenv("W95APP_NO_MPRIS", "1");
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
    QString testScript, testData;
    double testTimeout = 20;
    bool testStart = true;

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
            } else if (a == QLatin1String("--test") && i + 1 < args.size()) {
                mode = QStringLiteral("test");
                script = args.at(++i);
            } else if (a == QLatin1String("--data") && i + 1 < args.size()) {
                testData = args.at(++i);
            } else if (a == QLatin1String("--timeout") && i + 1 < args.size()) {
                testTimeout = args.at(++i).toDouble();
            } else if (a == QLatin1String("--no-start")) {
                testStart = false;
            } else if (a == QLatin1String("-o") && i + 1 < args.size()) {
                outPath = args.at(++i);
            } else if (projectPath.isEmpty()) {
                projectPath = a;
            } else {
                formName = a;
            }
        }
        if (projectPath.isEmpty()) {
            say(QStringLiteral("usage: win95apprun [--check | --build -o FILE | --qml | --shot -o FILE.png | --eval SCRIPT.js | --test SCRIPT.js [--data DIR] [--timeout S] [--no-start]] PROJECT.w95proj"));
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
        if (mode == QLatin1String("test")) {
            if (!problems.isEmpty()) {
                for (const QString &p : std::as_const(problems))
                    QTextStream(stdout) << "[error] " << p << '\n';
                return 1;
            }
            testScript = script;
            if (!testData.isEmpty())
                dataDir = testData;
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
    if (!testScript.isEmpty()) {
        // --- a test run ------------------------------------------------------
        QFile file(testScript);
        if (!file.open(QIODevice::ReadOnly)) {
            say(QStringLiteral("%1 can't be read").arg(testScript));
            return 2;
        }
        runtime.setTesting(true);
        TestDriver driver;
        driver.clock.start();
        // everything the app says, in order: console.log, warnings, mistakes in its code
        static TestDriver *current = &driver;
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
            // (Qt's own chatter: FFmpeg's decoders and the like)
            if (context.category && qstrncmp(context.category, "qt.", 3) == 0)
                return;
            if (type == QtDebugMsg || type == QtInfoMsg)
                TestDriver::line(QStringLiteral("[app] ") + msg);
            else
                current->error(msg);
        });
        QObject::disconnect(&engine, &QQmlEngine::warnings, nullptr, nullptr);
        QObject::connect(&engine, &QQmlEngine::warnings, &driver, [&runtime, &driver](const QList<QQmlError> &warnings) {
            for (const QQmlError &w : warnings)
                driver.error(runtime.located(w.toString()));
        });
        QObject::connect(&runtime, &AppRuntime::testSaid, &driver, [](const QString &what) { TestDriver::line(what); });
        int status = 0;
        driver.finish = [&]() {
            driver.finished = true;
            const double secs = driver.clock.elapsed() / 1000.0;
            TestDriver::line(QStringLiteral("Finished in %1 s: %2 passed, %3 failed, %4 %5.")
                                 .arg(secs, 0, 'f', 1).arg(driver.passed).arg(driver.failed).arg(driver.errors)
                                 .arg(driver.errors == 1 ? QStringLiteral("error") : QStringLiteral("errors")));
            status = driver.failed || driver.errors ? 1 : 0;
            app.exit(status);
        };
        QTimer::singleShot(int(qMax(1.0, testTimeout) * 1000), &driver, [&]() {
            if (driver.finished)
                return;
            driver.error(QStringLiteral("it timed out after %1 s (a Test.wait still waiting: Test.done() ends it)").arg(testTimeout));
            driver.finish();
        });
        if (!openDatabase(db, compiled, dataDir, &error)) {
            TestDriver::line(QStringLiteral("[error] ") + error);
            return 1;
        }
        engine.rootContext()->setContextProperty(QStringLiteral("TestDriver"), &driver);
        const QString startForm = info.value(QLatin1String("startForm")).toString();
        if (testStart) {
            if (!runtime.openForm(startForm)) {
                TestDriver::line(QStringLiteral("[error] the start form %1 didn't open").arg(startForm));
                return 1;
            }
            TestDriver::line(QStringLiteral("Test run: %1 open").arg(startForm));
        } else {
            TestDriver::line(QStringLiteral("Test run: no form open yet"));
        }
        // the script, inside a QML object so App, Db and the forms are in reach
        const QString prelude = QString::fromUtf8(testPrelude);
        TestDriver::kPreludeLines = int(prelude.count(QLatin1Char('\n'))) + 3;
        const QString qml = QStringLiteral("import QtQuick\nQtObject {\nfunction run() {\n") + prelude
                          + QString::fromUtf8(file.readAll()) + QStringLiteral("\n}\n}\n");
        QQmlComponent component(&engine);
        component.setData(qml.toUtf8(), QUrl(QStringLiteral("file:///test.qml")));
        QObject *runner = component.create(engine.rootContext());
        if (!runner) {
            for (const QQmlError &e : component.errors())
                driver.error(QStringLiteral("test script, line %1: %2").arg(e.line() - TestDriver::kPreludeLines).arg(e.description()));
            driver.finish();
            return status;
        }
        // (after the forms' load code has run)
        QTimer::singleShot(100, &driver, [&]() {
            QJSValue fn = engine.toScriptValue(runner).property(QStringLiteral("run"));
            const QJSValue r = fn.callWithInstance(engine.toScriptValue(runner));
            if (r.isError())
                driver.error(QStringLiteral("test script, line %1: %2").arg(TestDriver::scriptLine(r)).arg(r.toString()));
            driver.settle();
        });
        app.exec();
        qInstallMessageHandler(nullptr);
        return status;
    }
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
