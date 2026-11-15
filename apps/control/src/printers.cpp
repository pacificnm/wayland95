#include "printers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>
#include <pwd.h>
#include <unistd.h>

namespace {

// IPP operations (RFC 8011 and CUPS's own)
enum : quint16 {
    PrintJob = 0x0002, CancelJob = 0x0008, GetJobs = 0x000A, HoldJob = 0x000C, ReleaseJob = 0x000D,
    PausePrinter = 0x0010, ResumePrinter = 0x0011, PurgeJobs = 0x0012,
    CupsGetDefault = 0x4001, CupsGetPrinters = 0x4002, CupsAddModifyPrinter = 0x4003,
    CupsDeletePrinter = 0x4004, CupsGetDevices = 0x400B, CupsGetPpds = 0x400C,
};
// value tags
enum : quint8 {
    TagInteger = 0x21, TagBoolean = 0x22, TagEnum = 0x23, TagDateTime = 0x31, TagRange = 0x33,
    TagBeginCollection = 0x34, TagTextLang = 0x35, TagNameLang = 0x36, TagEndCollection = 0x37,
    TagText = 0x41, TagName = 0x42, TagKeyword = 0x44, TagUri = 0x45, TagCharset = 0x47,
    TagLanguage = 0x48, TagMimeType = 0x49,
};

QString userName()
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QStringLiteral("user");
}

void put16(QByteArray &b, quint16 v) { b.append(char(v >> 8)).append(char(v & 0xff)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v >> 16)); put16(b, quint16(v & 0xffff)); }

void encode(QByteArray &out, const IppAttribute &a)
{
    for (int i = 0; i < a.values.size(); ++i) {
        out.append(char(a.tag));
        // later values of the same attribute have no name
        const QByteArray name = i == 0 ? a.name : QByteArray();
        put16(out, quint16(name.size()));
        out.append(name);
        const QVariant &v = a.values.at(i);
        if (a.tag == TagInteger || a.tag == TagEnum) {
            put16(out, 4);
            put32(out, quint32(v.toInt()));
        } else if (a.tag == TagBoolean) {
            put16(out, 1);
            out.append(char(v.toBool() ? 1 : 0));
        } else {
            const QByteArray s = v.toString().toUtf8();
            put16(out, quint16(s.size()));
            out.append(s);
        }
    }
}

// the attribute groups of an IPP response, each as name -> value (or a
// list, for several values); collections are skipped
QList<QVariantMap> decode(const QByteArray &body, int *status)
{
    QList<QVariantMap> groups;
    *status = -1;
    if (body.size() < 8)
        return groups;
    const auto *d = reinterpret_cast<const uchar *>(body.constData());
    *status = qFromBigEndian<quint16>(d + 2);
    int pos = 8;
    QVariantMap current;
    QString lastName;
    bool inGroup = false;
    int depth = 0;
    auto add = [&](const QString &name, const QVariant &value) {
        if (current.contains(name)) {
            QVariant old = current.value(name);
            QVariantList list = old.typeId() == QMetaType::QVariantList ? old.toList() : QVariantList{ old };
            list << value;
            current.insert(name, list);
        } else {
            current.insert(name, value);
        }
    };
    while (pos < body.size()) {
        const quint8 tag = d[pos++];
        if (tag < 0x10) {
            // a new group (or the end)
            if (inGroup)
                groups << current;
            current.clear();
            inGroup = tag != 0x03;
            if (tag == 0x03)
                break;
            continue;
        }
        if (pos + 2 > body.size())
            break;
        const int nameLen = qFromBigEndian<quint16>(d + pos);
        pos += 2;
        const QString name = QString::fromUtf8(body.mid(pos, nameLen));
        pos += nameLen;
        if (pos + 2 > body.size())
            break;
        const int valueLen = qFromBigEndian<quint16>(d + pos);
        pos += 2;
        const QByteArray value = body.mid(pos, valueLen);
        pos += valueLen;

        if (tag == TagBeginCollection) {
            ++depth;
            continue;
        }
        if (tag == TagEndCollection) {
            depth = qMax(0, depth - 1);
            continue;
        }
        if (depth > 0)
            continue;
        if (!name.isEmpty())
            lastName = name;
        QVariant v;
        const auto *p = reinterpret_cast<const uchar *>(value.constData());
        if ((tag == TagInteger || tag == TagEnum) && valueLen == 4) {
            v = int(qFromBigEndian<qint32>(p));
        } else if (tag == TagBoolean && valueLen == 1) {
            v = p[0] != 0;
        } else if (tag == TagDateTime && valueLen == 11) {
            const QDate date(qFromBigEndian<quint16>(p), p[2], p[3]);
            const QTime time(p[4], p[5], p[6]);
            const int offset = (p[9] * 3600 + p[10] * 60) * (p[8] == '-' ? -1 : 1);
            v = QDateTime(date, time, QTimeZone::fromSecondsAheadOfUtc(offset));
        } else if (tag == TagRange && valueLen == 8) {
            v = QStringLiteral("%1-%2").arg(qFromBigEndian<qint32>(p)).arg(qFromBigEndian<qint32>(p + 4));
        } else if ((tag == TagTextLang || tag == TagNameLang) && valueLen >= 4) {
            const int langLen = qFromBigEndian<quint16>(p);
            const int textLen = qFromBigEndian<quint16>(p + 2 + langLen);
            v = QString::fromUtf8(value.mid(4 + langLen, textLen));
        } else if (tag >= 0x40) {
            v = QString::fromUtf8(value);
        } else {
            v = QVariant(); // out-of-band (no value, unknown...) or not used here
        }
        add(lastName, v);
    }
    if (inGroup)
        groups << current;
    return groups;
}

