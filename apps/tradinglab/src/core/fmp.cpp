// The FMP market-data client. Port of main/fmp.ts.

#include "fmp.h"

#include "db.h"
#include "normalize.h"
#include "http.h"
#include "jsnum.h"
#include "nytime.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace tl {

namespace {

const QString NO_KEY = QStringLiteral("No FMP API key. Add one in File → Settings.");

// `String(v)` for a value read from a JSON response
QString jsStr(const QVariant &v)
{
    if (isNull(v))
        return v.isValid() ? QStringLiteral("null") : QStringLiteral("undefined");
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::QString:
        return v.toString();
    case QMetaType::QVariantMap:
        return QStringLiteral("[object Object]");
    case QMetaType::QVariantList: case QMetaType::QStringList: {
        QStringList parts;
        for (const QVariant &x : v.toList())
            parts << (isNull(x) ? QString() : jsStr(x));
        return parts.join(QLatin1Char(','));
    }
    default:
        if (isNumber(v) || v.canConvert<double>())
            return jsString(v.toDouble());
        return v.toString();
    }
}

// `r[k] == null ? '' : String(r[k])`
QString str(const Rec &r, const QString &k)
{
    const QVariant v = r.value(k);
    return isNull(v) ? QString() : jsStr(v);
}

// `v ?? def` read as a number
double numOr(const QVariant &v, double def)
{
    return isNull(v) ? def : toNumber(v);
}

// `Number(v)` (null is 0, undefined NaN)
double jsNumber(const QVariant &v)
{
    if (!v.isValid())
        return NAN;
    if (isNull(v))
        return 0;
    return toNumber(v);
}

// nyToEpoch, NaN when the date part is not a date (upstream got NaN from Date.UTC)
double nyEpoch(const QString &s)
{
    const QString d = s.trimmed().split(QRegularExpression(QStringLiteral("[ T]"))).value(0);
    if (!QDate::fromString(d, Qt::ISODate).isValid())
        return NAN;
    return double(nyToEpoch(s));
}

// `Date.parse(d.replace(' ', 'T') + 'Z') / 1000`
double utcEpoch(const QString &s)
{
    QString t = s;
    const int sp = t.indexOf(QLatin1Char(' '));
    if (sp >= 0)
        t[sp] = QLatin1Char('T');
    const QDateTime dt = QDateTime::fromString(t + QLatin1Char('Z'), Qt::ISODateWithMs);
    return dt.isValid() ? double(dt.toMSecsSinceEpoch()) / 1000 : NAN;
}

// `new Date(s).getTime()` for YYYY-MM-DD (UTC midnight), NaN when invalid
double dayMs(const QString &s)
{
    const QDate d = QDate::fromString(s, Qt::ISODate);
    return d.isValid() ? double(QDateTime(d, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch()) : NAN;
}

// `day(new Date(ms))`: the UTC date of a time in ms
QString dayOf(double ms)
{
    if (!std::isfinite(ms))
        throw Error(QStringLiteral("Invalid time value"));
    return QDateTime::fromMSecsSinceEpoch(qint64(ms), QTimeZone::UTC).date().toString(Qt::ISODate);
}

QString today() { return utcDay(nowSec()); }

const QMap<QString, int> CHUNK_DAYS = {
    {QStringLiteral("1min"), 5}, {QStringLiteral("5min"), 30}, {QStringLiteral("15min"), 90},
    {QStringLiteral("30min"), 180}, {QStringLiteral("1hour"), 365}, {QStringLiteral("4hour"), 730},
};

const QRegularExpression &symbolRe()
{
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9.^=-]{1,15}$"));
    return re;
}

Rec barRec(const Bar &b)
{
    return Rec{{QStringLiteral("time"), b.time}, {QStringLiteral("open"), b.open}, {QStringLiteral("high"), b.high},
               {QStringLiteral("low"), b.low}, {QStringLiteral("close"), b.close}, {QStringLiteral("volume"), b.volume}};
}

// Handler wrapper: {ok: true, data} or {ok: false, error, kind}
template<typename F>
Rec wrap(F &&fn)
{
    try {
        return Rec{{QStringLiteral("ok"), true}, {QStringLiteral("data"), QVariant(fn())}};
    } catch (const FmpError &e) {
        return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("error"), e.message()}, {QStringLiteral("kind"), e.kind()}};
    } catch (const std::exception &e) {
        return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString::fromStdString(e.what())}, {QStringLiteral("kind"), QStringLiteral("other")}};
    }
}

// a section failing (e.g. not in plan) shouldn't blank the whole screen
template<typename T, typename F>
T soft(F &&fn, T fallback)
{
    try {
        return fn();
    } catch (const FmpError &e) {
        if (e.kind() == QLatin1String("auth") || e.kind() == QLatin1String("nokey") || e.kind() == QLatin1String("network"))
            throw;
        return fallback;
    } catch (const std::exception &) {
        return fallback;
    }
}

qint64 asOfSec(const Ctx &ctx)
{
    return ctx.hasAsOf() ? ctx.asOf.load() / 1000 : nowSec();
}

} // namespace

// ---- setup ----

