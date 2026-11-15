#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Everything the System Monitor shows, read straight from the kernel's
// /proc files every `interval` ms - no other libraries or daemons.
//
// Exposed to QML as the singleton `Monitor`.
class SystemMonitor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Monitor)
    QML_SINGLETON

    // update period; 0 pauses
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    // the latest sample:
    // { cpu, cores: [..], memUsed, memTotal, swapUsed, swapTotal,
    //   netRx, netTx, diskRead, diskWrite (bytes/s), processes, threads,
    //   uptime (s), load: "0.52 0.48 0.40" }
    Q_PROPERTY(QVariantMap now READ now NOTIFY updated)
    // the last `historyLength` samples of each: { cpu, mem, swap, netRx,
    // netTx, diskRead, diskWrite } (percent, or bytes/s)
    Q_PROPERTY(QVariantMap history READ history NOTIFY updated)
    Q_PROPERTY(int historyLength READ historyLength CONSTANT)
    // only gathered while wanted (the Processes tab is open):
    // [{ pid, name, command, user, cpu (% of the machine), memory (bytes), mine }]
    Q_PROPERTY(bool processesWanted READ processesWanted WRITE setProcessesWanted NOTIFY processesWantedChanged)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY processesChanged)
    Q_PROPERTY(int coreCount READ coreCount CONSTANT)

public:
    explicit SystemMonitor(QObject *parent = nullptr);

    int interval() const { return m_interval; }
    void setInterval(int ms);
    QVariantMap now() const { return m_now; }
    QVariantMap history() const;
    int historyLength() const { return 60; }
    bool processesWanted() const { return m_processesWanted; }
    void setProcessesWanted(bool wanted);
    QVariantList processes() const { return m_processes; }
    int coreCount() const { return m_coreCount; }

    // ends a process: politely (SIGTERM) or not (SIGKILL). Returns an
    // empty string, or why it couldn't.
    Q_INVOKABLE QString endProcess(int pid, bool force);
    // re-read right away
    Q_INVOKABLE void refresh();

signals:
    void intervalChanged();
    void updated();
    void processesWantedChanged();
    void processesChanged();

private:
    struct CpuTimes {
        quint64 busy = 0;
        quint64 total = 0;
    };
    QList<CpuTimes> readCpu() const; // [0] = all cores, then each core
    void sample();
    void sampleProcesses(quint64 totalDelta);
    void push(const QString &key, qreal value);
    QString userName(uint uid);

    QTimer m_timer;
    int m_interval = 1000;
    int m_coreCount = 1;
    qint64 m_lastSampleMs = 0;
    QList<CpuTimes> m_lastCpu;
    quint64 m_lastNetRx = 0, m_lastNetTx = 0, m_lastDiskRead = 0, m_lastDiskWrite = 0;
    bool m_haveLast = false;

    QVariantMap m_now;
    QHash<QString, QList<qreal>> m_history;

    bool m_processesWanted = false;
    QVariantList m_processes;
    QHash<int, quint64> m_lastProcTicks;
    QHash<uint, QString> m_userNames;
    uint m_myUid = 0;
};
