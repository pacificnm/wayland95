#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QtQmlIntegration/qqmlintegration.h>

// Applets that are programs of their own (Network Manager: its web pages
// need Chromium, kept out of the Control Panel): started here, the copy
// next to win95control first (a build tree), else the installed one.
// Exposed to QML as the singleton `Launcher`.
class Launcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE bool start(const QString &program, const QStringList &args = {}) const
    {
        if (program.isEmpty() || program.contains(QLatin1Char('/')))
            return false;
        const QString local = QCoreApplication::applicationDirPath() + QLatin1Char('/') + program;
        return QProcess::startDetached(QFileInfo::exists(local) ? local : program, args);
    }
};
