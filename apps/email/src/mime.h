#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Internet mail messages (RFC 5322 and MIME): reading what servers send,
// and writing what we send.
namespace Mime {

struct Address {
    QString name;  // "Sam Smith" (may be empty)
    QString email; // "sam@example.com"
    QString display() const { return name.isEmpty() ? email : name; }
    QString full() const; // "Sam Smith <sam@example.com>"
};

struct Attachment {
    QString name;
    QString type;      // "application/pdf"
    QByteArray data;
};

struct Message {
    QString subject;
    QList<Address> from, to, cc, replyTo;
    QDateTime date;
    QString messageId, inReplyTo, references;
    QString text;      // the plain-text body (from the HTML one if that's all there is)
    QString html;      // the HTML body, if any (made safe: no scripts, no pictures from the web)
    bool htmlHadRemoteImages = false;
    QList<Attachment> attachments;
};

// "=?utf-8?B?...?=" and its friends, to text
QString decodeWords(const QByteArray &header);
QList<Address> parseAddresses(const QString &field);
QString formatAddresses(const QList<Address> &list);
// the headers of a message (or just its header block), unfolded:
// lower-case name -> raw value (the first of each)
QMap<QByteArray, QByteArray> parseHeaders(const QByteArray &raw, int *bodyStart = nullptr);
// a whole message
Message parse(const QByteArray &raw);
// a body that's only HTML, as plain text (for replies and the list's preview)
QString htmlToText(const QString &html);

struct Outgoing {
    Address from;
    QList<Address> to, cc, bcc;
    QString subject, text;
    QString inReplyTo, references;
    QList<Attachment> attachments;
};
// the message as sent (Bcc left out of the headers), and its Message-ID
QByteArray build(const Outgoing &m, QString *messageId = nullptr);

QByteArray quotedPrintable(const QByteArray &utf8);
QByteArray encodeWord(const QString &text); // a header value, encoded only when it needs to be

} // namespace Mime
