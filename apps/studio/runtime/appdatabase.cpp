#include "appdatabase.h"
#include "apiclient.h"
#include "fieldrules.h"
#include "datetimes.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTime>
#include <QUuid>
#include <cmath>

namespace {

const QString kId = QStringLiteral("id");
const QString kCreated = QStringLiteral("created_on");
const QString kModified = QStringLiteral("modified_on");
using DateTimes::kDateFormat;
using DateTimes::kTimeFormat;
using DateTimes::kDateTimeFormat;
using DateTimes::parseDate;
using DateTimes::parseTime;
using DateTimes::parseDateTime;

bool isSystemColumn(const QString &name)
{
    return name == kId || name == kCreated || name == kModified;
}

bool isEmpty(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return true;
    if (v.typeId() == QMetaType::QString)
        return v.toString().trimmed().isEmpty();
    return false;
}

bool isNumberType(const QString &type)
{
    return type == QLatin1String("integer") || type == QLatin1String("decimal")
        || type == QLatin1String("float") || type == QLatin1String("currency");
}

bool isTextType(const QString &type)
{
    return type == QLatin1String("text") || type == QLatin1String("memo") || type == QLatin1String("email")
        || type == QLatin1String("url") || type == QLatin1String("phone") || type == QLatin1String("choice");
}

// a field's limit (Minimum, Maximum, Max Length...): unset if it's blank
bool limit(const QVariantMap &field, const char *key, double *out)
{
    const QVariant v = field.value(QLatin1String(key));
    if (isEmpty(v))
        return false;
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (ok)
        *out = d;
    return ok;
}

QString number(double d)
{
    return QString::number(d, 'g', 15);
}

} // namespace

AppDatabase::AppDatabase(QObject *parent)
    : QObject(parent)
{
}

AppDatabase::~AppDatabase()
{
    close();
}

bool AppDatabase::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

QVariantList AppDatabase::fieldTypes()
{
    auto t = [](const char *type, const char *label, const char *hint) {
        return QVariant(QVariantMap{ { QStringLiteral("type"), QLatin1String(type) },
                                     { QStringLiteral("label"), QLatin1String(label) },
                                     { QStringLiteral("hint"), QLatin1String(hint) } });
    };
    return {
        t("text", "Text", "A line of text, up to 255 characters."),
        t("memo", "Long Text", "Several lines of text."),
        t("integer", "Whole Number", "A number with no fraction."),
        t("decimal", "Decimal", "A number with a fixed count of decimal places."),
        t("float", "Floating Point", "A number with a fraction, as the computer rounds it."),
        t("currency", "Currency", "An amount of money (4 decimal places kept)."),
        t("boolean", "Yes/No", "Yes or No."),
        t("date", "Date", "A day."),
        t("time", "Time", "A time of day."),
        t("datetime", "Date and Time", "A day and a time."),
        t("choice", "Choice", "One of a list of values you give."),
        t("lookup", "Lookup", "A record of another table."),
        t("email", "E-mail", "An e-mail address."),
        t("url", "URL", "A web address."),
        t("phone", "Phone", "A telephone number."),
    };
}

QVariantMap AppDatabase::driversAvailable()
{
    return {
        { QStringLiteral("sqlite"), QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")) },
        { QStringLiteral("mysql"), QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL")) },
        { QStringLiteral("postgresql"), QSqlDatabase::isDriverAvailable(QStringLiteral("QPSQL")) },
    };
}

QString AppDatabase::nameProblem(const QString &name)
{
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    if (name.isEmpty())
        return QStringLiteral("A name is needed.");
    if (!ok.match(name).hasMatch())
        return QStringLiteral("Names start with a letter and have only letters, digits and _ in them.");
    if (name.size() > 60)
        return QStringLiteral("That name is too long (60 characters at most).");
    if (isSystemColumn(name.toLower()))
        return QStringLiteral("Every table already has a field named %1.").arg(name.toLower());
    return QString();
}

bool AppDatabase::fail(const QString &error)
{
    m_error = error;
    emit errorChanged();
    return false;
}

QVariantMap AppDatabase::failed(const QString &error)
{
    fail(error);
    return { { QStringLiteral("ok"), false },
             { QStringLiteral("error"), error },
             { QStringLiteral("errors"), QVariantMap() },
             { QStringLiteral("messages"), QStringList{ error } } };
}

bool AppDatabase::open(const QVariantMap &config, const QString &baseDir)
{
    close();
    const QString driver = config.value(QStringLiteral("driver"), QStringLiteral("sqlite")).toString().toLower();
    QString qtDriver;
    if (driver == QLatin1String("sqlite")) {
        qtDriver = QStringLiteral("QSQLITE");
        m_dialect = Sqlite;
    } else if (driver == QLatin1String("mysql") || driver == QLatin1String("mariadb")) {
        qtDriver = QStringLiteral("QMYSQL");
        m_dialect = Mysql;
    } else if (driver == QLatin1String("postgresql") || driver == QLatin1String("postgres")) {
        qtDriver = QStringLiteral("QPSQL");
        m_dialect = Postgres;
    } else {
        return fail(QStringLiteral("\"%1\" isn't a kind of database this can use.").arg(driver));
    }
    if (!QSqlDatabase::isDriverAvailable(qtDriver)) {
        return fail(m_dialect == Mysql
            ? QStringLiteral("Qt's MySQL driver isn't installed (the libqt6sql6-mysql package).")
            : m_dialect == Postgres
                ? QStringLiteral("Qt's PostgreSQL driver isn't installed (the libqt6sql6-psql package).")
                : QStringLiteral("Qt's SQLite driver isn't installed (the libqt6sql6-sqlite package)."));
    }

    m_connection = QStringLiteral("studio-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db = QSqlDatabase::addDatabase(qtDriver, m_connection);
    if (m_dialect == Sqlite) {
        QString file = config.value(QStringLiteral("file")).toString().trimmed();
        if (file.isEmpty())
            file = QStringLiteral("data.db");
        if (file.startsWith(QLatin1String("~/")))
            file = QDir::homePath() + file.mid(1);
        if (file != QLatin1String(":memory:") && QFileInfo(file).isRelative())
            file = QDir(baseDir.isEmpty() ? QDir::currentPath() : baseDir).filePath(file);
        if (file != QLatin1String(":memory:"))
            QDir().mkpath(QFileInfo(file).absolutePath());
        m_db.setDatabaseName(file);
    } else {
        // no host: the server on this computer, through its socket (how
        // Control Panel > Services sets up accounts: you log on as yourself)
        const QString host = config.value(QStringLiteral("host")).toString().trimmed();
        if (!host.isEmpty())
            m_db.setHostName(host);
        const int port = config.value(QStringLiteral("port")).toInt();
        if (port > 0)
            m_db.setPort(port);
        m_db.setDatabaseName(config.value(QStringLiteral("name")).toString().trimmed());
        const QString user = config.value(QStringLiteral("user")).toString().trimmed();
        if (!user.isEmpty())
            m_db.setUserName(user);
        const QString password = config.value(QStringLiteral("password")).toString();
        if (!password.isEmpty())
            m_db.setPassword(password);
        if (m_dialect == Mysql)
            m_db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=8"));
        else
            m_db.setConnectOptions(QStringLiteral("connect_timeout=8"));
    }
    if (!m_db.open()) {
        const QString error = m_db.lastError().text();
        close();
        return fail(error.isEmpty() ? QStringLiteral("The database didn't open.") : error);
    }
    m_driver = m_dialect == Sqlite ? QStringLiteral("sqlite")
             : m_dialect == Mysql ? QStringLiteral("mysql") : QStringLiteral("postgresql");
    m_error.clear();
    emit errorChanged();
    emit openChanged();
    return true;
}

void AppDatabase::close()
{
    if (m_connection.isEmpty())
        return;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
    m_connection.clear();
    m_driver.clear();
    emit openChanged();
}

void AppDatabase::setSchema(const QVariantList &tables)
{
    m_tables = tables;
    emit schemaChanged();
}

QVariantMap AppDatabase::tableInfo(const QString &table) const
{
    for (const QVariant &t : m_tables) {
        const QVariantMap map = t.toMap();
        if (map.value(QStringLiteral("name")).toString() == table)
            return map;
    }
    return {};
}

QVariantMap AppDatabase::fieldInfo(const QString &table, const QString &field) const
{
    const QVariantList fields = tableInfo(table).value(QStringLiteral("fields")).toList();
    for (const QVariant &f : fields) {
        const QVariantMap map = f.toMap();
        if (map.value(QStringLiteral("name")).toString() == field)
            return map;
    }
    return {};
}

QString AppDatabase::label(const QVariantMap &field) const
{
    const QString l = field.value(QStringLiteral("label")).toString().trimmed();
    return l.isEmpty() ? field.value(QStringLiteral("name")).toString() : l;
}

QString AppDatabase::quote(const QString &identifier) const
{
    if (m_dialect == Mysql)
        return QLatin1Char('`') + QString(identifier).replace(QLatin1Char('`'), QLatin1String("``")) + QLatin1Char('`');
    return QLatin1Char('"') + QString(identifier).replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"');
}

QString AppDatabase::keyColumn(const QString &name) const
{
    switch (m_dialect) {
    case Mysql:
        return quote(name) + QStringLiteral(" BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY");
    case Postgres:
        return quote(name) + QStringLiteral(" BIGSERIAL PRIMARY KEY");
    default:
        return quote(name) + QStringLiteral(" INTEGER PRIMARY KEY AUTOINCREMENT");
    }
}

QString AppDatabase::keyName(const QVariantMap &table) const
{
    return table.contains(QStringLiteral("key")) ? table.value(QStringLiteral("key")).toString() : kId;
}

bool AppDatabase::keyAuto(const QVariantMap &table) const
{
    return table.value(QStringLiteral("keyAuto"), true).toBool();
}

bool AppDatabase::hasStamps(const QVariantMap &table) const
{
    return table.value(QStringLiteral("stamps"), true).toBool();
}

QString AppDatabase::sqlTable(const QVariantMap &table) const
{
    const QString db = table.value(QStringLiteral("dbName")).toString();
    return quote(db.isEmpty() ? table.value(QStringLiteral("name")).toString() : db);
}

QString AppDatabase::column(const QVariantMap &field) const
{
    const QString c = field.value(QStringLiteral("column")).toString();
    return c.isEmpty() ? field.value(QStringLiteral("name")).toString() : c;
}

QVariantMap AppDatabase::keyField(const QVariantMap &table) const
{
    const QString key = keyName(table);
    if (key.isEmpty() || keyAuto(table))
        return {};
    for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
        if (column(f.toMap()) == key)
            return f.toMap();
    }
    return {};
}

QVariant AppDatabase::keyValue(const QVariantMap &table, const QVariant &id) const
{
    const QVariantMap field = keyField(table);
    if (field.isEmpty())
        return id.toLongLong();
    const QString type = field.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("integer") || type == QLatin1String("lookup"))
        return id.toLongLong();
    return toSql(field, id);
}

QString AppDatabase::columnOf(const QVariantMap &table, const QString &name) const
{
    if (name == kId)
        return keyName(table);
    if (name == kCreated || name == kModified)
        return hasStamps(table) ? name : QString();
    for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
        if (f.toMap().value(QStringLiteral("name")).toString() == name)
            return column(f.toMap());
    }
    return QString();
}

