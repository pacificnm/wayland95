// win95studio-mcp - Wayland Studio's tools for Claude: an MCP server (the
// Model Context Protocol, JSON-RPC on stdin and stdout) that makes and
// changes Wayland Studio projects, the .w95proj files the Studio edits.
//
//   claude mcp add wayland-studio -- win95studio-mcp
//
// Every change is checked as the Studio would (names, element types,
// their properties and events, field types, Validation Rules), written
// the way the Studio writes it, and then compiled (AppCompiler) to say
// what's wrong. The runner (win95apprun) does the rest: --check (the
// code compiled too), --shot (a picture of a form), --build, and running
// the app. An open Studio reloads a project that changes on disk.

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <string>

#include "appcompiler.h"
#include "appdatabase.h"
#include "fieldrules.h"

Q_IMPORT_QML_PLUGIN(Win95AppPlugin) // (elements.json is among its resources)

namespace {

// ------------------------------------------------------------------ results

struct Result
{
    QJsonArray content;
    bool error = false;
};

Result text(const QString &t, bool error = false)
{
    Result r;
    r.content.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), t } });
    r.error = error;
    return r;
}
Result fail(const QString &t) { return text(t, true); }

QString pretty(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
    return v.toVariant().toString();
}

// ------------------------------------------------------------------- files

QString resolve(const QString &path)
{
    QString p = path.trimmed();
    if (p == QLatin1String("~") || p.startsWith(QLatin1String("~/")))
        p = QDir::homePath() + p.mid(1);
    return QDir::cleanPath(QDir::current().absoluteFilePath(p));
}

// the project at `path`, or why not
bool load(const QString &path, QJsonObject *project, QString *error)
{
    QFile file(path);
    if (!file.exists()) {
        *error = QStringLiteral("%1 doesn't exist (new_project makes one).").arg(path);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 can't be read: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!doc.isObject() || !doc.object().contains(QLatin1String("forms"))) {
        *error = QStringLiteral("%1 isn't a Wayland Studio project.").arg(path);
        return false;
    }
    *project = doc.object();
    if (project->value(QLatin1String("format")).toInt(1) > 1) {
        *error = QStringLiteral("%1 was made by a newer Wayland Studio.").arg(path);
        return false;
    }
    if (!project->contains(QLatin1String("tables")))
        project->insert(QStringLiteral("tables"), QJsonArray());
    return true;
}

