#include "http.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSemaphore>
#include <QThreadPool>
#include <QThreadStorage>
#include <QTimer>

#include <exception>
#include <mutex>

namespace tl {

namespace {
QThreadStorage<QNetworkAccessManager *> g_nam;

QNetworkAccessManager *nam()
{
    if (!g_nam.hasLocalData()) {
        auto *m = new QNetworkAccessManager;
        m->setTransferTimeout(0);
        g_nam.setLocalData(m);
    }
    return g_nam.localData();
}

QNetworkRequest makeRequest(const QUrl &url, const Headers &headers)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("TradingLab-Wayland95"));
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    return req;
}

HttpResponse finish(QNetworkReply *reply, bool timedOut, bool cancelled)
{
    HttpResponse r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body += reply->readAll();
    r.headers = reply->rawHeaderPairs();
    if (timedOut)
        r.networkError = QStringLiteral("The request timed out.");
    else if (cancelled)
        r.networkError = QStringLiteral("Stopped.");
    else if (r.status == 0)
        r.networkError = reply->errorString();
    reply->deleteLater();
    return r;
}

HttpResponse wait(QNetworkReply *reply, int timeoutMs, const std::function<void(const QByteArray &)> &onData = {},
                  const std::atomic_bool *cancel = nullptr)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false, cancelled = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QByteArray streamed;
    if (onData) {
        // streaming: the timeout is an idle timeout, restarted by every chunk
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray chunk = reply->readAll();
            if (st >= 200 && st < 300)
                onData(chunk);
            else
                streamed += chunk; // an error body: returned whole
            timeout.start(timeoutMs);
        });
    }
    QTimer poll;
    if (cancel) {
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (cancel->load()) { cancelled = true; reply->abort(); } });
        poll.start(100);
    }
    timeout.start(timeoutMs);
    if (!reply->isFinished())
        loop.exec();
    HttpResponse r = finish(reply, timedOut, cancelled);
    if (!streamed.isEmpty())
        r.body = streamed + r.body;
    return r;
}

QThreadPool *netPool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(12);
        p->setExpiryTimeout(60000);
        return p;
    }();
    return pool;
}
thread_local bool t_inNetPool = false;
}

HttpResponse httpGet(const QUrl &url, const Headers &headers, int timeoutMs)
{
    return wait(nam()->get(makeRequest(url, headers)), timeoutMs);
}

HttpResponse httpPost(const QUrl &url, const Headers &headers, const QByteArray &body, int timeoutMs)
{
    return wait(nam()->post(makeRequest(url, headers), body), timeoutMs);
}

HttpResponse httpPostStream(const QUrl &url, const Headers &headers, const QByteArray &body,
                            const std::function<void(const QByteArray &)> &onData, const std::atomic_bool *cancel, int idleTimeoutMs)
{
    return wait(nam()->post(makeRequest(url, headers), body), idleTimeoutMs, onData, cancel);
}

void parallel(const std::vector<std::function<void()>> &tasks)
{
    if (tasks.empty())
        return;
    // already on the pool (a nested Promise.all): run in turn rather than risk waiting on ourselves
    if (t_inNetPool || tasks.size() == 1) {
        std::exception_ptr first;
        for (const auto &t : tasks) {
            try { t(); } catch (...) { if (!first) first = std::current_exception(); }
        }
        if (first)
            std::rethrow_exception(first);
        return;
    }
    QSemaphore done;
    std::mutex m;
    std::exception_ptr first;
    for (const auto &t : tasks) {
        netPool()->start([&done, &m, &first, t] {
            t_inNetPool = true;
            try { t(); } catch (...) { std::lock_guard<std::mutex> g(m); if (!first) first = std::current_exception(); }
            t_inNetPool = false;
            done.release();
        });
    }
    done.acquire(int(tasks.size()));
    if (first)
        std::rethrow_exception(first);
}

} // namespace tl