QByteArray decodeChunked(const QByteArray &in)
{
    QByteArray out;
    int pos = 0;
    while (pos < in.size()) {
        const int eol = in.indexOf("\r\n", pos);
        if (eol < 0)
            break;
        bool ok = false;
        const int size = in.mid(pos, eol - pos).split(';').first().trimmed().toInt(&ok, 16);
        if (!ok || size == 0)
            break;
        out += in.mid(eol + 2, size);
        pos = eol + 2 + size + 2;
    }
    return out;
}

QStringList asList(const QVariant &v)
{
    if (v.typeId() == QMetaType::QVariantList) {
        QStringList out;
        for (const QVariant &x : v.toList())
            out << x.toString();
        return out;
    }
    return v.isNull() ? QStringList() : QStringList{ v.toString() };
}

QString lastSegment(const QString &uri) { return uri.section(QLatin1Char('/'), -1); }

QString lpadminPath()
{
    for (const char *p : { "/usr/sbin/lpadmin", "/usr/bin/lpadmin", "/sbin/lpadmin" })
        if (QFileInfo(QLatin1String(p)).isExecutable())
            return QLatin1String(p);
    return QStandardPaths::findExecutable(QStringLiteral("lpadmin"));
}

// readable versions of CUPS's state reasons
QString reasonText(const QString &r)
{
    static const QHash<QString, QString> texts = {
        { QStringLiteral("media-empty"), QStringLiteral("Out of paper") },
        { QStringLiteral("media-needed"), QStringLiteral("Out of paper") },
        { QStringLiteral("media-jam"), QStringLiteral("Paper jam") },
        { QStringLiteral("toner-empty"), QStringLiteral("Out of toner") },
        { QStringLiteral("toner-low"), QStringLiteral("Toner low") },
        { QStringLiteral("marker-supply-empty"), QStringLiteral("Out of ink") },
        { QStringLiteral("marker-supply-low"), QStringLiteral("Ink low") },
        { QStringLiteral("door-open"), QStringLiteral("Door open") },
        { QStringLiteral("cover-open"), QStringLiteral("Cover open") },
        { QStringLiteral("offline"), QStringLiteral("Offline") },
        { QStringLiteral("offline-report"), QStringLiteral("Offline") },
        { QStringLiteral("paused"), QStringLiteral("Paused") },
        { QStringLiteral("connecting-to-device"), QStringLiteral("Connecting") },
    };
    QString key = r;
    key.remove(QRegularExpression(QStringLiteral("-(error|warning|report)$")));
    return texts.value(key, texts.value(r));
}

} // namespace