Fmp::Fmp(KeyGetter getKey, KeySetter storeKey, KeyClearer forgetKey, Broadcast broadcast)
    : m_key(std::move(getKey)), m_setKey(std::move(storeKey)), m_clearKey(std::move(forgetKey)), m_broadcast(std::move(broadcast))
{
    // FMP_BASE_URL points the app at a stand-in server for screenshots and tests
    m_base = qEnvironmentVariableIsSet("FMP_BASE_URL") ? qEnvironmentVariable("FMP_BASE_URL")
                                                       : QStringLiteral("https://financialmodelingprep.com/stable");
    m_request = std::make_unique<Requester>(
        m_store, [this](const QString &path, const Rec &params) { return fetchJson(path, params); },
        [](const std::exception_ptr &err) {
            try {
                std::rethrow_exception(err);
            } catch (const FmpError &e) {
                return e.kind() == QLatin1String("network") || e.kind() == QLatin1String("rate") || e.kind() == QLatin1String("other");
            } catch (...) {
                return false;
            }
        });
    m_timers.setMaxThreadCount(2);
    m_timers.setExpiryTimeout(-1); // keep the thread (and its database connection) instead of opening new ones
    if (!key().isEmpty() && !readCaps())
        checkSoon(6000);
}

Fmp::~Fmp()
{
    {
        std::lock_guard<std::mutex> g(m_stopMx);
        m_stopping = true;
    }
    m_stopCv.notify_all();
    m_timers.waitForDone();
}

QString Fmp::key() const
{
    const QString stored = m_key ? m_key() : QString();
    if (!stored.isEmpty())
        return stored;
    return qEnvironmentVariable("FMP_API_KEY");
}

// One network call. Caching, merging of identical requests and stale-on-error live in the Requester.
QVariant Fmp::fetchJson(const QString &path, const Rec &params)
{
    const QString k = key();
    if (k.isEmpty())
        throw FmpError(NO_KEY, QStringLiteral("nokey"));
    // URLSearchParams form encoding
    QStringList qs;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        const auto enc = [](const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s, QByteArrayLiteral("*"))).replace(QStringLiteral("%20"), QStringLiteral("+")); };
        qs << enc(it.key()) + QLatin1Char('=') + enc(paramString(it.value()));
    }
    const QUrl url(m_base + QLatin1Char('/') + path + QLatin1Char('?') + qs.join(QLatin1Char('&')) + QStringLiteral("&apikey=") + encodeUriComponent(k));
    const HttpResponse res = httpGet(url);
    if (res.status == 0)
        throw FmpError(QStringLiteral("Network error: %1").arg(res.networkError), QStringLiteral("network"));
    const QString text = QString::fromUtf8(res.body);
    const QVariant body = fromJson(text, text);
    QString apiMsg;
    if (body.metaType().id() == QMetaType::QVariantMap) {
        const Rec m = body.toMap();
        QVariant v = m.value(QStringLiteral("Error Message"));
        if (isNull(v))
            v = m.value(QStringLiteral("message"));
        apiMsg = isNull(v) ? QString() : jsStr(v);
    } else if (body.metaType().id() == QMetaType::QString) {
        apiMsg = body.toString().left(200);
    }
    static const QRegularExpression invalidKey(QStringLiteral("invalid api key"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression notInPlan(QStringLiteral("not available under your current subscription|upgrade"), QRegularExpression::CaseInsensitiveOption);
    const bool isList = body.metaType().id() == QMetaType::QVariantList;
    if (res.status == 401 || invalidKey.match(apiMsg).hasMatch())
        throw FmpError(QStringLiteral("FMP rejected the API key."), QStringLiteral("auth"));
    if (res.status == 402 || res.status == 403 || notInPlan.match(apiMsg).hasMatch()) {
        noteRefusal();
        throw FmpError(apiMsg.isEmpty() ? QStringLiteral("Not included in your FMP plan.") : apiMsg, QStringLiteral("plan"));
    }
    if (res.status == 429)
        throw FmpError(QStringLiteral("FMP rate limit reached. Try again shortly."), QStringLiteral("rate"));
    if (!res.ok())
        throw FmpError(apiMsg.isEmpty() ? QStringLiteral("FMP returned HTTP %1").arg(res.status) : apiMsg, QStringLiteral("other"));
    if (!apiMsg.isEmpty() && !isList)
        throw FmpError(apiMsg, QStringLiteral("other"));
    return body;
}

// `ttl` overrides the per-endpoint lifetime (see ttlFor); `ctx` collects the data's age and can force a refresh.
List Fmp::rows(const QString &path, const Rec &params, std::optional<qint64> ttl, Ctx *ctx)
{
    const QVariant v = m_request->get(path, params, ttl, ctx);
    return v.metaType().id() == QMetaType::QVariantList ? v.toList() : List();
}

QVariant Fmp::first(const QString &path, const Rec &params, std::optional<qint64> ttl, Ctx *ctx)
{
    const List l = rows(path, params, ttl, ctx);
    return l.isEmpty() || !l.first().isValid() ? null() : l.first();
}

Bars Fmp::fetchBars(const QString &symbol, const QString &interval, const QString &from, const QString &to, std::optional<qint64> ttl)
{
    const bool intraday = interval != QLatin1String("1day");
    QList<QPair<QString, QString>> windows;
    if (!intraday) {
        windows.append({from, to});
    } else {
        const double step = CHUNK_DAYS.contains(interval) ? CHUNK_DAYS.value(interval) * 86400000.0 : NAN;
        const double end = dayMs(to);
        for (double s = dayMs(from); s <= end; s += step + 86400000)
            windows.append({dayOf(s), dayOf(std::isnan(step) ? NAN : std::min(s + step, end))});
    }
    const QString path = intraday ? QStringLiteral("historical-chart/") + interval : QStringLiteral("historical-price-eod/full");
    Bars all;
    for (qsizetype i = 0; i < windows.size(); i += 4) {
        const qsizetype n = std::min<qsizetype>(4, windows.size() - i);
        std::vector<List> part(n);
        std::vector<std::function<void()>> tasks;
        for (qsizetype j = 0; j < n; ++j) {
            const auto w = windows[i + j];
            tasks.push_back([&, j, w] {
                part[j] = rows(path, Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("from"), w.first}, {QStringLiteral("to"), w.second}}, ttl);
            });
        }
        parallel(tasks);
        for (const List &list : part) {
            for (const QVariant &rv : list) {
                const Rec r = rv.toMap();
                const QString d = str(r, QStringLiteral("date"));
                double time;
                if (intraday) {
                    time = nyEpoch(d);
                } else {
                    const QDate dd(d.mid(0, 4).toInt(), d.mid(5, 2).toInt(), d.mid(8, 2).toInt());
                    time = dd.isValid() ? double(QDateTime(dd, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch()) : NAN;
                }
                const QVariant vol = r.value(QStringLiteral("volume"));
                const Bar bar{qint64(std::isfinite(time) ? time : 0), jsNumber(r.value(QStringLiteral("open"))), jsNumber(r.value(QStringLiteral("high"))),
                              jsNumber(r.value(QStringLiteral("low"))), jsNumber(r.value(QStringLiteral("close"))), isNull(vol) ? 0.0 : jsNumber(vol)};
                if (std::isfinite(time) && std::isfinite(bar.close))
                    all.push_back(bar);
            }
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const Bar &a, const Bar &b) { return a.time < b.time; });
    Bars out;
    out.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i)
        if (i == 0 || all[i].time != all[i - 1].time)
            out.push_back(all[i]);
    return out;
}

