#pragma once

#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <atomic>

// Image Writer's engine: writes a disk image (a hybrid .iso, .img) to a USB
// drive, then reads it back to check it, like KDE's ISO Image Writer.
//
// Everything privileged goes through UDisks2: it lists the drives, unmounts
// them, and opens the drive for writing after a password prompt (polkit,
// which Wayland 95 answers with its own dialog). So the program itself
// never runs as root.
//
// Exposed to QML as the singleton `Writer`.
class ImageWriter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Writer)
    QML_SINGLETON

    // [{ path (UDisks object), device ("/dev/sdb"), name, size }]
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    // "idle", "preparing" (unmounting, asking for the password), "writing",
    // "syncing" (the drive catching up), "verifying", "done", "failed",
    // "cancelled"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(qint64 done READ done NOTIFY progressChanged)
    Q_PROPERTY(qint64 total READ total NOTIFY progressChanged)
    // bytes a second, averaged over the last few seconds
    Q_PROPERTY(double speed READ speed NOTIFY progressChanged)

public:
    explicit ImageWriter(QObject *parent = nullptr);
    ~ImageWriter() override;

    QVariantList drives() const { return m_drives; }
    QString state() const { return m_state; }
    bool busy() const;
    QString error() const { return m_error; }
    qint64 done() const { return m_done; }
    qint64 total() const { return m_total; }
    double speed() const { return m_speed; }

    // about an image file: { ok, size, bootable (has a PC boot record, so
    // it starts from a USB drive), error }
    Q_INVOKABLE QVariantMap imageInfo(const QString &path) const;
    Q_INVOKABLE void refresh();
    // erase `drivePath` (a drives[].path) and write `image` to it
    Q_INVOKABLE void write(const QString &image, const QString &drivePath, bool verify);
    Q_INVOKABLE void cancel();
    // back to "idle" after done/failed/cancelled
    Q_INVOKABLE void reset();

signals:
    void drivesChanged();
    void stateChanged();
    void progressChanged();

private slots:
    void scheduleRefresh();

private:
    void setState(const QString &state, const QString &error = {});
    void unmountNext();
    void openDevice();
    void startWorker(int fd);
    void pollProgress();

    QVariantList m_drives;
    QString m_state = QStringLiteral("idle");
    QString m_error;

    // the job
    QString m_image, m_block;
    bool m_verify = true;
    QStringList m_toUnmount;          // filesystems still mounted on the drive
    QThread *m_worker = nullptr;
    std::atomic<qint64> m_progress{0};
    std::atomic<int> m_phase{0};      // 0 writing, 1 syncing, 2 verifying
    std::atomic<bool> m_cancel{false};
    QString m_workerError;            // set by the worker before it ends
    qint64 m_done = 0, m_total = 0;
    double m_speed = 0;
    QTimer m_poll, m_refreshTimer;
    QElapsedTimer m_clock;
    QList<QPair<qint64, qint64>> m_samples; // (ms, bytes) for the speed
};