PrinterManager::PrinterManager(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(2500);
    connect(&m_poll, &QTimer::timeout, this, &PrinterManager::refresh);
    m_poll.start();
    refresh();
}

QString PrinterManager::printerUri(const QString &name)
{
    return QStringLiteral("ipp://localhost/printers/") + name;
}

QList<IppAttribute> PrinterManager::baseAttrs(const QString &uri) const
{
    return {
        { TagCharset, "attributes-charset", { QStringLiteral("utf-8") } },
        { TagLanguage, "attributes-natural-language", { QStringLiteral("en") } },
        { TagUri, "printer-uri", { uri } },
        { TagName, "requesting-user-name", { userName() } },
    };
}

void PrinterManager::ipp(quint16 operation, const QByteArray &path, const QList<IppAttribute> &operationAttrs,
                         const QList<IppAttribute> &printerAttrs, const QByteArray &document, Handler handler)
{
    QByteArray body;
    body.append(char(2)).append(char(0)); // IPP 2.0
    put16(body, operation);
    put32(body, m_requestId++);
    body.append(char(0x01)); // operation attributes
    for (const IppAttribute &a : operationAttrs)
        encode(body, a);
    if (!printerAttrs.isEmpty()) {
        body.append(char(0x04)); // printer attributes
        for (const IppAttribute &a : printerAttrs)
            encode(body, a);
    }
    body.append(char(0x03));
    body.append(document);

    // CUPS's local socket (it knows who we are from it); TCP if there's none
    QIODevice *socket = nullptr;
    QString socketPath;
    for (const char *p : { "/run/cups/cups.sock", "/var/run/cups/cups.sock" })
        if (QFileInfo::exists(QLatin1String(p))) {
            socketPath = QLatin1String(p);
            break;
        }

    QByteArray request = "POST " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/ipp\r\n"
                         "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n";
    // On the local socket, administration (pausing, deleting...) is allowed
    // to members of lpadmin once we say who we are; CUPS checks the name
    // against the socket's peer credentials, so it can't be faked.
    if (!socketPath.isEmpty())
        request += "Authorization: PeerCred " + userName().toUtf8() + "\r\n";
    request += "\r\n" + body;
    auto response = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    auto finish = [this, response, done, handler]() {
        if (*done)
            return;
        *done = true;
        const QByteArray &r = *response;
        const int headerEnd = r.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            handler(-1, {}, QStringLiteral("CUPS, the printing service, isn't running."));
            return;
        }
        const QByteArray head = r.left(headerEnd);
        const int httpStatus = head.mid(9, 3).toInt();
        if (httpStatus == 401 || httpStatus == 403) {
            handler(-1, {}, QStringLiteral("Your account isn't allowed to manage printers. An administrator can allow it "
                                           "by adding you to the lpadmin group."));
            return;
        }
        QByteArray payload = r.mid(headerEnd + 4);
        if (head.toLower().contains("transfer-encoding: chunked"))
            payload = decodeChunked(payload);
        int status = -1;
        const Groups groups = decode(payload, &status);
        QString error;
        if (status >= 0x0400) {
            // the operation group's status-message says why
            for (const QVariantMap &g : groups)
                if (g.contains(QStringLiteral("status-message")))
                    error = g.value(QStringLiteral("status-message")).toString();
            if (error.isEmpty())
                error = QStringLiteral("The printing service refused (error 0x%1).").arg(status, 4, 16, QLatin1Char('0'));
        }
        handler(status, groups, error);
    };

    if (!socketPath.isEmpty()) {
        auto *s = new QLocalSocket(this);
        socket = s;
        connect(s, &QLocalSocket::connected, s, [s, request] { s->write(request); });
        connect(s, &QLocalSocket::readyRead, s, [s, response] { *response += s->readAll(); });
        connect(s, &QLocalSocket::disconnected, this, [s, response, finish] { *response += s->readAll(); s->deleteLater(); finish(); });
        connect(s, &QLocalSocket::errorOccurred, this, [s, finish](QLocalSocket::LocalSocketError e) {
            if (e != QLocalSocket::PeerClosedError) {
                s->deleteLater();
                finish();
            }
        });
        s->connectToServer(socketPath);
    } else {
        auto *s = new QTcpSocket(this);
        socket = s;
        connect(s, &QTcpSocket::connected, s, [s, request] { s->write(request); });
        connect(s, &QTcpSocket::readyRead, s, [s, response] { *response += s->readAll(); });
        connect(s, &QTcpSocket::disconnected, this, [s, response, finish] { *response += s->readAll(); s->deleteLater(); finish(); });
        connect(s, &QTcpSocket::errorOccurred, this, [s, finish](QAbstractSocket::SocketError e) {
            if (e != QAbstractSocket::RemoteHostClosedError) {
                s->deleteLater();
                finish();
            }
        });
        s->connectToHost(QStringLiteral("127.0.0.1"), 631);
    }
    // finding devices can take a while; nothing else should
    QTimer::singleShot(operation == CupsGetDevices ? 30000 : 10000, socket, [socket, finish] {
        socket->close();
        finish();
    });
}