// FMP doesn't say which time zone news timestamps use. Read them as New York time first; if that puts a
// brand-new article in the future they must be UTC, and the reading sticks for the rest of the session.
QList<double> Fmp::parseNewsTimes(const QStringList &dates)
{
    const double now = double(nowMs()) / 1000;
    if (!m_newsIsUtc.load()) {
        for (const QString &d : dates)
            if (nyEpoch(d) > now + 300) {
                m_newsIsUtc = true;
                break;
            }
    }
    const bool utc = m_newsIsUtc.load();
    QList<double> out;
    out.reserve(dates.size());
    for (const QString &d : dates)
        out << (utc ? utcEpoch(d) : nyEpoch(d));
    return out;
}

// ---- handlers ----

Rec Fmp::bars(const Rec &p)
{
    return wrap([&] {
        const QVariant to = p.value(QStringLiteral("to"));
        const Bars b = fetchBars(str(p, QStringLiteral("symbol")), str(p, QStringLiteral("interval")), str(p, QStringLiteral("from")),
                                 isNull(to) ? today() : jsStr(to));
        List out;
        out.reserve(qsizetype(b.size()));
        for (const Bar &x : b)
            out << barRec(x);
        return out;
    });
}

Rec Fmp::quotes(const QStringList &symbols)
{
    return wrap([&] {
        std::vector<QVariant> got(size_t(symbols.size()));
        std::vector<std::function<void()>> tasks;
        for (qsizetype i = 0; i < symbols.size(); ++i)
            tasks.push_back([&, i] { got[size_t(i)] = soft([&] { return first(QStringLiteral("quote"), Rec{{QStringLiteral("symbol"), symbols[i]}}); }, null()); });
        parallel(tasks);
        List out;
        for (const QVariant &q : got)
            if (!isNull(q))
                out << q;
        return out;
    });
}

Rec Fmp::search(const QString &q)
{
    return wrap([&] {
        List a, b;
        parallel({
            [&] { a = soft([&] { return rows(QStringLiteral("search-symbol"), Rec{{QStringLiteral("query"), q}, {QStringLiteral("limit"), 8}}); }, List()); },
            [&] { b = soft([&] { return rows(QStringLiteral("search-name"), Rec{{QStringLiteral("query"), q}, {QStringLiteral("limit"), 8}}); }, List()); },
        });
        QSet<QString> seen;
        List out;
        for (const QVariant &rv : a + b) {
            const Rec r = rv.toMap();
            const QString symbol = str(r, QStringLiteral("symbol"));
            if (symbol.isEmpty() || seen.contains(symbol))
                continue;
            seen.insert(symbol);
            QVariant ex = r.value(QStringLiteral("exchange"));
            if (isNull(ex))
                ex = r.value(QStringLiteral("exchangeFullName"));
            out << Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("name"), str(r, QStringLiteral("name"))},
                       {QStringLiteral("exchange"), isNull(ex) ? QString() : jsStr(ex)}};
            if (out.size() == 10)
                break;
        }
        return out;
    });
}

