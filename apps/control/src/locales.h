#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// The regions (locales) for Regional Settings: every one Qt knows (the
// desktop's own programs format with Qt's data), marked when the system
// has it installed too (which other programs need), plus the computer's
// default from systemd-localed.
//
// Exposed to QML as the singleton `Locales`.
class Locales : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // [{ name: "de_DE.UTF-8", display: "German (Germany)", installed }]
    Q_PROPERTY(QVariantList all READ all CONSTANT)
    // what this session uses now (LANG), and the computer's default
    Q_PROPERTY(QString session READ session CONSTANT)
    Q_PROPERTY(QString system READ system NOTIFY systemChanged)

public:
    explicit Locales(QObject *parent = nullptr);

    QVariantList all() const { return m_all; }
    QString session() const;
    QString system() const { return m_system; }

    Q_INVOKABLE QString displayName(const QString &name) const;
    Q_INVOKABLE bool isInstalled(const QString &name) const;
    // the computer's default (asks for a password); answers systemSet
    Q_INVOKABLE void setSystem(const QString &name);

signals:
    void systemChanged();
    void systemSet(bool ok, const QString &message);

private:
    void readSystem();

    QVariantList m_all;
    QStringList m_installed; // "de_DE"
    QString m_system;
};