void PrinterManager::act(quint16 operation, const QByteArray &path, const QList<IppAttribute> &attrs,
                         const QList<IppAttribute> &printerAttrs, const QByteArray &document)
{
    ipp(operation, path, attrs, printerAttrs, document, [this](int, const Groups &, const QString &error) {
        if (!error.isEmpty())
            emit failed(error);
        refresh();
    });
}

void PrinterManager::setBusy(bool on)
{
    m_busy += on ? 1 : -1;
    emit busyChanged();
}

QString PrinterManager::userDefault() const
{
    // lpoptions -d writes "Default NAME" (followed by options) here
    QFile f(QDir::homePath() + QStringLiteral("/.cups/lpoptions"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith("Default "))
            return QString::fromUtf8(line.mid(8).trimmed().split(' ').first().split('/').first());
    return QString();
}

void PrinterManager::refresh()
{
    QList<IppAttribute> attrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    attrs << IppAttribute{ TagKeyword, "requested-attributes",
                           { QStringLiteral("printer-name"), QStringLiteral("printer-info"), QStringLiteral("printer-location"),
                             QStringLiteral("printer-make-and-model"), QStringLiteral("device-uri"), QStringLiteral("printer-state"),
                             QStringLiteral("printer-state-message"), QStringLiteral("printer-state-reasons"),
                             QStringLiteral("printer-is-accepting-jobs"), QStringLiteral("printer-is-shared"),
                             QStringLiteral("queued-job-count") } };
    ipp(CupsGetPrinters, "/", attrs, {}, {}, [this](int status, const Groups &groups, const QString &) {
        const bool available = status >= 0;
        QVariantList list;
        for (const QVariantMap &g : groups) {
            const QString name = g.value(QStringLiteral("printer-name")).toString();
            if (name.isEmpty())
                continue;
            const int state = g.value(QStringLiteral("printer-state")).toInt();
            QStringList reasons;
            for (const QString &r : asList(g.value(QStringLiteral("printer-state-reasons"))))
                if (r != QLatin1String("none"))
                    reasons << r;
            QStringList readable;
            for (const QString &r : std::as_const(reasons)) {
                const QString t = reasonText(r);
                if (!t.isEmpty() && !readable.contains(t))
                    readable << t;
            }
            list << QVariantMap{
                { QStringLiteral("name"), name },
                { QStringLiteral("info"), g.value(QStringLiteral("printer-info")).toString() },
                { QStringLiteral("location"), g.value(QStringLiteral("printer-location")).toString() },
                { QStringLiteral("makeModel"), g.value(QStringLiteral("printer-make-and-model")).toString() },
                { QStringLiteral("uri"), g.value(QStringLiteral("device-uri")).toString() },
                { QStringLiteral("state"), state == 4 ? QStringLiteral("printing") : state == 5 ? QStringLiteral("stopped") : QStringLiteral("idle") },
                { QStringLiteral("stateMessage"), g.value(QStringLiteral("printer-state-message")).toString() },
                { QStringLiteral("reasons"), readable },
                { QStringLiteral("accepting"), g.value(QStringLiteral("printer-is-accepting-jobs"), true).toBool() },
                { QStringLiteral("shared"), g.value(QStringLiteral("printer-is-shared")).toBool() },
                { QStringLiteral("jobs"), g.value(QStringLiteral("queued-job-count")).toInt() },
            };
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
        });

        // your default, else the system's
        ipp(CupsGetDefault, "/", baseAttrs(QStringLiteral("ipp://localhost/")), {}, {},
            [this, list, available](int, const Groups &groups, const QString &) mutable {
            QString server;
            for (const QVariantMap &g : groups)
                if (g.contains(QStringLiteral("printer-name")))
                    server = g.value(QStringLiteral("printer-name")).toString();
            QString def = userDefault();
            bool exists = false;
            for (const QVariant &v : std::as_const(list))
                exists = exists || v.toMap().value(QStringLiteral("name")).toString() == def;
            if (!exists)
                def = server;
            for (QVariant &v : list) {
                QVariantMap m = v.toMap();
                m.insert(QStringLiteral("isDefault"), m.value(QStringLiteral("name")).toString() == def);
                v = m;
            }
            if (list != m_printers || def != m_default || available != m_available) {
                m_printers = list;
                m_default = def;
                m_available = available;
                emit printersChanged();
            }
        });
    });

    QList<IppAttribute> jobAttrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    jobAttrs << IppAttribute{ TagKeyword, "which-jobs", { QStringLiteral("not-completed") } }
             << IppAttribute{ TagKeyword, "requested-attributes",
                              { QStringLiteral("job-id"), QStringLiteral("job-name"), QStringLiteral("job-originating-user-name"),
                                QStringLiteral("job-state"), QStringLiteral("job-printer-uri"), QStringLiteral("job-k-octets"),
                                QStringLiteral("time-at-creation"), QStringLiteral("job-media-sheets-completed"),
                                QStringLiteral("job-printer-state-message") } };
    ipp(GetJobs, "/", jobAttrs, {}, {}, [this](int, const Groups &groups, const QString &) {
        QVariantList list;
        for (const QVariantMap &g : groups) {
            if (!g.contains(QStringLiteral("job-id")))
                continue;
            const int state = g.value(QStringLiteral("job-state")).toInt();
            list << QVariantMap{
                { QStringLiteral("id"), g.value(QStringLiteral("job-id")).toInt() },
                { QStringLiteral("printer"), lastSegment(g.value(QStringLiteral("job-printer-uri")).toString()) },
                { QStringLiteral("name"), g.value(QStringLiteral("job-name")).toString() },
                { QStringLiteral("owner"), g.value(QStringLiteral("job-originating-user-name")).toString() },
                { QStringLiteral("state"), state == 4 ? QStringLiteral("held") : state == 5 ? QStringLiteral("printing")
                                         : state == 6 ? QStringLiteral("stopped") : QStringLiteral("waiting") },
                { QStringLiteral("size"), g.value(QStringLiteral("job-k-octets")).toInt() },
                { QStringLiteral("created"), QDateTime::fromSecsSinceEpoch(g.value(QStringLiteral("time-at-creation")).toLongLong()) },
                { QStringLiteral("pages"), g.value(QStringLiteral("job-media-sheets-completed")).toInt() },
                { QStringLiteral("message"), g.value(QStringLiteral("job-printer-state-message")).toString() },
            };
        }
        if (list != m_jobs) {
            m_jobs = list;
            emit jobsChanged();
        }
    });
}

