#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Read-only facts about the machine for the Control Panel applets.
// Exposed to QML as the singleton `SystemInfo`.
class SystemInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString kernel READ kernel CONSTANT)
    Q_PROPERTY(QString cpu READ cpu CONSTANT)
    Q_PROPERTY(QString memory READ memory CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString timeZoneId READ timeZoneId CONSTANT)
    Q_PROPERTY(QString timeZoneName READ timeZoneName CONSTANT)

public:
    explicit SystemInfo(QObject *parent = nullptr);

    QString osName() const;
    QString kernel() const;
    QString cpu() const;
    QString memory() const;
    QString hostName() const;
    QString userName() const;
    QString qtVersion() const;
    QString timeZoneId() const;
    QString timeZoneName() const;

    // live numbers for the Performance tab:
    // { memoryFreePercent, memoryText, swapText, uptimeText, loadText }
    Q_INVOKABLE QVariantMap performance() const;

    // [{ code: "us", name: "English (US)" }, ...] from the xkb rules
    Q_INVOKABLE QVariantList keyboardLayouts() const;

    // image files from the usual wallpaper folders and ~/Pictures
    Q_INVOKABLE QStringList wallpapers() const;
};
