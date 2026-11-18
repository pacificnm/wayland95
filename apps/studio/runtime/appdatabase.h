#pragma once

#include <QHash>
#include <QJSEngine>
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Wayland Studio's ORM: the tables an app's project describes (Data
// Designer), kept in SQLite, MySQL/MariaDB or PostgreSQL. Every write goes
// through validate(): each field's rules (required, type, length, range,
// pattern, choices, lookup, unique, its Validation Rule) are checked here, and
// what fails comes back by field name for the form to show next to its
// controls:
//
//   var r = Db.insert("Customers", { Name: "", Email: "nope" });
//   // r = { ok: false,
//   //       errors: { Name: "Name is required.", Email: "Email must be an e-mail address." },
//   //       messages: ["Name is required.", "Email must be an e-mail address."] }
//
// Every table has `id` (its key, numbered by the database), `created_on`
// and `modified_on`. A table read from a database that was there already
// (readSchema(), Project > Import Tables) can be otherwise; its project
// table says how:
//
//   { name, dbName?: "Order Lines" (its name in the database, when it
//     isn't one code can use), key: "customer_id" (its key column: what
//     `id` is in records; "rowid" in SQLite; "" for none: read-only),
//     keyAuto: true (the database numbers it; false: the key is one of
//     the fields, typed in), stamps: false (no created_on/modified_on),
//     fields: [{ name, column?: "Order Date", ... }] } In an app's code this is `Db`; the Studio makes its
// own (AppDatabase {}) for the Data view.
class ApiClient;

class AppDatabase : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString driver READ driver NOTIFY openChanged)
    Q_PROPERTY(QVariantList tables READ tables NOTIFY schemaChanged)