void PrinterManager::setPaused(const QString &printer, bool paused)
{
    act(paused ? PausePrinter : ResumePrinter, "/admin/", baseAttrs(printerUri(printer)));
}

void PrinterManager::purge(const QString &printer)
{
    act(PurgeJobs, "/admin/", baseAttrs(printerUri(printer)));
}

void PrinterManager::cancelJob(int id)
{
    QList<IppAttribute> attrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    attrs[2] = { TagUri, "job-uri", { QStringLiteral("ipp://localhost/jobs/%1").arg(id) } };
    act(CancelJob, "/jobs/", attrs);
}

void PrinterManager::setJobHeld(int id, bool held)
{
    QList<IppAttribute> attrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    attrs[2] = { TagUri, "job-uri", { QStringLiteral("ipp://localhost/jobs/%1").arg(id) } };
    act(held ? HoldJob : ReleaseJob, "/jobs/", attrs);
}

void PrinterManager::printTestPage(const QString &printer)
{
    // CUPS's own test page (a template its filters fill in)
    QFile f(QStringLiteral("/usr/share/cups/data/testprint"));
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(QStringLiteral("The test page isn't installed on this computer."));
        return;
    }
    QList<IppAttribute> attrs = baseAttrs(printerUri(printer));
    attrs << IppAttribute{ TagName, "job-name", { QStringLiteral("Test Page") } }
          << IppAttribute{ TagMimeType, "document-format", { QStringLiteral("application/octet-stream") } };
    act(PrintJob, ("/printers/" + printer).toUtf8(), attrs, {}, f.readAll());
}

