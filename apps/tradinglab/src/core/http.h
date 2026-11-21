#pragma once

// Blocking HTTP for the core, which runs on worker threads (upstream awaited fetch() in Electron's main process;
// here a handler thread waits instead). Each thread has its own QNetworkAccessManager.

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include <atomic>
#include <functional>
#include <vector>

namespace tl {

struct HttpResponse
{
    int status = 0;          // 0 when no answer came back
    QByteArray body;
    QString networkError;    // set when the request failed before an HTTP answer
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray header(const QByteArray &name) const
    {
        for (const auto &h : headers)
            if (h.first.compare(name, Qt::CaseInsensitive) == 0)
                return h.second;
        return {};
    }
    bool ok() const { return status >= 200 && status < 300; }
};

using Headers = QList<QPair<QByteArray, QByteArray>>;

HttpResponse httpGet(const QUrl &url, const Headers &headers = {}, int timeoutMs = 30000);
HttpResponse httpPost(const QUrl &url, const Headers &headers, const QByteArray &body, int timeoutMs = 120000);

// A streaming POST (server-sent events): `onData` gets each chunk as it arrives. `cancel` set to true aborts it.
HttpResponse httpPostStream(const QUrl &url, const Headers &headers, const QByteArray &body,
                            const std::function<void(const QByteArray &chunk)> &onData,
                            const std::atomic_bool *cancel = nullptr, int idleTimeoutMs = 300000);

// Promise.all for blocking work: runs every task on the network pool and waits for all of them.
// The first exception thrown by a task is rethrown here after all have finished.
void parallel(const std::vector<std::function<void()>> &tasks);

} // namespace tl