QString AppDatabase::columnType(const QVariantMap &field) const
{
    const QString type = field.value(QStringLiteral("type")).toString();
    const bool lite = m_dialect == Sqlite;
    if (type == QLatin1String("text")) {
        double max = 0;
        const int n = limit(field, "maxLength", &max) && max >= 1 && max <= 255 ? int(max) : 255;
        return lite ? QStringLiteral("TEXT") : QStringLiteral("VARCHAR(%1)").arg(n);
    }
    if (type == QLatin1String("memo"))
        return m_dialect == Mysql ? QStringLiteral("MEDIUMTEXT") : QStringLiteral("TEXT");
    if (type == QLatin1String("lookup")) {
        // (what the other table's key is)
        const QVariantMap targetTable = tableInfo(field.value(QStringLiteral("lookupTable")).toString());
        if (isApi(targetTable))
            return lite ? QStringLiteral("TEXT") : QStringLiteral("VARCHAR(255)"); // (an API's ids: numbers or text)
        const QVariantMap target = keyField(targetTable);
        if (!target.isEmpty() && target.value(QStringLiteral("type")).toString() != QLatin1String("integer"))
            return columnType(target);
    }
    if (type == QLatin1String("integer") || type == QLatin1String("lookup"))
        return lite ? QStringLiteral("INTEGER") : QStringLiteral("BIGINT");
    if (type == QLatin1String("decimal") || type == QLatin1String("currency")) {
        double places = 2;
        if (type == QLatin1String("currency"))
            places = 4;
        else
            limit(field, "decimals", &places);
        const int scale = qBound(0, int(places), 10);
        if (lite)
            return QStringLiteral("NUMERIC");
        return (m_dialect == Mysql ? QStringLiteral("DECIMAL(19,%1)") : QStringLiteral("NUMERIC(19,%1)")).arg(scale);
    }
    if (type == QLatin1String("float"))
        return lite ? QStringLiteral("REAL") : m_dialect == Mysql ? QStringLiteral("DOUBLE") : QStringLiteral("DOUBLE PRECISION");
    if (type == QLatin1String("boolean"))
        return lite ? QStringLiteral("INTEGER") : m_dialect == Mysql ? QStringLiteral("TINYINT(1)") : QStringLiteral("BOOLEAN");
    if (type == QLatin1String("date"))
        return lite ? QStringLiteral("TEXT") : QStringLiteral("DATE");
    if (type == QLatin1String("time"))
        return lite ? QStringLiteral("TEXT") : QStringLiteral("TIME");
    if (type == QLatin1String("datetime"))
        return lite ? QStringLiteral("TEXT") : m_dialect == Mysql ? QStringLiteral("DATETIME") : QStringLiteral("TIMESTAMP");
    // choice, email, url, phone
    return lite ? QStringLiteral("TEXT") : QStringLiteral("VARCHAR(255)");
}

QStringList AppDatabase::existingTables() const
{
    return m_db.tables(QSql::Tables);
}

QVariantMap AppDatabase::schemaPlan()
{
    if (!isOpen())
        return failed(QStringLiteral("The database isn't open."));
    QStringList actions, notes, statements;
    const QStringList existing = existingTables();
    for (const QVariant &t : m_tables) {
        const QVariantMap table = t.toMap();
        const QString name = table.value(QStringLiteral("name")).toString();
        if (const QString problem = nameProblem(name); !problem.isEmpty())
            return failed(QStringLiteral("Table \"%1\": %2").arg(name, problem));
        if (isApi(table))
            continue; // (an API's endpoint: nothing in the database)
        const QVariantList fields = table.value(QStringLiteral("fields")).toList();
        QStringList seen;
        for (const QVariant &f : fields) {
            const QString field = f.toMap().value(QStringLiteral("name")).toString();
            if (const QString problem = nameProblem(field); !problem.isEmpty())
                return failed(QStringLiteral("%1.%2: %3").arg(name, field, problem));
            if (seen.contains(field, Qt::CaseInsensitive))
                return failed(QStringLiteral("%1 has two fields named %2.").arg(name, field));
            seen << field;
        }
        const QString dbName = table.value(QStringLiteral("dbName")).toString().isEmpty()
            ? name : table.value(QStringLiteral("dbName")).toString();
        const QString key = keyName(table);
        const bool stamps = hasStamps(table);
        const QVariantMap datetime{ { QStringLiteral("type"), QStringLiteral("datetime") } };
        if (!existing.contains(dbName)) {
            QStringList columns;
            if (!key.isEmpty() && keyAuto(table) && key != QLatin1String("rowid"))
                columns << keyColumn(key);
            if (stamps)
                columns << quote(kCreated) + QLatin1Char(' ') + columnType(datetime)
                        << quote(kModified) + QLatin1Char(' ') + columnType(datetime);
            for (const QVariant &f : fields) {
                const QVariantMap field = f.toMap();
                QString def = quote(column(field)) + QLatin1Char(' ') + columnType(field);
                if (!keyAuto(table) && column(field) == key)
                    def += QStringLiteral(" NOT NULL PRIMARY KEY");
                columns << def;
            }
            QString sql = QStringLiteral("CREATE TABLE %1 (%2)").arg(quote(dbName), columns.join(QLatin1String(", ")));
            if (m_dialect == Mysql)
                sql += QStringLiteral(" DEFAULT CHARSET=utf8mb4");
            statements << sql;
            actions << QStringLiteral("Create the table %1").arg(name);
            continue;
        }
        // what's there: ask for no rows and read the columns' names
        QSqlQuery query(m_db);
        QStringList have;
        if (query.exec(QStringLiteral("SELECT * FROM %1 WHERE 1 = 0").arg(quote(dbName)))) {
            const QSqlRecord record = query.record();
            for (int i = 0; i < record.count(); ++i)
                have << record.fieldName(i);
        } else {
            return failed(query.lastError().text());
        }
        if (!key.isEmpty() && key != QLatin1String("rowid") && !have.contains(key, Qt::CaseInsensitive)) {
            if (key == kId)
                return failed(QStringLiteral("The database already has a table named %1, and it isn't one of this "
                                             "project's (it has no id field). Import it (Project > Import Tables), "
                                             "or give yours another name.").arg(name));
            return failed(QStringLiteral("%1 in the database has no %2 (its key).").arg(dbName, key));
        }
        if (stamps) {
            for (const QString &system : { kCreated, kModified }) {
                if (!have.contains(system, Qt::CaseInsensitive)) {
                    statements << QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                        .arg(quote(dbName), quote(system), columnType(datetime));
                    actions << QStringLiteral("Add %1 to %2").arg(system, name);
                }
            }
        }
        QStringList columns;
        for (const QVariant &f : fields) {
            const QVariantMap field = f.toMap();
            const QString col = column(field);
            columns << col;
            if (have.contains(col, Qt::CaseInsensitive))
                continue;
            statements << QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(quote(dbName), quote(col), columnType(field));
            actions << QStringLiteral("Add the field %1 to %2").arg(field.value(QStringLiteral("name")).toString(), name);
        }
        for (const QString &col : std::as_const(have)) {
            const bool system = col.compare(key, Qt::CaseInsensitive) == 0
                || (stamps && (col.compare(kCreated, Qt::CaseInsensitive) == 0 || col.compare(kModified, Qt::CaseInsensitive) == 0));
            if (!system && !columns.contains(col, Qt::CaseInsensitive))
                notes << QStringLiteral("%1.%2 is in the database but not in the project (left as it is).").arg(name, col);
        }
    }
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("actions"), actions },
             { QStringLiteral("notes"), notes },
             { QStringLiteral("statements"), statements } };
}

QVariantMap AppDatabase::syncSchema()
{
    QVariantMap plan = schemaPlan();
    if (!plan.value(QStringLiteral("ok")).toBool())
        return plan;
    const QStringList statements = plan.value(QStringLiteral("statements")).toStringList();
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql))
            return failed(query.lastError().text() + QStringLiteral("\n\n") + sql);
    }
    return plan;
}

// --- values ------------------------------------------------------------------

QVariant AppDatabase::toSql(const QVariantMap &field, const QVariant &value) const
{
    if (!value.isValid() || value.isNull())
        return QVariant();
    const QString type = field.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("boolean"))
        return m_dialect == Postgres ? QVariant(value.toBool()) : QVariant(value.toBool() ? 1 : 0);
    // Dates and times go over as text, as they were typed ("2024-03-05
    // 08:30:00"), and come back as text (selectList): a Date and Time is
    // the time on the clock, with no zone. Handed over as a QDateTime,
    // Qt's MySQL and PostgreSQL drivers would turn it into UTC.
    if (m_dialect != Sqlite && type == QLatin1String("datetime"))
        return value.toString().replace(QLatin1Char('T'), QLatin1Char(' '));
    return value;
}

QVariant AppDatabase::fromSql(const QString &type, const QVariant &value) const
{
    if (!value.isValid() || value.isNull())
        return QVariant::fromValue(nullptr);
    if (type == QLatin1String("boolean")) {
        if (value.typeId() == QMetaType::QString)
            return value.toString() == QLatin1String("1") || value.toString().startsWith(QLatin1Char('t'), Qt::CaseInsensitive);
        return value.toBool();
    }
    if (type == QLatin1String("integer"))
        return value.toLongLong();
    if (type == QLatin1String("lookup")) {
        bool number = false;
        const qlonglong n = value.toLongLong(&number);
        return number ? QVariant(n) : QVariant(value.toString());
    }
    if (type == QLatin1String("decimal") || type == QLatin1String("float") || type == QLatin1String("currency"))
        return value.toDouble();
    if (type == QLatin1String("date")) {
        if (value.typeId() == QMetaType::QDate)
            return value.toDate().toString(kDateFormat);
        if (value.typeId() == QMetaType::QDateTime)
            return value.toDateTime().date().toString(kDateFormat);
        return value.toString().left(10);
    }
    if (type == QLatin1String("time")) {
        if (value.typeId() == QMetaType::QTime)
            return value.toTime().toString(kTimeFormat);
        return value.toString().left(8);
    }
    if (type == QLatin1String("datetime")) {
        if (value.typeId() == QMetaType::QDateTime)
            return value.toDateTime().toString(kDateTimeFormat);
        QString s = value.toString().left(19);
        if (s.size() > 10 && s.at(10) == QLatin1Char(' '))
            s[10] = QLatin1Char('T');
        return s;
    }
    return value.toString();
}

QVariantMap AppDatabase::rowToMap(const QVariantMap &table, const QSqlRecord &row) const
{
    QVariantMap map;
    auto column = [&row](const QString &name) {
        int i = row.indexOf(name);
        return i < 0 ? QVariant() : row.value(i);
    };
    // `id` is the key's value, whatever its column; none: null
    const QString key = keyName(table);
    const QVariantMap kf = keyField(table);
    if (key.isEmpty())
        map.insert(kId, QVariant::fromValue(nullptr));
    else if (kf.isEmpty())
        map.insert(kId, column(key).toLongLong());
    else
        map.insert(kId, fromSql(kf.value(QStringLiteral("type")).toString(), column(key)));
    if (hasStamps(table)) {
        map.insert(kCreated, fromSql(QStringLiteral("datetime"), column(kCreated)));
        map.insert(kModified, fromSql(QStringLiteral("datetime"), column(kModified)));
    }
    const QVariantList fields = table.value(QStringLiteral("fields")).toList();
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        map.insert(field.value(QStringLiteral("name")).toString(),
                   fromSql(field.value(QStringLiteral("type")).toString(), column(this->column(field))));
    }
    return map;
}

// what to SELECT for a table: every column by name, a Date and Time as the
// text the server keeps (see toSql)
QString AppDatabase::selectList(const QVariantMap &table) const
{
    auto asText = [this](const QString &name) {
        if (m_dialect == Sqlite)
            return quote(name);
        return QStringLiteral("CAST(%1 AS %2) AS %1").arg(quote(name), m_dialect == Mysql ? QStringLiteral("CHAR") : QStringLiteral("TEXT"));
    };
    QStringList columns;
    const QString key = keyName(table);
    if (key == QLatin1String("rowid"))
        columns << QStringLiteral("rowid AS ") + quote(key);
    else if (!key.isEmpty() && keyField(table).isEmpty())
        columns << quote(key);
    if (hasStamps(table))
        columns << asText(kCreated) << asText(kModified);
    const QVariantList fields = table.value(QStringLiteral("fields")).toList();
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        const QString name = column(field);
        columns << (field.value(QStringLiteral("type")).toString() == QLatin1String("datetime") ? asText(name) : quote(name));
    }
    return columns.join(QLatin1String(", "));
}

QString AppDatabase::labelField(const QVariantMap &table) const
{
    // the first text field that isn't the key (a typed-in key, like a code: if there's no other)
    const QVariantList fields = table.value(QStringLiteral("fields")).toList();
    const QString key = keyField(table).value(QStringLiteral("name")).toString();
    for (const char *named : { "name", "title", "label", "fullname", "full_name", "displayname", "display_name", "description" }) {
        for (const QVariant &f : fields) {
            const QVariantMap field = f.toMap();
            if (field.value(QStringLiteral("name")).toString().compare(QLatin1String(named), Qt::CaseInsensitive) == 0
                && isTextType(field.value(QStringLiteral("type")).toString()))
                return field.value(QStringLiteral("name")).toString();
        }
    }
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        if (isTextType(field.value(QStringLiteral("type")).toString()) && field.value(QStringLiteral("name")).toString() != key)
            return field.value(QStringLiteral("name")).toString();
    }
    if (!key.isEmpty())
        return key;
    return fields.isEmpty() ? kId : fields.first().toMap().value(QStringLiteral("name")).toString();
}