void PrinterManager::setDefault(const QString &printer)
{
    // ~/.cups/lpoptions: "Default NAME options..." marks yours; other lines
    // ("Dest NAME options...") keep their options
    const QString dir = QDir::homePath() + QStringLiteral("/.cups");
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/lpoptions"));
    QList<QByteArray> lines;
    if (f.open(QIODevice::ReadOnly)) {
        lines = f.readAll().split('\n');
        f.close();
    }
    bool found = false;
    for (QByteArray &line : lines) {
        if (line.startsWith("Default "))
            line = "Dest " + line.mid(8);
        const QByteArray dest = line.startsWith("Dest ") ? line.mid(5).trimmed().split(' ').first() : QByteArray();
        if (dest == printer.toUtf8()) {
            line = "Default " + line.mid(5);
            found = true;
        }
    }
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (!found)
        lines << "Default " + printer.toUtf8();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(QStringLiteral("Couldn't save the default printer in %1.").arg(f.fileName()));
        return;
    }
    f.write(lines.join('\n') + '\n');
    f.close();
    refresh();
}

void PrinterManager::setProperties(const QString &printer, const QString &info, const QString &location, bool shared)
{
    act(CupsAddModifyPrinter, "/admin/", baseAttrs(printerUri(printer)), {
        { TagText, "printer-info", { info } },
        { TagText, "printer-location", { location } },
        { TagBoolean, "printer-is-shared", { shared } },
    });
}

void PrinterManager::deletePrinter(const QString &printer)
{
    act(CupsDeletePrinter, "/admin/", baseAttrs(printerUri(printer)));
}

void PrinterManager::findDevices()
{
    setBusy(true);
    QList<IppAttribute> attrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    attrs.removeAt(2); // no printer-uri
    attrs << IppAttribute{ TagInteger, "timeout", { 8 } };
    ipp(CupsGetDevices, "/", attrs, {}, {}, [this](int, const Groups &groups, const QString &error) {
        setBusy(false);
        if (!error.isEmpty())
            emit failed(error);
        QVariantList list;
        QSet<QString> seen;
        for (const QVariantMap &g : groups) {
            const QString uri = g.value(QStringLiteral("device-uri")).toString();
            // bare schemes ("socket", "ipp") are for typing an address by hand
            if (!uri.contains(QLatin1Char(':')) || uri.endsWith(QLatin1Char(':')) || seen.contains(uri))
                continue;
            if (uri.startsWith(QLatin1String("file:")) || uri.startsWith(QLatin1String("cups-brf")))
                continue;
            seen.insert(uri);
            const QString cls = g.value(QStringLiteral("device-class")).toString();
            const QString info = g.value(QStringLiteral("device-info")).toString();
            const QString mm = g.value(QStringLiteral("device-make-and-model")).toString();
            list << QVariantMap{
                { QStringLiteral("uri"), uri },
                { QStringLiteral("info"), info.isEmpty() ? uri : info },
                { QStringLiteral("makeModel"), mm == QLatin1String("Unknown") ? QString() : mm },
                { QStringLiteral("kind"), uri.startsWith(QLatin1String("usb:")) ? QStringLiteral("usb")
                                          : cls == QLatin1String("network") ? QStringLiteral("network") : QStringLiteral("other") },
                { QStringLiteral("deviceId"), g.value(QStringLiteral("device-id")).toString() },
            };
        }
        // driverless network printers first: they need no driver at all
        std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            auto rank = [](const QVariantMap &m) {
                const QString u = m.value(QStringLiteral("uri")).toString();
                return u.startsWith(QLatin1String("ipp")) || u.startsWith(QLatin1String("dnssd")) ? 0 : m.value(QStringLiteral("kind")) == QLatin1String("usb") ? 1 : 2;
            };
            return rank(a.toMap()) < rank(b.toMap());
        });
        emit devicesFound(list);
    });
}

