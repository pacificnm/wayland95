#include "smartinfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>

namespace {

// smartctl.exit_status bits
constexpr int CommandLine = 0x01, OpenFailed = 0x02, DiskFailing = 0x08, PrefailNow = 0x10,
              FailedInPast = 0x20, ErrorLog = 0x40, SelfTestErrors = 0x80;

QString bytesText(double n)
{
    const char *units[] = { "bytes", "KB", "MB", "GB", "TB", "PB" };
    int u = 0;
    while (n >= 1000 && u < 5) {
        n /= 1000;
        ++u;
    }
    return u == 0 ? QStringLiteral("%1 bytes").arg(qint64(n))
                  : QStringLiteral("%1 %2").arg(n, 0, 'f', n < 10 ? 2 : 1).arg(QLatin1String(units[u]));
}

QString number(qint64 n) { return QLocale::system().toString(n); }

// Raw_Read_Error_Rate -> Raw Read Error Rate
QString prettyName(QString name)
{
    name.replace(u'_', u' ');
    name.replace(QLatin1String(" Ct"), QLatin1String(" Count"));
    name.replace(QLatin1String(" Cnt"), QLatin1String(" Count"));
    return name;
}

QVariantMap row(const QString &name, const QString &raw, bool bad = false)
{
    return { { QStringLiteral("name"), name }, { QStringLiteral("raw"), raw }, { QStringLiteral("bad"), bad } };
}

QStringList messages(const QJsonObject &root)
{
    QStringList out;
    const QJsonArray list = root.value(QLatin1String("smartctl")).toObject().value(QLatin1String("messages")).toArray();
    for (const QJsonValue &m : list) {
        const QString s = m.toObject().value(QLatin1String("string")).toString().trimmed();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

} // namespace

QString smartctlMessage(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return messages(doc.object()).join(u'\n');
}

QVariantMap parseSmartctl(const QByteArray &json)
{
    QVariantMap r;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("smartctl's report couldn't be read (%1).").arg(parseError.errorString()));
        return r;
    }
    const QJsonObject root = doc.object();
    const int exitStatus = root.value(QLatin1String("smartctl")).toObject().value(QLatin1String("exit_status")).toInt();
    const QStringList notes = messages(root);
    r.insert(QStringLiteral("exitStatus"), exitStatus);
    r.insert(QStringLiteral("messages"), notes.join(u'\n'));

