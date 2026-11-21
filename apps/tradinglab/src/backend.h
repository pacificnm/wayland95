#pragma once

// What upstream's main process did behind `window.api`: every IPC channel by its upstream name
// (trade:place, wl:lists, fmp:overview...), handled on a worker thread. QML calls
//   api.invoke("wl:lists", [], function (result) { ... })
// and listens to api.event(channel, payload) for broadcasts (trade:update, watchlists:changed, chat:event, tool:call...).

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#include <functional>
#include <memory>

class KeyStore;
namespace tl {
class Engine;
class Fmp;
class FmpMcp;
class Chat;
class OptionsFeed;
class PortfolioStore;
}

class Backend : public QObject
{
    Q_OBJECT
public:
    Backend(KeyStore *keys, const QString &dataDir, QObject *parent = nullptr);
    ~Backend() override;

    // asynchronous: the callback (optional) gets the result
    Q_INVOKABLE void invoke(const QString &channel, const QVariantList &args = {}, const QJSValue &callback = QJSValue());
    // the same handler run on the calling (GUI) thread, for quick local reads only (settings, content)
    Q_INVOKABLE QVariant invokeNow(const QString &channel, const QVariantList &args = {});
    Q_INVOKABLE QString dataDir() const { return m_dataDir; }

    // the engine's timer: working orders are checked against fresh bars while the app is open
    void start();
    void broadcast(const QString &channel, const QVariant &payload = QVariant());

signals:
    void event(const QString &channel, const QVariant &payload);
    // a restore finished: the app must restart
    void restartRequested();

private:
    using Handler = std::function<QVariant(const QVariantList &args)>;
    void registerHandlers();
    QVariant run(const QString &channel, const QVariantList &args);
    void tick();

    KeyStore *m_keys;
    QString m_dataDir;
    QHash<QString, Handler> m_handlers;
    QHash<int, QJSValue> m_callbacks;
    int m_nextCall = 1;
    QThreadPool m_pool;
    QTimer m_tick;
    std::unique_ptr<tl::Fmp> m_fmp;
    std::unique_ptr<tl::FmpMcp> m_mcp;
    std::unique_ptr<tl::Engine> m_engine;
    std::unique_ptr<tl::Chat> m_chat;
    std::unique_ptr<tl::OptionsFeed> m_options;
    std::unique_ptr<tl::PortfolioStore> m_portfolio;
};
