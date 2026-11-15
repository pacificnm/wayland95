#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

// Small filesystem helpers for QML (file dialogs etc.), exposed as the
// singleton `Win95Fs` in the Win95Ui module.
class Win95Fs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Win95Fs(QObject *parent = nullptr);

    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE bool isDir(const QString &path) const;
    Q_INVOKABLE bool isFile(const QString &path) const;
    Q_INVOKABLE QString parentDir(const QString &path) const;
    Q_INVOKABLE QString fileName(const QString &path) const;
    Q_INVOKABLE QString suffix(const QString &path) const;
    // the name to show for a folder ("/" stays "/")
    Q_INVOKABLE QString displayName(const QString &dir) const;
    // `name` relative to `dir`, with ~ expanded; absolute names win
    Q_INVOKABLE QString resolve(const QString &dir, const QString &name) const;
    Q_INVOKABLE QUrl toUrl(const QString &path) const;
};