// Company news (one or several symbols) or the general market feed.
Rec Fmp::news(const Rec &p)
{
    return wrap([&] {
        double lim = std::floor(numOr(p.value(QStringLiteral("limit")), 30));
        if (std::isnan(lim))
            lim = 30;
        const qint64 limit = qint64(std::max(1.0, std::min(50.0, lim)));
        double pg = std::floor(numOr(p.value(QStringLiteral("page")), 0));
        const qint64 page = qint64(std::max(0.0, std::isnan(pg) ? 0.0 : pg));
        const bool general = p.value(QStringLiteral("general")).toBool();
        List list;
        if (general) {
            list = rows(QStringLiteral("news/general-latest"), Rec{{QStringLiteral("limit"), limit}, {QStringLiteral("page"), page}});
        } else {
            QStringList given = p.value(QStringLiteral("symbols")).toStringList();
            if (given.isEmpty())
                given = QStringList{str(p, QStringLiteral("symbol"))};
            QStringList syms;
            for (const QString &x : given) {
                const QString s = x.trimmed().toUpper();
                if (symbolRe().match(s).hasMatch())
                    syms << s;
                if (syms.size() == 30)
                    break;
            }
            if (syms.isEmpty())
                return List();
            list = rows(QStringLiteral("news/stock"), Rec{{QStringLiteral("symbols"), syms.join(QLatin1Char(','))}, {QStringLiteral("limit"), limit}, {QStringLiteral("page"), page}});
        }
        QStringList dates;
        for (const QVariant &rv : list)
            dates << str(rv.toMap(), QStringLiteral("publishedDate"));
        const QList<double> times = parseNewsTimes(dates);
        List out;
        for (qsizetype i = 0; i < list.size(); ++i) {
            const Rec r = list[i].toMap();
            QString symbol = str(r, QStringLiteral("symbol"));
            if (symbol.isEmpty())
                symbol = general ? QString() : str(p, QStringLiteral("symbol"));
            QString publisher = str(r, QStringLiteral("publisher"));
            if (publisher.isEmpty())
                publisher = str(r, QStringLiteral("site"));
            const QString title = str(r, QStringLiteral("title"));
            const double time = times[i];
            if (title.isEmpty() || !std::isfinite(time))
                continue;
            out << Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("title"), title}, {QStringLiteral("publisher"), publisher},
                       {QStringLiteral("site"), str(r, QStringLiteral("site"))}, {QStringLiteral("text"), str(r, QStringLiteral("text"))},
                       {QStringLiteral("url"), str(r, QStringLiteral("url"))}, {QStringLiteral("image"), str(r, QStringLiteral("image"))},
                       {QStringLiteral("time"), std::floor(time) == time ? QVariant(qint64(time)) : QVariant(time)}};
        }
        return out;
    });
}

Rec Fmp::overview(const QString &symbol, bool force)
{
    return wrap([&] {
        Ctx ctx(force);
        const Rec sym{{QStringLiteral("symbol"), symbol}};
        QVariant quote, profile, quoteShort, afterTrade, afterQuote, priceChange;
        const auto softFirst = [&](const QString &path) { return soft([&] { return first(path, sym, std::nullopt, &ctx); }, null()); };
        parallel({
            [&] { quote = first(QStringLiteral("quote"), sym, std::nullopt, &ctx); },
            [&] { profile = softFirst(QStringLiteral("profile")); },
            [&] { quoteShort = softFirst(QStringLiteral("quote-short")); },
            [&] { afterTrade = softFirst(QStringLiteral("aftermarket-trade")); },
            [&] { afterQuote = softFirst(QStringLiteral("aftermarket-quote")); },
            [&] { priceChange = softFirst(QStringLiteral("stock-price-change")); },
        });
        return Rec{{QStringLiteral("quote"), quote}, {QStringLiteral("profile"), profile}, {QStringLiteral("quoteShort"), quoteShort},
                   {QStringLiteral("afterTrade"), afterTrade}, {QStringLiteral("afterQuote"), afterQuote},
                   {QStringLiteral("priceChange"), priceChange}, {QStringLiteral("asOf"), asOfSec(ctx)}};
    });
}

Rec Fmp::analyst(const QString &symbol, bool force)
{
    return wrap([&] {
        Ctx ctx(force);
        const Rec sym{{QStringLiteral("symbol"), symbol}};
        QVariant consensus, targets, snapshot, targetSummary;
        List grades, ratingsHistory, estimates, actuals;
        const auto softFirst = [&](const QString &path) { return soft([&] { return first(path, sym, std::nullopt, &ctx); }, null()); };
        const auto softRows = [&](const QString &path, Rec params) {
            params.insert(QStringLiteral("symbol"), symbol);
            return soft([&] { return rows(path, params, std::nullopt, &ctx); }, List());
        };
        parallel({
            [&] { consensus = softFirst(QStringLiteral("grades-consensus")); },
            [&] { targets = softFirst(QStringLiteral("price-target-consensus")); },
            [&] { grades = softRows(QStringLiteral("grades"), Rec{{QStringLiteral("limit"), 15}}); },
            [&] { snapshot = softFirst(QStringLiteral("ratings-snapshot")); },
            [&] { targetSummary = softFirst(QStringLiteral("price-target-summary")); },
            [&] { ratingsHistory = softRows(QStringLiteral("ratings-historical"), Rec{{QStringLiteral("limit"), 260}}); },
            [&] { estimates = softRows(QStringLiteral("analyst-estimates"), Rec{{QStringLiteral("period"), QStringLiteral("annual")}, {QStringLiteral("page"), 0}, {QStringLiteral("limit"), 10}}); },
            [&] { actuals = softRows(QStringLiteral("income-statement"), Rec{{QStringLiteral("period"), QStringLiteral("annual")}, {QStringLiteral("limit"), 4}}); },
        });
        return Rec{{QStringLiteral("consensus"), consensus}, {QStringLiteral("targets"), targets}, {QStringLiteral("grades"), grades},
                   {QStringLiteral("snapshot"), snapshot}, {QStringLiteral("targetSummary"), targetSummary},
                   {QStringLiteral("ratingsHistory"), ratingsHistory}, {QStringLiteral("estimates"), estimates},
                   {QStringLiteral("actuals"), actuals}, {QStringLiteral("asOf"), asOfSec(ctx)}};
    });
}