// --- reading -----------------------------------------------------------------

QVariantList AppDatabase::select(const QString &tableName, const QVariantMap &options)
{
    QVariantList rows;
    const QVariantMap table = tableInfo(tableName);
    if (table.isEmpty()) {
        fail(QStringLiteral("There is no table named %1.").arg(tableName));
        return rows;
    }
    if (isApi(table)) {
        QString error;
        const QVariantList all = apiRows(table, &error);
        if (!error.isEmpty()) {
            fail(error);
            return rows;
        }
        return filtered(table, all, options);
    }
    if (!isOpen()) {
        fail(QStringLiteral("The database isn't open."));
        return rows;
    }
    auto known = [&](const QString &name) { return !columnOf(table, name).isEmpty(); };

    QStringList conditions;
    QVariantList binds;
    const QVariantMap where = options.value(QStringLiteral("where")).toMap();
    for (auto it = where.cbegin(); it != where.cend(); ++it) {
        if (!known(it.key())) {
            fail(QStringLiteral("%1 has no field named %2.").arg(tableName, it.key()));
            return rows;
        }
        QString op = QStringLiteral("=");
        QVariant value = it.value();
        if (value.typeId() == QMetaType::QVariantMap) {
            // { op: ">=", value: 18 }
            static const QStringList ops{ QStringLiteral("="), QStringLiteral("<>"), QStringLiteral("<"),
                                          QStringLiteral("<="), QStringLiteral(">"), QStringLiteral(">="),
                                          QStringLiteral("like") };
            const QVariantMap test = value.toMap();
            op = test.value(QStringLiteral("op"), op).toString().toLower();
            if (op == QLatin1String("!="))
                op = QStringLiteral("<>");
            if (!ops.contains(op)) {
                fail(QStringLiteral("\"%1\" isn't a comparison this knows.").arg(op));
                return rows;
            }
            value = test.value(QStringLiteral("value"));
        }
        const QString col = columnOf(table, it.key()) == QLatin1String("rowid") ? QStringLiteral("rowid") : quote(columnOf(table, it.key()));
        if (!value.isValid() || value.isNull()) {
            conditions << col + (op == QLatin1String("<>") ? QStringLiteral(" IS NOT NULL") : QStringLiteral(" IS NULL"));
            continue;
        }
        const QVariantMap field = fieldInfo(tableName, it.key());
        if (op == QLatin1String("like")) {
            conditions << QStringLiteral("LOWER(%1) LIKE LOWER(?)").arg(col);
            binds << value.toString();
        } else {
            conditions << col + QLatin1Char(' ') + op + QStringLiteral(" ?");
            binds << (it.key() == kId ? keyValue(table, value) : field.isEmpty() ? value : toSql(field, value));
        }
    }
    const QString search = options.value(QStringLiteral("search")).toString().trimmed();
    if (!search.isEmpty()) {
        QStringList any;
        const QVariantList fields = table.value(QStringLiteral("fields")).toList();
        for (const QVariant &f : fields) {
            const QVariantMap field = f.toMap();
            if (!isTextType(field.value(QStringLiteral("type")).toString()))
                continue;
            any << QStringLiteral("LOWER(%1) LIKE LOWER(?)").arg(quote(column(field)));
            binds << QLatin1Char('%') + search + QLatin1Char('%');
        }
        if (!any.isEmpty())
            conditions << QLatin1Char('(') + any.join(QLatin1String(" OR ")) + QLatin1Char(')');
    }

    QString sql = QStringLiteral("SELECT ") + selectList(table) + QStringLiteral(" FROM ") + sqlTable(table);
    if (!conditions.isEmpty())
        sql += QStringLiteral(" WHERE ") + conditions.join(QLatin1String(" AND "));
    // in order: what's asked for, then the key (a table with no key: as the database has them)
    auto sqlColumn = [&](const QString &col) { return col == QLatin1String("rowid") ? col : quote(col); };
    const QString key = keyName(table);
    QString orderBy = options.value(QStringLiteral("orderBy")).toString();
    if (orderBy.isEmpty() || !known(orderBy))
        orderBy = kId;
    const QString orderColumn = columnOf(table, orderBy);
    if (!orderColumn.isEmpty()) {
        sql += QStringLiteral(" ORDER BY ") + sqlColumn(orderColumn);
        if (options.value(QStringLiteral("descending")).toBool())
            sql += QStringLiteral(" DESC");
        if (!key.isEmpty() && orderColumn != key)
            sql += QStringLiteral(", ") + sqlColumn(key);
    }
    const int limitRows = options.value(QStringLiteral("limit")).toInt();
    const int offset = options.value(QStringLiteral("offset")).toInt();
    if (limitRows > 0 || offset > 0) {
        sql += QStringLiteral(" LIMIT %1").arg(limitRows > 0 ? limitRows : 1000000000);
        if (offset > 0)
            sql += QStringLiteral(" OFFSET %1").arg(offset);
    }

    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.prepare(sql);
    for (const QVariant &bind : std::as_const(binds))
        query.addBindValue(bind);
    if (!query.exec()) {
        fail(query.lastError().text());
        return rows;
    }
    while (query.next())
        rows << rowToMap(table, query.record());
    return rows;
}

QVariantMap AppDatabase::get(const QString &table, const QVariant &id)
{
    if (keyName(tableInfo(table)).isEmpty() || isEmpty(id))
        return {};
    if (isApi(tableInfo(table))) {
        // (from its list: one request for a form moving through them)
        QString error;
        const QVariantMap info = tableInfo(table);
        for (const QVariant &r : apiRows(info, &error)) {
            if (r.toMap().value(kId).toString() == id.toString())
                return r.toMap();
        }
        if (!error.isEmpty())
            fail(error);
        return {};
    }
    const QVariantList rows = select(table, { { QStringLiteral("where"), QVariantMap{ { kId, id } } },
                                              { QStringLiteral("limit"), 1 } });
    return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}

int AppDatabase::count(const QString &table, const QVariantMap &where)
{
    // (through select() so the conditions are checked the same way; the
    // tables an app like this holds are small)
    return int(select(table, { { QStringLiteral("where"), where } }).size());
}

QVariantList AppDatabase::lookupChoices(const QString &tableName)
{
    QVariantList choices;
    const QVariantMap table = tableInfo(tableName);
    if (table.isEmpty())
        return choices;
    const QString field = labelField(table);
    const QVariantList rows = select(tableName, { { QStringLiteral("orderBy"), field } });
    for (const QVariant &r : rows) {
        const QVariantMap row = r.toMap();
        choices << QVariantMap{ { kId, row.value(kId) },
                                { QStringLiteral("label"), row.value(field).toString() } };
    }
    return choices;
}

QString AppDatabase::lookupLabel(const QString &tableName, const QVariant &id)
{
    if (isEmpty(id))
        return QString();
    const QVariantMap table = tableInfo(tableName);
    const QVariantMap row = get(tableName, id);
    return row.isEmpty() ? QString() : row.value(labelField(table)).toString();
}

QVariantMap AppDatabase::defaults(const QString &tableName) const
{
    QVariantMap record;
    const QVariantList fields = tableInfo(tableName).value(QStringLiteral("fields")).toList();
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        const QString type = field.value(QStringLiteral("type")).toString();
        const QString stamp = field.value(QStringLiteral("auto")).toString();
        const QVariant def = stamp == QLatin1String("created") || stamp == QLatin1String("modified")
            ? QVariant(type == QLatin1String("date") ? QStringLiteral("today") : QStringLiteral("now"))
            : field.value(QStringLiteral("default"));
        QVariant value = QVariant::fromValue(nullptr);
        if (type == QLatin1String("boolean")) {
            const QString s = def.toString().toLower();
            value = def.typeId() == QMetaType::Bool ? def.toBool()
                  : (s == QLatin1String("yes") || s == QLatin1String("true") || s == QLatin1String("1"));
        } else if (!isEmpty(def)) {
            if (type == QLatin1String("date"))
                value = parseDate(def).toString(kDateFormat);
            else if (type == QLatin1String("time"))
                value = parseTime(def).toString(kTimeFormat);
            else if (type == QLatin1String("datetime"))
                value = parseDateTime(def).toString(kDateTimeFormat);
            else if (isNumberType(type) || type == QLatin1String("lookup"))
                value = def.toDouble();
            else
                value = def.toString();
        }
        record.insert(field.value(QStringLiteral("name")).toString(), value);
    }
    return record;
}

// --- reading a database that was there already ---------------------------------

namespace {

// a column's type in the database -> a field's: { type, maxLength, decimals,
// choices } ("" type: kept out, like binary data)
QVariantMap fieldTypeOf(const QString &declared, int length, int scale)
{
    const QString t = declared.trimmed().toLower();
    QVariantMap f;
    auto set = [&](const char *type) { f.insert(QStringLiteral("type"), QLatin1String(type)); };
    static const QRegularExpression enumRe(QStringLiteral("^enum\\s*\\((.*)\\)$"));
    static const QRegularExpression sizeRe(QStringLiteral("\\(\\s*(\\d+)\\s*(?:,\\s*(\\d+)\\s*)?\\)"));
    const QRegularExpressionMatch size = sizeRe.match(t);
    if (length <= 0 && size.hasMatch())
        length = size.captured(1).toInt();
    if (scale < 0 && size.hasMatch() && !size.captured(2).isEmpty())
        scale = size.captured(2).toInt();
    if (const QRegularExpressionMatch m = enumRe.match(t); m.hasMatch()) {
        set("choice");
        QStringList choices;
        static const QRegularExpression item(QStringLiteral("'((?:[^']|'')*)'"));
        auto it = item.globalMatch(declared.mid(declared.indexOf(QLatin1Char('('))));
        while (it.hasNext())
            choices << it.next().captured(1).replace(QLatin1String("''"), QLatin1String("'"));
        f.insert(QStringLiteral("choices"), choices);
    } else if (t.contains(QLatin1String("bool")) || t.startsWith(QLatin1String("tinyint(1)")) || t == QLatin1String("bit")
               || t.startsWith(QLatin1String("bit(1)"))) {
        set("boolean");
    } else if (t.contains(QLatin1String("blob")) || t.contains(QLatin1String("binary")) || t == QLatin1String("bytea")
               || t == QLatin1String("image") || t.startsWith(QLatin1String("geometry")) || t == QLatin1String("point")) {
        set("");
    } else if (t.contains(QLatin1String("int")) || t.contains(QLatin1String("serial"))) {
        set("integer");
    } else if (t.startsWith(QLatin1String("money"))) {
        set("currency");
    } else if (t.startsWith(QLatin1String("decimal")) || t.startsWith(QLatin1String("numeric")) || t.startsWith(QLatin1String("number"))) {
        if (scale == 0) {
            set("integer");
        } else {
            set("decimal");
            f.insert(QStringLiteral("decimals"), scale > 0 ? scale : 2);
        }
    } else if (t.contains(QLatin1String("real")) || t.contains(QLatin1String("floa")) || t.contains(QLatin1String("doub"))) {
        set("float");
    } else if (t.startsWith(QLatin1String("datetime")) || t.startsWith(QLatin1String("timestamp"))) {
        set("datetime");
    } else if (t == QLatin1String("date")) {
        set("date");
    } else if (t.startsWith(QLatin1String("time"))) {
        set("time");
    } else if (t.contains(QLatin1String("char")) || t == QLatin1String("uuid") || t == QLatin1String("tinytext")
               || t == QLatin1String("citext") || t == QLatin1String("inet")) {
        if (t.contains(QLatin1String("char")) && length > 4000) {
            set("memo");
        } else {
            set("text");
            if (length > 0)
                f.insert(QStringLiteral("maxLength"), length);
            else if (t == QLatin1String("uuid"))
                f.insert(QStringLiteral("maxLength"), 36);
            else if (t.contains(QLatin1String("char")))
                f.insert(QStringLiteral("maxLength"), 4000);
        }
    } else {
        // text, clob, json, xml, mediumtext, longtext, and SQLite's anything
        set("memo");
    }
    return f;
}

// a column's DEFAULT as a field's Default Value ("" if it isn't a plain one)
QString defaultOf(const QVariant &raw, const QString &type)
{
    QString d = raw.toString().trimmed();
    if (raw.isNull() || d.isEmpty() || d.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0)
        return QString();
    const QString lower = d.toLower();
    if (lower.contains(QLatin1String("current_timestamp")) || lower.startsWith(QLatin1String("now(")) || lower.contains(QLatin1String("localtimestamp")))
        return type == QLatin1String("date") ? QStringLiteral("today") : QStringLiteral("now");
    if (lower.contains(QLatin1String("current_date")))
        return QStringLiteral("today");
    if (lower.startsWith(QLatin1String("nextval(")) || lower.contains(QLatin1String("(")))
        return QString(); // (a function: the database's own business)
    // 'text'::character varying, 'abc', 12.5, true
    static const QRegularExpression quoted(QStringLiteral("^'((?:[^']|'')*)'(::.*)?$"));
    if (const QRegularExpressionMatch m = quoted.match(d); m.hasMatch())
        d = m.captured(1).replace(QLatin1String("''"), QLatin1String("'"));
    if (type == QLatin1String("boolean"))
        return (d == QLatin1String("1") || d.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                || d.compare(QLatin1String("b'1'"), Qt::CaseInsensitive) == 0) ? QStringLiteral("yes") : QString();
    return d;
}

// a name code can use, made from one that isn't
QString codeName(const QString &name, const QStringList &taken)
{
    QString n;
    for (const QChar ch : name)
        n += (ch.isLetterOrNumber() && ch.unicode() < 128) || ch == QLatin1Char('_') ? ch : QLatin1Char('_');
    static const QRegularExpression runs(QStringLiteral("_+"));
    n.replace(runs, QStringLiteral("_"));
    if (n.endsWith(QLatin1Char('_')) && n.size() > 1)
        n.chop(1);
    if (n.startsWith(QLatin1Char('_')) && n.size() > 1)
        n.remove(0, 1);
    if (n.isEmpty() || !n.at(0).isLetter())
        n.prepend(QLatin1String("F"));
    if (isSystemColumn(n.toLower()))
        n += QLatin1Char('_');
    QString unique = n;
    for (int i = 2; taken.contains(unique, Qt::CaseInsensitive); ++i)
        unique = n + QString::number(i);
    return unique;
}

} // namespace