void PrinterManager::findDrivers()
{
    setBusy(true);
    QList<IppAttribute> attrs = baseAttrs(QStringLiteral("ipp://localhost/"));
    attrs.removeAt(2);
    attrs << IppAttribute{ TagKeyword, "requested-attributes",
                           { QStringLiteral("ppd-name"), QStringLiteral("ppd-make"), QStringLiteral("ppd-make-and-model") } };
    ipp(CupsGetPpds, "/", attrs, {}, {}, [this](int, const Groups &groups, const QString &error) {
        setBusy(false);
        if (!error.isEmpty())
            emit failed(error);
        QVariantList list;
        for (const QVariantMap &g : groups) {
            const QString name = g.value(QStringLiteral("ppd-name")).toString();
            if (name.isEmpty())
                continue;
            list << QVariantMap{
                { QStringLiteral("name"), name },
                { QStringLiteral("make"), g.value(QStringLiteral("ppd-make")).toString() },
                { QStringLiteral("makeModel"), g.value(QStringLiteral("ppd-make-and-model")).toString() },
            };
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("makeModel")).toString().compare(b.toMap().value(QStringLiteral("makeModel")).toString(), Qt::CaseInsensitive) < 0;
        });
        emit driversFound(list);
    });
}

QString PrinterManager::suggestName(const QString &description) const
{
    // CUPS: printable characters, no spaces, '/', '#' or quotes
    QString name = description.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\s/#\\\\'\"?]+")), QStringLiteral("_"));
    name.remove(QRegularExpression(QStringLiteral("[^\\x21-\\x7e]")));
    name = name.left(60);
    if (name.isEmpty())
        name = QStringLiteral("Printer");
    QString unique = name;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const QVariant &v : m_printers)
            taken = taken || v.toMap().value(QStringLiteral("name")).toString().compare(unique, Qt::CaseInsensitive) == 0;
        if (!taken)
            return unique;
        unique = name + QLatin1Char('_') + QString::number(n);
    }
}

void PrinterManager::addPrinter(const QString &name, const QString &uri, const QString &driver,
                                const QString &info, const QString &location, bool makeDefault)
{
    const QString lpadmin = lpadminPath();
    if (lpadmin.isEmpty()) {
        emit addFinished(false, QStringLiteral("lpadmin, which adds printers, isn't installed."));
        return;
    }
    setBusy(true);
    auto *p = new QProcess(this);
    QStringList args{ QStringLiteral("-p"), name, QStringLiteral("-E"), QStringLiteral("-v"), uri, QStringLiteral("-m"), driver };
    if (!info.isEmpty())
        args << QStringLiteral("-D") << info;
    if (!location.isEmpty())
        args << QStringLiteral("-L") << location;
    p->setProgram(lpadmin);
    p->setArguments(args);
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setStandardInputFile(QProcess::nullDevice()); // never wait for a typed password
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p->setProcessEnvironment(env);
    connect(p, &QProcess::finished, this, [this, p, name, makeDefault](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        setBusy(false);
        QString out = QString::fromLocal8Bit(p->readAll()).trimmed();
        out.remove(QRegularExpression(QStringLiteral("^lpadmin:\\s*")));
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (ok && makeDefault)
            setDefault(name);
        refresh();
        if (!ok && (out.contains(QLatin1String("Forbidden")) || out.contains(QLatin1String("Unauthorized"))))
            out = QStringLiteral("Your account isn't allowed to add printers. An administrator can allow it by adding you to the lpadmin group.");
        emit addFinished(ok, ok ? QString() : (out.isEmpty() ? QStringLiteral("The printer couldn't be added.") : out));
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        setBusy(false);
        emit addFinished(false, QStringLiteral("Couldn't run lpadmin."));
    });
    p->start();
}