// Sector/industry performance and P/E for one trading day, plus the current biggest movers.
Rec Fmp::market(const Rec &p)
{
    return wrap([&] {
        Ctx ctx(p.value(QStringLiteral("force")).toBool());
        const QString ex = str(p, QStringLiteral("exchange"));
        const QString exchange = QStringList{QStringLiteral("NASDAQ"), QStringLiteral("NYSE"), QStringLiteral("AMEX")}.contains(ex) ? ex : QStringLiteral("NASDAQ");
        const auto step = [](const QString &d, int by) { return QDate::fromString(d, Qt::ISODate).addDays(by).toString(Qt::ISODate); };
        const auto weekend = [](const QString &d) { const int w = QDate::fromString(d, Qt::ISODate).dayOfWeek(); return w == 6 || w == 7; };
        const QString given = str(p, QStringLiteral("date"));
        static const QRegularExpression dateRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
        QString date = dateRe.match(given).hasMatch() ? given : nyDate(nowSec());
        while (given.isEmpty() && weekend(date))
            date = step(date, -1);
        const auto snap = [&](const QString &path) {
            return rows(path, Rec{{QStringLiteral("date"), date}, {QStringLiteral("exchange"), exchange}}, std::nullopt, &ctx);
        };
        List sectors = snap(QStringLiteral("sector-performance-snapshot"));
        // before the open, on a holiday, or on a chosen non-trading day there is nothing yet: use the last day that has data
        for (int i = 0; i < 5 && sectors.isEmpty(); ++i) {
            date = step(date, -1);
            while (weekend(date))
                date = step(date, -1);
            sectors = snap(QStringLiteral("sector-performance-snapshot"));
        }
        List industries, sectorPe, industryPe, gainers, losers, actives;
        const auto softRows = [&](const QString &path, bool dated) {
            return soft([&] { return dated ? snap(path) : rows(path, Rec(), std::nullopt, &ctx); }, List());
        };
        parallel({
            [&] { industries = softRows(QStringLiteral("industry-performance-snapshot"), true); },
            [&] { sectorPe = softRows(QStringLiteral("sector-pe-snapshot"), true); },
            [&] { industryPe = softRows(QStringLiteral("industry-pe-snapshot"), true); },
            [&] { gainers = softRows(QStringLiteral("biggest-gainers"), false); },
            [&] { losers = softRows(QStringLiteral("biggest-losers"), false); },
            [&] { actives = softRows(QStringLiteral("most-actives"), false); },
        });
        return Rec{{QStringLiteral("date"), date}, {QStringLiteral("exchange"), exchange}, {QStringLiteral("sectors"), sectors},
                   {QStringLiteral("industries"), industries}, {QStringLiteral("sectorPe"), sectorPe}, {QStringLiteral("industryPe"), industryPe},
                   {QStringLiteral("gainers"), gainers}, {QStringLiteral("losers"), losers}, {QStringLiteral("actives"), actives},
                   {QStringLiteral("asOf"), asOfSec(ctx)}};
    });
}

// Senate / House financial disclosures: the latest filings, one symbol, or one member's history.
Rec Fmp::congress(const Rec &p)
{
    return wrap([&] {
        Ctx ctx(p.value(QStringLiteral("force")).toBool());
        const QString kind = str(p, QStringLiteral("kind"));
        const QString chamber = str(p, QStringLiteral("chamber"));
        const QStringList chambers = chamber == QLatin1String("both") ? QStringList{QStringLiteral("senate"), QStringLiteral("house")}
                                                                     : QStringList{chamber == QLatin1String("house") ? QStringLiteral("house") : QStringLiteral("senate")};
        const QString symbol = str(p, QStringLiteral("symbol")).trimmed().toUpper();
        const QString name = str(p, QStringLiteral("name")).trimmed();
        if (kind == QLatin1String("symbol") && !symbolRe().match(symbol).hasMatch())
            throw FmpError(QStringLiteral("Enter a valid ticker symbol."), QStringLiteral("other"));
        if (kind == QLatin1String("name") && name.size() < 2)
            throw FmpError(QStringLiteral("Enter at least two letters of a first or last name."), QStringLiteral("other"));
        const auto one = [&](const QString &ch) {
            List list;
            if (kind == QLatin1String("latest")) {
                double pg = std::floor(numOr(p.value(QStringLiteral("page")), 0));
                double lim = std::floor(numOr(p.value(QStringLiteral("limit")), 100));
                if (std::isnan(pg)) pg = 0;
                if (std::isnan(lim)) lim = 100;
                list = rows(ch + QStringLiteral("-latest"), Rec{{QStringLiteral("page"), qint64(std::max(0.0, pg))},
                                                                 {QStringLiteral("limit"), qint64(std::max(1.0, std::min(100.0, lim)))}}, std::nullopt, &ctx);
            } else if (kind == QLatin1String("symbol")) {
                list = rows(ch + QStringLiteral("-trades"), Rec{{QStringLiteral("symbol"), symbol}}, std::nullopt, &ctx);
            } else {
                list = rows(ch + QStringLiteral("-trades-by-name"), Rec{{QStringLiteral("name"), name}}, std::nullopt, &ctx);
            }
            QList<Rec> out;
            for (const QVariant &r : list)
                out << normalizeTrade(ch, r.toMap());
            return out;
        };
        // Promise.allSettled
        std::vector<QList<Rec>> results(size_t(chambers.size()));
        std::vector<std::exception_ptr> errors(size_t(chambers.size()));
        std::vector<std::function<void()>> tasks;
        for (qsizetype i = 0; i < chambers.size(); ++i)
            tasks.push_back([&, i] {
                try { results[size_t(i)] = one(chambers[i]); } catch (...) { errors[size_t(i)] = std::current_exception(); }
            });
        parallel(tasks);
        QList<Rec> trades;
        bool anyOk = false;
        for (size_t i = 0; i < results.size(); ++i)
            if (!errors[i]) {
                anyOk = true;
                trades += results[i];
            }
        if (!anyOk)
            std::rethrow_exception(errors[0]); // e.g. not in the plan, or the key is bad
        const auto when = [](const Rec &t) { return t.value(QStringLiteral("disclosed")).toString() + t.value(QStringLiteral("traded")).toString(); };
        std::stable_sort(trades.begin(), trades.end(), [&](const Rec &a, const Rec &b) { return when(b).compare(when(a)) < 0; });
        List out;
        for (const Rec &t : trades)
            out << t;
        return Rec{{QStringLiteral("trades"), out}, {QStringLiteral("asOf"), asOfSec(ctx)}};
    });
}