// one table of the database, as a project table (fields not yet named for
// their Lookups' tables: readSchema() does that)
QVariantMap AppDatabase::readTable(const QString &dbName, QStringList *notes)
{
    struct Column {
        QString name, declared;
        int length = 0, scale = -1;
        bool notNull = false, primary = false, autoNumber = false, unique = false;
        QVariant def;
        QString refTable, refColumn;
    };
    QList<Column> columns;
    QStringList primaries;
    bool withoutRowid = false;
    QSqlQuery q(m_db);

    if (m_dialect == Sqlite) {
        q.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = '%1'").arg(QString(dbName).replace(QLatin1Char('\''), QLatin1String("''"))));
        if (q.next())
            withoutRowid = q.value(0).toString().contains(QLatin1String("WITHOUT ROWID"), Qt::CaseInsensitive);
        q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(quote(dbName)));
        while (q.next()) {
            Column col;
            col.name = q.value(1).toString();
            col.declared = q.value(2).toString();
            col.notNull = q.value(3).toInt() != 0;
            col.def = q.value(4);
            col.primary = q.value(5).toInt() > 0;
            if (col.primary)
                primaries << col.name;
            columns << col;
        }
        // a single INTEGER PRIMARY KEY is the rowid: the database numbers it
        if (primaries.size() == 1) {
            for (Column &col : columns) {
                if (col.primary && col.declared.trimmed().compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) == 0)
                    col.autoNumber = true;
            }
        }
        q.exec(QStringLiteral("PRAGMA index_list(%1)").arg(quote(dbName)));
        QStringList uniques;
        while (q.next()) {
            if (q.value(2).toInt() != 0)
                uniques << q.value(1).toString();
        }
        for (const QString &index : std::as_const(uniques)) {
            q.exec(QStringLiteral("PRAGMA index_info(%1)").arg(quote(index)));
            QStringList cols;
            while (q.next())
                cols << q.value(2).toString();
            if (cols.size() == 1) {
                for (Column &col : columns) {
                    if (col.name == cols.first())
                        col.unique = true;
                }
            }
        }
        q.exec(QStringLiteral("PRAGMA foreign_key_list(%1)").arg(quote(dbName)));
        QHash<int, int> parts;
        QList<std::tuple<int, QString, QString, QString>> keys;
        while (q.next()) {
            parts[q.value(0).toInt()] += 1;
            keys << std::make_tuple(q.value(0).toInt(), q.value(2).toString(), q.value(3).toString(), q.value(4).toString());
        }
        for (const auto &[id, table, from, to] : std::as_const(keys)) {
            if (parts.value(id) != 1)
                continue;
            for (Column &col : columns) {
                if (col.name == from) {
                    col.refTable = table;
                    col.refColumn = to;
                }
            }
        }
    } else if (m_dialect == Mysql) {
        q.prepare(QStringLiteral("SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_DEFAULT, CHARACTER_MAXIMUM_LENGTH, "
                                 "NUMERIC_SCALE, COLUMN_KEY, EXTRA FROM information_schema.COLUMNS "
                                 "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION"));
        q.addBindValue(dbName);
        q.exec();
        while (q.next()) {
            Column col;
            col.name = q.value(0).toString();
            col.declared = q.value(1).toString();
            col.notNull = q.value(2).toString() == QLatin1String("NO");
            col.def = q.value(3);
            col.length = q.value(4).isNull() ? 0 : int(qMin<qlonglong>(q.value(4).toLongLong(), 1000000));
            col.scale = q.value(5).isNull() ? -1 : q.value(5).toInt();
            col.primary = q.value(6).toString() == QLatin1String("PRI");
            col.unique = q.value(6).toString() == QLatin1String("UNI");
            col.autoNumber = q.value(7).toString().contains(QLatin1String("auto_increment"), Qt::CaseInsensitive);
            if (col.primary)
                primaries << col.name;
            columns << col;
        }
        q.prepare(QStringLiteral("SELECT COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME, CONSTRAINT_NAME "
                                 "FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? "
                                 "AND REFERENCED_TABLE_NAME IS NOT NULL"));
        q.addBindValue(dbName);
        q.exec();
        QHash<QString, int> parts;
        QList<QStringList> keys;
        while (q.next()) {
            parts[q.value(3).toString()] += 1;
            keys << QStringList{ q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString() };
        }
        for (const QStringList &k : std::as_const(keys)) {
            if (parts.value(k[3]) != 1)
                continue;
            for (Column &col : columns) {
                if (col.name == k[0]) {
                    col.refTable = k[1];
                    col.refColumn = k[2];
                }
            }
        }
    } else {
        q.prepare(QStringLiteral("SELECT column_name, CASE WHEN data_type = 'USER-DEFINED' THEN udt_name ELSE data_type END, "
                                 "is_nullable, column_default, character_maximum_length, numeric_scale, is_identity "
                                 "FROM information_schema.columns WHERE table_schema = current_schema() AND table_name = ? "
                                 "ORDER BY ordinal_position"));
        q.addBindValue(dbName);
        q.exec();
        while (q.next()) {
            Column col;
            col.name = q.value(0).toString();
            col.declared = q.value(1).toString();
            col.notNull = q.value(2).toString() == QLatin1String("NO");
            col.def = q.value(3);
            col.length = q.value(4).isNull() ? 0 : q.value(4).toInt();
            col.scale = q.value(5).isNull() ? -1 : q.value(5).toInt();
            col.autoNumber = q.value(6).toString() == QLatin1String("YES")
                || col.def.toString().startsWith(QLatin1String("nextval("));
            columns << col;
        }
        // its primary key and unique columns (one-column constraints)
        q.prepare(QStringLiteral("SELECT tc.constraint_type, tc.constraint_name, kcu.column_name "
                                 "FROM information_schema.table_constraints tc JOIN information_schema.key_column_usage kcu "
                                 "ON tc.constraint_name = kcu.constraint_name AND tc.table_schema = kcu.table_schema "
                                 "WHERE tc.table_schema = current_schema() AND tc.table_name = ? "
                                 "AND tc.constraint_type IN ('PRIMARY KEY', 'UNIQUE')"));
        q.addBindValue(dbName);
        q.exec();
        QHash<QString, QStringList> constraints;
        QHash<QString, QString> kinds;
        while (q.next()) {
            constraints[q.value(1).toString()] << q.value(2).toString();
            kinds[q.value(1).toString()] = q.value(0).toString();
        }
        for (auto it = constraints.cbegin(); it != constraints.cend(); ++it) {
            const bool primary = kinds.value(it.key()) == QLatin1String("PRIMARY KEY");
            if (primary)
                primaries = it.value();
            if (it.value().size() != 1)
                continue;
            for (Column &col : columns) {
                if (col.name == it.value().first()) {
                    if (primary)
                        col.primary = true;
                    else
                        col.unique = true;
                }
            }
        }
        if (primaries.size() > 1) {
            for (Column &col : columns)
                col.primary = false;
        }
        q.prepare(QStringLiteral("SELECT kcu.column_name, ccu.table_name, ccu.column_name, tc.constraint_name "
                                 "FROM information_schema.table_constraints tc "
                                 "JOIN information_schema.key_column_usage kcu ON tc.constraint_name = kcu.constraint_name "
                                 "AND tc.table_schema = kcu.table_schema "
                                 "JOIN information_schema.constraint_column_usage ccu ON ccu.constraint_name = tc.constraint_name "
                                 "AND ccu.table_schema = tc.table_schema "
                                 "WHERE tc.constraint_type = 'FOREIGN KEY' AND tc.table_schema = current_schema() AND tc.table_name = ?"));
        q.addBindValue(dbName);
        q.exec();
        QHash<QString, int> parts;
        QList<QStringList> keys;
        while (q.next()) {
            parts[q.value(3).toString()] += 1;
            keys << QStringList{ q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString() };
        }
        for (const QStringList &k : std::as_const(keys)) {
            if (parts.value(k[3]) != 1)
                continue;
            for (Column &col : columns) {
                if (col.name == k[0]) {
                    col.refTable = k[1];
                    col.refColumn = k[2];
                }
            }
        }
    }

    // the key: one primary column (numbered by the database, or typed in);
    // none or several: SQLite's rowid, else none (read-only)
    QString key;
    bool numbered = true;
    if (primaries.size() == 1) {
        key = primaries.first();
        for (const Column &col : std::as_const(columns)) {
            if (col.name == key)
                numbered = col.autoNumber;
        }
    } else if (m_dialect == Sqlite && !withoutRowid) {
        key = QStringLiteral("rowid");
    }
    QStringList names;
    for (const Column &col : std::as_const(columns))
        names << col.name;
    const bool stamps = names.contains(kCreated) && names.contains(kModified);
    if (key.isEmpty())
        notes->append(QStringLiteral("%1 has no key in the database: its records can be read and added, but not changed or deleted.").arg(dbName));

    QVariantList fields;
    QStringList taken;
    for (const Column &col : std::as_const(columns)) {
        if ((numbered && col.name == key) || (stamps && (col.name == kCreated || col.name == kModified)))
            continue;
        QVariantMap field = fieldTypeOf(col.declared, col.length, col.scale);
        if (field.value(QStringLiteral("type")).toString().isEmpty()) {
            notes->append(QStringLiteral("%1.%2 (%3) is left out: forms can't show it.").arg(dbName, col.name, col.declared));
            continue;
        }
        const QString name = nameProblem(col.name).isEmpty() && !taken.contains(col.name, Qt::CaseInsensitive)
            ? col.name : codeName(col.name, taken);
        taken << name;
        field.insert(QStringLiteral("name"), name);
        if (name != col.name) {
            field.insert(QStringLiteral("column"), col.name);
            field.insert(QStringLiteral("label"), col.name); // (messages say it as the database does)
        }
        // SQLite says TEXT for any text: a line, unless it's named like a long one
        static const QRegularExpression longName(QStringLiteral("note|comment|descr|memo|body|address|remark|detail|summary"),
                                                 QRegularExpression::CaseInsensitiveOption);
        if (m_dialect == Sqlite && field.value(QStringLiteral("type")).toString() == QLatin1String("memo")
            && !longName.match(col.name).hasMatch()
            && (col.declared.trimmed().isEmpty() || col.declared.trimmed().compare(QLatin1String("TEXT"), Qt::CaseInsensitive) == 0)) {
            field.insert(QStringLiteral("type"), QStringLiteral("text"));
            field.insert(QStringLiteral("maxLength"), 4000);
        }
        const QString type = field.value(QStringLiteral("type")).toString();
        if (col.name == key) {
            // a key that's typed in: one to a record, always there
            field.insert(QStringLiteral("required"), true);
            field.insert(QStringLiteral("unique"), true);
        } else {
            const QString def = defaultOf(col.def, type);
            if (col.notNull && def.isEmpty() && !col.autoNumber && type != QLatin1String("boolean"))
                field.insert(QStringLiteral("required"), true);
            if (col.unique && type != QLatin1String("memo") && type != QLatin1String("boolean"))
                field.insert(QStringLiteral("unique"), true);
            if (!def.isEmpty())
                field.insert(QStringLiteral("default"), def);
        }
        if (!col.refTable.isEmpty()) {
            // (made a Lookup by readSchema(), once every table has its name)
            field.insert(QStringLiteral("refTable"), col.refTable);
            field.insert(QStringLiteral("refColumn"), col.refColumn);
        }
        fields << field;
    }

    QVariantMap table{ { QStringLiteral("key"), key }, { QStringLiteral("keyAuto"), numbered && !key.isEmpty() },
                       { QStringLiteral("stamps"), stamps }, { QStringLiteral("imported"), true },
                       { QStringLiteral("fields"), fields } };
    // how many records
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(quote(dbName))) && q.next())
        table.insert(QStringLiteral("records"), q.value(0).toLongLong());
    return table;
}

