#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// One IPP attribute to send: a value tag (0x44 keyword, 0x45 uri, 0x21
// integer...), its name, and one or more values.
struct IppAttribute
{
    quint8 tag;
    QByteArray name;
    QList<QVariant> values;
};

// Printers, through CUPS: the printers and their print queues, read and
// changed over IPP (the printing protocol CUPS speaks) on CUPS's local
// socket. That's the same thing lpstat and the CUPS web pages use, without
// parsing anything's output. Adding a printer goes through lpadmin, which
// also sets up driverless (IPP Everywhere) printers.
//
// Managing printers needs a CUPS administrator: someone in the lpadmin
// group (Debian puts the first user there).
//
// Exposed to QML as the singleton `Printers`.
class PrinterManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Printers)
    QML_SINGLETON

    // CUPS is running
    Q_PROPERTY(bool available READ available NOTIFY printersChanged)
    // [{ name, info, location, makeModel, uri, state ("idle", "printing",
    // "stopped"), stateMessage, reasons, accepting, shared, jobs, isDefault }]
    Q_PROPERTY(QVariantList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QString defaultPrinter READ defaultPrinter NOTIFY printersChanged)
    // documents waiting or printing: [{ id, printer, name, owner, state
    // ("waiting", "held", "printing", "stopped"), size (KB), created, pages,
    // message }]
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    // a long operation is running (finding printers or drivers, adding one)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit PrinterManager(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVariantList printers() const { return m_printers; }
    QString defaultPrinter() const { return m_default; }
    QVariantList jobs() const { return m_jobs; }
    bool busy() const { return m_busy > 0; }

    Q_INVOKABLE void refresh();

    // the queue
    Q_INVOKABLE void setPaused(const QString &printer, bool paused);
    Q_INVOKABLE void purge(const QString &printer);
    Q_INVOKABLE void cancelJob(int id);
    Q_INVOKABLE void setJobHeld(int id, bool held);
    Q_INVOKABLE void printTestPage(const QString &printer);

    // the printer
    // your default printer (in ~/.cups/lpoptions, which programs follow)
    Q_INVOKABLE void setDefault(const QString &printer);
    Q_INVOKABLE void setProperties(const QString &printer, const QString &info, const QString &location, bool shared);
    Q_INVOKABLE void deletePrinter(const QString &printer);

    // Add Printer: printers CUPS can see (answers devicesFound), drivers
    // (answers driversFound), then adding one (answers addFinished)
    Q_INVOKABLE void findDevices();
    Q_INVOKABLE void findDrivers();
    Q_INVOKABLE void addPrinter(const QString &name, const QString &uri, const QString &driver,
                                const QString &info, const QString &location, bool makeDefault);
    // a name CUPS accepts, made from a printer's description
    Q_INVOKABLE QString suggestName(const QString &description) const;

signals:
    void printersChanged();
    void jobsChanged();
    void busyChanged();
    // something the user asked for didn't work
    void failed(const QString &message);
    // [{ uri, info, makeModel, kind ("network", "usb", "other"), deviceId }]
    void devicesFound(const QVariantList &devices);
    // [{ name, make, makeModel }]
    void driversFound(const QVariantList &drivers);
    void addFinished(bool ok, const QString &message);

private:
    using Groups = QList<QVariantMap>;
    using Handler = std::function<void(int status, const Groups &groups, const QString &error)>;
    // one IPP request to CUPS; `path` is "/", "/admin/", "/jobs/"...
    void ipp(quint16 operation, const QByteArray &path, const QList<IppAttribute> &operationAttrs,
             const QList<IppAttribute> &printerAttrs, const QByteArray &document, Handler handler);
    // for actions: reports failure, refreshes after
    void act(quint16 operation, const QByteArray &path, const QList<IppAttribute> &attrs,
             const QList<IppAttribute> &printerAttrs = {}, const QByteArray &document = {});
    QList<IppAttribute> baseAttrs(const QString &printerUri) const;
    static QString printerUri(const QString &name);
    QString userDefault() const;
    void setBusy(bool on);

    bool m_available = false;
    QVariantList m_printers;
    QVariantList m_jobs;
    QString m_default;
    QString m_serverDefault;
    int m_busy = 0;
    quint32 m_requestId = 1;
    QTimer m_poll;
};