// Stock screener: FMP filters, sorted by market cap (descending) with no other sort option.
Rec Fmp::screener(const Rec &p)
{
    return wrap([&] {
        Ctx ctx(p.value(QStringLiteral("force")).toBool());
        const Rec q = p.value(QStringLiteral("query")).toMap();
        double lim = std::floor(numOr(q.value(QStringLiteral("limit")), 250));
        if (std::isnan(lim))
            lim = 250;
        Rec params{{QStringLiteral("limit"), qint64(std::max(1.0, std::min(3000.0, lim)))}};
        static const QRegularExpression textRe(QStringLiteral("^[A-Za-z0-9_ &.,'/()-]+$"));
        for (const char *k : {"sector", "industry", "exchange", "country"}) {
            const QString v = str(q, QLatin1String(k)).trimmed();
            if (!v.isEmpty() && v.size() <= 60 && textRe.match(v).hasMatch())
                params.insert(QLatin1String(k), v);
        }
        for (const char *k : {"marketCapMoreThan", "marketCapLowerThan", "priceMoreThan", "priceLowerThan", "volumeMoreThan", "betaMoreThan", "betaLowerThan"}) {
            const QVariant v = q.value(QLatin1String(k));
            if (isNumber(v) && v.toDouble() >= 0)
                params.insert(QLatin1String(k), v.toDouble());
        }
        for (const char *k : {"isEtf", "isFund", "isActivelyTrading"}) {
            const QVariant v = q.value(QLatin1String(k));
            if (v.metaType().id() == QMetaType::Bool)
                params.insert(QLatin1String(k), v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        }
        const List list = rows(QStringLiteral("company-screener"), params, std::nullopt, &ctx);
        List out;
        for (const QVariant &r : list) {
            const Rec row = normalizeScreenerRow(r.toMap());
            if (!row.value(QStringLiteral("symbol")).toString().isEmpty())
                out << row;
        }
        return Rec{{QStringLiteral("rows"), out}, {QStringLiteral("asOf"), asOfSec(ctx)}};
    });
}

Rec Fmp::screenerOptions()
{
    return wrap([&] {
        const auto list = [&](const QString &path, const QString &key) {
            QStringList out;
            for (const QVariant &r : soft([&] { return rows(path, Rec()); }, List())) {
                const QString s = str(r.toMap(), key);
                if (!s.isEmpty())
                    out << s;
            }
            return out;
        };
        QStringList sectors, industries, countries;
        parallel({
            [&] { sectors = list(QStringLiteral("available-sectors"), QStringLiteral("sector")); },
            [&] { industries = list(QStringLiteral("available-industries"), QStringLiteral("industry")); },
            [&] { countries = list(QStringLiteral("available-countries"), QStringLiteral("country")); },
        });
        std::sort(sectors.begin(), sectors.end());
        std::sort(industries.begin(), industries.end());
        std::sort(countries.begin(), countries.end(), [](const QString &a, const QString &b) {
            if (a == QLatin1String("US") || b == QLatin1String("US"))
                return a == QLatin1String("US") && b != QLatin1String("US");
            return QString::localeAwareCompare(a, b) < 0;
        });
        return Rec{{QStringLiteral("sectors"), sectors}, {QStringLiteral("industries"), industries}, {QStringLiteral("countries"), countries}};
    });
}

Rec Fmp::fundamentals(const QString &symbol)
{
    return wrap([&] {
        const Rec sym{{QStringLiteral("symbol"), symbol}};
        Rec annual4{{QStringLiteral("symbol"), symbol}, {QStringLiteral("period"), QStringLiteral("annual")}, {QStringLiteral("limit"), 4}};
        Rec out;
        out.insert(QStringLiteral("ratios"), soft([&] { return first(QStringLiteral("ratios-ttm"), sym); }, null()));
        out.insert(QStringLiteral("metrics"), soft([&] { return first(QStringLiteral("key-metrics-ttm"), sym); }, null()));
        out.insert(QStringLiteral("income"), soft([&] { return rows(QStringLiteral("income-statement"), annual4); }, List()));
        out.insert(QStringLiteral("balance"), soft([&] { return rows(QStringLiteral("balance-sheet-statement"), annual4); }, List()));
        out.insert(QStringLiteral("cashflow"), soft([&] { return rows(QStringLiteral("cash-flow-statement"), annual4); }, List()));
        return out;
    });
}

// ---- plan check ----

// Probes each endpoint the app uses so plan coverage and field names can be checked.
Fmp::Probed Fmp::runProbes()
{
    const QString to = today(), from = utcDay(nowSec() - 7 * 86400);
    // yesterday or earlier: always has a full session
    qint64 t = nowSec() - 86400;
    while (nyParts(t).weekday == 0 || nyParts(t).weekday == 6)
        t -= 86400;
    const QString lastWeekday = nyDate(t);
    const auto S = [](const char *s) { return QString::fromUtf8(s); };
    const Rec aapl{{S("symbol"), S("AAPL")}};
    const Rec range{{S("symbol"), S("AAPL")}, {S("from"), from}, {S("to"), to}};
    struct ProbeDef { QString name, path; Rec params; QString cap; };
    const std::vector<ProbeDef> probes = {
        {S("Quote"), S("quote"), aapl, S("quotes")},
        {S("Index quote"), S("quote"), Rec{{S("symbol"), S("^GSPC")}}, S("indices")},
        {S("Daily history"), S("historical-price-eod/full"), range, S("daily")},
        {S("1-minute bars"), S("historical-chart/1min"), range, S("bars1min")},
        {S("5-minute bars"), S("historical-chart/5min"), range, S("bars5min")},
        {S("15-minute bars"), S("historical-chart/15min"), range, S("bars15min")},
        {S("30-minute bars"), S("historical-chart/30min"), range, S("bars30min")},
        {S("1-hour bars"), S("historical-chart/1hour"), range, S("bars1hour")},
        {S("4-hour bars"), S("historical-chart/4hour"), range, S("bars4hour")},
        {S("Symbol search"), S("search-symbol"), Rec{{S("query"), S("AAPL")}, {S("limit"), 3}}, S("search")},
        {S("Stock news"), S("news/stock"), Rec{{S("symbols"), S("AAPL")}, {S("limit"), 3}}, S("news")},
        {S("General market news"), S("news/general-latest"), Rec{{S("limit"), 3}}, S("marketNews")},
        {S("Company profile"), S("profile"), aapl, S("profile")},
        {S("Analyst consensus"), S("grades-consensus"), aapl, S("analyst")},
        {S("Price targets"), S("price-target-consensus"), aapl, S("analyst")},
        {S("Analyst grades"), S("grades"), Rec{{S("symbol"), S("AAPL")}, {S("limit"), 3}}, S("analyst")},
        {S("Short quote"), S("quote-short"), aapl, S("quotes")},
        {S("Aftermarket trade"), S("aftermarket-trade"), aapl, S("aftermarket")},
        {S("Aftermarket quote"), S("aftermarket-quote"), aapl, S("aftermarket")},
        {S("Price change"), S("stock-price-change"), aapl, S("quotes")},
        {S("Sector performance"), S("sector-performance-snapshot"), Rec{{S("date"), lastWeekday}, {S("exchange"), S("NASDAQ")}}, S("sectors")},
        {S("Biggest gainers"), S("biggest-gainers"), Rec(), S("movers")},
        {S("Stock screener"), S("company-screener"), Rec{{S("marketCapMoreThan"), qint64(10000000000)}, {S("limit"), 3}}, S("screener")},
        {S("Senate disclosures"), S("senate-latest"), Rec{{S("page"), 0}, {S("limit"), 3}}, S("congress")},
        {S("House disclosures"), S("house-latest"), Rec{{S("page"), 0}, {S("limit"), 3}}, S("congress")},
        {S("Senate trades by symbol"), S("senate-trades"), aapl, S("congress")},
        {S("Ratings snapshot"), S("ratings-snapshot"), aapl, S("analyst")},
        {S("Price target summary"), S("price-target-summary"), aapl, S("analyst")},
        {S("Ratings history"), S("ratings-historical"), Rec{{S("symbol"), S("AAPL")}, {S("limit"), 3}}, S("analyst")},
        {S("Analyst estimates"), S("analyst-estimates"), Rec{{S("symbol"), S("AAPL")}, {S("period"), S("annual")}, {S("page"), 0}, {S("limit"), 5}}, S("estimates")},
        {S("Ratios (TTM)"), S("ratios-ttm"), aapl, S("fundamentals")},
        {S("Key metrics (TTM)"), S("key-metrics-ttm"), aapl, S("fundamentals")},
        {S("Income statement"), S("income-statement"), Rec{{S("symbol"), S("AAPL")}, {S("period"), S("annual")}, {S("limit"), 2}}, S("fundamentals")},
        {S("Balance sheet"), S("balance-sheet-statement"), Rec{{S("symbol"), S("AAPL")}, {S("period"), S("annual")}, {S("limit"), 2}}, S("fundamentals")},
        {S("Cash flow"), S("cash-flow-statement"), Rec{{S("symbol"), S("AAPL")}, {S("period"), S("annual")}, {S("limit"), 2}}, S("fundamentals")},
    };
    Probed out;
    for (const ProbeDef &pr : probes) {
        {
            std::lock_guard<std::mutex> g(m_stopMx);
            if (m_stopping) { // shutting down: an unfinished check must not be stored
                out.aborted = true;
                break;
            }
        }
        Rec row{{S("name"), pr.name}, {S("path"), pr.path}, {S("cap"), pr.cap}};
        try {
            Ctx ctx(true);
            const QVariant v = m_request->get(pr.path, pr.params, std::nullopt, &ctx);
            const List list = v.metaType().id() == QMetaType::QVariantList ? v.toList() : List();
            const qsizetype n = list.size();
            row.insert(S("ok"), n > 0);
            row.insert(S("status"), n ? S("ok") : S("empty"));
            row.insert(S("detail"), n ? QStringLiteral("%1 record%2").arg(n).arg(n == 1 ? QString() : S("s")) : S("Empty response"));
            if (n && list.first().metaType().id() == QMetaType::QVariantMap)
                row.insert(S("fields"), QStringList(list.first().toMap().keys()));
            out.rows << row;
        } catch (const std::exception &e) {
            const FmpError *fe = dynamic_cast<const FmpError *>(&e);
            row.insert(S("ok"), false);
            row.insert(S("status"), fe && fe->kind() == QLatin1String("plan") ? S("plan") : S("error"));
            row.insert(S("detail"), QString::fromStdString(e.what()));
            out.rows << row;
            if (fe && (fe->kind() == QLatin1String("auth") || fe->kind() == QLatin1String("nokey") || fe->kind() == QLatin1String("network"))) {
                out.aborted = true;
                break;
            }
        }
    }
    return out;
}

void Fmp::writeCaps(const std::optional<Rec> &caps)
{
    saveCaps(caps);
    if (m_broadcast)
        m_broadcast(QStringLiteral("fmp:caps:changed"), caps ? QVariant(*caps) : null());
}

// Runs the plan check and stores the outcome. Joins a check already in progress instead of starting a second.
std::optional<List> Fmp::checkPlan()
{
    if (key().isEmpty())
        return std::nullopt;
    std::shared_ptr<Probe> pr;
    bool owner = false;
    {
        std::lock_guard<std::mutex> g(m_probeMx);
        if (!m_probing) {
            m_probing = std::make_shared<Probe>();
            owner = true;
        }
        pr = m_probing;
    }
    if (owner) {
        std::optional<List> result;
        try {
            const Probed probed = runProbes();
            if (!probed.aborted) {
                // A capability is missing only when every probe for it was refused by the plan; an empty answer still counts as available.
                QStringList order;
                QHash<QString, bool> allPlan;
                for (const QVariant &rv : probed.rows) {
                    const Rec r = rv.toMap();
                    const QString cap = r.value(QStringLiteral("cap")).toString();
                    if (cap.isEmpty())
                        continue;
                    const bool plan = r.value(QStringLiteral("status")).toString() == QLatin1String("plan");
                    if (!allPlan.contains(cap)) {
                        order << cap;
                        allPlan.insert(cap, plan);
                    } else if (!plan) {
                        allPlan[cap] = false;
                    }
                }
                QStringList unavailable;
                for (const QString &c : order)
                    if (allPlan.value(c))
                        unavailable << c;
                writeCaps(Rec{{QStringLiteral("checkedAt"), nowSec()}, {QStringLiteral("unavailable"), unavailable}});
            }
            result = probed.rows;
        } catch (...) {
            result = std::nullopt;
        }
        {
            std::lock_guard<std::mutex> g(m_probeMx);
            m_probing.reset();
        }
        {
            std::lock_guard<std::mutex> g(pr->mx);
            pr->rows = result;
            pr->done = true;
        }
        pr->cv.notify_all();
    } else {
        std::unique_lock<std::mutex> lk(pr->mx);
        pr->cv.wait(lk, [&] { return pr->done; });
    }
    return pr->rows;
}

// Runs checkPlan after `ms` on the object's own small thread pool: the caller may be any handler thread (often in the
// middle of a request), no event loop is needed, and the wait is cut short when the Fmp is destroyed.
void Fmp::checkSoon(int ms)
{
    m_timers.start([this, ms] {
        {
            std::unique_lock<std::mutex> lk(m_stopMx);
            if (m_stopCv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return m_stopping; }))
                return;
        }
        m_lastCheckStart = nowMs();
        try {
            checkPlan();
        } catch (...) {
        }
    });
}

