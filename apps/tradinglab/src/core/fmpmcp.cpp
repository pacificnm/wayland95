// Port of main/fmpMcp.ts.
#include "fmpmcp.h"

#include "http.h"

#include <QRegularExpression>
#include <QUrlQuery>

namespace tl {

namespace {
const QString ENDPOINT = QStringLiteral("https://financialmodelingprep.com/mcp");
const QString PREFIX = QStringLiteral("fmp_");
const int MAX_RESULT_CHARS = 12000;

// Anthropic tool names allow [a-zA-Z0-9_-] up to 64 chars.
QString safeName(const QString &n)
{
    QString s = n;
    s.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
    return (PREFIX + s).left(64);
}

// the JSON-RPC message with this id, from a JSON body or an SSE stream of them
QVariant pickResponse(const QByteArray &body, int id)
{
    const QString text = QString::fromUtf8(body).trimmed();
    QStringList candidates;
    if (text.startsWith(QLatin1Char('{')) || text.startsWith(QLatin1Char('[')))
        candidates << text;
    else {
        QString data;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString l = line.trimmed();
            if (l.startsWith(QLatin1String("data:")))
                data += l.mid(5).trimmed();
            else if (l.isEmpty() && !data.isEmpty()) { candidates << data; data.clear(); }
        }
        if (!data.isEmpty())
            candidates << data;
    }
    for (const QString &c : candidates) {
        const QVariant v = fromJson(c);
        const QVariantList msgs = v.metaType().id() == QMetaType::QVariantList ? v.toList() : QVariantList{v};
        for (const QVariant &m : msgs)
            if (m.toMap().value(QStringLiteral("id")).toInt() == id)
                return m;
    }
    return {};
}
}

FmpMcp::FmpMcp(std::function<QString()> key)
    : m_key(std::move(key))
{
}

QVariant FmpMcp::rpc(Conn &c, const QString &method, const Rec &params, bool notification)
{
    QUrl url(ENDPOINT);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("apikey"), c.key);
    url.setQuery(q);
    Rec msg{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}};
    if (!params.isEmpty())
        msg.insert(QStringLiteral("params"), params);
    const int id = c.nextId++;
    if (!notification)
        msg.insert(QStringLiteral("id"), id);
    Headers h{{"Content-Type", "application/json"}, {"Accept", "application/json, text/event-stream"}};
    if (!c.session.isEmpty())
        h.append({"Mcp-Session-Id", c.session});
    const HttpResponse r = httpPost(url, h, toJson(msg).toUtf8(), 60000);
    if (!r.networkError.isEmpty())
        throw Error(r.networkError);
    if (!r.header("Mcp-Session-Id").isEmpty())
        c.session = r.header("Mcp-Session-Id");
    if (!r.ok())
        throw Error(QStringLiteral("HTTP %1: %2").arg(r.status).arg(QString::fromUtf8(r.body.left(200))));
    if (notification)
        return {};
    const Rec m = pickResponse(r.body, id).toMap();
    if (m.isEmpty())
        throw Error(QStringLiteral("No answer from the FMP MCP server."));
    if (m.contains(QStringLiteral("error")))
        throw Error(m.value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString());
    return m.value(QStringLiteral("result"));
}

FmpMcp::Conn FmpMcp::connect(const QString &key)
{
    Conn c;
    c.key = key;
    rpc(c, QStringLiteral("initialize"), Rec{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
                                             {QStringLiteral("capabilities"), Rec{}},
                                             {QStringLiteral("clientInfo"), Rec{{QStringLiteral("name"), QStringLiteral("trading-lab")}, {QStringLiteral("version"), QStringLiteral("0.1.0")}}}});
    rpc(c, QStringLiteral("notifications/initialized"), {}, true);
    QString cursor;
    do {
        const Rec res = rpc(c, QStringLiteral("tools/list"), cursor.isEmpty() ? Rec{} : Rec{{QStringLiteral("cursor"), cursor}}).toMap();
        for (const QVariant &tv : res.value(QStringLiteral("tools")).toList()) {
            const Rec t = tv.toMap();
            const QString real = t.value(QStringLiteral("name")).toString();
            QString name = safeName(real);
            for (int i = 2; c.byName.contains(name); ++i)
                name = safeName(real).left(60) + QLatin1Char('_') + QString::number(i);
            c.byName.insert(name, real);
            Rec schema = t.value(QStringLiteral("inputSchema")).toMap();
            schema.insert(QStringLiteral("type"), QStringLiteral("object"));
            const QString desc = t.value(QStringLiteral("description")).toString();
            c.tools.append(Rec{{QStringLiteral("name"), name}, {QStringLiteral("description"), QStringLiteral("[FMP] ") + (desc.isEmpty() ? real : desc)},
                               {QStringLiteral("input_schema"), schema}});
        }
        cursor = res.value(QStringLiteral("nextCursor")).toString();
    } while (!cursor.isEmpty());
    return c;
}