QString AppDatabase::execForTest(const QString &statement)
{
    QSqlQuery query(m_db);
    return query.exec(statement) ? QString() : query.lastError().text();
}

QVariantMap AppDatabase::readSchema()
{
    if (!isOpen())
        return failed(QStringLiteral("The database isn't open."));
    QStringList notes, names;
    QVariantList tables;
    QHash<QString, QString> projectNameOf; // database name -> project name
    QStringList taken;
    for (const QVariant &t : std::as_const(m_tables))
        taken << t.toMap().value(QStringLiteral("name")).toString();
    QStringList dbTables = existingTables();
    std::sort(dbTables.begin(), dbTables.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const QString &dbName : std::as_const(dbTables)) {
        if (dbName.startsWith(QLatin1String("sqlite_")))
            continue;
        QVariantMap table = readTable(dbName, &notes);
        const QString name = nameProblem(dbName).isEmpty() && !taken.contains(dbName, Qt::CaseInsensitive) ? dbName : codeName(dbName, taken);
        taken << name;
        table.insert(QStringLiteral("name"), name);
        if (name != dbName)
            table.insert(QStringLiteral("dbName"), dbName);
        projectNameOf.insert(dbName, name);
        tables << table;
    }
    // the project's tables are in it too, by their database names
    for (const QVariant &t : std::as_const(m_tables)) {
        const QVariantMap table = t.toMap();
        const QString db = table.value(QStringLiteral("dbName")).toString();
        projectNameOf.insert(db.isEmpty() ? table.value(QStringLiteral("name")).toString() : db, table.value(QStringLiteral("name")).toString());
    }
    // a column that refers to another table's key: a Lookup of it
    for (QVariant &t : tables) {
        QVariantMap table = t.toMap();
        QVariantList fields = table.value(QStringLiteral("fields")).toList();
        for (QVariant &f : fields) {
            QVariantMap field = f.toMap();
            const QString refTable = field.take(QStringLiteral("refTable")).toString();
            const QString refColumn = field.take(QStringLiteral("refColumn")).toString();
            if (!refTable.isEmpty() && projectNameOf.contains(refTable)) {
                QVariantMap target;
                for (const QVariant &other : std::as_const(tables)) {
                    if (other.toMap().value(QStringLiteral("name")).toString() == projectNameOf.value(refTable))
                        target = other.toMap();
                }
                if (target.isEmpty())
                    target = tableInfo(projectNameOf.value(refTable));
                if (keyName(target) == refColumn) {
                    field.insert(QStringLiteral("type"), QStringLiteral("lookup"));
                    field.insert(QStringLiteral("lookupTable"), projectNameOf.value(refTable));
                    field.remove(QStringLiteral("maxLength"));
                    field.remove(QStringLiteral("decimals"));
                    field.remove(QStringLiteral("default"));
                }
            }
            f = field;
        }
        table.insert(QStringLiteral("fields"), fields);
        t = table;
    }
    return { { QStringLiteral("ok"), true }, { QStringLiteral("tables"), tables }, { QStringLiteral("notes"), notes } };
}

// --- web APIs ----------------------------------------------------------------------

namespace {

// a value inside a JSON answer: "owner.login", "data.items" ("" the answer)
QVariant valueAt(const QVariant &data, const QString &path)
{
    if (path.isEmpty())
        return data;
    QVariant v = data;
    for (const QString &part : path.split(QLatin1Char('.'))) {
        if (v.typeId() == QMetaType::QVariantList) {
            bool number = false;
            const int i = part.toInt(&number);
            v = number ? v.toList().value(i) : QVariant();
        } else {
            v = v.toMap().value(part);
        }
    }
    return v;
}
void setAt(QVariantMap &map, const QStringList &path, const QVariant &value)
{
    if (path.size() == 1) {
        map.insert(path.first(), value);
        return;
    }
    QVariantMap inner = map.value(path.first()).toMap();
    setAt(inner, path.mid(1), value);
    map.insert(path.first(), inner);
}
QString apiPathOf(const QVariantMap &field)
{
    const QString p = field.value(QStringLiteral("path")).toString();
    return p.isEmpty() ? field.value(QStringLiteral("name")).toString() : p;
}

// a value from JSON as a field keeps it
QVariant fromApi(const QString &type, const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return QVariant::fromValue(nullptr);
    if (type == QLatin1String("boolean"))
        return v.typeId() == QMetaType::QString ? (v.toString() == QLatin1String("true") || v.toString() == QLatin1String("1")) : v.toBool();
    if (type == QLatin1String("integer"))
        return v.toLongLong();
    if (type == QLatin1String("decimal") || type == QLatin1String("float") || type == QLatin1String("currency"))
        return v.toDouble();
    if (type == QLatin1String("date")) {
        const QDate d = DateTimes::parseDate(v);
        return d.isValid() ? QVariant(d.toString(DateTimes::kDateFormat)) : QVariant(v.toString());
    }
    if (type == QLatin1String("time")) {
        const QTime t = DateTimes::parseTime(v);
        return t.isValid() ? QVariant(t.toString(DateTimes::kTimeFormat)) : QVariant(v.toString());
    }
    if (type == QLatin1String("datetime")) {
        // (an API's times are usually UTC, "2024-05-06T10:30:00Z": shown in this computer's)
        QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = DateTimes::parseDateTime(v);
        return dt.isValid() ? QVariant(dt.toLocalTime().toString(DateTimes::kDateTimeFormat)) : QVariant(v.toString());
    }
    if (type == QLatin1String("lookup"))
        return v.typeId() == QMetaType::QString ? v : QVariant(v.toLongLong());
    if (v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QVariantList)
        return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
    return v.toString();
}
// a field's value as JSON
QVariant toApi(const QString &type, const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return QVariant::fromValue(nullptr);
    if (type == QLatin1String("boolean"))
        return v.toBool();
    if (type == QLatin1String("integer"))
        return v.toLongLong();
    if (type == QLatin1String("decimal") || type == QLatin1String("float") || type == QLatin1String("currency"))
        return v.toDouble();
    if (type == QLatin1String("datetime")) {
        const QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
        return dt.isValid() ? QVariant(dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)) : v;
    }
    return v;
}

// a field's type from JSON values (what most of them are)
QString typeOfValues(const QVariantList &values, int *longest)
{
    int whole = 0, fraction = 0, yesNo = 0, date = 0, dateTime = 0, email = 0, url = 0, text = 0, structured = 0;
    *longest = 0;
    static const QRegularExpression dateRe(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d$"));
    static const QRegularExpression dateTimeRe(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d[T ]\\d\\d:\\d\\d(:\\d\\d)?(\\.\\d+)?(Z|[+-]\\d\\d:?\\d\\d)?$"));
    static const QRegularExpression emailRe(QStringLiteral("^[^@\\s]+@[^@\\s.]+(\\.[^@\\s.]+)+$"));
    static const QRegularExpression urlRe(QStringLiteral("^https?://\\S+$"));
    for (const QVariant &v : values) {
        if (!v.isValid() || v.isNull())
            continue;
        const int t = v.typeId();
        if (t == QMetaType::Bool) {
            ++yesNo;
        } else if (t == QMetaType::Double || t == QMetaType::LongLong || t == QMetaType::Int || t == QMetaType::ULongLong) {
            if (v.toDouble() == std::floor(v.toDouble()) && std::fabs(v.toDouble()) < 9e15)
                ++whole;
            else
                ++fraction;
        } else if (t == QMetaType::QVariantMap || t == QMetaType::QVariantList) {
            ++structured;
        } else {
            const QString s = v.toString();
            *longest = qMax(*longest, int(s.size()));
            if (dateRe.match(s).hasMatch()) ++date;
            else if (dateTimeRe.match(s).hasMatch()) ++dateTime;
            else if (emailRe.match(s).hasMatch()) ++email;
            else if (urlRe.match(s).hasMatch()) ++url;
            else ++text;
        }
    }
    if (structured)
        return QStringLiteral("memo");
    if (fraction)
        return QStringLiteral("float");
    if (whole && !text && !date && !dateTime)
        return QStringLiteral("integer");
    if (yesNo && !text)
        return QStringLiteral("boolean");
    if (text || (email && url))
        return *longest > 255 ? QStringLiteral("memo") : QStringLiteral("text");
    if (dateTime)
        return QStringLiteral("datetime");
    if (date)
        return QStringLiteral("date");
    if (email)
        return QStringLiteral("email");
    if (url)
        return QStringLiteral("url");
    return QStringLiteral("text");
}

} // namespace

bool AppDatabase::isApi(const QVariantMap &table) const
{
    return table.value(QStringLiteral("source")).toString() == QLatin1String("api");
}

void AppDatabase::setApis(const QVariantList &apis)
{
    if (!m_api)
        m_api = new ApiClient(this);
    m_api->setApis(apis);
    m_apiLists.clear();
}

QVariantMap AppDatabase::callApi(const QString &api, const QString &method, const QString &path, const QVariant &body,
                                 const QVariantMap &query)
{
    if (!m_api)
        setApis({});
    return m_api->request(api, method, path, query, body);
}

QVariantMap AppDatabase::signIn(const QString &api)
{
    if (!m_api)
        setApis({});
    m_apiLists.clear();
    return m_api->signIn(api);
}

void AppDatabase::signOut(const QString &api)
{
    if (m_api)
        m_api->signOut(api);
    m_apiLists.clear();
}

// a record of the API as the table's: { id, Field: value... }
QVariantMap AppDatabase::apiRow(const QVariantMap &table, const QVariant &record) const
{
    QVariantMap row;
    const QString key = keyName(table);
    const QVariant id = key.isEmpty() ? QVariant() : valueAt(record, key);
    row.insert(kId, !id.isValid() || id.isNull() ? QVariant::fromValue(nullptr)
                    : (id.typeId() == QMetaType::QString ? id : QVariant(id.toLongLong() == id.toDouble() ? QVariant(id.toLongLong()) : id)));
    for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
        const QVariantMap field = f.toMap();
        row.insert(field.value(QStringLiteral("name")).toString(),
                   fromApi(field.value(QStringLiteral("type")).toString(), valueAt(record, apiPathOf(field))));
    }
    return row;
}