// as the Studio writes it (StudioTools::saveProject)
bool save(const QString &path, const QJsonObject &project, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        *error = QStringLiteral("%1 can't be written: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// the runner: beside this program (a build tree, an install), else on PATH
QString runner()
{
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/win95apprun");
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(QStringLiteral("win95apprun"));
}

struct Ran
{
    bool started = false;
    int exitCode = -1;
    QString out, err;
};

Ran runRunner(const QStringList &args, int timeoutMs = 60000)
{
    Ran r;
    const QString exe = runner();
    if (exe.isEmpty()) {
        r.err = QStringLiteral("win95apprun (Wayland Studio's runner) isn't installed.");
        return r;
    }
    QProcess p;
    // (none of --check, --build, --shot needs the screen)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    p.setProcessEnvironment(env);
    p.start(exe, args);
    if (!p.waitForStarted(10000)) {
        r.err = QStringLiteral("win95apprun didn't start: %1").arg(p.errorString());
        return r;
    }
    r.started = true;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.err = QStringLiteral("win95apprun took too long (%1 s) and was stopped.").arg(timeoutMs / 1000);
        return r;
    }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    r.err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return r;
}

// ---------------------------------------------------------------- registry

QJsonObject registry() { return AppCompiler::registry(); }

QJsonObject elementInfo(const QString &type)
{
    for (const QJsonValue &e : registry().value(QLatin1String("elements")).toArray()) {
        if (e.toObject().value(QLatin1String("type")).toString() == type)
            return e.toObject();
    }
    return {};
}

QStringList elementTypes()
{
    QStringList out;
    for (const QJsonValue &e : registry().value(QLatin1String("elements")).toArray())
        out << e.toObject().value(QLatin1String("type")).toString();
    return out;
}

QStringList namesOf(const QJsonArray &list)
{
    QStringList out;
    for (const QJsonValue &v : list)
        out << v.toObject().value(QLatin1String("name")).toString();
    return out;
}

// is `v` what a property of this kind holds? ("" if so)
QString valueProblem(const QJsonObject &prop, const QJsonValue &v)
{
    const QString type = prop.value(QLatin1String("type")).toString();
    if (type == QLatin1String("bool"))
        return v.isBool() ? QString() : QStringLiteral("true or false");
    if (type == QLatin1String("int"))
        return v.isDouble() && v.toDouble() == double(qint64(v.toDouble())) ? QString() : QStringLiteral("a whole number");
    if (type == QLatin1String("real"))
        return v.isDouble() ? QString() : QStringLiteral("a number");
    if (type == QLatin1String("list")) {
        if (!v.isArray())
            return QStringLiteral("a list of strings");
        for (const QJsonValue &x : v.toArray()) {
            if (!x.isString())
                return QStringLiteral("a list of strings");
        }
        return QString();
    }
    if (type == QLatin1String("menu") || type == QLatin1String("toolbar"))
        return v.isArray() ? QString() : QStringLiteral("a list of items (studio_reference, topic format: Menus and toolbars)");
    if (type == QLatin1String("graph"))
        return v.isObject() ? QString() : QStringLiteral("an object (studio_manual forms.md: Graph)");
    if (type == QLatin1String("icon"))
        return v.isString() && (v.toString().isEmpty() || AppCompiler::isIcon(v.toString()))
                   ? QString() : QStringLiteral("one of Wayland 95's icons (studio_reference, topic elements: Icons)");
    if (!v.isString())
        return QStringLiteral("a string");
    if (type == QLatin1String("enum")) {
        const QJsonArray values = prop.value(QLatin1String("values")).toArray();
        QStringList allowed;
        for (const QJsonValue &x : values)
            allowed << x.toString();
        if (!allowed.isEmpty() && !allowed.contains(v.toString()))
            return QStringLiteral("one of: %1").arg(allowed.join(QStringLiteral(", ")));
    }
    if (type == QLatin1String("color") && !v.toString().isEmpty()) {
        static const QRegularExpression hex(QStringLiteral("^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
        static const QRegularExpression word(QStringLiteral("^[a-zA-Z]+$"));
        if (!hex.match(v.toString()).hasMatch() && !word.match(v.toString()).hasMatch())
            return QStringLiteral("a colour: #rrggbb or a name (\"navy\")");
    }
    return QString();
}

// the property `name` of a list of property descriptions
QJsonObject propNamed(const QJsonArray &props, const QString &name)
{
    for (const QJsonValue &p : props) {
        if (p.toObject().value(QLatin1String("name")).toString() == name)
            return p.toObject();
    }
    return {};
}

void checkProps(const QString &where, const QJsonArray &known, const QJsonObject &props, QStringList *errors)
{
    for (auto it = props.begin(); it != props.end(); ++it) {
        const QJsonObject p = propNamed(known, it.key());
        if (p.isEmpty()) {
            errors->append(QStringLiteral("%1: there's no property \"%2\". Its properties: %3.")
                               .arg(where, it.key(), namesOf(known).join(QStringLiteral(", "))));
            continue;
        }
        const QString problem = valueProblem(p, it.value());
        if (!problem.isEmpty())
            errors->append(QStringLiteral("%1: %2 is %3.").arg(where, it.key(), problem));
    }
}

void checkEvents(const QString &where, const QJsonArray &known, const QJsonObject &events, QStringList *errors,
                 bool general = false)
{
    QStringList names = namesOf(known);
    if (general)
        names.prepend(QStringLiteral("general"));
    for (auto it = events.begin(); it != events.end(); ++it) {
        if (!names.contains(it.key()))
            errors->append(QStringLiteral("%1: there's no event \"%2\". Its events: %3.")
                               .arg(where, it.key(), names.isEmpty() ? QStringLiteral("(none)") : names.join(QStringLiteral(", "))));
        else if (!it.value().isString())
            errors->append(QStringLiteral("%1: the code for %2 is a string.").arg(where, it.key()));
    }
}

bool isNumber(const QJsonValue &v) { return v.isDouble(); }

// an element, its defaults filled in; what's wrong in `errors`
QJsonObject checkElement(const QString &formName, QJsonObject el, QStringList *errors)
{
    const QString type = el.value(QLatin1String("type")).toString();
    const QString name = el.value(QLatin1String("name")).toString();
    const QString where = QStringLiteral("%1, %2").arg(formName, name.isEmpty() ? QStringLiteral("(no name)") : name);
    const QJsonObject info = elementInfo(type);
    if (info.isEmpty()) {
        errors->append(QStringLiteral("%1: there's no element type \"%2\". The types: %3.")
                           .arg(where, type, elementTypes().join(QStringLiteral(", "))));
        return el;
    }
    const QString nameProblem = AppCompiler::nameProblem(name, true);
    if (!nameProblem.isEmpty())
        errors->append(QStringLiteral("%1: %2").arg(where, nameProblem));
    static const QStringList keys{ QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("x"), QStringLiteral("y"),
                                   QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("props"),
                                   QStringLiteral("events") };
    for (auto it = el.begin(); it != el.end(); ++it) {
        if (!keys.contains(it.key()))
            errors->append(QStringLiteral("%1: \"%2\" isn't part of an element (%3). Its properties go in props.")
                               .arg(where, it.key(), keys.join(QStringLiteral(", "))));
    }
    const QJsonArray size = info.value(QLatin1String("size")).toArray();
    if (!el.contains(QLatin1String("width")))
        el.insert(QStringLiteral("width"), size.at(0).toInt(80));
    if (!el.contains(QLatin1String("height")))
        el.insert(QStringLiteral("height"), size.at(1).toInt(24));
    for (const char *k : { "x", "y" }) {
        if (!el.contains(QLatin1String(k)))
            el.insert(QLatin1String(k), 0);
    }
    for (const char *k : { "x", "y", "width", "height" }) {
        if (!isNumber(el.value(QLatin1String(k))))
            errors->append(QStringLiteral("%1: %2 is a number (pixels).").arg(where, QLatin1String(k)));
    }
    if (el.value(QLatin1String("width")).toDouble() < 1 || el.value(QLatin1String("height")).toDouble() < 1)
        errors->append(QStringLiteral("%1: width and height are at least 1.").arg(where));
    if (el.contains(QLatin1String("props")) && !el.value(QLatin1String("props")).isObject())
        errors->append(QStringLiteral("%1: props is an object.").arg(where));
    if (el.contains(QLatin1String("events")) && !el.value(QLatin1String("events")).isObject())
        errors->append(QStringLiteral("%1: events is an object: { \"eventName\": \"code\" }.").arg(where));
    if (!el.contains(QLatin1String("props")))
        el.insert(QStringLiteral("props"), QJsonObject());
    checkProps(where, AppCompiler::elementProps(type), el.value(QLatin1String("props")).toObject(), errors);
    if (el.contains(QLatin1String("events"))) {
        QJsonObject events = el.value(QLatin1String("events")).toObject();
        checkEvents(where, info.value(QLatin1String("events")).toArray(), events, errors);
        // (blank code is no code)
        for (const QString &k : events.keys()) {
            if (events.value(k).toString().trimmed().isEmpty())
                events.remove(k);
        }
        if (events.isEmpty())
            el.remove(QStringLiteral("events"));
        else
            el.insert(QStringLiteral("events"), events);
    }
    return el;
}

// Menus and ToolBars across the top, a Menu first (Main.qml's redock)
void redock(QJsonObject &form)
{
    QJsonArray controls = form.value(QLatin1String("controls")).toArray();
    QList<int> docked;
    for (int i = 0; i < controls.size(); ++i) {
        if (elementInfo(controls.at(i).toObject().value(QLatin1String("type")).toString()).value(QLatin1String("dock")).toBool())
            docked << i;
    }
    std::stable_sort(docked.begin(), docked.end(), [&](int a, int b) {
        const int ma = controls.at(a).toObject().value(QLatin1String("type")).toString() == QLatin1String("Menu") ? 0 : 1;
        const int mb = controls.at(b).toObject().value(QLatin1String("type")).toString() == QLatin1String("Menu") ? 0 : 1;
        return ma < mb;
    });
    int y = 0;
    for (int i : docked) {
        QJsonObject c = controls.at(i).toObject();
        c.insert(QStringLiteral("x"), 0);
        c.insert(QStringLiteral("y"), y);
        c.insert(QStringLiteral("width"), form.value(QLatin1String("width")).toInt());
        y += c.value(QLatin1String("height")).toInt();
        controls.replace(i, c);
    }
    form.insert(QStringLiteral("controls"), controls);
}

// a form, its defaults filled in; what's wrong in `errors`
QJsonObject checkForm(QJsonObject form, QStringList *errors)
{
    const QString name = form.value(QLatin1String("name")).toString();
    const QString where = name.isEmpty() ? QStringLiteral("(a form with no name)") : name;
    const QString nameProblem = AppCompiler::nameProblem(name, false);
    if (!nameProblem.isEmpty())
        errors->append(QStringLiteral("%1: %2").arg(where, nameProblem));
    static const QStringList keys{ QStringLiteral("name"), QStringLiteral("width"), QStringLiteral("height"),
                                   QStringLiteral("props"), QStringLiteral("code"), QStringLiteral("controls") };
    for (auto it = form.begin(); it != form.end(); ++it) {
        if (!keys.contains(it.key()))
            errors->append(QStringLiteral("%1: \"%2\" isn't part of a form (%3).").arg(where, it.key(), keys.join(QStringLiteral(", "))));
    }
    if (!form.contains(QLatin1String("width")))
        form.insert(QStringLiteral("width"), 400);
    if (!form.contains(QLatin1String("height")))
        form.insert(QStringLiteral("height"), 296);
    if (!isNumber(form.value(QLatin1String("width"))) || !isNumber(form.value(QLatin1String("height"))))
        errors->append(QStringLiteral("%1: width and height are numbers (pixels).").arg(where));
    const QJsonObject formInfo = registry().value(QLatin1String("form")).toObject();
    QJsonObject props = form.value(QLatin1String("props")).toObject();
    if (!props.contains(QLatin1String("title")))
        props.insert(QStringLiteral("title"), name);
    checkProps(where, formInfo.value(QLatin1String("props")).toArray(), props, errors);
    form.insert(QStringLiteral("props"), props);
    QJsonObject code = form.value(QLatin1String("code")).toObject();
    checkEvents(where, formInfo.value(QLatin1String("events")).toArray(), code, errors, true);
    form.insert(QStringLiteral("code"), code);

    if (form.contains(QLatin1String("controls")) && !form.value(QLatin1String("controls")).isArray())
        errors->append(QStringLiteral("%1: controls is a list of elements.").arg(where));
    QJsonArray controls;
    QStringList taken;
    for (const QJsonValue &c : form.value(QLatin1String("controls")).toArray()) {
        const QJsonObject el = checkElement(name, c.toObject(), errors);
        const QString n = el.value(QLatin1String("name")).toString();
        if (taken.contains(n))
            errors->append(QStringLiteral("%1: two elements are named %2.").arg(where, n));
        taken << n;
        controls.append(el);
    }
    form.insert(QStringLiteral("controls"), controls);
    redock(form);
    return form;
}

const QStringList &fieldKeys()
{
    static const QStringList keys{
        QStringLiteral("name"), QStringLiteral("type"), QStringLiteral("label"), QStringLiteral("required"),
        QStringLiteral("unique"), QStringLiteral("default"), QStringLiteral("auto"), QStringLiteral("minLength"),
        QStringLiteral("maxLength"), QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("decimals"),
        QStringLiteral("choices"), QStringLiteral("lookupTable"), QStringLiteral("pattern"), QStringLiteral("validator"),
        QStringLiteral("validatorArg"), QStringLiteral("rule"), QStringLiteral("message"), QStringLiteral("path"),
        QStringLiteral("readOnly"),
    };
    return keys;
}

QJsonObject checkTable(const QJsonObject &table, const QJsonObject &project, QStringList *errors)
{
    const QString name = table.value(QLatin1String("name")).toString();
    const QString where = name.isEmpty() ? QStringLiteral("(a table with no name)") : name;
    const QString nameProblem = AppCompiler::nameProblem(name, false);
    if (!nameProblem.isEmpty())
        errors->append(QStringLiteral("%1: %2").arg(where, nameProblem));
    if (!table.value(QLatin1String("fields")).isArray()) {
        errors->append(QStringLiteral("%1: fields is a list.").arg(where));
        return table;
    }
    QStringList types;
    for (const QVariant &t : AppDatabase::fieldTypes())
        types << t.toMap().value(QStringLiteral("type")).toString();
    QStringList tables = namesOf(project.value(QLatin1String("tables")).toArray());
    tables << name;
    static const QRegularExpression fieldName(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    QStringList taken;
    for (const QJsonValue &v : table.value(QLatin1String("fields")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString fname = f.value(QLatin1String("name")).toString();
        const QString at = QStringLiteral("%1.%2").arg(where, fname.isEmpty() ? QStringLiteral("(no name)") : fname);
        if (!fieldName.match(fname).hasMatch())
            errors->append(QStringLiteral("%1: a field's name starts with a letter and has only letters, digits and _.").arg(at));
        if (fname.compare(QLatin1String("id"), Qt::CaseInsensitive) == 0)
            errors->append(QStringLiteral("%1: every table has its id already (the key, numbered by the database).").arg(at));
        if (taken.contains(fname, Qt::CaseInsensitive))
            errors->append(QStringLiteral("%1: two fields have this name.").arg(at));
        taken << fname;
        const QString type = f.value(QLatin1String("type")).toString();
        if (!types.contains(type)) {
            errors->append(QStringLiteral("%1: \"%2\" isn't a data type. The types: %3.").arg(at, type, types.join(QStringLiteral(", "))));
            continue;
        }
        for (auto it = f.begin(); it != f.end(); ++it) {
            if (!fieldKeys().contains(it.key()))
                errors->append(QStringLiteral("%1: \"%2\" isn't a field setting (studio_reference, topic fields).").arg(at, it.key()));
        }
        if (type == QLatin1String("choice") && !f.value(QLatin1String("choices")).isArray())
            errors->append(QStringLiteral("%1: a choice field has its choices: [\"One\", \"Two\"].").arg(at));
        if (type == QLatin1String("lookup") && !tables.contains(f.value(QLatin1String("lookupTable")).toString()))
            errors->append(QStringLiteral("%1: lookupTable names one of the project's tables (%2).")
                               .arg(at, tables.join(QStringLiteral(", "))));
        const QString auto_ = f.value(QLatin1String("auto")).toString();
        if (f.contains(QLatin1String("auto")) && auto_ != QLatin1String("created") && auto_ != QLatin1String("modified"))
            errors->append(QStringLiteral("%1: auto is \"created\" or \"modified\" (date, time and datetime fields).").arg(at));
        const QString validator = f.value(QLatin1String("validator")).toString();
        if (!validator.isEmpty()) {
            QStringList ids;
            for (const QVariant &r : FieldRules::forType(type))
                ids << r.toMap().value(QStringLiteral("id")).toString();
            if (!ids.contains(validator))
                errors->append(QStringLiteral("%1: \"%2\" isn't a Validation Rule for a %3 field. Its rules: %4.")
                                   .arg(at, validator, type, ids.join(QStringLiteral(", "))));
        }
    }
    return table;
}

int indexNamed(const QJsonArray &list, const QString &name)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value(QLatin1String("name")).toString() == name)
            return i;
    }
    return -1;
}

QString str(const QJsonObject &args, const char *key) { return args.value(QLatin1String(key)).toString(); }

// what names something that isn't there: the start form, a form's or a
// DataGrid's table, an element's field, a lookup's table, a menu item's
// action and form (the compiler leaves these to when the app runs)
QStringList crossChecks(const QJsonObject &project)
{
    QStringList out;
    const QJsonArray tables = project.value(QLatin1String("tables")).toArray();
    const QJsonArray forms = project.value(QLatin1String("forms")).toArray();
    auto fieldsOf = [&](const QString &table) {
        QStringList names{ QStringLiteral("id") };
        const int i = indexNamed(tables, table);
        if (i >= 0)
            names += namesOf(tables.at(i).toObject().value(QLatin1String("fields")).toArray());
        return names;
    };
    const QString start = project.value(QLatin1String("app")).toObject().value(QLatin1String("startForm")).toString();
    if (indexNamed(forms, start) < 0)
        out << QStringLiteral("app: the start form %1 isn't in the project (set_app).").arg(start.isEmpty() ? QStringLiteral("(none)") : start);
    for (const QJsonValue &tv : tables) {
        for (const QJsonValue &fv : tv.toObject().value(QLatin1String("fields")).toArray()) {
            const QJsonObject f = fv.toObject();
            if (f.value(QLatin1String("type")).toString() == QLatin1String("lookup")
                && indexNamed(tables, f.value(QLatin1String("lookupTable")).toString()) < 0)
                out << QStringLiteral("%1.%2: its lookupTable %3 isn't in the project.")
                           .arg(str(tv.toObject(), "name"), str(f, "name"), str(f, "lookupTable"));
        }
    }
    static const QStringList actions{ QStringLiteral("openForm"), QStringLiteral("closeForm"), QStringLiteral("quit"),
                                      QStringLiteral("newRecord"), QStringLiteral("save"), QStringLiteral("deleteRecord"),
                                      QStringLiteral("first"), QStringLiteral("previous"), QStringLiteral("next"),
                                      QStringLiteral("last") };
    for (const QJsonValue &fv : forms) {
        const QJsonObject form = fv.toObject();
        const QString fname = str(form, "name");
        const QString formTable = form.value(QLatin1String("props")).toObject().value(QLatin1String("table")).toString();
        if (!formTable.isEmpty() && indexNamed(tables, formTable) < 0)
            out << QStringLiteral("%1: its table %2 isn't in the project.").arg(fname, formTable);
        for (const QJsonValue &cv : form.value(QLatin1String("controls")).toArray()) {
            const QJsonObject c = cv.toObject();
            const QJsonObject props = c.value(QLatin1String("props")).toObject();
            const QString where = QStringLiteral("%1, %2").arg(fname, str(c, "name"));
            const QString ownTable = props.value(QLatin1String("table")).toString();
            if (!ownTable.isEmpty() && indexNamed(tables, ownTable) < 0)
                out << QStringLiteral("%1: its table %2 isn't in the project.").arg(where, ownTable);
            const QString table = ownTable.isEmpty() ? formTable : ownTable;
            const QString field = props.value(QLatin1String("field")).toString();
            if (!field.isEmpty()) {
                if (table.isEmpty())
                    out << QStringLiteral("%1: its field %2 needs the form to have a table (props.table).").arg(where, field);
                else if (indexNamed(tables, table) >= 0 && !fieldsOf(table).contains(field))
                    out << QStringLiteral("%1: %2 isn't a field of %3 (%4).").arg(where, field, table, fieldsOf(table).join(QStringLiteral(", ")));
            }
            if (str(c, "type") == QLatin1String("DataGrid")) {
                if (table.isEmpty())
                    out << QStringLiteral("%1: a DataGrid needs a table (its own, or the form's).").arg(where);
                for (const QJsonValue &col : props.value(QLatin1String("columns")).toArray()) {
                    // "Title" or "Title:180" (a width in pixels)
                    static const QRegularExpression spec(QStringLiteral("^\\s*([^:]+?)\\s*(?::\\s*(\\d+)\\s*)?$"));
                    const QRegularExpressionMatch m = spec.match(col.toString());
                    const QString colName = m.hasMatch() ? m.captured(1) : col.toString().trimmed();
                    if (!m.hasMatch())
                        out << QStringLiteral("%1: its column \"%2\" isn't a field's name, or a name and a width (\"Title:180\").").arg(where, col.toString());
                    else if (indexNamed(tables, table) >= 0 && !fieldsOf(table).contains(colName)
                             && colName != QLatin1String("created_on") && colName != QLatin1String("modified_on"))
                        out << QStringLiteral("%1: its column %2 isn't a field of %3.").arg(where, colName, table);
                }
            }
            // menu and toolbar items, and a menu's items inside its menus
            QJsonArray items = props.value(QLatin1String("buttons")).toArray();
            for (const QJsonValue &m : props.value(QLatin1String("menus")).toArray()) {
                items.append(m);
                for (const QJsonValue &i : m.toObject().value(QLatin1String("items")).toArray())
                    items.append(i);
            }
            for (const QJsonValue &iv : items) {
                const QJsonObject item = iv.toObject();
                const QString action = str(item, "action");
                if (action.isEmpty())
                    continue;
                if (!actions.contains(action))
                    out << QStringLiteral("%1: the item %2's action %3 isn't one of: %4.")
                               .arg(where, str(item, "name"), action, actions.join(QStringLiteral(", ")));
                // (an item that opens a form that isn't there: the compiler says so)
                else if (action == QLatin1String("openForm"))
                    continue;
                else if (formTable.isEmpty() && QStringList{ QStringLiteral("newRecord"), QStringLiteral("save"),
                                                             QStringLiteral("deleteRecord"), QStringLiteral("first"),
                                                             QStringLiteral("previous"), QStringLiteral("next"),
                                                             QStringLiteral("last") }.contains(action))
                    out << QStringLiteral("%1: the item %2's action %3 needs the form to have a table.").arg(where, str(item, "name"), action);
            }
        }
    }
    return out;
}

// what the compiler finds wrong with the whole project, for after a write
QString problemsText(const QJsonObject &project, const QString &path)
{
    QStringList problems;
    AppCompiler::compile(project, QFileInfo(path).absolutePath(), &problems);
    problems += crossChecks(project);
    if (problems.isEmpty())
        return QStringLiteral("The compiler finds nothing wrong (check_project also compiles the code).");
    return QStringLiteral("What the compiler finds wrong in the project:\n- ") + problems.join(QStringLiteral("\n- "));
}

Result saved(const QString &path, const QJsonObject &project, const QString &what)
{
    QString error;
    if (!save(path, project, &error))
        return fail(error);
    return text(what + QLatin1Char('\n') + problemsText(project, path));
}

// ---------------------------------------------------------------- reference

QString describeProp(const QJsonObject &p)
{
    QString s = QStringLiteral("- `%1` (%2").arg(p.value(QLatin1String("name")).toString(), p.value(QLatin1String("type")).toString());
    const QJsonArray values = p.value(QLatin1String("values")).toArray();
    if (!values.isEmpty()) {
        QStringList v;
        for (const QJsonValue &x : values)
            v << x.toString();
        s += QStringLiteral(": ") + v.join(QStringLiteral(" | "));
    }
    const QJsonValue def = p.value(QLatin1String("default"));
    if (!def.isUndefined()) {
        const QString d = def.isString() ? QStringLiteral("\"%1\"").arg(def.toString())
                                         : QString::fromUtf8(QJsonDocument(QJsonArray{ def }).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        s += QStringLiteral(", default ") + d;
    }
    s += QStringLiteral(")");
    const QString label = p.value(QLatin1String("label")).toString();
    if (!label.isEmpty() && label.compare(p.value(QLatin1String("name")).toString(), Qt::CaseInsensitive) != 0)
        s += QStringLiteral(" — \"%1\" in the Properties window").arg(label);
    return s;
}

QString describeEvents(const QJsonArray &events)
{
    QString s;
    for (const QJsonValue &e : events) {
        const QString hint = e.toObject().value(QLatin1String("hint")).toString();
        s += QStringLiteral("- `%1`%2\n").arg(e.toObject().value(QLatin1String("name")).toString(),
                                             hint.isEmpty() ? QString() : QStringLiteral(": ") + hint);
    }
    return s.isEmpty() ? QStringLiteral("(none)\n") : s;
}

QString elementsReference(const QString &only)
{
    const QJsonObject reg = registry();
    QString s;
    if (only.isEmpty()) {
        const QJsonObject form = reg.value(QLatin1String("form")).toObject();
        s += QStringLiteral("# Forms and elements\n\n## A form's props\n\n");
        for (const QJsonValue &p : form.value(QLatin1String("props")).toArray())
            s += describeProp(p.toObject()) + QLatin1Char('\n');
        s += QStringLiteral("\n## A form's code (its events, and `general`)\n\n- `general`: functions and variables for the form's other code\n")
             + describeEvents(form.value(QLatin1String("events")).toArray());
        s += QStringLiteral("\n## Every element has\n\n");
        for (const QJsonValue &p : reg.value(QLatin1String("common")).toArray())
            s += describeProp(p.toObject()) + QLatin1Char('\n');
        s += QStringLiteral("\n## Elements with the `appearance` set have\n\n");
        for (const QJsonValue &p : reg.value(QLatin1String("appearance")).toArray())
            s += describeProp(p.toObject()) + QLatin1Char('\n');
        s += QStringLiteral("\n## Elements with the `data` set have\n\n");
        for (const QJsonValue &p : reg.value(QLatin1String("data")).toArray())
            s += describeProp(p.toObject()) + QStringLiteral(" — the field of the form's table it shows and edits\n");
        s += QLatin1Char('\n');
    }
    for (const QJsonValue &v : reg.value(QLatin1String("elements")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString type = e.value(QLatin1String("type")).toString();
        if (!only.isEmpty() && type.compare(only, Qt::CaseInsensitive) != 0)
            continue;
        const QJsonArray size = e.value(QLatin1String("size")).toArray();
        QStringList sets;
        for (const QJsonValue &x : e.value(QLatin1String("sets")).toArray())
            sets << x.toString();
        s += QStringLiteral("## %1\n\nDefault size %2x%3%4%5%6.\n\n")
                 .arg(type)
                 .arg(size.at(0).toInt())
                 .arg(size.at(1).toInt())
                 .arg(sets.isEmpty() ? QString() : QStringLiteral("; sets: ") + sets.join(QStringLiteral(", ")))
                 .arg(e.value(QLatin1String("dock")).toBool() ? QStringLiteral("; docks across the top") : QString())
                 .arg(e.value(QLatin1String("behind")).toBool() ? QStringLiteral("; drawn behind the other elements") : QString());
        if (e.contains(QLatin1String("initial")))
            s += QStringLiteral("The Studio starts a new one with props %1\n\n")
                     .arg(QString::fromUtf8(QJsonDocument(e.value(QLatin1String("initial")).toObject()).toJson(QJsonDocument::Compact)));
        s += QStringLiteral("Props:\n");
        for (const QJsonValue &p : e.value(QLatin1String("props")).toArray())
            s += describeProp(p.toObject()) + QLatin1Char('\n');
        s += QStringLiteral("\nEvents:\n") + describeEvents(e.value(QLatin1String("events")).toArray()) + QLatin1Char('\n');
    }
    if (only.isEmpty() || only.compare(QLatin1String("icons"), Qt::CaseInsensitive) == 0) {
        // the names an icon property, or a ToolBar button's "icon", can have
        s += QStringLiteral("## Icons\n\nWayland 95's own 16x16 icons, for a Button's `icon` and a ToolBar button's "
                            "`\"icon\"` (drawn grey when it's disabled). The name, then what it shows:\n\n");
        QStringList all;
        for (const QJsonValue &i : AppCompiler::icons())
            all << QStringLiteral("`%1` %2").arg(i.toObject().value(QLatin1String("name")).toString(),
                                               i.toObject().value(QLatin1String("label")).toString());
        s += all.join(QStringLiteral(", ")) + QStringLiteral("\n");
    }
    if (s.isEmpty())
        s = QStringLiteral("There's no element type \"%1\". The types: %2 (or Icons).").arg(only, elementTypes().join(QStringLiteral(", ")));
    return s;
}

QString fieldsReference()
{
    QString s = QStringLiteral(
        "# Fields\n\n"
        "A field: `{ \"name\": \"Title\", \"type\": \"text\", ... }`. Its settings, all optional:\n\n"
        "- `label`: what messages call it (\"Title is required.\"); blank: its name\n"
        "- `required` (bool): refused while empty (a boolean: unless it's Yes)\n"
        "- `unique` (bool): refused if another record has the same value (not boolean, memo)\n"
        "- `default`: what a new record starts with. boolean: \"yes\" or \"\"; date: \"today\", \"today+7\", "
        "\"today-1w\" or \"2030-01-01\"; time: \"now\", \"now+1h\", \"09:00\"; datetime: \"now\", \"now+2h\", \"2030-01-01 09:00\"\n"
        "- `auto` (date, time, datetime): \"created\" (set when the record is made) or \"modified\" (set on every save)\n"
        "- `minLength`, `maxLength` (text, memo, email, url, phone): characters; text is 255 at most\n"
        "- `min`, `max`: the range (numbers; dates and times as text: \"2020-01-01\", \"08:00\", \"today\")\n"
        "- `decimals` (decimal): digits kept after the point, 2 if not set\n"
        "- `choices` (choice): [\"One\", \"Two\"]; a ComboBox bound to it lists them\n"
        "- `lookupTable` (lookup): the table whose record it points at (it holds the id)\n"
        "- `pattern`: a regular expression the whole value matches (not boolean, lookup, choice)\n"
        "- `validator`, `validatorArg`: a Validation Rule (below) and its value, if it takes one\n"
        "- `rule`: with `\"validator\": \"expression\"`, the rule written out\n"
        "- `message`: what's said when the rule or pattern isn't met; blank: the rule's own message\n\n"
        "## Data types\n\n");
    for (const QVariant &t : AppDatabase::fieldTypes()) {
        const QVariantMap m = t.toMap();
        s += QStringLiteral("- `%1` (%2): %3\n").arg(m.value(QStringLiteral("type")).toString(),
                                                    m.value(QStringLiteral("label")).toString(),
                                                    m.value(QStringLiteral("hint")).toString());
    }
    // each rule once, with the types it's for
    s += QStringLiteral("\n## Validation Rules\n\n`validator`: the id. A rule with a value takes it as `validatorArg` "
                        "(number, count, text, list: [\"a\", \"b\"], field: another field's name, expression: in `rule`).\n\n");
    QStringList order;
    QHash<QString, QVariantMap> rules;
    QHash<QString, QStringList> forTypes;
    for (const QVariant &t : AppDatabase::fieldTypes()) {
        const QString type = t.toMap().value(QStringLiteral("type")).toString();
        for (const QVariant &r : FieldRules::forType(type)) {
            const QVariantMap m = r.toMap();
            const QString id = m.value(QStringLiteral("id")).toString();
            if (!rules.contains(id)) {
                order << id;
                rules.insert(id, m);
            }
            forTypes[id] << type;
        }
    }
    for (const QString &id : std::as_const(order)) {
        const QVariantMap m = rules.value(id);
        QString line = QStringLiteral("- `%1` (%2): %3").arg(id, forTypes.value(id).join(QStringLiteral(", ")),
                                                           m.value(QStringLiteral("label")).toString());
        const QString arg = m.value(QStringLiteral("arg")).toString();
        if (!arg.isEmpty() && arg != QLatin1String("none")) {
            line += QStringLiteral(" [value: %1").arg(arg);
            const QString hint = m.value(QStringLiteral("argHint")).toString();
            if (!hint.isEmpty())
                line += QStringLiteral(": ") + hint;
            line += QLatin1Char(']');
        }
        s += line + QLatin1Char('\n');
    }
    return s;
}

QString manualDir()
{
    QStringList places;
    const QString env = qEnvironmentVariable("W95_STUDIO_MANUAL");
    if (!env.isEmpty())
        places << env;
    places << QStringLiteral(WAYLAND95_STUDIO_MANUAL)
           << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/wayland95/studio-manual")
           << QStringLiteral(WAYLAND95_STUDIO_MANUAL_SOURCE);
    for (const QString &dir : std::as_const(places)) {
        if (QFileInfo::exists(dir + QStringLiteral("/index.md")))
            return QDir(dir).absolutePath();
    }
    return QString();
}

// ------------------------------------------------------------------- tools

struct Tool
{
    QString name;
    QString description;
    QJsonObject schema;
    std::function<Result(const QJsonObject &)> run;
};

QJsonObject prop(const char *type, const QString &description)
{
    return { { QStringLiteral("type"), QLatin1String(type) }, { QStringLiteral("description"), description } };
}

QJsonObject schema(const QJsonObject &props, const QStringList &required)
{
    return { { QStringLiteral("type"), QStringLiteral("object") },
             { QStringLiteral("properties"), props },
             { QStringLiteral("required"), QJsonArray::fromStringList(required) } };
}

const QString pathHelp = QStringLiteral("The project file (.w95proj): absolute, ~/..., or relative to where the server started.");

// a project loaded for a tool: `path` resolved, or a failure
struct Loaded
{
    QString path;
    QJsonObject project;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
Loaded loadArg(const QJsonObject &args)
{
    Loaded l;
    if (str(args, "path").trimmed().isEmpty()) {
        l.error = QStringLiteral("path is needed: the .w95proj file.");
        return l;
    }
    l.path = resolve(str(args, "path"));
    load(l.path, &l.project, &l.error);
    return l;
}

QList<Tool> tools()
{
    QList<Tool> t;

    t << Tool{ QStringLiteral("studio_reference"),
               QStringLiteral("How Wayland Studio apps are made, for these tools: the project's JSON (app, database, tables, forms, "
                              "elements), the way to work, laying out forms the Windows 95 way, menus, toolbars and their actions, "
                              "and the code API in brief (topic \"format\", read it first). Topic \"elements\": every element type's "
                              "properties (with types, values and defaults) and events, and the form's (`element` narrows it to one "
                              "type). Topic \"fields\": the data types, field settings and Validation Rules."),
               schema({ { QStringLiteral("topic"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
                                                               { QStringLiteral("enum"), QJsonArray{ QStringLiteral("format"), QStringLiteral("elements"), QStringLiteral("fields") } },
                                                               { QStringLiteral("description"), QStringLiteral("format (the default), elements or fields") } } },
                        { QStringLiteral("element"), prop("string", QStringLiteral("With topic elements: one type, like TextBox (or Icons: the icons' names)")) } },
                      {}),
               [](const QJsonObject &a) {
                   const QString topic = str(a, "topic");
                   if (topic == QLatin1String("elements"))
                       return text(elementsReference(str(a, "element")));
                   if (topic == QLatin1String("fields"))
                       return text(fieldsReference());
                   QFile f(QStringLiteral(":/studio-mcp/reference.md"));
                   f.open(QIODevice::ReadOnly);
                   return text(QString::fromUtf8(f.readAll()));
               } };

    t << Tool{ QStringLiteral("studio_manual"),
               QStringLiteral("A page of the Wayland Studio manual (Markdown), written for people using the Studio: code.md is the "
                              "full API for event code (form, App, Db, AppDates, each element's methods) with examples; forms.md "
                              "each element in detail (Graph, DataGrid, Menu...); data-designer.md tables and Validation Rules; "
                              "ai-chat.md the AIChat element. No page: the list of pages."),
               schema({ { QStringLiteral("page"), prop("string", QStringLiteral("Like code.md")) } }, {}),
               [](const QJsonObject &a) {
                   const QString dir = manualDir();
                   if (dir.isEmpty())
                       return fail(QStringLiteral("The Wayland Studio manual isn't installed."));
                   const QString page = QFileInfo(str(a, "page")).fileName();
                   QFile f(QDir(dir).filePath(page));
                   if (page.isEmpty() || !f.open(QIODevice::ReadOnly)) {
                       const QStringList pages = QDir(dir).entryList({ QStringLiteral("*.md") }, QDir::Files, QDir::Name);
                       return text((page.isEmpty() ? QString() : QStringLiteral("There's no page %1. ").arg(page))
                                   + QStringLiteral("The pages: ") + pages.join(QStringLiteral(", ")), !page.isEmpty());
                   }
                   return text(QString::fromUtf8(f.readAll()));
               } };

    t << Tool{ QStringLiteral("new_project"),
               QStringLiteral("Make a new project file with one empty form (Form1, the start form) and no tables, its database a "
                              "SQLite file beside it. The folder is made if it isn't there."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp + QStringLiteral(" .w95proj is added if it's missing.")) },
                        { QStringLiteral("title"), prop("string", QStringLiteral("What people read: \"Book Club\"")) },
                        { QStringLiteral("name"), prop("string", QStringLiteral("The program's name: small letters, no spaces (default: from the file's name)")) },
                        { QStringLiteral("overwrite"), prop("boolean", QStringLiteral("Replace a project that's there already")) } },
                      { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   QString path = resolve(str(a, "path"));
                   if (!path.endsWith(QLatin1String(".w95proj")))
                       path += QStringLiteral(".w95proj");
                   if (QFileInfo::exists(path) && !a.value(QLatin1String("overwrite")).toBool())
                       return fail(QStringLiteral("%1 is there already: get_project reads it (overwrite: true replaces it).").arg(path));
                   if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                       return fail(QStringLiteral("The folder %1 can't be made.").arg(QFileInfo(path).absolutePath()));
                   QString name = str(a, "name").trimmed();
                   if (name.isEmpty())
                       name = QFileInfo(path).completeBaseName().toLower().remove(QRegularExpression(QStringLiteral("[^a-z0-9_-]")));
                   if (name.isEmpty())
                       name = QStringLiteral("project1");
                   const QString title = str(a, "title").trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : str(a, "title").trimmed();
                   const QJsonObject form{ { QStringLiteral("name"), QStringLiteral("Form1") },
                                           { QStringLiteral("width"), 400 },
                                           { QStringLiteral("height"), 296 },
                                           { QStringLiteral("props"), QJsonObject{ { QStringLiteral("title"), title } } },
                                           { QStringLiteral("code"), QJsonObject() },
                                           { QStringLiteral("controls"), QJsonArray() } };
                   const QJsonObject project{
                       { QStringLiteral("format"), 1 },
                       { QStringLiteral("app"), QJsonObject{ { QStringLiteral("name"), name },
                                                             { QStringLiteral("title"), title },
                                                             { QStringLiteral("version"), QStringLiteral("1.0") },
                                                             { QStringLiteral("startForm"), QStringLiteral("Form1") } } },
                       { QStringLiteral("database"), QJsonObject{ { QStringLiteral("driver"), QStringLiteral("sqlite") },
                                                                  { QStringLiteral("file"), name + QStringLiteral(".db") } } },
                       { QStringLiteral("tables"), QJsonArray() },
                       { QStringLiteral("forms"), QJsonArray{ form } },
                   };
                   QString error;
                   if (!save(path, project, &error))
                       return fail(error);
                   return text(QStringLiteral("Made %1:\n%2").arg(path, pretty(project)));
               } };

    t << Tool{ QStringLiteral("get_project"),
               QStringLiteral("Read a project: all of it, or one form or table (form/table)."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("form"), prop("string", QStringLiteral("Just this form")) },
                        { QStringLiteral("table"), prop("string", QStringLiteral("Just this table")) } },
                      { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   for (const char *kind : { "form", "table" }) {
                       const QString name = str(a, kind);
                       if (name.isEmpty())
                           continue;
                       const QJsonArray list = l.project.value(QLatin1String(kind) + QStringLiteral("s")).toArray();
                       const int i = indexNamed(list, name);
                       if (i < 0)
                           return fail(QStringLiteral("There's no %1 %2. The %1s: %3.").arg(QLatin1String(kind), name,
                                                                                            namesOf(list).join(QStringLiteral(", "))));
                       return text(pretty(list.at(i)));
                   }
                   return text(QStringLiteral("%1:\n%2").arg(l.path, pretty(l.project)));
               } };

    t << Tool{ QStringLiteral("set_app"),
               QStringLiteral("Change the project's app settings (name, title, version, startForm) and/or its database "
                              "(driver: sqlite with file; mysql or postgresql with host, port, name, user, password). Only "
                              "the keys given change."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("app"), prop("object", QStringLiteral("{ name, title, version, startForm }: any of them")) },
                        { QStringLiteral("database"), prop("object", QStringLiteral("{ driver, file } or { driver, host, port, name, user, password }")) } },
                      { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QStringList errors;
                   QJsonObject app = l.project.value(QLatin1String("app")).toObject();
                   const QJsonObject newApp = a.value(QLatin1String("app")).toObject();
                   static const QStringList appKeys{ QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("version"),
                                                     QStringLiteral("startForm") };
                   for (auto it = newApp.begin(); it != newApp.end(); ++it) {
                       if (!appKeys.contains(it.key()))
                           errors << QStringLiteral("app: \"%1\" isn't an app setting (%2).").arg(it.key(), appKeys.join(QStringLiteral(", ")));
                       else if (!it.value().isString())
                           errors << QStringLiteral("app: %1 is a string.").arg(it.key());
                       else
                           app.insert(it.key(), it.value());
                   }
                   if (newApp.contains(QLatin1String("startForm"))
                       && indexNamed(l.project.value(QLatin1String("forms")).toArray(), str(newApp, "startForm")) < 0)
                       errors << QStringLiteral("app: there's no form %1 to start with.").arg(str(newApp, "startForm"));
                   static const QRegularExpression appName(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
                   if (newApp.contains(QLatin1String("name")) && !appName.match(str(newApp, "name")).hasMatch())
                       errors << QStringLiteral("app: name is small letters, digits, - and _ (no spaces).");
                   QJsonObject db = l.project.value(QLatin1String("database")).toObject();
                   const QJsonObject newDb = a.value(QLatin1String("database")).toObject();
                   static const QStringList dbKeys{ QStringLiteral("driver"), QStringLiteral("file"), QStringLiteral("host"),
                                                    QStringLiteral("port"), QStringLiteral("name"), QStringLiteral("user"),
                                                    QStringLiteral("password") };
                   for (auto it = newDb.begin(); it != newDb.end(); ++it) {
                       if (!dbKeys.contains(it.key()))
                           errors << QStringLiteral("database: \"%1\" isn't a database setting (%2).").arg(it.key(), dbKeys.join(QStringLiteral(", ")));
                       else
                           db.insert(it.key(), it.value());
                   }
                   const QString driver = db.value(QLatin1String("driver")).toString(QStringLiteral("sqlite"));
                   if (driver != QLatin1String("sqlite") && driver != QLatin1String("mysql") && driver != QLatin1String("postgresql"))
                       errors << QStringLiteral("database: driver is sqlite, mysql or postgresql.");
                   if (!errors.isEmpty())
                       return fail(QStringLiteral("Nothing was changed:\n- ") + errors.join(QStringLiteral("\n- ")));
                   l.project.insert(QStringLiteral("app"), app);
                   l.project.insert(QStringLiteral("database"), db);
                   return saved(l.path, l.project, QStringLiteral("Saved: app %1, database %2.")
                                                       .arg(QString::fromUtf8(QJsonDocument(app).toJson(QJsonDocument::Compact)),
                                                            QString::fromUtf8(QJsonDocument(db).toJson(QJsonDocument::Compact))));
               } };

    t << Tool{ QStringLiteral("put_table"),
               QStringLiteral("Add a table, or replace the one with the same name (all of it: get_project with table reads it "
                              "first). { name, label?, fields: [{ name, type, ... }] }. Every table has an id already. "
                              "studio_reference topic fields has the types and settings. The app's database gets the table "
                              "(and new fields) when it next runs; data in a field that's taken away stays in the database."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("table"), prop("object", QStringLiteral("The table")) } },
                      { QStringLiteral("path"), QStringLiteral("table") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QStringList errors;
                   const QJsonObject table = checkTable(a.value(QLatin1String("table")).toObject(), l.project, &errors);
                   if (!errors.isEmpty())
                       return fail(QStringLiteral("Nothing was changed:\n- ") + errors.join(QStringLiteral("\n- ")));
                   QJsonArray tables = l.project.value(QLatin1String("tables")).toArray();
                   const QString name = str(table, "name");
                   const int i = indexNamed(tables, name);
                   QJsonObject merged = table;
                   if (i >= 0) {
                       // what the Studio keeps that the caller may not know about (imported tables, APIs)
                       const QJsonObject old = tables.at(i).toObject();
                       for (auto it = old.begin(); it != old.end(); ++it) {
                           if (it.key() != QLatin1String("fields") && it.key() != QLatin1String("label") && !merged.contains(it.key()))
                               merged.insert(it.key(), it.value());
                       }
                       tables.replace(i, merged);
                   } else {
                       tables.append(merged);
                   }
                   l.project.insert(QStringLiteral("tables"), tables);
                   return saved(l.path, l.project, QStringLiteral("%1 the table %2 (%3 fields).")
                                                       .arg(i >= 0 ? QStringLiteral("Replaced") : QStringLiteral("Added"), name)
                                                       .arg(table.value(QLatin1String("fields")).toArray().size()));
               } };

    t << Tool{ QStringLiteral("remove_table"),
               QStringLiteral("Take a table out of the project (its data stays in the database)."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) }, { QStringLiteral("name"), prop("string", QStringLiteral("The table")) } },
                      { QStringLiteral("path"), QStringLiteral("name") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QJsonArray tables = l.project.value(QLatin1String("tables")).toArray();
                   const int i = indexNamed(tables, str(a, "name"));
                   if (i < 0)
                       return fail(QStringLiteral("There's no table %1.").arg(str(a, "name")));
                   tables.removeAt(i);
                   l.project.insert(QStringLiteral("tables"), tables);
                   return saved(l.path, l.project, QStringLiteral("Removed the table %1.").arg(str(a, "name")));
               } };

    t << Tool{ QStringLiteral("put_form"),
               QStringLiteral("Add a form, or replace the one with the same name: { name, width, height, props: { title, "
                              "table, ... }, code: { general, load, ... }, controls: [elements] }. A form's name starts with "
                              "a letter. Elements: { type, name, x, y, width, height, props, events } (studio_reference: format "
                              "and elements). Menus and ToolBars are docked for you."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) }, { QStringLiteral("form"), prop("object", QStringLiteral("The form")) } },
                      { QStringLiteral("path"), QStringLiteral("form") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QStringList errors;
                   const QJsonObject form = checkForm(a.value(QLatin1String("form")).toObject(), &errors);
                   const QString table = form.value(QLatin1String("props")).toObject().value(QLatin1String("table")).toString();
                   if (!table.isEmpty() && indexNamed(l.project.value(QLatin1String("tables")).toArray(), table) < 0)
                       errors << QStringLiteral("%1: its table %2 isn't in the project (put_table first).").arg(str(form, "name"), table);
                   if (!errors.isEmpty())
                       return fail(QStringLiteral("Nothing was changed:\n- ") + errors.join(QStringLiteral("\n- ")));
                   QJsonArray forms = l.project.value(QLatin1String("forms")).toArray();
                   const int i = indexNamed(forms, str(form, "name"));
                   if (i >= 0)
                       forms.replace(i, form);
                   else
                       forms.append(form);
                   l.project.insert(QStringLiteral("forms"), forms);
                   return saved(l.path, l.project, QStringLiteral("%1 the form %2 (%3 elements).")
                                                       .arg(i >= 0 ? QStringLiteral("Replaced") : QStringLiteral("Added"), str(form, "name"))
                                                       .arg(form.value(QLatin1String("controls")).toArray().size()));
               } };

    t << Tool{ QStringLiteral("remove_form"),
               QStringLiteral("Take a form out of the project (not the start form: set_app another first)."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) }, { QStringLiteral("name"), prop("string", QStringLiteral("The form")) } },
                      { QStringLiteral("path"), QStringLiteral("name") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QJsonArray forms = l.project.value(QLatin1String("forms")).toArray();
                   const int i = indexNamed(forms, str(a, "name"));
                   if (i < 0)
                       return fail(QStringLiteral("There's no form %1.").arg(str(a, "name")));
                   if (l.project.value(QLatin1String("app")).toObject().value(QLatin1String("startForm")).toString() == str(a, "name"))
                       return fail(QStringLiteral("%1 is the start form: set_app another startForm first.").arg(str(a, "name")));
                   forms.removeAt(i);
                   l.project.insert(QStringLiteral("forms"), forms);
                   return saved(l.path, l.project, QStringLiteral("Removed the form %1.").arg(str(a, "name")));
               } };

    t << Tool{ QStringLiteral("put_element"),
               QStringLiteral("Add an element to a form, or replace the one with the same name: { type, name, x, y, width, "
                              "height, props, events } (width and height default to the type's size). merge: true changes "
                              "only what's given (props and events merged into the element's). A new element goes in front "
                              "of the others."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("form"), prop("string", QStringLiteral("The form it's on")) },
                        { QStringLiteral("element"), prop("object", QStringLiteral("The element")) },
                        { QStringLiteral("merge"), prop("boolean", QStringLiteral("Change only what's given")) } },
                      { QStringLiteral("path"), QStringLiteral("form"), QStringLiteral("element") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QJsonArray forms = l.project.value(QLatin1String("forms")).toArray();
                   const int fi = indexNamed(forms, str(a, "form"));
                   if (fi < 0)
                       return fail(QStringLiteral("There's no form %1. The forms: %2.").arg(str(a, "form"), namesOf(forms).join(QStringLiteral(", "))));
                   QJsonObject form = forms.at(fi).toObject();
                   QJsonArray controls = form.value(QLatin1String("controls")).toArray();
                   QJsonObject el = a.value(QLatin1String("element")).toObject();
                   const int ei = indexNamed(controls, str(el, "name"));
                   if (a.value(QLatin1String("merge")).toBool()) {
                       if (ei < 0)
                           return fail(QStringLiteral("There's no element %1 on %2 to change.").arg(str(el, "name"), str(a, "form")));
                       QJsonObject old = controls.at(ei).toObject();
                       for (const char *k : { "props", "events" }) {
                           QJsonObject m = old.value(QLatin1String(k)).toObject();
                           const QJsonObject add = el.value(QLatin1String(k)).toObject();
                           for (auto it = add.begin(); it != add.end(); ++it) {
                               if (it.value().isNull())
                                   m.remove(it.key());
                               else
                                   m.insert(it.key(), it.value());
                           }
                           el.insert(QLatin1String(k), m);
                       }
                       for (auto it = old.begin(); it != old.end(); ++it) {
                           if (!el.contains(it.key()))
                               el.insert(it.key(), it.value());
                       }
                   }
                   QStringList errors;
                   el = checkElement(str(a, "form"), el, &errors);
                   if (!errors.isEmpty())
                       return fail(QStringLiteral("Nothing was changed:\n- ") + errors.join(QStringLiteral("\n- ")));
                   if (ei >= 0)
                       controls.replace(ei, el);
                   else
                       controls.append(el);
                   form.insert(QStringLiteral("controls"), controls);
                   redock(form);
                   forms.replace(fi, form);
                   l.project.insert(QStringLiteral("forms"), forms);
                   const QJsonObject placed = form.value(QLatin1String("controls")).toArray().at(ei >= 0 ? ei : controls.size() - 1).toObject();
                   return saved(l.path, l.project, QStringLiteral("%1 %2 (%3) on %4 at %5,%6, %7x%8.")
                                                       .arg(ei >= 0 ? QStringLiteral("Changed") : QStringLiteral("Added"), str(el, "name"),
                                                            str(el, "type"), str(a, "form"))
                                                       .arg(placed.value(QLatin1String("x")).toInt())
                                                       .arg(placed.value(QLatin1String("y")).toInt())
                                                       .arg(placed.value(QLatin1String("width")).toInt())
                                                       .arg(placed.value(QLatin1String("height")).toInt()));
               } };

    t << Tool{ QStringLiteral("remove_element"),
               QStringLiteral("Take an element off a form."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("form"), prop("string", QStringLiteral("The form")) },
                        { QStringLiteral("name"), prop("string", QStringLiteral("The element")) } },
                      { QStringLiteral("path"), QStringLiteral("form"), QStringLiteral("name") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QJsonArray forms = l.project.value(QLatin1String("forms")).toArray();
                   const int fi = indexNamed(forms, str(a, "form"));
                   if (fi < 0)
                       return fail(QStringLiteral("There's no form %1.").arg(str(a, "form")));
                   QJsonObject form = forms.at(fi).toObject();
                   QJsonArray controls = form.value(QLatin1String("controls")).toArray();
                   const int ei = indexNamed(controls, str(a, "name"));
                   if (ei < 0)
                       return fail(QStringLiteral("There's no element %1 on %2.").arg(str(a, "name"), str(a, "form")));
                   controls.removeAt(ei);
                   form.insert(QStringLiteral("controls"), controls);
                   redock(form);
                   forms.replace(fi, form);
                   l.project.insert(QStringLiteral("forms"), forms);
                   return saved(l.path, l.project, QStringLiteral("Removed %1 from %2.").arg(str(a, "name"), str(a, "form")));
               } };

    t << Tool{ QStringLiteral("set_code"),
               QStringLiteral("Set the JavaScript for one event: an element's (element and event, like clicked) or the form's "
                              "(no element; event load, current, saved, validationFailed, unload, or general for functions and "
                              "variables the form's code shares). Empty code takes it away. studio_manual code.md has the API."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("form"), prop("string", QStringLiteral("The form")) },
                        { QStringLiteral("element"), prop("string", QStringLiteral("The element (none: the form's own code)")) },
                        { QStringLiteral("event"), prop("string", QStringLiteral("The event")) },
                        { QStringLiteral("code"), prop("string", QStringLiteral("JavaScript")) } },
                      { QStringLiteral("path"), QStringLiteral("form"), QStringLiteral("event"), QStringLiteral("code") }),
               [](const QJsonObject &a) {
                   Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QJsonArray forms = l.project.value(QLatin1String("forms")).toArray();
                   const int fi = indexNamed(forms, str(a, "form"));
                   if (fi < 0)
                       return fail(QStringLiteral("There's no form %1.").arg(str(a, "form")));
                   QJsonObject form = forms.at(fi).toObject();
                   const QString event = str(a, "event");
                   const QString code = str(a, "code");
                   QStringList errors;
                   QString where;
                   if (str(a, "element").isEmpty()) {
                       QJsonObject c = form.value(QLatin1String("code")).toObject();
                       checkEvents(str(a, "form"), registry().value(QLatin1String("form")).toObject().value(QLatin1String("events")).toArray(),
                                   QJsonObject{ { event, code } }, &errors, true);
                       if (code.trimmed().isEmpty())
                           c.remove(event);
                       else
                           c.insert(event, code);
                       form.insert(QStringLiteral("code"), c);
                       where = QStringLiteral("%1's %2").arg(str(a, "form"), event);
                   } else {
                       QJsonArray controls = form.value(QLatin1String("controls")).toArray();
                       const int ei = indexNamed(controls, str(a, "element"));
                       if (ei < 0)
                           return fail(QStringLiteral("There's no element %1 on %2.").arg(str(a, "element"), str(a, "form")));
                       QJsonObject el = controls.at(ei).toObject();
                       checkEvents(QStringLiteral("%1, %2").arg(str(a, "form"), str(a, "element")),
                                   elementInfo(str(el, "type")).value(QLatin1String("events")).toArray(), QJsonObject{ { event, code } }, &errors);
                       QJsonObject events = el.value(QLatin1String("events")).toObject();
                       if (code.trimmed().isEmpty())
                           events.remove(event);
                       else
                           events.insert(event, code);
                       if (events.isEmpty())
                           el.remove(QStringLiteral("events"));
                       else
                           el.insert(QStringLiteral("events"), events);
                       controls.replace(ei, el);
                       form.insert(QStringLiteral("controls"), controls);
                       where = QStringLiteral("%1.%2's %3").arg(str(a, "form"), str(a, "element"), event);
                   }
                   if (!errors.isEmpty())
                       return fail(QStringLiteral("Nothing was changed:\n- ") + errors.join(QStringLiteral("\n- ")));
                   forms.replace(fi, form);
                   l.project.insert(QStringLiteral("forms"), forms);
                   return saved(l.path, l.project, code.trimmed().isEmpty() ? QStringLiteral("Took away the code for %1.").arg(where)
                                                                            : QStringLiteral("Set the code for %1.").arg(where));
               } };

    t << Tool{ QStringLiteral("check_project"),
               QStringLiteral("Compile the whole project as Make Program would, the event code too: what would stop it running, "
                              "one problem per line, by form, element, event and line."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) } }, { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   const Ran r = runRunner({ QStringLiteral("--check"), l.path });
                   if (!r.started || r.exitCode < 0)
                       return fail(r.err);
                   QStringList problems;
                   if (r.exitCode != 0)
                       problems += (r.out.isEmpty() ? r.err : r.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                   problems += crossChecks(l.project);
                   if (problems.isEmpty())
                       return text(QStringLiteral("No problems: it compiles, and everything it names is there."));
                   return text(QStringLiteral("Problems:\n- ") + problems.join(QStringLiteral("\n- ")), true);
               } };

    t << Tool{ QStringLiteral("show_form"),
               QStringLiteral("A picture of a form as it opens in the app (its load code run, its table's records showing), "
                              "the inside of the window without the title bar. Look at it after changing a form: alignment, "
                              "text that doesn't fit, overlaps. What the form's code said or got wrong while it opened comes "
                              "with it. No form: the start form."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) }, { QStringLiteral("form"), prop("string", QStringLiteral("The form")) } },
                      { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QTemporaryDir dir;
                   const QString png = dir.filePath(QStringLiteral("form.png"));
                   QStringList args{ QStringLiteral("--shot"), l.path };
                   if (!str(a, "form").isEmpty())
                       args << str(a, "form");
                   args << QStringLiteral("-o") << png;
                   const Ran r = runRunner(args, 30000);
                   QFile file(png);
                   if (!r.started || r.exitCode != 0 || !file.open(QIODevice::ReadOnly))
                       return fail(QStringLiteral("The form wasn't drawn.\n") + r.err);
                   Result res;
                   res.content.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("image") },
                                                   { QStringLiteral("data"), QString::fromLatin1(file.readAll().toBase64()) },
                                                   { QStringLiteral("mimeType"), QStringLiteral("image/png") } });
                   if (!r.err.isEmpty())
                       res.content.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") },
                                                       { QStringLiteral("text"), QStringLiteral("While it opened:\n") + r.err } });
                   return res;
               } };

    t << Tool{ QStringLiteral("run_test"),
               QStringLiteral("Run the app off the screen (muted, on a copy of its SQLite database: the project's own data "
                              "isn't changed) with a JavaScript test driving it as a person would, and say what happened: "
                              "every check, message box, file box, log line, and mistake in the app's code (by form, element, "
                              "event and line). The start form is open when the script starts; App, Db and the forms are in "
                              "reach, and `Test`: Test.click(form, element[, menuItemName | gridRow]), Test.doubleClick(form, grid, "
                              "row), Test.type(form, textBox, text[, pressEnter]), Test.pick(form, comboOrList, indexOrText | slider, "
                              "value), Test.tick(form, timer), Test.answer(buttonTextOrIndex) for the next message box (else its "
                              "first button), Test.choose(pathOrPaths) for the next file box (else it's cancelled), "
                              "Test.check(ok, what), Test.equal(got, want, what), Test.log(...), Test.wait(ms, function), "
                              "Test.done(), Test.element(form, name), Test.form(name), Test.open(name, args), Test.messages(), "
                              "Test.lastMessage(). Message boxes and readTags answer after the code that asked goes on: "
                              "Test.wait(200, ...) before checking what they did. It ends when nothing is left to wait for, at "
                              "Test.done(), or at the timeout. An error result: a check failed, the code went wrong, or it timed out."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("script"), prop("string", QStringLiteral("The test: JavaScript")) },
                        { QStringLiteral("timeout"), prop("number", QStringLiteral("Seconds before it's stopped (default 20, at most 120)")) },
                        { QStringLiteral("start"), prop("boolean", QStringLiteral("Open the start form first (default true)")) },
                        { QStringLiteral("serverDatabase"), prop("boolean", QStringLiteral("A MySQL or PostgreSQL project: run on that database itself (its data can change). Without it, such a project isn't run.")) } },
                      { QStringLiteral("path"), QStringLiteral("script") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QTemporaryDir dir;
                   if (!dir.isValid())
                       return fail(QStringLiteral("There's nowhere to make the database's copy."));
                   // the database: a copy of the SQLite file (or none: an empty one)
                   const QJsonObject database = l.project.value(QLatin1String("database")).toObject();
                   const QString driver = database.value(QLatin1String("driver")).toString(QStringLiteral("sqlite"));
                   if (driver != QLatin1String("sqlite") && !a.value(QLatin1String("serverDatabase")).toBool())
                       return fail(QStringLiteral("This project keeps its data in %1, which can't be copied for a test. "
                                                  "serverDatabase: true runs it on that database itself (its data can change).").arg(driver));
                   if (driver == QLatin1String("sqlite")) {
                       const QString file = database.value(QLatin1String("file")).toString();
                       const QString from = QFileInfo(file).isAbsolute() ? file : QFileInfo(l.path).absoluteDir().filePath(file);
                       const QString to = QFileInfo(file).isAbsolute() ? dir.filePath(QFileInfo(file).fileName()) : dir.filePath(file);
                       QDir().mkpath(QFileInfo(to).absolutePath());
                       if (QFileInfo::exists(from) && !QFile::copy(from, to))
                           return fail(QStringLiteral("%1 couldn't be copied for the test.").arg(from));
                   }
                   const QString script = dir.filePath(QStringLiteral("test.js"));
                   QFile f(script);
                   if (!f.open(QIODevice::WriteOnly) || f.write(str(a, "script").toUtf8()) < 0)
                       return fail(QStringLiteral("The script couldn't be written."));
                   f.close();
                   const double timeout = qBound(1.0, a.value(QLatin1String("timeout")).toDouble(20), 120.0);
                   QStringList args{ QStringLiteral("--test"), script, l.path, QStringLiteral("--timeout"), QString::number(timeout) };
                   if (driver == QLatin1String("sqlite"))
                       args << QStringLiteral("--data") << dir.path();
                   if (a.contains(QLatin1String("start")) && !a.value(QLatin1String("start")).toBool())
                       args << QStringLiteral("--no-start");
                   const Ran r = runRunner(args, int(timeout * 1000) + 15000);
                   if (!r.started)
                       return fail(r.err);
                   QString said = r.out;
                   // (stderr: what didn't go through Qt, minus the video drivers' noise)
                   QStringList extra;
                   for (const QString &line : r.err.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                       if (!line.contains(QLatin1String("VDPAU")) && !line.startsWith(QLatin1Char('[')))
                           extra << line;
                   }
                   if (!extra.isEmpty())
                       said += QStringLiteral("\n(stderr)\n") + extra.join(QLatin1Char('\n'));
                   if (r.exitCode < 0)
                       said += QStringLiteral("\nIt crashed or was stopped.");
                   return text(said, r.exitCode != 0);
               } };

    t << Tool{ QStringLiteral("build_program"),
               QStringLiteral("Make the app a program file (Make Program): one file that runs without the Studio. Its data is "
                              "kept in ~/.local/share/<app name>. Only if the project compiles."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) },
                        { QStringLiteral("output"), prop("string", QStringLiteral("The program file to write (default: the app's name, beside the project)")) } },
                      { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   QString out = str(a, "output").trimmed();
                   if (out.isEmpty())
                       out = QFileInfo(l.path).absoluteDir().filePath(
                           l.project.value(QLatin1String("app")).toObject().value(QLatin1String("name")).toString(QStringLiteral("app")));
                   out = resolve(out);
                   const Ran r = runRunner({ QStringLiteral("--build"), l.path, QStringLiteral("-o"), out });
                   if (!r.started || r.exitCode != 0)
                       return fail(QStringLiteral("No program was made:\n") + (r.out.isEmpty() ? r.err : r.out));
                   return text(QStringLiteral("Made %1 (%2 KB).").arg(out).arg(QFileInfo(out).size() / 1024));
               } };

    t << Tool{ QStringLiteral("run_app"),
               QStringLiteral("Start the app on the user's desktop (as the Studio's Start does) for them to try. It runs on its "
                              "own: what it says comes back if it stops within a few seconds."),
               schema({ { QStringLiteral("path"), prop("string", pathHelp) } }, { QStringLiteral("path") }),
               [](const QJsonObject &a) {
                   const Loaded l = loadArg(a);
                   if (!l.ok())
                       return fail(l.error);
                   if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
                       return fail(QStringLiteral("There's no desktop to show it on (no WAYLAND_DISPLAY)."));
                   const QString exe = runner();
                   if (exe.isEmpty())
                       return fail(QStringLiteral("win95apprun (Wayland Studio's runner) isn't installed."));
                   // started on its own (it outlives this server), its output to a file
                   const QString log = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                           .filePath(QStringLiteral("win95studio-mcp-%1.log").arg(QCoreApplication::applicationPid()));
                   QProcess p;
                   p.setProgram(exe);
                   p.setArguments({ l.path });
                   p.setWorkingDirectory(QFileInfo(l.path).absolutePath());
                   p.setStandardOutputFile(log);
                   p.setStandardErrorFile(log);
                   qint64 pid = 0;
                   if (!p.startDetached(&pid))
                       return fail(QStringLiteral("It didn't start: %1").arg(p.errorString()));
                   // stopped already? (a form that doesn't compile, a database that won't open)
                   for (int i = 0; i < 30 && QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)); ++i)
                       QThread::msleep(100);
                   QFile f(log);
                   const QString said = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
                   if (!QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))
                       return fail(QStringLiteral("It stopped straight away.\n") + said);
                   return text(QStringLiteral("Running (process %1) on the desktop.%2").arg(pid)
                                   .arg(said.isEmpty() ? QString() : QStringLiteral("\nIt said:\n") + said));
               } };

    return t;
}

