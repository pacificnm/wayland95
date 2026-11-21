// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimschemehandler.h"

#include "articlestore.h"
#include "core/zimreader.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QPalette>
#include "wayland95settings.h"
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

void ZimSchemeHandler::registerScheme()
{
    QWebEngineUrlScheme scheme("zim");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Secure (no mixed-content warnings) and local (web pages cannot load it). LocalAccessAllowed is what lets a zim:
    // page embed zim: images at all: only schemes with it may load from a LocalScheme, and without it every image was
    // silently blocked before reaching this handler (the page itself, a navigation, still loaded). file: stays out of
    // reach: the sanitiser keeps only zim: URLs, the page's CSP allows images from zim: only, and the view sets
    // localContentCanAccessFileUrls to false.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

// pages in the colours of Control Panel > Display > Appearance (a list's, and its selection)
static QPalette win95Palette()
{
    const QVariantMap c = Wayland95Settings::instance()->colors();
    QPalette p = QGuiApplication::palette();
    auto set = [&](QPalette::ColorRole role, const char *key) {
        const QColor color(c.value(QLatin1String(key)).toString());
        if (color.isValid())
            p.setColor(role, color);
    };
    set(QPalette::Base, "window");
    set(QPalette::Text, "windowText");
    set(QPalette::Highlight, "selection");
    // the article's scrollbars are drawn in the 3D colours, like a Win95 window's
    set(QPalette::Button, "face");
    set(QPalette::Light, "highlight");
    set(QPalette::Mid, "shadow");
    set(QPalette::Shadow, "dark");
    set(QPalette::Midlight, "light");
    set(QPalette::ButtonText, "windowText");
    p.setColor(QPalette::Link, QColor(0, 0, 0xc0));
    p.setColor(QPalette::LinkVisited, QColor(0x80, 0, 0x80));
    return p;
}

void ZimSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    auto reply = [job](const QByteArray &mime, const QByteArray &data) {
        auto *buffer = new QBuffer(job); // lives as long as the job
        buffer->setData(data);
        job->reply(mime, buffer);
    };

    qint64 libraryId = 0;
    QString path;
    if (job->requestMethod() != "GET" || !ArticleStore::parse(job->requestUrl(), &libraryId, &path)) {
        job->fail(QWebEngineUrlRequestJob::UrlInvalid);
        return;
    }
    const QPalette palette = win95Palette();
    // a redirect (the ZIM's own, or an HTML page forwarding to a section): show the real address, so Back, the
    // breadcrumb and Recently read all name the article that is actually on screen
    if (const auto content = ZimReader::content(libraryId, path); content && (content->path != path || !content->fragment.isEmpty())) {
        job->redirect(ArticleStore::url(libraryId, content->path, content->fragment));
        return;
    }
    if (const auto article = ArticleStore::article(libraryId, path)) {
        reply("text/html;charset=utf-8", ArticleStore::page(*article, palette));
        return;
    }
    const auto content = ZimReader::content(libraryId, path);
    if (!content) {
        reply("text/html;charset=utf-8", ArticleStore::notFoundPage(libraryId, path, palette));
        return;
    }
    if (content->mimetype.startsWith(QLatin1String("text/html"))) { // HTML that is not an article (a custom main page)
        reply("text/html;charset=utf-8", ArticleStore::notFoundPage(libraryId, path, palette));
        return;
    }
    if (content->mimetype.startsWith(QLatin1String("image/"))) {
        reply(content->mimetype.toUtf8(), content->data);
        return;
    }
    job->fail(QWebEngineUrlRequestJob::RequestDenied); // the ZIM's scripts, stylesheets, raw HTML
}
