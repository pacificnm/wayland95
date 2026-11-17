#include "displays.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

Displays::Displays(QObject *parent) : QObject(parent)
{
    // the compositor writes a new file and moves it into place: watch the
    // folder as well as the file
    const QString dir = QFileInfo(statePath()).absolutePath();
    if (!dir.isEmpty() && QFileInfo(dir).isDir())
        m_watcher.addPath(dir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Displays::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Displays::reload);
    reload();
}

QString Displays::statePath() const
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QString socket = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (runtime.isEmpty() || socket.isEmpty())
        return QString();
    return runtime + QStringLiteral("/wayland95-displays-") + QFileInfo(socket).fileName() + QStringLiteral(".ini");
}

QString Displays::configPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/wayland95/displays.ini");
}

void Displays::reload()
{
    const QString path = statePath();
    QVariantList outputs;
    const bool available = !path.isEmpty() && QFileInfo::exists(path);
    if (available) {
        QSettings state(path, QSettings::IniFormat);
        int number = 0;
        // (in the file's order: the compositor's, oldest first)
        QStringList groups;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
                    groups << line.mid(1, line.size() - 2);
            }
        }
        for (const QString &name : std::as_const(groups)) {
            state.beginGroup(name);
            QVariantMap o;
            o[QStringLiteral("name")] = name;
            const QString description = state.value(QStringLiteral("Description")).toString();
            const QString model = state.value(QStringLiteral("Model")).toString();
            const QString make = state.value(QStringLiteral("Make")).toString();
            o[QStringLiteral("description")] = description;
            QString what = (make + QLatin1Char(' ') + model).trimmed();
            if (what.isEmpty() || what == QLatin1String("Unknown Unknown"))
                what = name;
            else
                what += QStringLiteral(" (") + name + QLatin1Char(')');
            o[QStringLiteral("label")] = QString::number(++number) + QStringLiteral(". ") + what;
            o[QStringLiteral("number")] = number;
            o[QStringLiteral("enabled")] = state.value(QStringLiteral("Enabled")).toBool();
            o[QStringLiteral("primary")] = state.value(QStringLiteral("Primary")).toBool();
            for (const char *key : { "Width", "Height", "Refresh", "Transform", "X", "Y", "LogicalWidth", "LogicalHeight" }) {
                QString k = QString::fromLatin1(key);
                k[0] = k[0].toLower();
                o[k] = state.value(QString::fromLatin1(key)).toInt();
            }
            o[QStringLiteral("scale")] = state.value(QStringLiteral("Scale"), 1.0).toDouble();
            o[QStringLiteral("adaptiveSyncSupported")] = state.value(QStringLiteral("AdaptiveSyncSupported")).toBool();
            o[QStringLiteral("adaptiveSync")] = state.value(QStringLiteral("AdaptiveSync")).toBool();
            QVariantList modes;
            const QStringList list = state.value(QStringLiteral("Modes")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (QString m : list) {
                const bool preferred = m.endsWith(QLatin1Char('*'));
                if (preferred)
                    m.chop(1);
                const int at = m.indexOf(QLatin1Char('@')), by = m.indexOf(QLatin1Char('x'));
                if (at < 0 || by < 0)
                    continue;
                modes << QVariantMap{ { QStringLiteral("width"), m.left(by).toInt() },
                                      { QStringLiteral("height"), m.mid(by + 1, at - by - 1).toInt() },
                                      { QStringLiteral("refresh"), m.mid(at + 1).toInt() },
                                      { QStringLiteral("preferred"), preferred } };
            }
            o[QStringLiteral("modes")] = modes;
            state.endGroup();
            outputs << o;
        }
    }
    if (available == m_available && outputs == m_outputs)
        return;
    m_available = available;
    m_outputs = outputs;
    Q_EMIT changed();
}

void Displays::apply(const QVariantList &settings)
{
    const QString path = configPath();
    QFile old(path);
    m_hadBefore = old.open(QIODevice::ReadOnly);
    m_before = m_hadBefore ? old.readAll() : QByteArray();
    old.close();

    // displays that aren't connected now keep their settings
    QString kept;
    {
        QStringList names;
        for (const QVariant &v : settings)
            names << v.toMap().value(QStringLiteral("name")).toString();
        bool keep = false;
        const QList<QByteArray> lines = m_before.split('\n');
        for (const QByteArray &raw : lines) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
                keep = !names.contains(line.mid(1, line.size() - 2));
            if (keep && !line.isEmpty())
                kept += line + QLatin1Char('\n');
        }
    }

    QString text;
    QTextStream out(&text);
    for (const QVariant &v : settings) {
        const QVariantMap o = v.toMap();
        out << '[' << o.value(QStringLiteral("name")).toString() << "]\n";
        out << "Enabled=" << (o.value(QStringLiteral("enabled")).toBool() ? "true" : "false") << '\n';
        const int width = o.value(QStringLiteral("width")).toInt(), height = o.value(QStringLiteral("height")).toInt();
        if (width > 0 && height > 0)
            out << "Mode=" << width << 'x' << height << '@' << o.value(QStringLiteral("refresh")).toInt() << '\n';
        out << "Scale=" << QString::number(o.value(QStringLiteral("scale"), 1.0).toDouble(), 'f', 2) << '\n';
        out << "Transform=" << o.value(QStringLiteral("transform")).toInt() << '\n';
        out << "X=" << o.value(QStringLiteral("x")).toInt() << '\n';
        out << "Y=" << o.value(QStringLiteral("y")).toInt() << '\n';
        out << "AdaptiveSync=" << (o.value(QStringLiteral("adaptiveSync")).toBool() ? "true" : "false") << '\n';
        out << "Primary=" << (o.value(QStringLiteral("primary")).toBool() ? "true" : "false") << "\n\n";
    }
    out << kept;
    out.flush();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path); // (a new file moved into place: the compositor sees one change)
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text.toUtf8());
        file.commit();
    }
}

void Displays::revert()
{
    const QString path = configPath();
    if (!m_hadBefore) {
        // there were no settings: every display at its own best again (an
        // empty file says so; removing it wouldn't be noticed)
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.commit();
        return;
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_before);
        file.commit();
    }
}
