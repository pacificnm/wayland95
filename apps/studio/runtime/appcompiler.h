#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Wayland Studio's compiler: a project (the .w95 file's JSON) into what the
// runner (win95apprun) runs, and that into a program file.
//
// A compiled app is { app, database, tables, forms: { Name: qml },
// order: [names], lines: { Name: [{ from, to, what }] }, assets: { name:
// base64 } }: each form as a QML file (an AppForm with the form's elements
// and its event code as handlers), the tables for the ORM, and the pictures.
//
// A program is the runner itself with the compiled app (compressed) on the
// end: "<runner><payload><length: 8 bytes><W95APP01>". The runner looks for
// that on its own file when it starts.
namespace AppCompiler {

// elements.json: the elements, their properties and events
QJsonObject registry();
QJsonObject elementInfo(const QString &type);
// every property an element of this type has, common ones included
QJsonArray elementProps(const QString &type);

// Wayland 95's icons (Win95Ui's icons.json): [{ name, label }], and
// whether `name` is one of them
QJsonArray icons();
bool isIcon(const QString &name);

// "" if `name` can name a form or an element (an identifier that starts
// with a small letter for elements: QML's rule for ids)
QString nameProblem(const QString &name, bool element);

// `problems` gets what would stop it running ("Form1, button1: ..."); the
// result is still as complete as could be made
QJsonObject compile(const QJsonObject &project, const QString &projectDir, QStringList *problems);

// one form's QML (what View > QML Source shows)
QString formQml(const QJsonObject &form, const QJsonObject &project, const QString &projectDir,
                QJsonObject *assets = nullptr, QJsonArray *lines = nullptr, QStringList *problems = nullptr);

QByteArray pack(const QJsonObject &compiled);
QJsonObject unpack(const QByteArray &payload);
// the payload on the end of a program file ("" if there's none), and how
// long the program is without it
QByteArray payloadOf(const QString &programPath, qint64 *stubSize = nullptr);
// the runner at `runnerPath` (minus any payload of its own) + `compiled` -> `outPath`
bool writeProgram(const QString &runnerPath, const QJsonObject &compiled, const QString &outPath, QString *error);

} // namespace AppCompiler
