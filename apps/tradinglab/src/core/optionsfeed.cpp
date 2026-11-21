// Cboe delayed option chains. Port of main/options.ts.

#include "optionsfeed.h"

#include "nytime.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>
#include <set>

namespace tl {

namespace {
const QString SOURCE = QStringLiteral("Cboe delayed quotes");
const QString URL_BASE = QStringLiteral("https://cdn.cboe.com/api/global/delayed_quotes/options");
constexpr int MAX_DTE = 400;

// `typeof v === 'number' && Number.isFinite(v) ? v : null`
std::optional<double> jnum(const QJsonValue &v)
{
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        return std::nullopt;
    return v.toDouble();
}
}

QString cboeSymbol(const QString &s)
{
    static const QMap<QString, QString> INDEX = {
        {QStringLiteral("^GSPC"), QStringLiteral("_SPX")}, {QStringLiteral("^SPX"), QStringLiteral("_SPX")},
        {QStringLiteral("^NDX"), QStringLiteral("_NDX")}, {QStringLiteral("^RUT"), QStringLiteral("_RUT")},
        {QStringLiteral("^VIX"), QStringLiteral("_VIX")}, {QStringLiteral("^DJI"), QStringLiteral("_DJX")},
    };
    const QString u = s.toUpper();
    return INDEX.value(u, u);
}

std::optional<ContractName> parseContractName(const QString &name)
{
    if (name.size() < 16)
        return std::nullopt;
    static const QRegularExpression re(QStringLiteral("^(\\d{2})(\\d{2})(\\d{2})([CP])(\\d{8})$"));
    const auto m = re.match(name.right(15));
    if (!m.hasMatch())
        return std::nullopt;
    return ContractName{QStringLiteral("20%1-%2-%3").arg(m.captured(1), m.captured(2), m.captured(3)), m.captured(4),
                        m.captured(5).toDouble() / 1000};
}

Rec parseCboe(const QJsonValue &raw, const QString &symbol, qint64 nowSec)
{
    const QJsonValue dv = raw.toObject().value(QStringLiteral("data"));
    const QJsonObject d = dv.toObject();
    const QJsonValue list = d.value(QStringLiteral("options"));
    if (!dv.isObject() || !list.isArray())
        throw Error(QStringLiteral("Unexpected response from the options feed."));
    const auto price = jnum(d.value(QStringLiteral("current_price")));
    if (!price)
        throw Error(QStringLiteral("The options feed had no underlying price."));
    // calendar days from today (New York date) to the expiration date, so Sep 30 -> Oct 16 is 16
    const QDate today = QDate::fromString(nyDate(nowSec), Qt::ISODate);
    const auto dteOf = [&](const QString &exp) { return std::max<qint64>(0, today.daysTo(QDate::fromString(exp, Qt::ISODate))); };
    List contracts;
    std::set<QString> expSet;
    const auto orNull = [](const std::optional<double> &v) { return v ? QVariant(*v) : null(); };
    for (const QJsonValue &ov : list.toArray()) {
        const QJsonObject o = ov.toObject();
        const QJsonValue name = o.value(QStringLiteral("option"));
        const auto p = parseContractName(name.isString() ? name.toString() : name.isUndefined() || name.isNull() ? QString() : name.toVariant().toString());
        if (!p)
            continue;
        if (dteOf(p->exp) > MAX_DTE || nyToEpoch(p->exp + QStringLiteral(" 16:00:00")) < nowSec - 3600)
            continue; // far-dated, or already expired
        const double bid = jnum(o.value(QStringLiteral("bid"))).value_or(0), ask = jnum(o.value(QStringLiteral("ask"))).value_or(0);
        const double oi = jnum(o.value(QStringLiteral("open_interest"))).value_or(0), volume = jnum(o.value(QStringLiteral("volume"))).value_or(0);
        if (bid == 0 && ask == 0 && oi == 0 && volume == 0)
            continue;
        const auto ivRaw = jnum(o.value(QStringLiteral("iv")));
        // deep ITM / expiring quotes report nonsense
        const bool ivOk = ivRaw && *ivRaw > 0 && *ivRaw < 5 && (bid > 0 || ask > 0);
        const auto last = jnum(o.value(QStringLiteral("last_trade_price")));
        contracts << Rec{
            {QStringLiteral("exp"), p->exp}, {QStringLiteral("type"), p->type}, {QStringLiteral("strike"), p->strike},
            {QStringLiteral("bid"), bid}, {QStringLiteral("ask"), ask},
            {QStringLiteral("last"), last && *last > 0 ? QVariant(*last) : null()},
            {QStringLiteral("volume"), volume}, {QStringLiteral("oi"), oi},
            {QStringLiteral("iv"), ivOk ? QVariant(*ivRaw) : null()},
            {QStringLiteral("delta"), orNull(jnum(o.value(QStringLiteral("delta"))))},
            {QStringLiteral("gamma"), orNull(jnum(o.value(QStringLiteral("gamma"))))},
            {QStringLiteral("theta"), orNull(jnum(o.value(QStringLiteral("theta"))))},
            {QStringLiteral("vega"), orNull(jnum(o.value(QStringLiteral("vega"))))},
            {QStringLiteral("theo"), orNull(jnum(o.value(QStringLiteral("theo"))))},
            {QStringLiteral("change"), orNull(jnum(o.value(QStringLiteral("change"))))},
        };
        expSet.insert(p->exp);
    }
    if (contracts.isEmpty())
        throw Error(QStringLiteral("No listed options found for %1.").arg(symbol));
    const auto iv30 = jnum(d.value(QStringLiteral("iv30")));
    List expirations;
    for (const QString &date : expSet) // std::set iterates sorted
        expirations << Rec{{QStringLiteral("date"), date}, {QStringLiteral("dte"), dteOf(date)}};
    return Rec{
        {QStringLiteral("symbol"), symbol}, {QStringLiteral("price"), *price},
        {QStringLiteral("change"), orNull(jnum(d.value(QStringLiteral("price_change"))))},
        {QStringLiteral("changePct"), orNull(jnum(d.value(QStringLiteral("price_change_percent"))))},
        {QStringLiteral("iv30"), iv30 ? QVariant(*iv30 / 100) : null()},
        {QStringLiteral("fetchedAt"), nowSec}, {QStringLiteral("expirations"), expirations},
        {QStringLiteral("contracts"), contracts}, {QStringLiteral("source"), SOURCE},
    };
}

OptionsFeed::OptionsFeed(Fetch fetch, std::function<qint64()> clock)
    : m_fetch(fetch ? std::move(fetch) : Fetch([](const QUrl &url, const Headers &h) { return httpGet(url, h, 60000); })),
      m_clock(std::move(clock))
{
}

Rec OptionsFeed::chain(const QString &symbolIn, bool force, qint64 ttlMs)
{
    const QString symbol = cboeSymbol(symbolIn.trimmed());
    static const QRegularExpression valid(QStringLiteral("^[A-Z0-9_.-]{1,12}$"));
    if (!valid.match(symbol).hasMatch())
        throw Error(QStringLiteral("“%1” is not a valid symbol.").arg(symbolIn));

    std::optional<Hit> hit;
    std::shared_ptr<Flight> flight;
    bool owner = false;
    {
        std::lock_guard<std::mutex> g(m_mx);
        const auto it = m_cache.find(symbol);
        if (it != m_cache.end())
            hit = it->second;
        if (hit && !force && m_clock() - hit->at < ttlMs)
            return hit->chain;
        const auto f = m_inflight.find(symbol);
        if (f == m_inflight.end()) {
            flight = std::make_shared<Flight>();
            m_inflight.emplace(symbol, flight);
            owner = true;
        } else {
            flight = f->second;
        }
    }

    if (owner) {
        Rec parsed;
        std::exception_ptr err;
        try {
            const HttpResponse res = m_fetch(QUrl(URL_BASE + QLatin1Char('/') + symbol + QStringLiteral(".json")),
                                             Headers{{QByteArrayLiteral("User-Agent"), QByteArrayLiteral("Mozilla/5.0 TradingLab")},
                                                     {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")}});
            if (res.status == 0)
                throw Error(QStringLiteral("Could not reach the options feed: %1").arg(res.networkError));
            // a bot check (Cloudflare) refusing this network is not "no options": say which it is
            if (res.status == 403 && (!res.header("cf-mitigated").isEmpty() || res.body.trimmed().startsWith("<")))
                throw Error(QStringLiteral("Cboe's delayed-quotes service refused this network (a bot check), so option chains cannot be loaded from here right now."));
            if (res.status == 403 || res.status == 404)
                throw Error(QStringLiteral("No listed options found for %1. It may not be optionable, or is not a US symbol.").arg(symbolIn.toUpper()));
            if (!res.ok())
                throw Error(QStringLiteral("The options feed returned HTTP %1.").arg(res.status));
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(res.body, &pe);
            if (pe.error != QJsonParseError::NoError)
                throw Error(pe.errorString()); // upstream: res.json() rejected with the parser's message
            parsed = parseCboe(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), symbolIn.toUpper(), m_clock() / 1000);
            std::lock_guard<std::mutex> g(m_mx);
            m_cache[symbol] = Hit{m_clock(), parsed};
        } catch (...) {
            err = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> g(m_mx);
            m_inflight.erase(symbol);
        }
        {
            std::lock_guard<std::mutex> g(flight->mx);
            flight->chain = parsed;
            flight->error = err;
            flight->done = true;
        }
        flight->cv.notify_all();
    } else {
        std::unique_lock<std::mutex> lk(flight->mx);
        flight->cv.wait(lk, [&] { return flight->done; });
    }
    if (!flight->error)
        return flight->chain;
    if (hit)
        return hit->chain; // feed hiccup: keep showing the last good chain
    std::rethrow_exception(flight->error);
}

} // namespace tl
