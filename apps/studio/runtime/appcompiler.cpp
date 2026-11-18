#include "appcompiler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>

namespace {

const QByteArray kMagic = QByteArrayLiteral("W95APP01");

// a string as QML source (JSON's escaping is JavaScript's)
QString quoted(const QString &s)
{
    const QByteArray json = QJsonDocument(QJsonArray{ s }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

// a property's value as QML source, by the type elements.json gives it
QString literal(const QJsonObject &prop, const QJsonValue &value)
{
    const QString type = prop.value(QLatin1String("type")).toString();
    if (type == QLatin1String("bool"))
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (type == QLatin1String("int"))
        return QString::number(qRound64(value.toDouble()));
    if (type == QLatin1String("real"))
        return QString::number(value.toDouble(), 'g', 12);
    if (type == QLatin1String("menu") || type == QLatin1String("toolbar")) {
        // (the Menu Editor's items: JSON is a JavaScript literal)
        const QJsonArray array = value.toArray();
        return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    }
    if (type == QLatin1String("graph"))
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (type == QLatin1String("list")) {
        QStringList items;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            items << quoted(item.toString());
        return QLatin1Char('[') + items.join(QLatin1String(", ")) + QLatin1Char(']');
    }
    return quoted(value.toString());
}

// user code, indented to sit inside a handler
QString indented(const QString &code, const QString &indent)
{
    QStringList lines = code.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    for (QString &line : lines) {
        if (!line.trimmed().isEmpty())
            line.prepend(indent);
    }
    return lines.join(QLatin1Char('\n'));
}

struct Writer {
    QString text;
    int line = 1;
    void add(const QString &s)
    {
        text += s;
        line += s.count(QLatin1Char('\n'));
    }
};

// a picture: into the assets, and the URL the runner serves it at
QString asset(const QString &path, const QString &projectDir, QJsonObject *assets, QString *problem)
{
    if (path.isEmpty())
        return QString();
    QString file = path;
    if (file.startsWith(QLatin1String("~/")))
        file = QDir::homePath() + file.mid(1);
    if (QFileInfo(file).isRelative())
        file = QDir(projectDir).filePath(file);
    if (!assets)
        return QUrl::fromLocalFile(file).toString(); // (just showing the QML)
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *problem = QStringLiteral("the picture %1 can't be read").arg(path);
        return QString();
    }
    const QByteArray data = f.readAll();
    // one name per file, whatever folder it came from
    QString name = QFileInfo(file).fileName();
    const QString encoded = QString::fromLatin1(data.toBase64());
    for (int n = 2; assets->contains(name) && assets->value(name).toString() != encoded; ++n)
        name = QStringLiteral("%1-%2").arg(n).arg(QFileInfo(file).fileName());
    assets->insert(name, encoded);
    return QStringLiteral("image://app/") + name;
}

void writeHandler(Writer &out, const QString &indent, const QString &signal, const QString &code,
                  const QString &what, QJsonArray *lines)
{
    QString handler = QStringLiteral("on") + signal.left(1).toUpper() + signal.mid(1);
    out.add(indent + handler + QStringLiteral(": {\n"));
    const int from = out.line;
    out.add(indented(code, indent + QStringLiteral("    ")) + QLatin1Char('\n'));
    if (lines)
        lines->append(QJsonObject{ { QStringLiteral("from"), from },
                                   { QStringLiteral("to"), out.line - 1 },
                                   { QStringLiteral("what"), what } });
    out.add(indent + QStringLiteral("}\n"));
}

} // namespace

namespace AppCompiler {

QJsonObject registry()
{
    static QJsonObject cached;
    if (cached.isEmpty()) {
        QFile file(QStringLiteral(":/qt/qml/Win95App/elements.json"));
        if (file.open(QIODevice::ReadOnly))
            cached = QJsonDocument::fromJson(file.readAll()).object();
    }
    return cached;
}

QJsonObject elementInfo(const QString &type)
{
    const QJsonArray elements = registry().value(QLatin1String("elements")).toArray();
    for (const QJsonValue &e : elements) {
        if (e.toObject().value(QLatin1String("type")).toString() == type)
            return e.toObject();
    }
    return {};
}

QJsonArray elementProps(const QString &type)
{
    const QJsonObject reg = registry();
    const QJsonObject info = elementInfo(type);
    QJsonArray props = info.value(QLatin1String("props")).toArray();
    QStringList have;
    for (const QJsonValue &p : std::as_const(props))
        have << p.toObject().value(QLatin1String("name")).toString();
    auto append = [&](const QString &set) {
        const QJsonArray more = reg.value(set).toArray();
        for (const QJsonValue &p : more) {
            if (!have.contains(p.toObject().value(QLatin1String("name")).toString()))
                props.append(p);
        }
    };
    const QJsonArray sets = info.value(QLatin1String("sets")).toArray();
    for (const QJsonValue &set : sets)
        append(set.toString());
    append(QStringLiteral("common"));
    return props;
}

QString nameProblem(const QString &name, bool element)
{
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    static const QStringList reserved{
        QStringLiteral("form"), QStringLiteral("parent"), QStringLiteral("root"), QStringLiteral("this"),
        QStringLiteral("var"), QStringLiteral("let"), QStringLiteral("const"), QStringLiteral("function"),
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("do"), QStringLiteral("return"), QStringLiteral("new"), QStringLiteral("delete"),
        QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("typeof"), QStringLiteral("true"),
        QStringLiteral("false"), QStringLiteral("null"), QStringLiteral("undefined"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("break"), QStringLiteral("continue"),
        QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("finally"), QStringLiteral("throw"),
        QStringLiteral("class"), QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("property"),
        QStringLiteral("signal"), QStringLiteral("readonly"), QStringLiteral("id"), QStringLiteral("width"),
        QStringLiteral("height"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("title"),
        QStringLiteral("table"), QStringLiteral("record"), QStringLiteral("errors"), QStringLiteral("db"),
        QStringLiteral("app"), QStringLiteral("args"), QStringLiteral("visible"), QStringLiteral("color"),
        QStringLiteral("content"), QStringLiteral("data"), QStringLiteral("dirty"),
    };
    if (name.isEmpty())
        return QStringLiteral("A name is needed.");
    if (!identifier.match(name).hasMatch())
        return QStringLiteral("Names start with a letter and have only letters, digits and _ in them.");
    if (element) {
        if (!name.at(0).isLower())
            return QStringLiteral("An element's name starts with a small letter (its code uses the name).");
        if (reserved.contains(name) || name == QLatin1String("App") || name == QLatin1String("Db"))
            return QStringLiteral("\"%1\" already means something in an app's code.").arg(name);
    }
    return QString();
}

QString formQml(const QJsonObject &form, const QJsonObject &project, const QString &projectDir,
                QJsonObject *assets, QJsonArray *lines, QStringList *problems)
{
    const QString formName = form.value(QLatin1String("name")).toString();
    QStringList formNames;
    for (const QJsonValue &f : project.value(QLatin1String("forms")).toArray())
        formNames << f.toObject().value(QLatin1String("name")).toString();
    auto problem = [&](const QString &where, const QString &what) {
        if (problems)
            problems->append(where.isEmpty() ? QStringLiteral("%1: %2").arg(formName, what)
                                             : QStringLiteral("%1, %2: %3").arg(formName, where, what));
    };
    const QJsonObject reg = registry();
    const QJsonObject formInfo = reg.value(QLatin1String("form")).toObject();
    const QString in1 = QStringLiteral("    "), in2 = QStringLiteral("        ");

    Writer out;
    out.add(QStringLiteral("// %1 - written by Wayland Studio; change the project, not this\n").arg(formName));
    out.add(QStringLiteral("import QtQuick\nimport Win95Ui\nimport Win95App\n\nAppForm {\n"));
    out.add(in1 + QStringLiteral("id: form\n"));
    out.add(in1 + QStringLiteral("formName: %1\n").arg(quoted(formName)));
    out.add(in1 + QStringLiteral("width: %1\n").arg(qMax(40, form.value(QLatin1String("width")).toInt(400))));
    out.add(in1 + QStringLiteral("height: %1\n").arg(qMax(20, form.value(QLatin1String("height")).toInt(300))));
    const QJsonObject formProps = form.value(QLatin1String("props")).toObject();
    const QJsonArray formPropList = formInfo.value(QLatin1String("props")).toArray();
    for (const QJsonValue &p : formPropList) {
        const QJsonObject prop = p.toObject();
        const QString name = prop.value(QLatin1String("name")).toString();
        const QJsonValue value = formProps.contains(name) ? formProps.value(name) : prop.value(QLatin1String("default"));
        if (name == QLatin1String("title")) {
            out.add(in1 + QStringLiteral("title: %1\n").arg(quoted(value.toString().isEmpty() ? formName : value.toString())));
            continue;
        }
        if (value == prop.value(QLatin1String("default")))
            continue;
        if (prop.value(QLatin1String("type")).toString() == QLatin1String("image")) {
            QString why;
            const QString url = asset(value.toString(), projectDir, assets, &why);
            if (!why.isEmpty())
                problem(QString(), why);
            if (!url.isEmpty())
                out.add(in1 + QStringLiteral("%1: %2\n").arg(name, quoted(url)));
            continue;
        }
        out.add(in1 + QStringLiteral("%1: %2\n").arg(name, literal(prop, value)));
    }

    // (General): the form's own functions and properties
    const QJsonObject formCode = form.value(QLatin1String("code")).toObject();
    const QString general = formCode.value(QLatin1String("general")).toString();
    if (!general.trimmed().isEmpty()) {
        out.add(QLatin1Char('\n') + in1 + QStringLiteral("// (General)\n"));
        const int from = out.line;
        out.add(indented(general, in1) + QLatin1Char('\n'));
        if (lines)
            lines->append(QJsonObject{ { QStringLiteral("from"), from }, { QStringLiteral("to"), out.line - 1 },
                                       { QStringLiteral("what"), QStringLiteral("(General)") } });
    }

    QStringList names;
    const QJsonArray controls = form.value(QLatin1String("controls")).toArray();
    for (const QJsonValue &c : controls) {
        const QJsonObject control = c.toObject();
        const QString type = control.value(QLatin1String("type")).toString();
        const QString name = control.value(QLatin1String("name")).toString();
        const QJsonObject info = elementInfo(type);
        if (info.isEmpty()) {
            problem(name, QStringLiteral("there is no element called %1").arg(type));
            continue;
        }
        if (const QString why = nameProblem(name, true); !why.isEmpty()) {
            problem(name.isEmpty() ? type : name, why);
            continue;
        }
        if (names.contains(name)) {
            problem(name, QStringLiteral("two elements have this name"));
            continue;
        }
        names << name;

        out.add(QLatin1Char('\n') + in1 + info.value(QLatin1String("component")).toString() + QStringLiteral(" {\n"));
        out.add(in2 + QStringLiteral("id: %1\n").arg(name));
        out.add(in2 + QStringLiteral("objectName: %1\n").arg(quoted(name)));
        out.add(in2 + QStringLiteral("form: form\n"));
        if (info.value(QLatin1String("dock")).toBool()) {
            // across the top: as wide as the form, whatever its size
            out.add(in2 + QStringLiteral("x: 0; y: %1; width: form.width; height: %2\n")
                              .arg(control.value(QLatin1String("y")).toInt())
                              .arg(qMax(1, control.value(QLatin1String("height")).toInt())));
        } else {
            out.add(in2 + QStringLiteral("x: %1; y: %2; width: %3; height: %4\n")
                              .arg(control.value(QLatin1String("x")).toInt())
                              .arg(control.value(QLatin1String("y")).toInt())
                              .arg(qMax(1, control.value(QLatin1String("width")).toInt()))
                              .arg(qMax(1, control.value(QLatin1String("height")).toInt())));
        }
        const QJsonObject props = control.value(QLatin1String("props")).toObject();
        const QJsonArray propList = elementProps(type);
        for (const QJsonValue &p : propList) {
            const QJsonObject prop = p.toObject();
            const QString propName = prop.value(QLatin1String("name")).toString();
            if (!props.contains(propName) || props.value(propName) == prop.value(QLatin1String("default")))
                continue;
            if (prop.value(QLatin1String("type")).toString() == QLatin1String("image")) {
                QString why;
                const QString url = asset(props.value(propName).toString(), projectDir, assets, &why);
                if (!why.isEmpty())
                    problem(name, why);
                if (!url.isEmpty())
                    out.add(in2 + QStringLiteral("%1: %2\n").arg(propName, quoted(url)));
                continue;
            }
            if (prop.value(QLatin1String("type")).toString() == QLatin1String("media")) {
                // a sound or a video: a web address as it is; a file inside the
                // program (App.asset() unpacks it), or with Embed off, where it is
                const QString value = props.value(propName).toString().trimmed();
                static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
                if (value.isEmpty())
                    continue;
                if (scheme.match(value).hasMatch()) {
                    out.add(in2 + QStringLiteral("%1: %2\n").arg(propName, quoted(value)));
                    continue;
                }
                const bool embed = !props.contains(QLatin1String("embed")) || props.value(QLatin1String("embed")).toBool();
                QString why;
                const QString url = asset(value, projectDir, embed ? assets : nullptr, &why);
                if (!why.isEmpty())
                    problem(name, why.replace(QLatin1String("picture"), QLatin1String("sound or video")));
                if (url.startsWith(QLatin1String("image://app/")))
                    out.add(in2 + QStringLiteral("%1: App.asset(%2)\n").arg(propName, quoted(url.mid(12))));
                else if (!url.isEmpty())
                    out.add(in2 + QStringLiteral("%1: %2\n").arg(propName, quoted(url)));
                continue;
            }
            const QString propType = prop.value(QLatin1String("type")).toString();
            if (propType == QLatin1String("graph")) {
                // a graph of a table reads one that's in the project, and its fields
                const QJsonObject g = props.value(propName).toObject();
                const QString tableName = g.value(QLatin1String("table")).toString();
                if (g.value(QLatin1String("source")).toString() != QLatin1String("code") && !tableName.isEmpty()) {
                    QJsonObject table;
                    for (const QJsonValue &t : project.value(QLatin1String("tables")).toArray()) {
                        if (t.toObject().value(QLatin1String("name")).toString() == tableName)
                            table = t.toObject();
                    }
                    if (table.isEmpty()) {
                        problem(name, QStringLiteral("its graph reads %1, which isn't in the project").arg(tableName));
                    } else {
                        QStringList fields = { QStringLiteral("id") };
                        for (const QJsonValue &f : table.value(QLatin1String("fields")).toArray())
                            fields << f.toObject().value(QLatin1String("name")).toString();
                        QStringList used = { g.value(QLatin1String("labelField")).toString(), g.value(QLatin1String("filterField")).toString() };
                        // (a stock graph's)
                        for (const char *key : { "openField", "highField", "lowField", "closeField", "volumeField" })
                            used << g.value(QLatin1String(key)).toString();
                        for (const QJsonValue &s : g.value(QLatin1String("series")).toArray())
                            used << s.toObject().value(QLatin1String("field")).toString();
                        for (const QString &f : std::as_const(used)) {
                            if (!f.isEmpty() && !fields.contains(f))
                                problem(name, QStringLiteral("its graph uses %1, which isn't a field of %2").arg(f, tableName));
                        }
                    }
                }
            }
            if (propType == QLatin1String("menu") || propType == QLatin1String("toolbar")) {
                // an item that opens a form names one that's in the project
                auto checkItem = [&](const QJsonObject &item, const QString &where) {
                    if (item.value(QLatin1String("action")).toString() != QLatin1String("openForm"))
                        return;
                    const QString target = item.value(QLatin1String("form")).toString();
                    const QString what = item.value(QLatin1String("text")).toString().remove(QLatin1Char('&'));
                    if (target.isEmpty())
                        problem(name, QStringLiteral("%1%2 opens a form, but which isn't chosen").arg(where, what));
                    else if (!formNames.contains(target))
                        problem(name, QStringLiteral("%1%2 opens %3, which isn't in the project").arg(where, what, target));
                };
                for (const QJsonValue &v : props.value(propName).toArray()) {
                    const QJsonObject top = v.toObject();
                    checkItem(top, QString());
                    const QString menuText = top.value(QLatin1String("text")).toString().remove(QLatin1Char('&'));
                    for (const QJsonValue &sub : top.value(QLatin1String("items")).toArray())
                        checkItem(sub.toObject(), menuText + QStringLiteral(" > "));
                }
            }
            if (propType == QLatin1String("toolbar")) {
                // the buttons' pictures, into the program
                QJsonArray buttons = props.value(propName).toArray();
                for (qsizetype i = 0; i < buttons.size(); ++i) {
                    QJsonObject b = buttons.at(i).toObject();
                    const QString picture = b.value(QLatin1String("picture")).toString();
                    if (picture.isEmpty())
                        continue;
                    QString why;
                    b.insert(QStringLiteral("picture"), asset(picture, projectDir, assets, &why));
                    if (!why.isEmpty())
                        problem(name, why);
                    buttons.replace(i, b);
                }
                out.add(in2 + QStringLiteral("%1: %2\n").arg(propName, literal(prop, buttons)));
                continue;
            }
            out.add(in2 + QStringLiteral("%1: %2\n").arg(propName, literal(prop, props.value(propName))));
        }
        const QJsonObject events = control.value(QLatin1String("events")).toObject();
        const QJsonArray eventList = info.value(QLatin1String("events")).toArray();
        for (const QJsonValue &e : eventList) {
            const QJsonObject event = e.toObject();
            const QString eventName = event.value(QLatin1String("name")).toString();
            const QString code = events.value(eventName).toString();
            if (code.trimmed().isEmpty())
                continue;
            writeHandler(out, in2, event.value(QLatin1String("signal")).toString(), code,
                         name + QLatin1Char('.') + eventName, lines);
        }
        out.add(in1 + QStringLiteral("}\n"));
    }

    const QJsonArray formEvents = formInfo.value(QLatin1String("events")).toArray();
    for (const QJsonValue &e : formEvents) {
        const QJsonObject event = e.toObject();
        const QString eventName = event.value(QLatin1String("name")).toString();
        const QString code = formCode.value(eventName).toString();
        if (code.trimmed().isEmpty())
            continue;
        out.add(QStringLiteral("\n"));
        writeHandler(out, in1, event.value(QLatin1String("signal")).toString(), code,
                     QStringLiteral("form.") + eventName, lines);
    }
    out.add(QStringLiteral("}\n"));
    return out.text;
}

QJsonObject compile(const QJsonObject &project, const QString &projectDir, QStringList *problems)
{
    QJsonObject forms, lines, assets;
    QJsonArray order;
    const QJsonArray formList = project.value(QLatin1String("forms")).toArray();
    for (const QJsonValue &f : formList) {
        const QJsonObject form = f.toObject();
        const QString name = form.value(QLatin1String("name")).toString();
        if (const QString why = nameProblem(name, false); !why.isEmpty()) {
            if (problems)
                problems->append(QStringLiteral("The form \"%1\": %2").arg(name, why));
            continue;
        }
        if (forms.contains(name)) {
            if (problems)
                problems->append(QStringLiteral("Two forms are named %1.").arg(name));
            continue;
        }
        QJsonArray formLines;
        forms.insert(name, formQml(form, project, projectDir, &assets, &formLines, problems));
        lines.insert(name, formLines);
        order.append(name);
    }
    if (order.isEmpty() && problems)
        problems->append(QStringLiteral("The project has no forms."));

    QJsonObject app = project.value(QLatin1String("app")).toObject();
    QString start = app.value(QLatin1String("startForm")).toString();
    if (!forms.contains(start)) {
        if (!start.isEmpty() && problems)
            problems->append(QStringLiteral("The startup form, %1, isn't in the project.").arg(start));
        start = order.isEmpty() ? QString() : order.first().toString();
    }
    app.insert(QStringLiteral("startForm"), start);

    return { { QStringLiteral("format"), 1 },
             { QStringLiteral("app"), app },
             { QStringLiteral("database"), project.value(QLatin1String("database")) },
             { QStringLiteral("tables"), project.value(QLatin1String("tables")) },
             { QStringLiteral("apis"), project.value(QLatin1String("apis")) },
             // (Project > Properties: the AI Chat's Anthropic API key)
             { QStringLiteral("ai"), project.value(QLatin1String("ai")) },
             { QStringLiteral("forms"), forms },
             { QStringLiteral("order"), order },
             { QStringLiteral("lines"), lines },
             { QStringLiteral("assets"), assets } };
}

QByteArray pack(const QJsonObject &compiled)
{
    return qCompress(QJsonDocument(compiled).toJson(QJsonDocument::Compact), 9);
}

QJsonObject unpack(const QByteArray &payload)
{
    return QJsonDocument::fromJson(qUncompress(payload)).object();
}

QByteArray payloadOf(const QString &programPath, qint64 *stubSize)
{
    QFile file(programPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    if (stubSize)
        *stubSize = size;
    const qint64 trailer = 8 + kMagic.size();
    if (size < trailer || !file.seek(size - trailer))
        return {};
    const QByteArray end = file.read(trailer);
    if (end.size() != trailer || !end.endsWith(kMagic))
        return {};
    const quint64 length = qFromLittleEndian<quint64>(end.constData());
    if (length == 0 || qint64(length) > size - trailer)
        return {};
    if (!file.seek(size - trailer - qint64(length)))
        return {};
    if (stubSize)
        *stubSize = size - trailer - qint64(length);
    return file.read(qint64(length));
}

bool writeProgram(const QString &runnerPath, const QJsonObject &compiled, const QString &outPath, QString *error)
{
    qint64 stubSize = 0;
    payloadOf(runnerPath, &stubSize);
    QFile runner(runnerPath);
    if (!runner.open(QIODevice::ReadOnly) || stubSize <= 0) {
        *error = QStringLiteral("The runner (%1) can't be read.").arg(runnerPath);
        return false;
    }
    const QByteArray stub = runner.read(stubSize);
    const QByteArray payload = pack(compiled);
    QByteArray length(8, '\0');
    qToLittleEndian<quint64>(quint64(payload.size()), length.data());

    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)
        || out.write(stub) != stub.size()
        || out.write(payload) != payload.size()
        || out.write(length) != length.size()
        || out.write(kMagic) != kMagic.size()
        || !out.commit()) {
        *error = QStringLiteral("%1 can't be written: %2").arg(outPath, out.errorString());
        return false;
    }
    QFile::setPermissions(outPath, QFile::permissions(outPath) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther
                                       | QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    return true;
}

} // namespace AppCompiler