// An endpoint refused for plan reasons usually means the plan changed (or was never checked). Re-check, but not more than every 10 minutes:
// one refused request can also be about a single symbol, so a lone error never switches a feature off by itself.
void Fmp::noteRefusal()
{
    {
        std::lock_guard<std::mutex> g(m_probeMx);
        if (m_probing)
            return;
    }
    if (nowMs() - m_lastCheckStart.load() > 10 * 60000)
        checkSoon(1500);
}

Rec Fmp::test()
{
    return wrap([&] {
        m_lastCheckStart = nowMs();
        const std::optional<List> rows = checkPlan();
        if (!rows)
            throw FmpError(NO_KEY, QStringLiteral("nokey"));
        return *rows;
    });
}

// ---- plain channels ----

QVariant Fmp::capsGet()
{
    const auto c = readCaps();
    return c ? QVariant(*c) : null();
}

Rec Fmp::keyStatus()
{
    // keys stored through the getter live in the desktop keyring, so a stored key is always encrypted
    const QString stored = m_key ? m_key() : QString();
    if (!stored.isEmpty())
        return Rec{{QStringLiteral("source"), QStringLiteral("stored")}, {QStringLiteral("encrypted"), true}};
    return Rec{{QStringLiteral("source"), qEnvironmentVariableIsEmpty("FMP_API_KEY") ? QStringLiteral("none") : QStringLiteral("env")},
               {QStringLiteral("encrypted"), false}};
}

