#pragma once

#include <QHash>
#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// System Log: the systemd journal, read with journalctl.
//
//  - "system": everything the computer logged. Someone in systemd-journal
//    or adm (or root) reads it straight away; anyone else sees only their
//    own messages until they ask for all of them, which goes through
//    session/wayland95-services (pkexec: an administrator's password,
//    remembered for a few minutes).
//  - "kernel": the kernel's messages (dmesg), the same way.
//  - "user": your own session's services and programs (journalctl --user).
//
// The newest 2000 messages of one start-up (boot), as bad as `priority`
// or worse (0 emergency ... 3 error, 4 warning, 6 info, 7 debug).
//
// Exposed to QML as the singleton `SystemLog`.
class SystemLog : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SystemLog)
    QML_SINGLETON

    // newest first: [{ key (journal cursor), time (ms), priority, source
    //    (identifier), unit, pid, message }]
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // what went wrong with the last load, or ""
    Q_PROPERTY(QString error READ error NOTIFY entriesChanged)
    // only your own messages were shown (not allowed to read the rest)
    Q_PROPERTY(bool partial READ partial NOTIFY entriesChanged)
    // allowed to read the whole journal without a password
    Q_PROPERTY(bool canReadAll READ canReadAll CONSTANT)
    // asked for the whole journal with an administrator's password
    Q_PROPERTY(bool admin READ admin NOTIFY adminChanged)
    // [{ index (0 this start-up, -1 the one before...), first, last (ms) }],
    // newest first
    Q_PROPERTY(QVariantList boots READ boots NOTIFY bootsChanged)

public:
    explicit SystemLog(QObject *parent = nullptr);

    QVariantList entries() const { return m_entries; }
    bool loading() const { return m_loading > 0; }
    QString error() const { return m_error; }
    bool partial() const { return m_partial; }
    bool canReadAll() const { return m_canReadAll; }
    bool admin() const { return m_admin; }
    QVariantList boots() const { return m_boots; }

    // log: system, kernel or user; boot: 0, -1...; priority: 0-7
    Q_INVOKABLE void load(const QString &log, int boot, int priority);
    // the start-ups: as the user, or (admin) through the helper
    Q_INVOKABLE void loadBoots(const QString &log);
    // from now on, read the computer's log with an administrator's password
    Q_INVOKABLE void setAdmin(bool on);

    // every field journald kept for an entry, FIELD=value, one per line
    Q_INVOKABLE QString fields(const QString &key) const;
    // an entry as one line of text: date time source[pid]: message
    Q_INVOKABLE QString line(const QString &key) const;
    Q_INVOKABLE void copy(const QString &text) const;
    // writes `keys`' lines to `path`: "" or what went wrong
    Q_INVOKABLE QString save(const QString &path, const QStringList &keys) const;

    // "Error", "Warning"... for a priority
    Q_INVOKABLE static QString priorityName(int priority);

signals:
    void entriesChanged();
    void loadingChanged();
    void adminChanged();
    void bootsChanged();

private:
    using Done = std::function<void(int code, const QByteArray &out, const QString &err)>;
    void run(const QString &program, const QStringList &args, const Done &done);
    bool useHelper(const QString &log) const;
    QString helperPath() const;
    void finish(quint64 generation, const QByteArray &json, const QString &err, bool partial);

    QVariantList m_entries;
    QHash<QString, QJsonObject> m_raw;
    QVariantList m_boots;
    QString m_error;
    bool m_partial = false;
    bool m_canReadAll = false;
    bool m_admin = false;
    int m_loading = 0;
    quint64 m_generation = 0;
    quint64 m_bootGeneration = 0;
};