    const QString model = root.value(QLatin1String("model_name")).toString();
    if ((exitStatus & (CommandLine | OpenFailed)) && model.isEmpty()) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), notes.isEmpty() ? QStringLiteral("smartctl couldn't read this disk.")
                                                          : notes.join(u'\n'));
        return r;
    }
    r.insert(QStringLiteral("ok"), true);

    const QJsonObject device = root.value(QLatin1String("device")).toObject();
    const QString protocol = device.value(QLatin1String("protocol")).toString();
    const bool ata = protocol == QLatin1String("ATA");
    const bool nvme = protocol == QLatin1String("NVMe");
    r.insert(QStringLiteral("protocol"), protocol);
    r.insert(QStringLiteral("model"), model.isEmpty() ? root.value(QLatin1String("scsi_model_name")).toString() : model);
    r.insert(QStringLiteral("family"), root.value(QLatin1String("model_family")).toString());
    r.insert(QStringLiteral("serial"), root.value(QLatin1String("serial_number")).toString());
    r.insert(QStringLiteral("firmware"), root.value(QLatin1String("firmware_version")).toString());
    qint64 capacity = qint64(root.value(QLatin1String("user_capacity")).toObject().value(QLatin1String("bytes")).toDouble());
    if (capacity <= 0)
        capacity = qint64(root.value(QLatin1String("nvme_total_capacity")).toDouble());
    r.insert(QStringLiteral("capacity"), capacity);
    r.insert(QStringLiteral("rpm"), root.contains(QLatin1String("rotation_rate"))
                                        ? root.value(QLatin1String("rotation_rate")).toInt() : (nvme ? 0 : -1));
    r.insert(QStringLiteral("formFactor"), root.value(QLatin1String("form_factor")).toObject().value(QLatin1String("name")).toString());
    r.insert(QStringLiteral("interfaceSpeed"), root.value(QLatin1String("interface_speed")).toObject()
                                                   .value(QLatin1String("current")).toObject().value(QLatin1String("string")).toString());

    const QJsonObject support = root.value(QLatin1String("smart_support")).toObject();
    const bool available = nvme || support.value(QLatin1String("available")).toBool(root.contains(QLatin1String("smart_status")));
    const bool enabled = nvme || support.value(QLatin1String("enabled")).toBool(available);
    r.insert(QStringLiteral("smartAvailable"), available);
    r.insert(QStringLiteral("smartEnabled"), enabled);

    r.insert(QStringLiteral("temperature"), root.value(QLatin1String("temperature")).toObject().value(QLatin1String("current")).toInt());
    r.insert(QStringLiteral("powerOnHours"), qint64(root.value(QLatin1String("power_on_time")).toObject().value(QLatin1String("hours")).toDouble()));
    r.insert(QStringLiteral("powerCycles"), qint64(root.value(QLatin1String("power_cycle_count")).toDouble()));

    // ---- the verdict, and why
    QStringList failing, warnings;
    const QJsonObject status = root.value(QLatin1String("smart_status")).toObject();
    const bool hasStatus = status.contains(QLatin1String("passed"));
    if ((hasStatus && !status.value(QLatin1String("passed")).toBool()) || (exitStatus & DiskFailing))
        failing << QStringLiteral("The disk reports that it is failing. Back up what's on it now, and replace it.");
    if (exitStatus & PrefailNow)
        failing << QStringLiteral("Some of the disk's measurements are past the point where it is expected to fail soon.");
    if (exitStatus & FailedInPast)
        warnings << QStringLiteral("Some of the disk's measurements were past their limit at some time in the past.");
    if (exitStatus & SelfTestErrors)
        warnings << QStringLiteral("A self-test found errors.");

    QVariantList attributes;
    if (ata) {
        const QJsonArray table = root.value(QLatin1String("ata_smart_attributes")).toObject().value(QLatin1String("table")).toArray();
        for (const QJsonValue &v : table) {
            const QJsonObject a = v.toObject();
            const int id = a.value(QLatin1String("id")).toInt();
            const QString whenFailed = a.value(QLatin1String("when_failed")).toString();
            const QJsonObject rawObj = a.value(QLatin1String("raw")).toObject();
            const qint64 raw = qint64(rawObj.value(QLatin1String("value")).toDouble());
            const bool prefail = a.value(QLatin1String("flags")).toObject().value(QLatin1String("prefailure")).toBool();
            // the counts that mean the surface is wearing out
            const bool sectors = (id == 5 || id == 196 || id == 197 || id == 198 || id == 187) && raw > 0;
            const bool bad = whenFailed == QLatin1String("now") || sectors;
            if (sectors) {
                switch (id) {
                case 5: warnings << QStringLiteral("%1 bad sectors have been replaced with spares (Reallocated Sector Count).").arg(number(raw)); break;
                case 197: warnings << QStringLiteral("%1 sectors are waiting to be replaced (Current Pending Sector).").arg(number(raw)); break;
                case 198: warnings << QStringLiteral("%1 sectors couldn't be read at all (Offline Uncorrectable).").arg(number(raw)); break;
                case 187: warnings << QStringLiteral("%1 errors couldn't be corrected (Reported Uncorrect).").arg(number(raw)); break;
                default: break;
                }
            }
            attributes << QVariantMap{
                { QStringLiteral("id"), id },
                { QStringLiteral("name"), prettyName(a.value(QLatin1String("name")).toString()) },
                { QStringLiteral("value"), a.value(QLatin1String("value")).toInt() },
                { QStringLiteral("worst"), a.value(QLatin1String("worst")).toInt() },
                { QStringLiteral("thresh"), a.value(QLatin1String("thresh")).toInt() },
                { QStringLiteral("raw"), rawObj.value(QLatin1String("string")).toString() },
                { QStringLiteral("type"), prefail ? QStringLiteral("Pre-fail") : QStringLiteral("Old age") },
                { QStringLiteral("status"), whenFailed == QLatin1String("now") ? QStringLiteral("FAILING NOW")
                                          : whenFailed == QLatin1String("past") ? QStringLiteral("Failed in the past")
                                          : QStringLiteral("OK") },
                { QStringLiteral("bad"), bad },
            };
        }
    } else if (nvme) {
        const QJsonObject log = root.value(QLatin1String("nvme_smart_health_information_log")).toObject();
        auto n = [&log](const char *key) { return qint64(log.value(QLatin1String(key)).toDouble()); };
        const qint64 critical = n("critical_warning");
        const qint64 spare = n("available_spare"), spareThreshold = n("available_spare_threshold");
        const qint64 used = n("percentage_used"), mediaErrors = n("media_errors");
        if (critical != 0) {
            QStringList what;
            if (critical & 0x01) what << QStringLiteral("its spare space is running out");
            if (critical & 0x02) what << QStringLiteral("it is too hot or too cold");
            if (critical & 0x04) what << QStringLiteral("it is less reliable because of errors");
            if (critical & 0x08) what << QStringLiteral("it has become read-only");
            if (critical & 0x10) what << QStringLiteral("its backup memory has failed");
            failing << QStringLiteral("The disk gives a critical warning: %1.").arg(what.isEmpty() ? QStringLiteral("code %1").arg(critical) : what.join(QLatin1String(", ")));
        }
        if (log.contains(QLatin1String("available_spare")) && spare < spareThreshold)
            failing << QStringLiteral("The disk's spare space (%1%) is below its limit (%2%).").arg(spare).arg(spareThreshold);
        if (used >= 100)
            warnings << QStringLiteral("The disk has used up its rated life (%1%). It may keep working, but back it up often.").arg(used);
        if (mediaErrors > 0)
            warnings << QStringLiteral("The disk has had %1 errors it couldn't correct (Media Errors).").arg(number(mediaErrors));

        // data units are 1000 blocks of 512 bytes
        attributes << row(QStringLiteral("Critical Warning"), QStringLiteral("0x%1").arg(critical, 2, 16, QLatin1Char('0')), critical != 0)
                   << row(QStringLiteral("Temperature"), QStringLiteral("%1 °C").arg(n("temperature")))
                   << row(QStringLiteral("Available Spare"), QStringLiteral("%1%").arg(spare), spare < spareThreshold)
                   << row(QStringLiteral("Available Spare Threshold"), QStringLiteral("%1%").arg(spareThreshold))
                   << row(QStringLiteral("Percentage Used"), QStringLiteral("%1%").arg(used), used >= 100)
                   << row(QStringLiteral("Data Read"), bytesText(double(n("data_units_read")) * 512000.0))
                   << row(QStringLiteral("Data Written"), bytesText(double(n("data_units_written")) * 512000.0))
                   << row(QStringLiteral("Host Read Commands"), number(n("host_reads")))
                   << row(QStringLiteral("Host Write Commands"), number(n("host_writes")))
                   << row(QStringLiteral("Controller Busy Time"), QStringLiteral("%1 minutes").arg(number(n("controller_busy_time"))))
                   << row(QStringLiteral("Power Cycles"), number(n("power_cycles")))
                   << row(QStringLiteral("Power On Hours"), number(n("power_on_hours")))
                   << row(QStringLiteral("Unsafe Shutdowns"), number(n("unsafe_shutdowns")))
                   << row(QStringLiteral("Media and Data Integrity Errors"), number(mediaErrors), mediaErrors > 0)
                   << row(QStringLiteral("Error Information Log Entries"), number(n("num_err_log_entries")))
                   << row(QStringLiteral("Warning Temperature Time"), QStringLiteral("%1 minutes").arg(number(n("warning_temp_time"))))
                   << row(QStringLiteral("Critical Temperature Time"), QStringLiteral("%1 minutes").arg(number(n("critical_comp_time"))));
        const QJsonArray sensors = log.value(QLatin1String("temperature_sensors")).toArray();
        for (int i = 0; i < sensors.size(); ++i)
            attributes << row(QStringLiteral("Temperature Sensor %1").arg(i + 1), QStringLiteral("%1 °C").arg(sensors[i].toInt()));
    } else {
        // SCSI and SAS disks
        if (root.contains(QLatin1String("scsi_grown_defect_list"))) {
            const qint64 defects = qint64(root.value(QLatin1String("scsi_grown_defect_list")).toDouble());
            attributes << row(QStringLiteral("Grown Defects"), number(defects), defects > 0);
            if (defects > 0)
                warnings << QStringLiteral("The disk has %1 grown defects (bad areas found since it was made).").arg(number(defects));
        }
        const QJsonObject counters = root.value(QLatin1String("scsi_error_counter_log")).toObject();
        static const char *const dirs[][2] = { { "read", "Read" }, { "write", "Write" }, { "verify", "Verify" } };
        for (const auto &dir : dirs) {
            const QJsonObject c = counters.value(QLatin1String(dir[0])).toObject();
            if (c.isEmpty())
                continue;
            const qint64 uncorrected = qint64(c.value(QLatin1String("total_uncorrected_errors")).toDouble());
            attributes << row(QStringLiteral("Uncorrected %1 Errors").arg(QLatin1String(dir[1])), number(uncorrected), uncorrected > 0);
        }
        if (root.contains(QLatin1String("scsi_percentage_used_endurance_indicator")))
            attributes << row(QStringLiteral("Percentage Used"),
                              QStringLiteral("%1%").arg(root.value(QLatin1String("scsi_percentage_used_endurance_indicator")).toInt()));
    }
    if (exitStatus & ErrorLog)
        r.insert(QStringLiteral("errorLogNote"), QStringLiteral("The disk's error log has entries. A few old ones are common and usually harmless."));

    QString health;
    if (!failing.isEmpty())
        health = QStringLiteral("failing");
    else if (!warnings.isEmpty())
        health = QStringLiteral("warning");
    else if (hasStatus || nvme)
        health = QStringLiteral("ok");
    r.insert(QStringLiteral("health"), health);
    r.insert(QStringLiteral("problems"), failing + warnings);
    r.insert(QStringLiteral("attributes"), attributes);

    // ---- self-tests
    QVariantList tests;
    bool running = false, canTest = false;
    int remaining = 0, shortMinutes = 0, longMinutes = 0;
    QString testStatus;
    if (ata) {
        const QJsonObject data = root.value(QLatin1String("ata_smart_data")).toObject();
        const QJsonObject self = data.value(QLatin1String("self_test")).toObject();
        const QJsonObject st = self.value(QLatin1String("status")).toObject();
        const int value = st.value(QLatin1String("value")).toInt();
        running = (value >> 4) == 0xF;
        remaining = st.value(QLatin1String("remaining_percent")).toInt();
        testStatus = st.value(QLatin1String("string")).toString();
        shortMinutes = self.value(QLatin1String("polling_minutes")).toObject().value(QLatin1String("short")).toInt();
        longMinutes = self.value(QLatin1String("polling_minutes")).toObject().value(QLatin1String("extended")).toInt();
        canTest = data.value(QLatin1String("capabilities")).toObject().value(QLatin1String("self_tests_supported")).toBool(shortMinutes > 0);
        const QJsonObject logs = root.value(QLatin1String("ata_smart_self_test_log")).toObject();
        QJsonArray table = logs.value(QLatin1String("extended")).toObject().value(QLatin1String("table")).toArray();
        if (table.isEmpty())
            table = logs.value(QLatin1String("standard")).toObject().value(QLatin1String("table")).toArray();
        for (const QJsonValue &v : table) {
            const QJsonObject t = v.toObject();
            const QJsonObject s = t.value(QLatin1String("status")).toObject();
            tests << QVariantMap{
                { QStringLiteral("type"), t.value(QLatin1String("type")).toObject().value(QLatin1String("string")).toString() },
                { QStringLiteral("status"), s.value(QLatin1String("string")).toString() },
                { QStringLiteral("hours"), qint64(t.value(QLatin1String("lifetime_hours")).toDouble()) },
                { QStringLiteral("ok"), s.value(QLatin1String("passed")).toBool(true) },
            };
        }
    } else if (nvme) {
        canTest = root.value(QLatin1String("nvme_optional_admin_commands")).toObject().value(QLatin1String("self_test")).toBool(
            root.contains(QLatin1String("nvme_self_test_log")));
        const QJsonObject log = root.value(QLatin1String("nvme_self_test_log")).toObject();
        const QJsonObject current = log.value(QLatin1String("current_self_test_operation")).toObject();
        running = current.value(QLatin1String("value")).toInt() != 0;
        if (running) {
            remaining = 100 - log.value(QLatin1String("current_self_test_completion_percent")).toInt();
            testStatus = current.value(QLatin1String("string")).toString() + QStringLiteral(" in progress");
        }
        // NVMe reports how long the long test takes (minutes)
        longMinutes = root.value(QLatin1String("nvme_controller_capabilities")).toObject().value(QLatin1String("self_test_minutes")).toInt();
        if (longMinutes <= 0)
            longMinutes = root.value(QLatin1String("nvme_extended_self_test_time")).toInt();
        shortMinutes = 2;
        const QJsonArray table = log.value(QLatin1String("table")).toArray();
        for (const QJsonValue &v : table) {
            const QJsonObject t = v.toObject();
            const QJsonObject result = t.value(QLatin1String("self_test_result")).toObject();
            tests << QVariantMap{
                { QStringLiteral("type"), t.value(QLatin1String("self_test_code")).toObject().value(QLatin1String("string")).toString() },
                { QStringLiteral("status"), result.value(QLatin1String("string")).toString() },
                { QStringLiteral("hours"), qint64(t.value(QLatin1String("power_on_hours")).toDouble()) },
                // 0 no error, 1 aborted by the user, 2 by a reset...
                { QStringLiteral("ok"), result.value(QLatin1String("value")).toInt() <= 2 },
            };
        }
    }
    r.insert(QStringLiteral("selfTests"), tests);
    r.insert(QStringLiteral("canSelfTest"), canTest && enabled);
    r.insert(QStringLiteral("testRunning"), running);
    r.insert(QStringLiteral("testRemaining"), remaining);
    r.insert(QStringLiteral("testStatus"), testStatus);
    r.insert(QStringLiteral("shortMinutes"), shortMinutes);
    r.insert(QStringLiteral("longMinutes"), longMinutes);
    return r;
}