void Fmp::setKey(const QString &k)
{
    m_store.clear();
    if (m_setKey)
        m_setKey(k);
    writeCaps(std::nullopt);
    checkSoon(1000);
}

void Fmp::clearKey()
{
    m_store.clear();
    if (m_clearKey)
        m_clearKey();
    writeCaps(std::nullopt);
}

Rec Fmp::cacheStats() { return m_store.stats(); }
void Fmp::clearCache() { m_store.clear(); }

// ---- used by the paper-trading engine ----

std::optional<double> Fmp::quote(const QString &symbol)
{
    const QVariant q = first(QStringLiteral("quote"), Rec{{QStringLiteral("symbol"), symbol}}, 8000);
    const double v = jsNumber(q.toMap().value(QStringLiteral("price")));
    return std::isfinite(v) && v > 0 ? std::optional<double>(v) : std::nullopt;
}

std::optional<FillGrain> Fmp::grain()
{
    return fillGrain(unavailableOf(readCaps()));
}

Bars Fmp::barsFine(const QString &symbol, qint64 fromEpoch)
{
    const auto g = fillGrain(unavailableOf(readCaps()));
    if (!g)
        return {};
    const qint64 floor = nowSec() - 10 * 86400;
    const qint64 start = std::max(fromEpoch, floor);
    const qint64 boundary = qint64(std::floor(double(start - g->offset) / g->seconds)) * g->seconds + g->offset;
    const Bars all = fetchBars(symbol, g->interval, nyDate(start), today(), 15000);
    Bars out;
    for (const Bar &b : all)
        if (b.time >= boundary)
            out.push_back(b);
    return out;
}

std::optional<Rec> Fmp::caps() { return readCaps(); }

} // namespace tl
