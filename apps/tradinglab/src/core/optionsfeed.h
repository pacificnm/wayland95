#pragma once

// Option chains from Cboe's public delayed-quotes feed (FMP has no options data). Port of main/options.ts
// (createOptionsClient, parseCboe, parseContractName, cboeSymbol); the chain is the OptionsChain shape of shared/options.ts:
// {symbol, price, change, changePct, iv30, fetchedAt, expirations: [{date, dte}], contracts: [OptionContract], source}
// with OptionContract {exp, type: C|P, strike, bid, ask, last, volume, oi, iv, delta, gamma, theta, vega, theo, change}.

#include "http.h"
#include "util.h"

#include <QJsonValue>
#include <QString>

#include <condition_variable>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace tl {

// Cboe lists index options under an underscore prefix (^GSPC -> _SPX).
QString cboeSymbol(const QString &s);

struct ContractName
{
    QString exp;  // YYYY-MM-DD
    QString type; // C or P
    double strike = 0;
};
// OCC-style contract name: ROOT + YYMMDD + C/P + strike x 1000 (8 digits).
std::optional<ContractName> parseContractName(const QString &name);

// Turns Cboe's raw JSON into a compact chain: nearby expirations only, bad IVs dropped. Throws tl::Error.
Rec parseCboe(const QJsonValue &raw, const QString &symbol, qint64 nowSec = tl::nowSec());

// Fetches and caches option chains. The feed is delayed about 15 minutes, so a minute of caching costs nothing;
// identical concurrent requests share one download (chains run from 2 to 13 MB). Thread-safe; chain() blocks.
class OptionsFeed
{
public:
    using Fetch = std::function<HttpResponse(const QUrl &url, const Headers &headers)>;
    explicit OptionsFeed(Fetch fetch = {}, std::function<qint64()> clock = [] { return tl::nowMs(); });

    // Throws tl::Error with a user-facing message (upstream's 'options:chain' handler turned it into {ok: false, error}).
    Rec chain(const QString &symbolIn, bool force = false, qint64 ttlMs = 60000);

private:
    struct Flight
    {
        std::mutex mx;
        std::condition_variable cv;
        bool done = false;
        Rec chain;
        std::exception_ptr error;
    };
    struct Hit
    {
        qint64 at = 0;
        Rec chain;
    };
    Fetch m_fetch;
    std::function<qint64()> m_clock;
    std::mutex m_mx;
    std::map<QString, Hit> m_cache;
    std::map<QString, std::shared_ptr<Flight>> m_inflight;
};

} // namespace tl