// the table's values as the API's JSON (a field's path makes its nesting)
QVariantMap AppDatabase::apiBody(const QVariantMap &table, const QVariantMap &values) const
{
    QVariantMap body;
    for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
        const QVariantMap field = f.toMap();
        const QString name = field.value(QStringLiteral("name")).toString();
        // (one the API sets itself, like its updated time: shown, never sent)
        if (values.contains(name) && !field.value(QStringLiteral("readOnly")).toBool())
            setAt(body, apiPathOf(field).split(QLatin1Char('.')), toApi(field.value(QStringLiteral("type")).toString(), values.value(name)));
    }
    return body;
}

QString AppDatabase::apiItemPath(const QVariantMap &table, const QVariant &id) const
{
    QString endpoint = table.value(QStringLiteral("endpoint")).toString();
    while (endpoint.endsWith(QLatin1Char('/')))
        endpoint.chop(1);
    return endpoint + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(id.toString()));
}

// the endpoint's records (kept for 3 seconds; a write forgets them)
QVariantList AppDatabase::apiRows(const QVariantMap &table, QString *error)
{
    const QString name = table.value(QStringLiteral("name")).toString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto kept = m_apiLists.constFind(name);
    if (kept != m_apiLists.cend() && now - kept->at < 3000)
        return kept->rows;
    if (!m_api)
        setApis({});
    QVariantMap query;
    const QUrlQuery extra(table.value(QStringLiteral("query")).toString());
    for (const auto &[k, v] : extra.queryItems(QUrl::FullyDecoded))
        query.insert(k, v);
    const QVariantMap r = m_api->request(table.value(QStringLiteral("api")).toString(), QStringLiteral("GET"),
                                         table.value(QStringLiteral("endpoint")).toString(), query);
    if (!r.value(QStringLiteral("ok")).toBool()) {
        *error = r.value(QStringLiteral("error")).toString();
        return {};
    }
    const QVariant list = valueAt(r.value(QStringLiteral("data")), table.value(QStringLiteral("recordsPath")).toString());
    QVariantList rows;
    for (const QVariant &record : list.toList())
        rows << apiRow(table, record);
    m_apiLists.insert(name, { rows, now });
    return rows;
}

// where, search, orderBy, descending, limit, offset: as select() does them in SQL
QVariantList AppDatabase::filtered(const QVariantMap &table, QVariantList rows, const QVariantMap &options)
{
    auto known = [&](const QString &name) {
        if (name == kId)
            return true;
        for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
            if (f.toMap().value(QStringLiteral("name")).toString() == name)
                return true;
        }
        return false;
    };
    auto compare = [](const QVariant &a, const QVariant &b) {
        bool na = false, nb = false;
        const double x = a.toDouble(&na), y = b.toDouble(&nb);
        if (na && nb && a.typeId() != QMetaType::QString)
            return x < y ? -1 : x > y ? 1 : 0;
        return QString::compare(a.toString(), b.toString());
    };
    const QVariantMap where = options.value(QStringLiteral("where")).toMap();
    for (auto it = where.cbegin(); it != where.cend(); ++it) {
        if (!known(it.key())) {
            fail(QStringLiteral("%1 has no field named %2.").arg(table.value(QStringLiteral("name")).toString(), it.key()));
            return {};
        }
    }
    const QString search = options.value(QStringLiteral("search")).toString().trimmed();
    QVariantList out;
    for (const QVariant &r : std::as_const(rows)) {
        const QVariantMap row = r.toMap();
        bool keep = true;
        for (auto it = where.cbegin(); keep && it != where.cend(); ++it) {
            QString op = QStringLiteral("=");
            QVariant want = it.value();
            if (want.typeId() == QMetaType::QVariantMap) {
                op = want.toMap().value(QStringLiteral("op"), op).toString().toLower();
                want = want.toMap().value(QStringLiteral("value"));
            }
            const QVariant have = row.value(it.key());
            const bool none = !have.isValid() || have.isNull();
            if (!want.isValid() || want.isNull()) {
                keep = op == QLatin1String("<>") || op == QLatin1String("!=") ? !none : none;
            } else if (none) {
                keep = false;
            } else if (op == QLatin1String("like")) {
                const QString pattern = QRegularExpression::escape(want.toString()).replace(QLatin1String("%"), QLatin1String(".*"))
                                            .replace(QLatin1String("_"), QLatin1String("."));
                keep = QRegularExpression(QRegularExpression::anchoredPattern(pattern), QRegularExpression::CaseInsensitiveOption)
                           .match(have.toString()).hasMatch();
            } else {
                const int c = compare(have, want);
                keep = op == QLatin1String("<") ? c < 0 : op == QLatin1String("<=") ? c <= 0 : op == QLatin1String(">") ? c > 0
                     : op == QLatin1String(">=") ? c >= 0 : op == QLatin1String("<>") || op == QLatin1String("!=") ? c != 0 : c == 0;
            }
        }
        if (keep && !search.isEmpty()) {
            keep = false;
            for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
                const QVariantMap field = f.toMap();
                if (isTextType(field.value(QStringLiteral("type")).toString())
                    && row.value(field.value(QStringLiteral("name")).toString()).toString().contains(search, Qt::CaseInsensitive))
                    keep = true;
            }
        }
        if (keep)
            out << row;
    }
    const QString orderBy = options.value(QStringLiteral("orderBy")).toString();
    if (!orderBy.isEmpty() && known(orderBy)) {
        const bool down = options.value(QStringLiteral("descending")).toBool();
        std::stable_sort(out.begin(), out.end(), [&](const QVariant &a, const QVariant &b) {
            const QVariant x = a.toMap().value(orderBy), y = b.toMap().value(orderBy);
            // (empty ones last)
            const bool nx = !x.isValid() || x.isNull(), ny = !y.isValid() || y.isNull();
            if (nx != ny)
                return ny;
            const int c = compare(x, y);
            return down ? c > 0 : c < 0;
        });
    }
    const int offset = options.value(QStringLiteral("offset")).toInt();
    const int limit = options.value(QStringLiteral("limit")).toInt();
    if (offset > 0)
        out = out.mid(offset);
    if (limit > 0)
        out = out.mid(0, limit);
    return out;
}

QVariantMap AppDatabase::readEndpoint(const QString &api, const QString &path, const QString &recordsPath, const QString &query)
{
    QVariantMap q;
    const QUrlQuery extra(query);
    for (const auto &[k, v] : extra.queryItems(QUrl::FullyDecoded))
        q.insert(k, v);
    const QVariantMap r = callApi(api, QStringLiteral("GET"), path, QVariant(), q);
    if (!r.value(QStringLiteral("ok")).toBool())
        return r;
    const QVariant data = r.value(QStringLiteral("data"));
    // where the list is: as said, the answer itself, or the first list in it
    QString at = recordsPath;
    QVariant list = valueAt(data, at);
    if (at.isEmpty() && data.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = data.toMap();
        for (const char *guess : { "data", "items", "results", "records", "rows", "value", "list" }) {
            if (map.value(QLatin1String(guess)).typeId() == QMetaType::QVariantList) {
                at = QLatin1String(guess);
                break;
            }
        }
        if (at.isEmpty()) {
            for (auto it = map.cbegin(); it != map.cend(); ++it) {
                if (it.value().typeId() == QMetaType::QVariantList) {
                    at = it.key();
                    break;
                }
            }
        }
        list = valueAt(data, at);
    }
    if (list.typeId() != QMetaType::QVariantList)
        return failed(QStringLiteral("The answer has no list of records%1.").arg(at.isEmpty() ? QString() : QStringLiteral(" at %1").arg(at)));
    const QVariantList records = list.toList();

    // its fields: what the records have (an object inside one: its fields, "owner.login")
    QStringList paths;
    QHash<QString, QVariantList> values;
    std::function<void(const QVariantMap &, const QString &, int)> walk = [&](const QVariantMap &object, const QString &prefix, int depth) {
        for (auto it = object.cbegin(); it != object.cend(); ++it) {
            const QString p = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
            if (it.value().typeId() == QMetaType::QVariantMap && depth < 1) {
                walk(it.value().toMap(), p, depth + 1);
                continue;
            }
            if (!paths.contains(p))
                paths << p;
            values[p] << it.value();
        }
    };
    for (const QVariant &record : records.mid(0, 200))
        walk(record.toMap(), QString(), 0);
    // (in the API's own order: by where each one's last name first comes in the answer)
    const QStringList order = r.value(QStringLiteral("keyOrder")).toStringList();
    std::stable_sort(paths.begin(), paths.end(), [&](const QString &a, const QString &b) {
        const auto at = [&](const QString &p) {
            const qsizetype first = order.indexOf(p.section(QLatin1Char('.'), 0, 0));
            const qsizetype last = order.indexOf(p.section(QLatin1Char('.'), -1));
            return std::make_pair(first < 0 ? 1 << 30 : first, last < 0 ? 1 << 30 : last);
        };
        return at(a) < at(b);
    });
    QString key;
    for (const char *guess : { "id", "_id", "uuid", "key", "ID", "Id" }) {
        if (paths.contains(QLatin1String(guess))) {
            key = QLatin1String(guess);
            break;
        }
    }
    QVariantList fields;
    QStringList taken;
    for (const QString &p : std::as_const(paths)) {
        if (p == key)
            continue;
        int longest = 0;
        const QString type = typeOfValues(values.value(p), &longest);
        const QString flat = QString(p).replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString name = nameProblem(flat).isEmpty() && !taken.contains(flat, Qt::CaseInsensitive) && !isSystemColumn(flat.toLower())
            ? flat : codeName(flat, taken);
        taken << name;
        QVariantMap field{ { QStringLiteral("name"), name }, { QStringLiteral("type"), type } };
        if (name != p)
            field.insert(QStringLiteral("path"), p);
        // named as the API's own times are (updated, created_at...): it sets them
        static const QRegularExpression stamp(QStringLiteral("^(created|updated|modified|deleted)([_ ]?(at|on|date|time))?$|_at$|[a-z]At$"),
                                              QRegularExpression::CaseInsensitiveOption);
        if ((type == QLatin1String("datetime") || type == QLatin1String("date")) && stamp.match(p.section(QLatin1Char('.'), -1)).hasMatch())
            field.insert(QStringLiteral("readOnly"), true);
        if (type == QLatin1String("text"))
            field.insert(QStringLiteral("maxLength"), longest > 200 ? 4000 : 255);
        fields << field;
    }
    QVariantMap table{ { QStringLiteral("name"), QStringLiteral("probe") }, { QStringLiteral("key"), key },
                       { QStringLiteral("fields"), fields } };
    QVariantList sample;
    for (const QVariant &record : records.mid(0, 5))
        sample << apiRow(table, record);
    return { { QStringLiteral("ok"), true }, { QStringLiteral("recordsPath"), at }, { QStringLiteral("key"), key },
             { QStringLiteral("fields"), fields }, { QStringLiteral("sample"), sample }, { QStringLiteral("count"), records.size() } };
}

// --- validation --------------------------------------------------------------