public:
    explicit AppDatabase(QObject *parent = nullptr);
    ~AppDatabase() override;

    bool isOpen() const;
    QString lastError() const { return m_error; }
    QString driver() const { return m_driver; }
    QVariantList tables() const { return m_tables; }

    // the field types the Data Designer offers: [{ type, label, hint }]
    Q_INVOKABLE static QVariantList fieldTypes();
    // which of sqlite / mysql / postgresql this machine's Qt can talk to
    Q_INVOKABLE static QVariantMap driversAvailable();
    // the Validation Rules a field of this type can have:
    // [{ id, label, arg, argLabel, argHint, argTypes?, message }]
    // arg: "" | number | count | text | list | field | expression
    Q_INVOKABLE static QVariantList validators(const QString &type);
    // what's said when the field's rule (or its Pattern) isn't met and it
    // has no Validation Text of its own; the rule in a few words
    Q_INVOKABLE static QString ruleMessage(const QVariantMap &table, const QVariantMap &field);
    Q_INVOKABLE static QString ruleSummary(const QVariantMap &table, const QVariantMap &field);
    // "" if `name` can be a table or field name, else what's wrong with it
    Q_INVOKABLE static QString nameProblem(const QString &name);

    // config: { driver: "sqlite"|"mysql"|"postgresql", file, host, port,
    // name, user, password }; a relative SQLite `file` is under `baseDir`
    Q_INVOKABLE bool open(const QVariantMap &config, const QString &baseDir = QString());
    Q_INVOKABLE void close();
    // the project's tables: [{ name, label, fields: [{ name, type, ... }] }]
    Q_INVOKABLE void setSchema(const QVariantList &tables);
    // the project's web APIs (see apiclient.h). A table can be one of their
    // endpoints: { name, source: "api", api: "shop", endpoint: "/products",
    // recordsPath: "data" (where the list is in the answer; "" the answer),
    // itemPath: "data" (where a record is in a one-record answer), key: "id"
    // (its id in a record), keyAuto: true (the API gives it), stamps: false,
    // updateMethod: "PATCH" | "PUT", canCreate, canUpdate, canDelete,
    // query: "per_page=100" (on every list), fields: [{ name, path?: "owner.login" }] }.
    // It's read and written as any table: select() gets the list (kept for
    // a few seconds) and filters it here; insert() POSTs, update() PATCHes or
    // PUTs, remove() DELETEs `endpoint/id`.
    Q_INVOKABLE void setApis(const QVariantList &apis);
    // a request of an API, for code: Db.callApi("shop", "GET", "/me") ->
    // { ok, status, data, error }
    Q_INVOKABLE QVariantMap callApi(const QString &api, const QString &method, const QString &path,
                                    const QVariant &body = QVariant(), const QVariantMap &query = QVariantMap());
    // sign in now (OAuth), and forget the tokens: { ok, error }
    Q_INVOKABLE QVariantMap signIn(const QString &api);
    Q_INVOKABLE void signOut(const QString &api);
    // an endpoint's answer, as a table (Add API Table): { ok, recordsPath,
    // key, fields, sample: [first rows], count, error }
    Q_INVOKABLE QVariantMap readEndpoint(const QString &api, const QString &path, const QString &recordsPath = QString(),
                                         const QString &query = QString());

    // SQL as it is ("" or what went wrong): not for apps (not Q_INVOKABLE);
    // the runner's --eval tests make databases with it
    QString execForTest(const QString &statement);

    // the tables in the open database, as project tables (what
    // Project > Import Tables adds): { ok, tables: [{ name, dbName?, key,
    // keyAuto, stamps, imported: true, fields, records }], notes: [...] }
    Q_INVOKABLE QVariantMap readSchema();

    // what the database lacks next to the schema: { ok, actions: ["Create
    // table Customers", ...], notes: [...] }; syncSchema() then does it
    // (tables and fields are added, never dropped or retyped)
    Q_INVOKABLE QVariantMap schemaPlan();
    Q_INVOKABLE QVariantMap syncSchema();

    // rows as { id, created_on, modified_on, Field: value... }. options:
    // { where: { Field: value }, search: "text", orderBy: "Field",
    //   descending: bool, limit: n, offset: n }
    Q_INVOKABLE QVariantList select(const QString &table, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap get(const QString &table, const QVariant &id);
    Q_INVOKABLE int count(const QString &table, const QVariantMap &where = QVariantMap());
    // [{ id, label }] for a Lookup's list: the table's first text field
    Q_INVOKABLE QVariantList lookupChoices(const QString &table);
    Q_INVOKABLE QString lookupLabel(const QString &table, const QVariant &id);

    // { ok, errors: { Field: message }, messages: [...], values: {...} }
    // `id` is the row being changed (so its own value isn't a duplicate);
    // with `partial` only the fields given are checked (an update)
    Q_INVOKABLE QVariantMap validate(const QString &table, const QVariantMap &record,
                                     const QVariant &id = QVariant(), bool partial = false);
    // validate, then write: { ok, id, errors, messages, error }
    Q_INVOKABLE QVariantMap insert(const QString &table, const QVariantMap &record);
    Q_INVOKABLE QVariantMap update(const QString &table, const QVariant &id, const QVariantMap &record);
    // refuses while another table's Lookup still points at the row
    Q_INVOKABLE QVariantMap remove(const QString &table, const QVariant &id);

    // the table and everything in it, out of the database (the tests start
    // from nothing with this; syncSchema() makes it again): { ok, error }
    Q_INVOKABLE QVariantMap dropTable(const QString &table);

    // a table's or field's description from the schema ({} if unknown)
    Q_INVOKABLE QVariantMap tableInfo(const QString &table) const;
    Q_INVOKABLE QVariantMap fieldInfo(const QString &table, const QString &field) const;
    // a record of a table's defaults, for a new row
    Q_INVOKABLE QVariantMap defaults(const QString &table) const;

signals:
    void openChanged();
    void errorChanged();
    void schemaChanged();
    // rows were added, changed or removed
    void changed(const QString &table);

private:
    enum Dialect { Sqlite, Mysql, Postgres };

    QString quote(const QString &identifier) const;
    QString columnType(const QVariantMap &field) const;
    QString keyColumn(const QString &name = QStringLiteral("id")) const;
    // a table's key column ("" if it has none), whether the database
    // numbers it, whether it has created_on/modified_on, its name in SQL
    QString keyName(const QVariantMap &table) const;
    bool keyAuto(const QVariantMap &table) const;
    bool hasStamps(const QVariantMap &table) const;
    QString sqlTable(const QVariantMap &table) const;
    // a field's column in the database
    QString column(const QVariantMap &field) const;
    // the field that's the table's key ({} for a key the database numbers)
    QVariantMap keyField(const QVariantMap &table) const;
    // an id as the key column holds it
    QVariant keyValue(const QVariantMap &table, const QVariant &id) const;
    // a name code uses (a field, id, created_on...) -> its column ("" if none)
    QString columnOf(const QVariantMap &table, const QString &name) const;
    QVariantMap readTable(const QString &name, QStringList *notes);
    QVariant toSql(const QVariantMap &field, const QVariant &value) const;
    QVariant fromSql(const QString &type, const QVariant &value) const;
    QVariantMap rowToMap(const QVariantMap &table, const class QSqlRecord &row) const;
    QString label(const QVariantMap &field) const;
    QString checkField(const QVariantMap &table, const QVariantMap &field, QVariant &value,
                       const QVariantMap &record, const QVariant &id);
    QString checkRule(const QVariantMap &table, const QVariantMap &field, const QVariant &value,
                      const QVariantMap &record, bool blank);
    QString labelField(const QVariantMap &table) const;
    QString selectList(const QVariantMap &table) const;
    bool fail(const QString &error);
    QVariantMap failed(const QString &error);
    QStringList existingTables() const;

    QSqlDatabase m_db;
    QString m_connection;
    QString m_driver;
    Dialect m_dialect = Sqlite;
    QVariantList m_tables;
    QString m_error;
    QJSEngine *m_rules = nullptr; // fields' own rules (expressions)
    // API tables: theirs, and their lists for a few seconds
    bool isApi(const QVariantMap &table) const;
    QVariantList apiRows(const QVariantMap &table, QString *error);
    QVariantMap apiRow(const QVariantMap &table, const QVariant &record) const;
    QVariantMap apiBody(const QVariantMap &table, const QVariantMap &values) const;
    QString apiItemPath(const QVariantMap &table, const QVariant &id) const;
    QVariantList filtered(const QVariantMap &table, QVariantList rows, const QVariantMap &options);
    ApiClient *m_api = nullptr;
    struct ApiList { QVariantList rows; qint64 at = 0; };
    QHash<QString, ApiList> m_apiLists;
};
