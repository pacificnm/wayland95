// The IPC channels of main/index.ts, main/fmp.ts and main/chat.ts, by their upstream names.
#include "backend.h"

#include "keystore.h"

#include "core/accounts.h"
#include "core/backup.h"
#include "core/chat.h"
#include "core/db.h"
#include "core/engine.h"
#include "core/fmp.h"
#include "core/fmpmcp.h"
#include "core/nytime.h"
#include "core/optionsfeed.h"
#include "core/portfolioorders.h"
#include "core/portfoliostore.h"
#include "core/stores.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QQmlEngine>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

using namespace tl;

namespace {
const QString APP_VERSION = QStringLiteral("1.1.1");
qint64 id(const QVariantList &a, int i = 0) { return a.value(i).toLongLong(); }
QString str(const QVariantList &a, int i = 0) { return a.value(i).toString(); }
Rec rec(const QVariantList &a, int i = 0) { return a.value(i).toMap(); }
}

Backend::Backend(KeyStore *keys, const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_keys(keys)
    , m_dataDir(dataDir)
{
    m_pool.setMaxThreadCount(8);
    m_pool.setExpiryTimeout(-1); // keep threads (and their database connections)
    auto bc = [this](const QString &ch, const QVariant &p) { broadcast(ch, p); };
    m_fmp = std::make_unique<Fmp>([keys] { return keys->fmpKey(); }, [keys](const QString &k) { keys->setFmpKey(k); },
                                  [keys] { keys->clearFmpKey(); }, bc);
    m_mcp = std::make_unique<FmpMcp>([keys] { return keys->fmpKey(); });
    m_engine = std::make_unique<Engine>(
        Market{[this](const QString &s) { return m_fmp->quote(s); }, [this](const QString &s, qint64 from) { return m_fmp->barsFine(s, from); },
               [this] { return m_fmp->grain(); }},
        [] { return accounts::activeId(); });
    m_chat = std::make_unique<Chat>([keys] { return keys->anthropicKey(); }, m_mcp.get(), bc);
    m_options = std::make_unique<OptionsFeed>();
    m_portfolio = std::make_unique<PortfolioStore>();
    registerHandlers();
}

Backend::~Backend()
{
    m_tick.stop();
    m_pool.waitForDone(3000);
}

void Backend::broadcast(const QString &channel, const QVariant &payload)
{
    QMetaObject::invokeMethod(this, [this, channel, payload] { emit event(channel, payload); }, Qt::QueuedConnection);
}

void Backend::start()
{
    connect(&m_tick, &QTimer::timeout, this, &Backend::tick);
    m_tick.start(15000);
    QTimer::singleShot(3000, this, &Backend::tick);
}

void Backend::tick()
{
    m_pool.start([this] {
        try {
            const List ev = m_engine->tick();
            if (!ev.isEmpty())
                broadcast(QStringLiteral("trade:update"), ev);
        } catch (...) {
        }
    });
}

QVariant Backend::run(const QString &channel, const QVariantList &args)
{
    const auto h = m_handlers.constFind(channel);
    if (h == m_handlers.constEnd())
        return failError(QStringLiteral("Unknown channel %1").arg(channel));
    try {
        return (*h)(args);
    } catch (const Error &e) {
        // trade:* answered {ok:false, errors}; everything else {ok:false, error}
        if (channel.startsWith(QLatin1String("trade:")))
            return failErrors({e.message()});
        return failError(e.message());
    } catch (const std::exception &e) {
        return failError(QString::fromUtf8(e.what()));
    }
}

void Backend::invoke(const QString &channel, const QVariantList &args, const QJSValue &callback)
{
    const int call = m_nextCall++;
    if (callback.isCallable())
        m_callbacks.insert(call, callback);
    // JS objects inside the arguments may still be QJSValues: plain maps and lists for the worker thread
    QVariantList plainArgs;
    for (const QVariant &a : args)
        plainArgs.append(a.metaType() == QMetaType::fromType<QJSValue>() ? a.value<QJSValue>().toVariant() : a);
    m_pool.start([this, channel, args = plainArgs, call] {
        const QVariant result = run(channel, args);
        QMetaObject::invokeMethod(this, [this, call, result] {
            QJSValue cb = m_callbacks.take(call);
            if (!cb.isCallable())
                return;
            QJSEngine *engine = qjsEngine(this);
            const QJSValue r = engine ? engine->toScriptValue(result) : QJSValue();
            const QJSValue out = cb.call({r});
            if (out.isError())
                qWarning("Trading Lab: callback for %s failed: %s", qPrintable(QString()), qPrintable(out.toString()));
        }, Qt::QueuedConnection);
    });
}

QVariant Backend::invokeNow(const QString &channel, const QVariantList &args) { return run(channel, args); }

void Backend::registerHandlers()
{
    auto &h = m_handlers;
    Engine *engine = m_engine.get();
    Fmp *fmp = m_fmp.get();
    auto bc = [this](const QString &ch, const QVariant &p = QVariant()) { broadcast(ch, p); };

    // ---- paper trading
    h[QStringLiteral("trade:snapshot")] = [engine](const QVariantList &) { return QVariant(engine->snapshot()); };
    h[QStringLiteral("trade:place")] = [engine, bc](const QVariantList &a) {
        const Rec r = engine->place(rec(a));
        bc(QStringLiteral("trade:update"), r.value(QStringLiteral("ok")).toBool() ? r.value(QStringLiteral("events")) : QVariant(List{}));
        return QVariant(r);
    };
    h[QStringLiteral("trade:cancel")] = [engine, bc](const QVariantList &a) { const Rec r = engine->cancel(id(a)); bc(QStringLiteral("trade:update"), List{}); return QVariant(r); };
    h[QStringLiteral("trade:modify")] = [engine, bc](const QVariantList &a) { const Rec r = engine->modify(id(a), rec(a, 1)); bc(QStringLiteral("trade:update"), List{}); return QVariant(r); };
    h[QStringLiteral("trade:close")] = [engine, bc](const QVariantList &a) { const Rec r = engine->closePosition(str(a)); bc(QStringLiteral("trade:update"), List{}); return QVariant(r); };
    h[QStringLiteral("trade:reset")] = [engine, bc](const QVariantList &a) { engine->reset(a.value(0).toDouble()); bc(QStringLiteral("trade:update"), List{}); return QVariant(); };

    // ---- paper accounts: one is active at a time (new orders go there), but working orders in every account keep filling
    auto acct = [bc](std::function<QVariant()> fn) {
        try {
            const QVariant data = fn();
            bc(QStringLiteral("trade:update"), List{});
            return QVariant(ok(Rec{{QStringLiteral("data"), data}}));
        } catch (const Error &e) {
            return QVariant(failError(e.message()));
        }
    };
    h[QStringLiteral("acct:list")] = [engine](const QVariantList &a) {
        List l = accounts::list();
        if (a.value(0).toBool()) {
            for (QVariant &v : l) {
                Rec r = v.toMap();
                try { r.insert(QStringLiteral("equity"), engine->values(r.value(QStringLiteral("id")).toLongLong()).value(QStringLiteral("equity"))); } catch (...) { }
                v = r;
            }
        }
        return QVariant(Rec{{QStringLiteral("list"), l}, {QStringLiteral("activeId"), accounts::activeId()}});
    };
    h[QStringLiteral("acct:transfers")] = [](const QVariantList &a) { return QVariant(accounts::transfers(id(a))); };
    h[QStringLiteral("acct:create")] = [acct](const QVariantList &a) { return acct([a] { return QVariant(accounts::create(rec(a), a.value(1))); }); };
    h[QStringLiteral("acct:update")] = [acct](const QVariantList &a) { return acct([a] { return QVariant(accounts::update(id(a), rec(a, 1))); }); };
    h[QStringLiteral("acct:setBalance")] = [acct](const QVariantList &a) { return acct([a] { return QVariant(accounts::setBalance(id(a), a.value(1), str(a, 2))); }); };
    h[QStringLiteral("acct:setActive")] = [acct](const QVariantList &a) { return acct([a] { accounts::setActive(id(a)); return QVariant(); }); };
    h[QStringLiteral("acct:remove")] = [acct](const QVariantList &a) { return acct([a] { accounts::remove(id(a)); return QVariant(); }); };

    // ---- watchlists
    auto wlChanged = [bc](const QVariant &v) { bc(QStringLiteral("watchlists:changed")); return v; };
    h[QStringLiteral("wl:lists")] = [](const QVariantList &) { return QVariant(watchlists::lists()); };
    h[QStringLiteral("wl:create")] = [wlChanged](const QVariantList &a) { return wlChanged(watchlists::create(str(a))); };
    h[QStringLiteral("wl:rename")] = [wlChanged](const QVariantList &a) { return wlChanged(watchlists::rename(id(a), str(a, 1))); };
    h[QStringLiteral("wl:delete")] = [wlChanged](const QVariantList &a) { return wlChanged(watchlists::remove(id(a))); };
    h[QStringLiteral("wl:add")] = [wlChanged](const QVariantList &a) { return wlChanged(watchlists::add(id(a), str(a, 1))); };
    h[QStringLiteral("wl:remove")] = [wlChanged](const QVariantList &a) { return wlChanged(watchlists::removeSymbol(id(a), str(a, 1))); };

    // ---- Portfolio: planned allocations and holdings. Bookkeeping only, except the two order buttons (guarded in portfolioorders).
    PortfolioStore *pf = m_portfolio.get();
    auto pfChanged = [bc](const QVariant &v) { bc(QStringLiteral("portfolio:changed")); return v; };
    h[QStringLiteral("pf:list")] = [pf](const QVariantList &) { return QVariant(pf->list()); };
    h[QStringLiteral("pf:create")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->create(a.value(0), a.value(1))); };
    h[QStringLiteral("pf:update")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->update(id(a), rec(a, 1))); };
    h[QStringLiteral("pf:remove")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->remove(id(a))); };
    h[QStringLiteral("pf:addItem")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->addItem(id(a), rec(a, 1))); };
    h[QStringLiteral("pf:updateItem")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->updateItem(id(a), rec(a, 1))); };
    h[QStringLiteral("pf:removeItem")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->removeItem(id(a))); };
    h[QStringLiteral("pf:setTargets")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->setTargets(id(a), a.value(1).toList())); };
    // a linked portfolio follows one paper account: its balances and positions, read without switching to it
    h[QStringLiteral("pf:accountView")] = [engine](const QVariantList &a) {
        try { return QVariant(ok(Rec{{QStringLiteral("data"), engine->view(id(a))}})); } catch (const Error &e) { return QVariant(failError(e.message())); }
    };
    // the two buttons that send paper orders from this screen (buy the plan, sell a percentage)
    h[QStringLiteral("pf:placeOrders")] = [pf, engine, bc, pfChanged](const QVariantList &a) {
        OrderDeps deps{[pf](qint64 pid) { return pf->get(pid); }, [] { return accounts::activeId(); }, [engine](const Rec &spec) { return engine->place(spec); }};
        const Rec r = placePortfolioOrders(deps, id(a), str(a, 1), a.value(2).toList());
        bc(QStringLiteral("trade:update"), r.value(QStringLiteral("ok")).toBool() ? r.value(QStringLiteral("events")) : QVariant(List{}));
        return pfChanged(r);
    };
    h[QStringLiteral("pf:recordSells")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->recordSells(id(a), a.value(1).toList())); };
    h[QStringLiteral("pf:recordBuys")] = [pf, pfChanged](const QVariantList &a) { return pfChanged(pf->recordBuys(id(a), a.value(1).toList())); };

    // ---- options chains come from Cboe's public delayed feed (FMP has no options data)
    OptionsFeed *options = m_options.get();
    h[QStringLiteral("options:chain")] = [options](const QVariantList &a) {
        try {
            return QVariant(ok(Rec{{QStringLiteral("data"), options->chain(str(a), a.value(1).toBool(), isMarketOpen(nowSec()) ? 60000 : 15 * 60000)}}));
        } catch (const Error &e) {
            return QVariant(failError(e.message()));
        }
    };

    // ---- strategies (built-in documents come from content/strategies.json)
    auto stratChanged = [bc](const QVariant &v) { bc(QStringLiteral("strategies:changed")); return v; };
    h[QStringLiteral("strat:builtin")] = [](const QVariantList &) { return content(QStringLiteral("strategies.json")); };
    h[QStringLiteral("strat:list")] = [](const QVariantList &) { return QVariant(strategies::list()); };
    h[QStringLiteral("strat:create")] = [stratChanged](const QVariantList &a) { return stratChanged(strategies::create(rec(a))); };
    h[QStringLiteral("strat:update")] = [stratChanged](const QVariantList &a) { return stratChanged(strategies::update(str(a), rec(a, 1))); };
    h[QStringLiteral("strat:delete")] = [stratChanged](const QVariantList &a) { strategies::remove(str(a)); return stratChanged(QVariant()); };
    h[QStringLiteral("strat:progress")] = [](const QVariantList &) { return QVariant(strategies::progress()); };
    h[QStringLiteral("strat:setProgress")] = [stratChanged](const QVariantList &a) { return stratChanged(strategies::setProgress(str(a), rec(a, 1))); };

    // ---- journal
    auto changed = [bc](const QVariant &v) { bc(QStringLiteral("journal:changed")); return v; };
    h[QStringLiteral("journal:list")] = [](const QVariantList &a) { return QVariant(journal::list(rec(a))); };
    h[QStringLiteral("journal:get")] = [](const QVariantList &a) { return journal::get(id(a)); };
    h[QStringLiteral("journal:image")] = [](const QVariantList &a) { return journal::image(id(a)); };
    h[QStringLiteral("journal:create")] = [changed](const QVariantList &a) { return changed(journal::create(rec(a))); };
    h[QStringLiteral("journal:update")] = [changed](const QVariantList &a) { return changed(journal::update(id(a), rec(a, 1))); };
    h[QStringLiteral("journal:comment")] = [changed](const QVariantList &a) { return changed(journal::comment(id(a), rec(a, 1))); };
    h[QStringLiteral("journal:delete")] = [changed](const QVariantList &a) { journal::remove(id(a)); return changed(QVariant()); };

    // ---- market data (FMP); the key never leaves this process
    h[QStringLiteral("fmp:key:status")] = [fmp](const QVariantList &) { return QVariant(fmp->keyStatus()); };
    h[QStringLiteral("fmp:key:set")] = [fmp](const QVariantList &a) { fmp->setKey(str(a)); return QVariant(); };
    h[QStringLiteral("fmp:key:clear")] = [fmp](const QVariantList &) { fmp->clearKey(); return QVariant(); };
    h[QStringLiteral("fmp:cache:stats")] = [fmp](const QVariantList &) { return QVariant(fmp->cacheStats()); };
    h[QStringLiteral("fmp:cache:clear")] = [fmp](const QVariantList &) { fmp->clearCache(); return QVariant(); };
    h[QStringLiteral("fmp:bars")] = [fmp](const QVariantList &a) { return QVariant(fmp->bars(rec(a))); };
    h[QStringLiteral("fmp:quotes")] = [fmp](const QVariantList &a) { return QVariant(fmp->quotes(a.value(0).toStringList())); };
    h[QStringLiteral("fmp:search")] = [fmp](const QVariantList &a) { return QVariant(fmp->search(str(a))); };
    h[QStringLiteral("fmp:news")] = [fmp](const QVariantList &a) { return QVariant(fmp->news(rec(a))); };
    h[QStringLiteral("fmp:overview")] = [fmp](const QVariantList &a) { return QVariant(fmp->overview(str(a), a.value(1).toBool())); };
    h[QStringLiteral("fmp:analyst")] = [fmp](const QVariantList &a) { return QVariant(fmp->analyst(str(a), a.value(1).toBool())); };
    h[QStringLiteral("fmp:market")] = [fmp](const QVariantList &a) { return QVariant(fmp->market(rec(a))); };
    h[QStringLiteral("fmp:congress")] = [fmp](const QVariantList &a) { return QVariant(fmp->congress(rec(a))); };
    h[QStringLiteral("fmp:screener")] = [fmp](const QVariantList &a) { return QVariant(fmp->screener(rec(a))); };
    h[QStringLiteral("fmp:screenerOptions")] = [fmp](const QVariantList &) { return QVariant(fmp->screenerOptions()); };
    h[QStringLiteral("fmp:fundamentals")] = [fmp](const QVariantList &a) { return QVariant(fmp->fundamentals(str(a))); };
    h[QStringLiteral("fmp:test")] = [fmp](const QVariantList &) { return QVariant(fmp->test()); };
    h[QStringLiteral("fmp:caps:get")] = [fmp](const QVariantList &) { return fmp->capsGet(); };
    FmpMcp *mcp = m_mcp.get();
    h[QStringLiteral("fmp:mcp:status")] = [mcp](const QVariantList &) { return QVariant(mcp->status()); };

    // ---- Claude
    Chat *chat = m_chat.get();
    KeyStore *keys = m_keys;
    h[QStringLiteral("chat:send")] = [chat](const QVariantList &a) { return QVariant(chat->send(rec(a))); };
    h[QStringLiteral("chat:stop")] = [chat](const QVariantList &a) { chat->stop(str(a)); return QVariant(); };
    h[QStringLiteral("chat:list")] = [chat](const QVariantList &) { return QVariant(chat->list()); };
    h[QStringLiteral("chat:get")] = [chat](const QVariantList &a) { return QVariant(chat->get(str(a))); };
    h[QStringLiteral("chat:delete")] = [chat](const QVariantList &a) { chat->remove(str(a)); return QVariant(); };
    h[QStringLiteral("chat:key:status")] = [keys](const QVariantList &) { return QVariant(keys->anthropicStatus()); };
    h[QStringLiteral("chat:key:set")] = [keys](const QVariantList &a) { keys->setAnthropicKey(str(a)); return QVariant(); };
    h[QStringLiteral("chat:key:clear")] = [keys](const QVariantList &) { keys->clearAnthropicKey(); return QVariant(); };
    h[QStringLiteral("tool:result")] = [chat](const QVariantList &a) { chat->toolResult(str(a), rec(a, 1)); return QVariant(); };

    // ---- settings, content, app
    h[QStringLiteral("settings:get")] = [](const QVariantList &a) { return getSetting(str(a), null()); };
    h[QStringLiteral("settings:set")] = [](const QVariantList &a) { setSetting(str(a), a.value(1)); return QVariant(); };
    h[QStringLiteral("content:get")] = [](const QVariantList &a) { return content(str(a)); };
    h[QStringLiteral("content:text")] = [](const QVariantList &a) { return QVariant(contentText(str(a))); };
    h[QStringLiteral("app:about")] = [this](const QVariantList &) {
        return QVariant(Rec{{QStringLiteral("name"), QStringLiteral("Trading Lab")},
                            {QStringLiteral("description"), QStringLiteral("Trading Lab: a paper-trading learning tool with charts, market data and a Claude assistant")},
                            {QStringLiteral("version"), APP_VERSION}, {QStringLiteral("license"), QStringLiteral("MIT")},
                            {QStringLiteral("qt"), QString::fromLatin1(qVersion())}, {QStringLiteral("dataFolder"), m_dataDir},
                            {QStringLiteral("repository"), QStringLiteral("https://github.com/pacificnm/trading-labs.app")}});
    };
    // article links open in the user's browser; only web URLs are allowed
    h[QStringLiteral("app:openExternal")] = [](const QVariantList &a) {
        const QUrl u(str(a));
        if (u.scheme() == QLatin1String("https") || u.scheme() == QLatin1String("http"))
            QMetaObject::invokeMethod(qApp, [u] { QDesktopServices::openUrl(u); }, Qt::QueuedConnection);
        return QVariant();
    };

    // ---- File > Back Up / Restore (the file dialogs are in QML; paths come here)
    const QString dbFile = QDir(m_dataDir).filePath(QStringLiteral("trading.db"));
    h[QStringLiteral("backup:create")] = [](const QVariantList &a) {
        const QString path = str(a);
        const Rec sum = createBackup(path, APP_VERSION);
        return QVariant(ok(Rec{{QStringLiteral("path"), path}, {QStringLiteral("size"), QFileInfo(path).size()}, {QStringLiteral("contents"), describeBackup(sum)}}));
    };
    h[QStringLiteral("backup:inspect")] = [dbFile](const QVariantList &a) {
        if (QFileInfo(str(a)).canonicalFilePath() == QFileInfo(dbFile).canonicalFilePath())
            return QVariant(failError(QStringLiteral("That is the live database. Choose a backup file.")));
        Rec r = inspectBackup(str(a), APP_VERSION);
        if (r.value(QStringLiteral("ok")).toBool())
            r.insert(QStringLiteral("contents"), describeBackup(r.value(QStringLiteral("summary")).toMap()));
        return QVariant(r);
    };
    h[QStringLiteral("backup:restore")] = [this, dbFile](const QVariantList &a) {
        const QString safety = restoreBackup(dbFile, str(a), QDir(m_dataDir).filePath(QStringLiteral("backups")), APP_VERSION);
        qInfo("restored backup; safety copy at %s", qPrintable(safety));
        QMetaObject::invokeMethod(this, [this] { emit restartRequested(); }, Qt::QueuedConnection);
        return QVariant(ok());
    };
}