// One field's value against its rules. Returns what's wrong ("" if nothing)
// and leaves `value` as it will be stored.
QString AppDatabase::checkField(const QVariantMap &table, const QVariantMap &field, QVariant &value,
                                const QVariantMap &record, const QVariant &id)
{
    const QString name = field.value(QStringLiteral("name")).toString();
    const QString type = field.value(QStringLiteral("type")).toString();
    const QString what = label(field);
    const QString custom = field.value(QStringLiteral("message")).toString().trimmed();

    if (type == QLatin1String("boolean")) {
        if (value.typeId() != QMetaType::Bool) {
            const QString s = value.toString().trimmed().toLower();
            if (isEmpty(value) || s == QLatin1String("0") || s == QLatin1String("no") || s == QLatin1String("false"))
                value = false;
            else if (s == QLatin1String("1") || s == QLatin1String("yes") || s == QLatin1String("true"))
                value = true;
            else
                return QStringLiteral("%1 must be Yes or No.").arg(what);
        }
        if (field.value(QStringLiteral("required")).toBool() && !value.toBool())
            return QStringLiteral("%1 must be Yes.").arg(what);
    } else if (isEmpty(value)) {
        value = QVariant();
        if (field.value(QStringLiteral("required")).toBool())
            return QStringLiteral("%1 is required.").arg(what);
        // (Required when another field is filled in, and the like)
        return checkRule(table, field, value, record, true);
    } else if (isTextType(type)) {
        QString text = value.toString();
        if (type != QLatin1String("memo"))
            text = text.trimmed();
        value = text;
        double n = 0;
        if (limit(field, "minLength", &n) && text.size() < int(n))
            return QStringLiteral("%1 must be at least %2 characters.").arg(what).arg(int(n));
        const bool hasMax = limit(field, "maxLength", &n) && n >= 1;
        const int max = hasMax ? int(n) : (type == QLatin1String("memo") ? 0 : 255);
        if (max > 0 && text.size() > max)
            return QStringLiteral("%1 can be at most %2 characters.").arg(what).arg(max);
        if (type == QLatin1String("email")) {
            static const QRegularExpression re(QStringLiteral("^[^@\\s]+@[^@\\s.]+(\\.[^@\\s.]+)+$"));
            if (!re.match(text).hasMatch())
                return QStringLiteral("%1 must be an e-mail address.").arg(what);
        } else if (type == QLatin1String("url")) {
            static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://[^\\s/]+\\S*$"));
            if (!re.match(text).hasMatch())
                return QStringLiteral("%1 must be a web address, like https://example.com.").arg(what);
        } else if (type == QLatin1String("phone")) {
            static const QRegularExpression re(QStringLiteral("^\\+?[0-9()\\-. ]*[0-9][0-9()\\-. ]*( ?(x|ext\\.?) ?[0-9]+)?$"));
            static const QRegularExpression digits(QStringLiteral("[0-9]"));
            if (!re.match(text).hasMatch() || text.count(digits) < 3)
                return QStringLiteral("%1 must be a telephone number.").arg(what);
        } else if (type == QLatin1String("choice")) {
            const QStringList choices = field.value(QStringLiteral("choices")).toStringList();
            if (!choices.contains(text))
                return QStringLiteral("%1 must be one of: %2.").arg(what, choices.join(QLatin1String(", ")));
        }
    } else if (type == QLatin1String("lookup") && isApi(tableInfo(field.value(QStringLiteral("lookupTable")).toString()))) {
        // a Lookup of an API's table: its id as the API has it (a number, or text)
        const QString target = field.value(QStringLiteral("lookupTable")).toString();
        bool number = false;
        const qlonglong n = value.toString().trimmed().toLongLong(&number);
        value = number ? QVariant(n) : QVariant(value.toString().trimmed());
        if (get(target, value).isEmpty())
            return QStringLiteral("%1: that record isn't in %2.").arg(what, target);
    } else if (type == QLatin1String("lookup") && !keyField(tableInfo(field.value(QStringLiteral("lookupTable")).toString())).isEmpty()
               && !QStringList{ QStringLiteral("integer"), QStringLiteral("lookup") }.contains(
                      keyField(tableInfo(field.value(QStringLiteral("lookupTable")).toString())).value(QStringLiteral("type")).toString())) {
        // a Lookup of a table whose key is typed in (a code, a name): that key
        const QString target = field.value(QStringLiteral("lookupTable")).toString();
        value = value.toString().trimmed();
        if (get(target, value).isEmpty())
            return QStringLiteral("%1: that record isn't in %2.").arg(what, target);
    } else if (type == QLatin1String("integer") || type == QLatin1String("lookup")) {
        bool ok = false;
        qlonglong n = 0;
        if (value.typeId() == QMetaType::QString) {
            n = value.toString().trimmed().toLongLong(&ok);
        } else {
            const double d = value.toDouble(&ok);
            ok = ok && std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e18;
            n = qlonglong(d);
        }
        if (!ok)
            return type == QLatin1String("lookup") ? QStringLiteral("%1 isn't a record that can be chosen.").arg(what)
                                                    : QStringLiteral("%1 must be a whole number.").arg(what);
        value = n;
        if (type == QLatin1String("lookup")) {
            const QString target = field.value(QStringLiteral("lookupTable")).toString();
            if (tableInfo(target).isEmpty())
                return QStringLiteral("%1 looks up a table that isn't in the project (%2).").arg(what, target);
            if (get(target, n).isEmpty())
                return QStringLiteral("%1: that record isn't in %2 any more.").arg(what, target);
        }
    } else if (isNumberType(type)) {
        bool ok = false;
        double d = value.typeId() == QMetaType::QString ? value.toString().trimmed().toDouble(&ok) : value.toDouble(&ok);
        if (!ok || !std::isfinite(d))
            return QStringLiteral("%1 must be a number.").arg(what);
        if (type != QLatin1String("float")) {
            double places = type == QLatin1String("currency") ? 4 : 2;
            if (type == QLatin1String("decimal"))
                limit(field, "decimals", &places);
            const double scale = std::pow(10.0, qBound(0, int(places), 10));
            d = std::round(d * scale) / scale;
        }
        value = d;
    } else if (type == QLatin1String("date")) {
        const QDate d = parseDate(value);
        if (!d.isValid())
            return QStringLiteral("%1 must be a date, like %2.").arg(what, QDate::currentDate().toString(kDateFormat));
        value = d.toString(kDateFormat);
    } else if (type == QLatin1String("time")) {
        const QTime t = parseTime(value);
        if (!t.isValid())
            return QStringLiteral("%1 must be a time, like 14:30.").arg(what);
        value = t.toString(kTimeFormat);
    } else if (type == QLatin1String("datetime")) {
        const QDateTime dt = parseDateTime(value);
        if (!dt.isValid())
            return QStringLiteral("%1 must be a date and time, like %2 14:30.").arg(what, QDate::currentDate().toString(kDateFormat));
        value = dt.toString(kDateTimeFormat);
    } else {
        return QStringLiteral("%1 has a type this doesn't know (%2).").arg(what, type);
    }

    // Minimum and Maximum: numbers by value, dates and times by their text
    // (which sorts the same way)
    if (type == QLatin1String("integer") || isNumberType(type)) {
        double n = 0;
        if (limit(field, "min", &n) && value.toDouble() < n)
            return QStringLiteral("%1 must be %2 or more.").arg(what, number(n));
        if (limit(field, "max", &n) && value.toDouble() > n)
            return QStringLiteral("%1 must be %2 or less.").arg(what, number(n));
    } else if (type == QLatin1String("date") || type == QLatin1String("time") || type == QLatin1String("datetime")) {
        auto bound = [&](const char *key) {
            const QVariant v = field.value(QLatin1String(key));
            if (isEmpty(v))
                return QString();
            if (type == QLatin1String("date"))
                return parseDate(v).toString(kDateFormat);
            if (type == QLatin1String("time"))
                return parseTime(v).toString(kTimeFormat);
            return parseDateTime(v).toString(kDateTimeFormat);
        };
        const QString min = bound("min"), max = bound("max");
        if (!min.isEmpty() && value.toString() < min)
            return QStringLiteral("%1 can't be before %2.").arg(what, QString(min).replace(QLatin1Char('T'), QLatin1Char(' ')));
        if (!max.isEmpty() && value.toString() > max)
            return QStringLiteral("%1 can't be after %2.").arg(what, QString(max).replace(QLatin1Char('T'), QLatin1Char(' ')));
    }

    // Pattern: the whole value has to match
    const QString pattern = field.value(QStringLiteral("pattern")).toString();
    if (!pattern.isEmpty() && type != QLatin1String("boolean")) {
        const QRegularExpression re(QRegularExpression::anchoredPattern(pattern));
        if (!re.isValid())
            return QStringLiteral("%1's pattern isn't one that can be read (%2).").arg(what, re.errorString());
        if (!re.match(value.toString()).hasMatch())
            return custom.isEmpty() ? QStringLiteral("%1 isn't in the form it has to be.").arg(what) : custom;
    }

    // Validation Rule
    const QString problem = checkRule(table, field, value, record,
                                      type == QLatin1String("boolean") && !value.toBool());
    if (!problem.isEmpty())
        return problem;

    // No Duplicates (an API's table: in its list)
    if (field.value(QStringLiteral("unique")).toBool() && isApi(table) && !isEmpty(value)) {
        QString error;
        for (const QVariant &r : apiRows(table, &error)) {
            const QVariantMap row = r.toMap();
            if (row.value(name).toString() == value.toString() && (isEmpty(id) || row.value(kId).toString() != id.toString()))
                return QStringLiteral("There is already a record with this %1.").arg(what);
        }
    } else if (field.value(QStringLiteral("unique")).toBool() && isOpen() && !isEmpty(value)) {
        QSqlQuery query(m_db);
        // (not counting the record itself)
        const QString key = keyName(table);
        const bool other = !isEmpty(id) && !key.isEmpty();
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = ?").arg(sqlTable(table), quote(column(field)))
                      + (other ? QStringLiteral(" AND %1 <> ?").arg(key == QLatin1String("rowid") ? key : quote(key)) : QString()));
        query.addBindValue(toSql(field, value));
        if (other)
            query.addBindValue(keyValue(table, id));
        if (query.exec() && query.next() && query.value(0).toInt() > 0)
            return QStringLiteral("There is already a record with this %1.").arg(what);
    }
    return QString();
}

// The field's Validation Rule (one from the list, or an expression of
// `value` and `record` that has to be true): what's wrong, or "".
QString AppDatabase::checkRule(const QVariantMap &table, const QVariantMap &field, const QVariant &value,
                               const QVariantMap &record, bool blank)
{
    const QString rule = FieldRules::ruleOf(field);
    if (rule.isEmpty())
        return QString();
    const QString what = label(field);
    const QString custom = field.value(QStringLiteral("message")).toString().trimmed();

    if (rule == QLatin1String("expression")) {
        const QString expression = field.value(QStringLiteral("rule")).toString().trimmed();
        if (blank || expression.isEmpty())
            return QString();
        if (!m_rules)
            m_rules = new QJSEngine(this);
        QJSValue function = m_rules->evaluate(QStringLiteral("(function (value, record) { return (%1\n); })").arg(expression));
        QJSValue result = function.isCallable()
            ? function.call({ m_rules->toScriptValue(value), m_rules->toScriptValue(record) })
            : function;
        if (result.isError())
            return QStringLiteral("%1's validation rule doesn't run: %2").arg(what, result.toString());
        if (!result.toBool())
            return custom.isEmpty() ? QStringLiteral("%1 isn't valid.").arg(what) : custom;
        return QString();
    }

    // another field's value as it would be stored: its type's checks only
    auto valueOf = [&](const QVariantMap &other) {
        QVariantMap bare{ { QStringLiteral("name"), other.value(QStringLiteral("name")) },
                          { QStringLiteral("type"), other.value(QStringLiteral("type")) },
                          { QStringLiteral("decimals"), other.value(QStringLiteral("decimals")) },
                          { QStringLiteral("choices"), other.value(QStringLiteral("choices")) } };
        if (bare.value(QStringLiteral("type")).toString() == QLatin1String("lookup"))
            bare.insert(QStringLiteral("type"), QStringLiteral("integer"));
        QVariant v = record.value(other.value(QStringLiteral("name")).toString());
        if (!checkField(table, bare, v, record, QVariant()).isEmpty())
            return QVariant();
        return v;
    };
    const FieldRules::Check c = FieldRules::check(table, field, value, blank, valueOf);
    if (c.result == FieldRules::Result::Fail)
        return custom.isEmpty() ? c.problem : custom;
    return c.problem;
}

QVariantList AppDatabase::validators(const QString &type)
{
    return FieldRules::forType(type);
}

QString AppDatabase::ruleMessage(const QVariantMap &table, const QVariantMap &field)
{
    return FieldRules::defaultMessage(table, field);
}

QString AppDatabase::ruleSummary(const QVariantMap &table, const QVariantMap &field)
{
    return FieldRules::summary(table, field);
}

