#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// The problem reports in ~/.local/state/wayland95/crashes (crashhandler.h,
// and wayland95-session for the compositor): listed for Control Panel >
// Problem Reports, watched by the desktop so it can say when a program
// crashed.
//
// Exposed to QML as the singleton `ProblemReports`.
class ProblemReports : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // newest first: [{ file, program, name, version, time (ms), signal,
    //                  summary, seen }]
    Q_PROPERTY(QVariantList reports READ reports NOTIFY reportsChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)

public:
    explicit ProblemReports(QObject *parent = nullptr);

    QVariantList reports() const { return m_reports; }
    QString folder() const { return m_dir; }

    Q_INVOKABLE void reload();
    // the whole report
    Q_INVOKABLE QString text(const QString &file) const;
    Q_INVOKABLE bool remove(const QString &file);
    Q_INVOKABLE void removeAll();
    // reports the desktop hasn't told the user about yet, and marking them
    Q_INVOKABLE QVariantList unseen() const;
    Q_INVOKABLE void markSeen(const QString &file);
    Q_INVOKABLE void markAllSeen();
    // a GitHub "new issue" page filled in with the report, to look over
    // and submit (nothing is sent without that)
    Q_INVOKABLE QString issueUrl(const QString &file) const;
    Q_INVOKABLE void copy(const QString &file) const;
    // "File Manager" for win95files
    Q_INVOKABLE static QString displayName(const QString &program);

signals:
    void reportsChanged();
    // a new report appeared while we were watching
    void reportAdded(const QVariantMap &report);

private:
    QVariantMap parse(const QString &file) const;
    QSet<QString> readSeen() const;
    void writeSeen(const QSet<QString> &seen);

    QString m_dir;
    QVariantList m_reports;
    QSet<QString> m_known;
    QFileSystemWatcher m_watcher;
};
