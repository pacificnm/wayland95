#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QObject>
#include <QProcess>

// Control Panel > Services, where Ollama and its models are installed:
// the copy next to this program (a build tree), else the installed one
class ControlPanel : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool openServices() const
    {
        const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/win95control");
        return QProcess::startDetached(QFileInfo::exists(local) ? local : QStringLiteral("win95control"),
                                       { QStringLiteral("--applet"), QStringLiteral("services") });
    }
};