QVariantMap AppDatabase::validate(const QString &tableName, const QVariantMap &record, const QVariant &id, bool partial)
{
    const QVariantMap table = tableInfo(tableName);
    if (table.isEmpty())
        return failed(QStringLiteral("There is no table named %1.").arg(tableName));

    QVariantMap errors, values;
    QStringList messages;
    // the whole record as the rules see it: what's given over the defaults
    QVariantMap whole = partial ? QVariantMap() : defaults(tableName);
    for (auto it = record.cbegin(); it != record.cend(); ++it)
        whole.insert(it.key(), it.value());

    const QVariantList fields = table.value(QStringLiteral("fields")).toList();
    // the dates and times the database sets by itself: "created" when the
    // record is made (and never after), "modified" every time it's saved
    QStringList stamped;
    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        const QString name = field.value(QStringLiteral("name")).toString();
        const QString stamp = field.value(QStringLiteral("auto")).toString();
        const QString now = field.value(QStringLiteral("type")).toString() == QLatin1String("date")
            ? QStringLiteral("today") : QStringLiteral("now");
        if (stamp == QLatin1String("modified") || (stamp == QLatin1String("created") && !partial)) {
            whole.insert(name, now);
            stamped << name;
        } else if (stamp == QLatin1String("created")) {
            stamped << name; // (an update leaves it as it is)
            whole.remove(name);
        }
    }

    for (const QVariant &f : fields) {
        const QVariantMap field = f.toMap();
        const QString name = field.value(QStringLiteral("name")).toString();
        const bool keep = field.value(QStringLiteral("auto")).toString() == QLatin1String("created") && partial;
        if (keep || (partial && !record.contains(name) && !stamped.contains(name)))
            continue;
        QVariant value = whole.value(name);
        const QString error = checkField(table, field, value, whole, id);
        if (!error.isEmpty()) {
            errors.insert(name, error);
            messages << error;
        }
        values.insert(name, value);
    }
    for (auto it = record.cbegin(); it != record.cend(); ++it) {
        if (isSystemColumn(it.key()) || values.contains(it.key()) || stamped.contains(it.key()))
            continue;
        const QString error = QStringLiteral("%1 has no field named %2.").arg(tableName, it.key());
        errors.insert(it.key(), error);
        messages << error;
    }
    return { { QStringLiteral("ok"), errors.isEmpty() },
             { QStringLiteral("errors"), errors },
             { QStringLiteral("messages"), messages },
             { QStringLiteral("values"), values } };
}

// --- writing -----------------------------------------------------------------

QVariantMap AppDatabase::insert(const QString &tableName, const QVariantMap &record)
{
    if (isApi(tableInfo(tableName))) {
        const QVariantMap table = tableInfo(tableName);
        if (!table.value(QStringLiteral("canCreate"), true).toBool())
            return failed(QStringLiteral("%1's records can't be added here.").arg(tableName));
        QVariantMap result = validate(tableName, record);
        if (!result.value(QStringLiteral("ok")).toBool())
            return result;
        const QVariantMap r = callApi(table.value(QStringLiteral("api")).toString(), QStringLiteral("POST"),
                                      table.value(QStringLiteral("endpoint")).toString(),
                                      apiBody(table, result.value(QStringLiteral("values")).toMap()));
        m_apiLists.remove(tableName);
        if (!r.value(QStringLiteral("ok")).toBool())
            return failed(r.value(QStringLiteral("error")).toString());
        const QVariant made = valueAt(r.value(QStringLiteral("data")), table.value(QStringLiteral("itemPath")).toString());
        result.insert(kId, apiRow(table, made).value(kId));
        emit changed(tableName);
        return result;
    }
    if (!isOpen())
        return failed(QStringLiteral("The database isn't open."));
    QVariantMap result = validate(tableName, record);
    if (!result.value(QStringLiteral("ok")).toBool())
        return result;
    const QVariantMap values = result.value(QStringLiteral("values")).toMap();
    const QVariantMap table = tableInfo(tableName);
    const QString key = keyName(table);
    const bool numbered = !key.isEmpty() && keyAuto(table);
    const QVariantMap datetime{ { QStringLiteral("type"), QStringLiteral("datetime") } };
    const QVariant now = toSql(datetime, QDateTime::currentDateTime().toString(kDateTimeFormat));

    QStringList columns;
    QVariantList binds;
    if (hasStamps(table)) {
        columns << quote(kCreated) << quote(kModified);
        binds << now << now;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QVariantMap field = fieldInfo(tableName, it.key());
        columns << quote(column(field));
        binds << toSql(field, it.value());
    }
    QStringList marks;
    for (int i = 0; i < columns.size(); ++i)
        marks << QStringLiteral("?");
    QString sql = columns.isEmpty() ? QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(sqlTable(table))
        : QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
              .arg(sqlTable(table), columns.join(QLatin1String(", ")), marks.join(QLatin1String(", ")));
    if (m_dialect == Postgres && numbered)
        sql += QStringLiteral(" RETURNING ") + quote(key);
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : std::as_const(binds))
        query.addBindValue(bind);
    if (!query.exec())
        return failed(query.lastError().text());
    // the new record's id: the database's number, or the key that was given
    QVariant id = QVariant::fromValue(nullptr);
    if (numbered && m_dialect == Postgres) {
        if (query.next())
            id = query.value(0).toLongLong();
    } else if (numbered) {
        id = query.lastInsertId().toLongLong();
    } else if (!key.isEmpty()) {
        id = values.value(keyField(table).value(QStringLiteral("name")).toString());
    }
    result.insert(kId, id);
    emit changed(tableName);
    return result;
}

QVariantMap AppDatabase::update(const QString &tableName, const QVariant &id, const QVariantMap &record)
{
    if (isApi(tableInfo(tableName))) {
        const QVariantMap table = tableInfo(tableName);
        if (!table.value(QStringLiteral("canUpdate"), true).toBool())
            return failed(QStringLiteral("%1's records can't be changed here.").arg(tableName));
        const QVariantMap now = get(tableName, id);
        if (now.isEmpty())
            return failed(m_error.isEmpty() ? QStringLiteral("That record isn't in %1 any more.").arg(tableName) : m_error);
        QVariantMap result = validate(tableName, record, id, true);
        if (!result.value(QStringLiteral("ok")).toBool())
            return result;
        // PATCH: what changed; PUT: the whole record, changed
        const bool put = table.value(QStringLiteral("updateMethod")).toString().compare(QLatin1String("PUT"), Qt::CaseInsensitive) == 0;
        QVariantMap values = result.value(QStringLiteral("values")).toMap();
        if (put) {
            QVariantMap whole = now;
            whole.remove(kId);
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                whole.insert(it.key(), it.value());
            values = whole;
        }
        const QVariantMap r = callApi(table.value(QStringLiteral("api")).toString(), put ? QStringLiteral("PUT") : QStringLiteral("PATCH"),
                                      apiItemPath(table, id), apiBody(table, values));
        m_apiLists.remove(tableName);
        if (!r.value(QStringLiteral("ok")).toBool())
            return failed(r.value(QStringLiteral("error")).toString());
        result.insert(kId, id);
        emit changed(tableName);
        return result;
    }
    if (!isOpen())
        return failed(QStringLiteral("The database isn't open."));
    if (!tableInfo(tableName).isEmpty() && keyName(tableInfo(tableName)).isEmpty())
        return failed(QStringLiteral("%1 has no key in the database, so its records can only be added and read here.").arg(tableName));
    if (get(tableName, id).isEmpty())
        return failed(m_error.isEmpty() || !tableInfo(tableName).isEmpty()
                          ? QStringLiteral("That record isn't in %1 any more.").arg(tableName) : m_error);
    QVariantMap result = validate(tableName, record, id, true);
    if (!result.value(QStringLiteral("ok")).toBool())
        return result;
    const QVariantMap values = result.value(QStringLiteral("values")).toMap();
    const QVariantMap table = tableInfo(tableName);
    const QString key = keyName(table);
    const QVariantMap datetime{ { QStringLiteral("type"), QStringLiteral("datetime") } };

    QStringList sets;
    QVariantList binds;
    if (hasStamps(table)) {
        sets << quote(kModified) + QStringLiteral(" = ?");
        binds << toSql(datetime, QDateTime::currentDateTime().toString(kDateTimeFormat));
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QVariantMap field = fieldInfo(tableName, it.key());
        sets << quote(column(field)) + QStringLiteral(" = ?");
        binds << toSql(field, it.value());
    }
    QVariant newId = id;
    const QString keyFieldName = keyField(table).value(QStringLiteral("name")).toString();
    if (!keyFieldName.isEmpty() && values.contains(keyFieldName))
        newId = values.value(keyFieldName); // (a key that's typed in can change)
    if (sets.isEmpty()) {
        result.insert(kId, newId);
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3 = ?")
                      .arg(sqlTable(table), sets.join(QLatin1String(", ")), key == QLatin1String("rowid") ? key : quote(key)));
    for (const QVariant &bind : std::as_const(binds))
        query.addBindValue(bind);
    query.addBindValue(keyValue(table, id));
    if (!query.exec())
        return failed(query.lastError().text());
    result.insert(kId, newId);
    emit changed(tableName);
    return result;
}

QVariantMap AppDatabase::remove(const QString &tableName, const QVariant &id)
{
    if (!isOpen() && !isApi(tableInfo(tableName)))
        return failed(QStringLiteral("The database isn't open."));
    if (tableInfo(tableName).isEmpty())
        return failed(QStringLiteral("There is no table named %1.").arg(tableName));
    const QVariantMap table = tableInfo(tableName);
    const QString key = keyName(table);
    if (key.isEmpty())
        return failed(QStringLiteral("%1 has no key in the database, so its records can only be added and read here.").arg(tableName));
    // a Lookup somewhere still pointing at it?
    for (const QVariant &t : std::as_const(m_tables)) {
        const QVariantMap other = t.toMap();
        const QString otherName = other.value(QStringLiteral("name")).toString();
        const QVariantList fields = other.value(QStringLiteral("fields")).toList();
        for (const QVariant &f : fields) {
            const QVariantMap field = f.toMap();
            if (field.value(QStringLiteral("type")).toString() != QLatin1String("lookup")
                || field.value(QStringLiteral("lookupTable")).toString() != tableName)
                continue;
            const int uses = count(otherName, { { field.value(QStringLiteral("name")).toString(), id } });
            if (uses > 0)
                return failed(uses == 1
                    ? QStringLiteral("This can't be deleted: a record in %1 still uses it.").arg(otherName)
                    : QStringLiteral("This can't be deleted: %1 records in %2 still use it.").arg(uses).arg(otherName));
        }
    }
    if (isApi(table)) {
        if (!table.value(QStringLiteral("canDelete"), true).toBool())
            return failed(QStringLiteral("%1's records can't be deleted here.").arg(tableName));
        const QVariantMap r = callApi(table.value(QStringLiteral("api")).toString(), QStringLiteral("DELETE"), apiItemPath(table, id));
        m_apiLists.remove(tableName);
        if (!r.value(QStringLiteral("ok")).toBool())
            return failed(r.value(QStringLiteral("error")).toString());
        emit changed(tableName);
        return { { QStringLiteral("ok"), true }, { kId, id },
                 { QStringLiteral("errors"), QVariantMap() }, { QStringLiteral("messages"), QStringList() } };
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2 = ?").arg(sqlTable(table), key == QLatin1String("rowid") ? key : quote(key)));
    query.addBindValue(keyValue(table, id));
    if (!query.exec())
        return failed(query.lastError().text());
    emit changed(tableName);
    return { { QStringLiteral("ok"), true }, { kId, id },
             { QStringLiteral("errors"), QVariantMap() }, { QStringLiteral("messages"), QStringList() } };
}

QVariantMap AppDatabase::dropTable(const QString &tableName)
{
    if (!isOpen())
        return failed(QStringLiteral("The database isn't open."));
    // only the project's own: a name from anywhere else isn't SQL to run
    if (tableInfo(tableName).isEmpty())
        return failed(QStringLiteral("There is no table named %1.").arg(tableName));
    const QVariantMap table = tableInfo(tableName);
    const QString dbName = table.value(QStringLiteral("dbName")).toString().isEmpty() ? tableName : table.value(QStringLiteral("dbName")).toString();
    if (existingTables().contains(dbName)) {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("DROP TABLE ") + sqlTable(table)))
            return failed(query.lastError().text());
    }
    emit changed(tableName);
    return { { QStringLiteral("ok"), true }, { QStringLiteral("errors"), QVariantMap() },
             { QStringLiteral("messages"), QStringList() } };
}