// ------------------------------------------------------------------ JSON-RPC

void send(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    std::cout.write(line.constData(), line.size());
    std::cout << '\n';
    std::cout.flush();
}

QJsonObject reply(const QJsonValue &id, const QJsonValue &result)
{
    return { { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), id }, { QStringLiteral("result"), result } };
}

QJsonObject replyError(const QJsonValue &id, int code, const QString &message)
{
    return { { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
             { QStringLiteral("id"), id },
             { QStringLiteral("error"), QJsonObject{ { QStringLiteral("code"), code }, { QStringLiteral("message"), message } } } };
}

const QString instructions = QStringLiteral(
    "Wayland Studio makes Windows 95-style desktop apps (tables with validation, forms with JavaScript event code) "
    "for Wayland 95. Before building, call studio_reference (topic format), then topics elements and fields as needed. "
    "Check with check_project and look at every form with show_form before saying an app is done.");

// one message; a reply if it wants one
std::optional<QJsonObject> handle(const QJsonObject &msg, const QList<Tool> &all)
{
    const QString method = msg.value(QLatin1String("method")).toString();
    const QJsonValue id = msg.value(QLatin1String("id"));
    const bool notification = !msg.contains(QLatin1String("id"));
    const QJsonObject params = msg.value(QLatin1String("params")).toObject();

    if (method == QLatin1String("initialize")) {
        static const QStringList known{ QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05") };
        const QString asked = params.value(QLatin1String("protocolVersion")).toString();
        return reply(id, QJsonObject{
                             { QStringLiteral("protocolVersion"), known.contains(asked) ? asked : known.first() },
                             { QStringLiteral("capabilities"), QJsonObject{ { QStringLiteral("tools"), QJsonObject() } } },
                             { QStringLiteral("serverInfo"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("wayland-studio") },
                                                                          { QStringLiteral("title"), QStringLiteral("Wayland Studio") },
                                                                          { QStringLiteral("version"), QStringLiteral("1.0") } } },
                             { QStringLiteral("instructions"), instructions },
                         });
    }
    if (notification)
        return std::nullopt; // notifications/initialized, cancelled...
    if (method == QLatin1String("ping"))
        return reply(id, QJsonObject());
    if (method == QLatin1String("tools/list")) {
        QJsonArray list;
        for (const Tool &t : all)
            list.append(QJsonObject{ { QStringLiteral("name"), t.name },
                                     { QStringLiteral("description"), t.description },
                                     { QStringLiteral("inputSchema"), t.schema } });
        return reply(id, QJsonObject{ { QStringLiteral("tools"), list } });
    }
    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QLatin1String("name")).toString();
        for (const Tool &t : all) {
            if (t.name != name)
                continue;
            const Result r = t.run(params.value(QLatin1String("arguments")).toObject());
            return reply(id, QJsonObject{ { QStringLiteral("content"), r.content }, { QStringLiteral("isError"), r.error } });
        }
        return replyError(id, -32602, QStringLiteral("There's no tool %1.").arg(name));
    }
    return replyError(id, -32601, QStringLiteral("Not handled: %1").arg(method));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95studio-mcp"));
    if (app.arguments().contains(QStringLiteral("--help"))) {
        std::cout << "win95studio-mcp: Wayland Studio's tools for Claude, an MCP server on stdin and stdout.\n"
                     "  claude mcp add wayland-studio -- win95studio-mcp\n";
        return 0;
    }
    const QList<Tool> all = tools();
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parse);
        if (doc.isNull()) {
            send(replyError(QJsonValue::Null, -32700, QStringLiteral("Not JSON: %1").arg(parse.errorString())));
            continue;
        }
        if (doc.isArray()) {
            QJsonArray replies;
            for (const QJsonValue &m : doc.array()) {
                if (const auto r = handle(m.toObject(), all))
                    replies.append(*r);
            }
            if (!replies.isEmpty()) {
                const QByteArray out = QJsonDocument(replies).toJson(QJsonDocument::Compact);
                std::cout.write(out.constData(), out.size());
                std::cout << '\n';
                std::cout.flush();
            }
            continue;
        }
        if (const auto r = handle(doc.object(), all))
            send(*r);
    }
    return 0;
}