FmpMcp::Conn &FmpMcp::ensure()
{
    const QString k = m_key();
    if (k.isEmpty())
        throw Error(QStringLiteral("No FMP API key."));
    if (m_conn && m_conn->key == k)
        return *m_conn;
    m_conn.reset();
    // after a failure, don't hammer the server on every chat message
    if (m_lastFailure && m_lastFailure->key == k && nowMs() - m_lastFailure->at < 60000)
        throw Error(m_lastFailure->error);
    try {
        m_conn = connect(k);
        m_lastFailure.reset();
        return *m_conn;
    } catch (const Error &e) {
        m_lastFailure = Failure{k, nowMs(), e.message()};
        throw;
    }
}

List FmpMcp::tools()
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    return ensure().tools;
}

bool FmpMcp::owns(const QString &name)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    return m_conn && m_conn->byName.contains(name);
}

Rec FmpMcp::call(const QString &name, const QVariant &args)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    try {
        Conn &c = ensure();
        const QString real = c.byName.value(name);
        if (real.isEmpty())
            return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("text"), QStringLiteral("Unknown FMP tool %1.").arg(name)}};
        const Rec res = rpc(c, QStringLiteral("tools/call"), Rec{{QStringLiteral("name"), real},
                                                                 {QStringLiteral("arguments"), args.metaType().id() == QMetaType::QVariantMap ? args : QVariant(Rec{})}}).toMap();
        QStringList parts;
        for (const QVariant &p : res.value(QStringLiteral("content")).toList())
            if (p.toMap().value(QStringLiteral("type")).toString() == QLatin1String("text"))
                parts << p.toMap().value(QStringLiteral("text")).toString();
        QString text = parts.join(QLatin1Char('\n'));
        if (text.isEmpty())
            text = res.contains(QStringLiteral("structuredContent")) ? toJson(res.value(QStringLiteral("structuredContent"))) : QStringLiteral("(no content)");
        if (text.size() > MAX_RESULT_CHARS)
            text = text.left(MAX_RESULT_CHARS) + QStringLiteral("\n…[truncated: %1 more characters. Ask for a narrower query (a specific symbol, period or limit).]").arg(text.size() - MAX_RESULT_CHARS);
        return Rec{{QStringLiteral("ok"), !res.value(QStringLiteral("isError")).toBool()}, {QStringLiteral("text"), text}};
    } catch (const Error &e) {
        m_conn.reset(); // force a reconnect next time
        return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("text"), QStringLiteral("FMP data request failed: %1").arg(e.message())}};
    }
}

Rec FmpMcp::status()
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    if (m_key().isEmpty())
        return failError(QStringLiteral("No FMP API key."));
    try {
        m_lastFailure.reset();
        Conn &c = ensure();
        QStringList sample;
        for (int i = 0; i < std::min<int>(12, c.tools.size()); ++i) {
            const QString n = c.tools[i].toMap().value(QStringLiteral("name")).toString();
            sample << c.byName.value(n, n);
        }
        return ok(Rec{{QStringLiteral("count"), c.tools.size()}, {QStringLiteral("transport"), QStringLiteral("streamable-http")}, {QStringLiteral("sample"), sample}});
    } catch (const Error &e) {
        return failError(e.message());
    }
}

} // namespace tl
