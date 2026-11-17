#include "mime.h"

#include <QHostInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextDocument>
#include <QUuid>

namespace Mime {

namespace {

QString decodeCharset(const QByteArray &bytes, const QByteArray &charset)
{
    QByteArray cs = charset.trimmed().toLower();
    if (cs.isEmpty() || cs == "us-ascii" || cs == "ascii")
        cs = "utf-8";
    // a common mislabel: Windows-1252 sent as ISO-8859-1
    if (cs == "iso-8859-1" || cs == "latin1")
        cs = "windows-1252";
    QStringDecoder dec(cs.constData());
    if (!dec.isValid())
        dec = QStringDecoder(QStringDecoder::Utf8);
    QString s = dec.decode(bytes);
    if (dec.hasError() && cs == "utf-8")
        s = QStringDecoder("windows-1252").decode(bytes);
    return s;
}

QByteArray fromQuotedPrintable(const QByteArray &in, bool header)
{
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c == '=' && i + 2 < in.size() + 0 && i + 1 < in.size()) {
            if (in.at(i + 1) == '\r' || in.at(i + 1) == '\n') {
                // a soft line break
                ++i;
                if (in.at(i) == '\r' && i + 1 < in.size() && in.at(i + 1) == '\n')
                    ++i;
                continue;
            }
            if (i + 2 < in.size()) {
                bool ok = false;
                const int v = in.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    out += char(v);
                    i += 2;
                    continue;
                }
            }
            out += c;
        } else if (header && c == '_') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

// key=value; key2="value 2" -> map (keys lower-case), with RFC 2231's
// name*=utf-8''... and name*0*=... pieces put together
QMap<QByteArray, QString> parameters(const QByteArray &value, QByteArray *main)
{
    QMap<QByteArray, QString> out;
    QMap<QByteArray, QByteArray> pieces; // name*N[*] -> raw
    QList<QByteArray> parts;
    QByteArray cur;
    bool quoted = false;
    for (char c : value) {
        if (c == '"')
            quoted = !quoted;
        if (c == ';' && !quoted) {
            parts << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts << cur;
    if (main)
        *main = parts.value(0).trimmed().toLower();
    for (int i = 1; i < parts.size(); ++i) {
        const QByteArray p = parts.at(i).trimmed();
        const int eq = p.indexOf('=');
        if (eq <= 0)
            continue;
        QByteArray key = p.left(eq).trimmed().toLower();
        QByteArray v = p.mid(eq + 1).trimmed();
        if (v.startsWith('"') && v.endsWith('"') && v.size() >= 2)
            v = v.mid(1, v.size() - 2).replace("\\\"", "\"").replace("\\\\", "\\");
        if (key.contains('*'))
            pieces.insert(key, v);
        else
            out.insert(key, decodeWords(v));
    }
    // name*=charset'lang'percent-encoded, or name*0*= name*1*= ...
    QMap<QByteArray, QList<QPair<int, QByteArray>>> grouped;
    for (auto it = pieces.cbegin(); it != pieces.cend(); ++it) {
        const QList<QByteArray> k = it.key().split('*');
        const int n = k.size() > 1 && !k.at(1).isEmpty() ? k.at(1).toInt() : 0;
        grouped[k.at(0)] << qMakePair(n, it.key().endsWith('*') ? QByteArray("*") + it.value() : it.value());
    }
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        QByteArray charset = "utf-8", joined;
        bool first = true;
        for (const auto &piece : *it) {
            QByteArray v = piece.second;
            const bool encoded = v.startsWith('*');
            if (encoded)
                v = v.mid(1);
            if (encoded && first) {
                const QList<QByteArray> cl = v.split('\'');
                if (cl.size() >= 3) {
                    charset = cl.at(0);
                    v = cl.mid(2).join('\'');
                }
            }
            joined += encoded ? QByteArray::fromPercentEncoding(v) : v;
            first = false;
        }
        out.insert(it.key(), decodeCharset(joined, charset));
    }
    return out;
}

QByteArray decodeBody(const QByteArray &body, const QByteArray &transfer)
{
    const QByteArray t = transfer.trimmed().toLower();
    if (t == "base64")
        return QByteArray::fromBase64(QByteArray(body).replace("\r", "").replace("\n", "").replace(" ", ""));
    if (t == "quoted-printable")
        return fromQuotedPrintable(body, false);
    return body;
}

// pictures from the web track when and where mail is read: left out, and
// scripts, styles and forms with them
QString safeHtml(const QString &html, bool *hadRemote)
{
    QString s = html;
    static const QRegularExpression scripts(QStringLiteral("<(script|style|iframe|object|embed|form)\\b[^>]*>.*?</\\1\\s*>"),
                                            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    s.remove(scripts);
    static const QRegularExpression remoteImg(QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*[\"']?\\s*(https?:|//)[^>]*>"),
                                              QRegularExpression::CaseInsensitiveOption);
    if (hadRemote)
        *hadRemote = s.contains(remoteImg);
    s.remove(remoteImg);
    static const QRegularExpression events(QStringLiteral("\\son[a-z]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    s.remove(events);
    return s;
}

void walk(const QByteArray &raw, Message &m, int depth)
{
    if (depth > 20)
        return;
    int bodyStart = 0;
    const QMap<QByteArray, QByteArray> h = parseHeaders(raw, &bodyStart);
    const QByteArray body = raw.mid(bodyStart);
    QByteArray type;
    const QMap<QByteArray, QString> params = parameters(h.value("content-type", "text/plain"), &type);
    if (type.isEmpty())
        type = "text/plain";
    QByteArray disposition;
    const QMap<QByteArray, QString> dparams = parameters(h.value("content-disposition"), &disposition);
    const QString filename = dparams.value("filename", params.value("name"));

    if (type.startsWith("multipart/")) {
        const QByteArray boundary = params.value("boundary").toUtf8();
        if (boundary.isEmpty())
            return;
        const QByteArray delim = "--" + boundary;
        QList<QByteArray> parts;
        int pos = body.indexOf(delim);
        while (pos >= 0) {
            int start = body.indexOf('\n', pos);
            if (start < 0)
                break;
            ++start;
            if (body.mid(pos + delim.size(), 2) == "--")
                break; // the closing one
            int next = body.indexOf(delim, start);
            if (next < 0)
                next = int(body.size());
            QByteArray part = body.mid(start, next - start);
            // (the line break before the delimiter belongs to it)
            if (part.endsWith("\r\n"))
                part.chop(2);
            else if (part.endsWith('\n'))
                part.chop(1);
            parts << part;
            pos = next < body.size() ? next : -1;
        }
        if (type == "multipart/alternative") {
            // the last one we can show is the best; we keep text and HTML both
            for (const QByteArray &p : std::as_const(parts))
                walk(p, m, depth + 1);
            return;
        }
        for (const QByteArray &p : std::as_const(parts))
            walk(p, m, depth + 1);
        return;
    }

    const QByteArray data = decodeBody(body, h.value("content-transfer-encoding"));
    const bool attachment = disposition == "attachment" || (!filename.isEmpty() && !type.startsWith("text/"));
    if (!attachment && type == "text/plain" && m.text.isEmpty()) {
        m.text = decodeCharset(data, params.value("charset").toLatin1());
        return;
    }
    if (!attachment && type == "text/html" && m.html.isEmpty()) {
        m.html = decodeCharset(data, params.value("charset").toLatin1());
        return;
    }
    if (type == "message/rfc822" && !attachment) {
        // a forwarded message: an attachment of its own
    }
    Attachment a;
    a.type = QString::fromLatin1(type);
    a.data = data;
    a.name = filename;
    if (a.name.isEmpty()) {
        if (type == "message/rfc822") {
            a.name = Mime::parse(data).subject;
            if (a.name.isEmpty())
                a.name = QStringLiteral("message");
            a.name += QStringLiteral(".eml");
        } else {
            a.name = QStringLiteral("attachment");
        }
    }
    m.attachments << a;
}

} // namespace

QString Address::full() const
{
    if (name.isEmpty())
        return email;
    QString n = name;
    if (n.contains(QRegularExpression(QStringLiteral("[,;<>@\"()]"))))
        n = QLatin1Char('"') + n.replace(QLatin1Char('"'), QLatin1String("\\\"")) + QLatin1Char('"');
    return n + QStringLiteral(" <") + email + QLatin1Char('>');
}

QString decodeWords(const QByteArray &header)
{
    static const QRegularExpression word(QStringLiteral("=\\?([^?]+)\\?([bBqQ])\\?([^?]*)\\?="));
    const QString h = QString::fromLatin1(header);
    QString out;
    int last = 0;
    bool prevWasWord = false;
    auto it = word.globalMatch(h);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString between = h.mid(last, m.capturedStart() - last);
        // white space between two encoded words is dropped
        if (!(prevWasWord && between.trimmed().isEmpty()))
            out += decodeCharset(between.toLatin1(), "utf-8");
        QByteArray charset = m.captured(1).toLatin1();
        if (const int star = int(charset.indexOf('*')); star >= 0)
            charset = charset.left(star); // (=?utf-8*en?...)
        const QByteArray text = m.captured(3).toLatin1();
        const QByteArray bytes = m.captured(2).toLower() == QLatin1String("b") ? QByteArray::fromBase64(text)
                                                                               : fromQuotedPrintable(text, true);
        out += decodeCharset(bytes, charset);
        last = int(m.capturedEnd());
        prevWasWord = true;
    }
    out += decodeCharset(h.mid(last).toLatin1(), "utf-8");
    return out.trimmed();
}

QList<Address> parseAddresses(const QString &field)
{
    QList<Address> out;
    QStringList items;
    QString cur;
    bool quoted = false;
    int angle = 0;
    for (QChar c : field) {
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        else if (c == QLatin1Char('<') && !quoted)
            ++angle;
        else if (c == QLatin1Char('>') && !quoted)
            angle = qMax(0, angle - 1);
        if ((c == QLatin1Char(',') || c == QLatin1Char(';')) && !quoted && angle == 0) {
            items << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    items << cur;
    for (QString item : std::as_const(items)) {
        item = item.trimmed();
        if (item.isEmpty())
            continue;
        Address a;
        const int lt = int(item.lastIndexOf(QLatin1Char('<')));
        const int gt = int(item.lastIndexOf(QLatin1Char('>')));
        if (lt >= 0 && gt > lt) {
            a.email = item.mid(lt + 1, gt - lt - 1).trimmed();
            a.name = item.left(lt).trimmed();
        } else {
            a.email = item;
        }
        if (a.name.startsWith(QLatin1Char('"')) && a.name.endsWith(QLatin1Char('"')) && a.name.size() >= 2)
            a.name = a.name.mid(1, a.name.size() - 2).replace(QLatin1String("\\\""), QLatin1String("\""));
        // a group's name ("undisclosed-recipients:;") isn't an address
        if (a.email.endsWith(QLatin1Char(':')) || (!a.email.contains(QLatin1Char('@')) && a.name.isEmpty() && a.email.contains(QLatin1Char(':'))))
            continue;
        out << a;
    }
    return out;
}

QString formatAddresses(const QList<Address> &list)
{
    QStringList s;
    for (const Address &a : list)
        s << a.full();
    return s.join(QStringLiteral(", "));
}

QMap<QByteArray, QByteArray> parseHeaders(const QByteArray &raw, int *bodyStart)
{
    QMap<QByteArray, QByteArray> out;
    int end = int(raw.indexOf("\r\n\r\n"));
    int sep = 4;
    const int lf = int(raw.indexOf("\n\n"));
    if (end < 0 || (lf >= 0 && lf < end)) {
        end = lf;
        sep = 2;
    }
    if (end < 0) {
        end = int(raw.size());
        sep = 0;
    }
    if (bodyStart)
        *bodyStart = qMin(int(raw.size()), end + sep);
    QByteArray name, value;
    auto flush = [&]() {
        if (!name.isEmpty() && !out.contains(name))
            out.insert(name, value.trimmed());
        name.clear();
        value.clear();
    };
    for (QByteArray line : raw.left(end).split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if ((line.at(0) == ' ' || line.at(0) == '\t') && !name.isEmpty()) {
            value += ' ' + line.trimmed();
            continue;
        }
        flush();
        const int colon = int(line.indexOf(':'));
        if (colon <= 0)
            continue;
        name = line.left(colon).trimmed().toLower();
        value = line.mid(colon + 1);
    }
    flush();
    return out;
}

Message parse(const QByteArray &raw)
{
    Message m;
    const QMap<QByteArray, QByteArray> h = parseHeaders(raw);
    m.subject = decodeWords(h.value("subject"));
    m.from = parseAddresses(decodeWords(h.value("from")));
    m.to = parseAddresses(decodeWords(h.value("to")));
    m.cc = parseAddresses(decodeWords(h.value("cc")));
    m.replyTo = parseAddresses(decodeWords(h.value("reply-to")));
    QString date = QString::fromLatin1(h.value("date")).trimmed();
    // "Tue, 17 Nov 2026 09:30:00 -0800 (PST)": Qt wants it without the comment
    date.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)\\s*$")));
    m.date = QDateTime::fromString(date, Qt::RFC2822Date);
    m.messageId = QString::fromLatin1(h.value("message-id")).trimmed();
    m.inReplyTo = QString::fromLatin1(h.value("in-reply-to")).trimmed();
    m.references = QString::fromLatin1(h.value("references")).trimmed();
    walk(raw, m, 0);
    if (!m.html.isEmpty())
        m.html = safeHtml(m.html, &m.htmlHadRemoteImages);
    if (m.text.isEmpty() && !m.html.isEmpty())
        m.text = htmlToText(m.html);
    return m;
}

QString htmlToText(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QByteArray quotedPrintable(const QByteArray &utf8)
{
    QByteArray out;
    int lineLen = 0;
    auto put = [&](const QByteArray &s) {
        if (lineLen + s.size() > 75) {
            out += "=\r\n";
            lineLen = 0;
        }
        out += s;
        lineLen += int(s.size());
    };
    for (int i = 0; i < utf8.size(); ++i) {
        const uchar c = uchar(utf8.at(i));
        if (c == '\n') {
            // a space at a line's end would be lost
            if (out.endsWith(' ') || out.endsWith('\t')) {
                const char last = out.at(out.size() - 1);
                out.chop(1);
                out += last == ' ' ? "=20" : "=09";
            }
            out += "\r\n";
            lineLen = 0;
            continue;
        }
        if (c == '\r')
            continue;
        // a line starting with "." or "From " survives every server as =2E / =46
        const bool lineStart = lineLen == 0;
        if ((c >= 33 && c <= 126 && c != '=' && !(lineStart && (c == '.' || c == 'F'))) || c == ' ' || c == '\t')
            put(QByteArray(1, char(c)));
        else
            put("=" + QByteArray::number(c, 16).toUpper().rightJustified(2, '0'));
    }
    return out;
}

QByteArray encodeWord(const QString &text)
{
    bool ascii = true;
    for (QChar c : text)
        if (c.unicode() > 126 || (c.unicode() < 32 && c != QLatin1Char('\t')))
            ascii = false;
    if (ascii)
        return text.toLatin1();
    // B encoding, in pieces short enough for a header line
    QByteArray out;
    QString chunk;
    auto flush = [&]() {
        if (chunk.isEmpty())
            return;
        if (!out.isEmpty())
            out += "\r\n ";
        out += "=?UTF-8?B?" + chunk.toUtf8().toBase64() + "?=";
        chunk.clear();
    };
    for (QChar c : text) {
        chunk += c;
        if (chunk.toUtf8().size() >= 45)
            flush();
    }
    flush();
    return out;
}

namespace {
QByteArray addressHeader(const QList<Address> &list)
{
    QList<QByteArray> parts;
    for (const Address &a : list) {
        if (a.name.isEmpty()) {
            parts << a.email.toUtf8();
            continue;
        }
        QByteArray name = encodeWord(a.name);
        if (name == a.name.toLatin1() && a.name.contains(QRegularExpression(QStringLiteral("[,;<>@\"().:]"))))
            name = '"' + QByteArray(name).replace("\\", "\\\\").replace("\"", "\\\"") + '"';
        parts << name + " <" + a.email.toUtf8() + ">";
    }
    return parts.join(", ");
}

QByteArray wrapBase64(const QByteArray &data)
{
    const QByteArray b = data.toBase64();
    QByteArray out;
    for (int i = 0; i < b.size(); i += 76)
        out += b.mid(i, 76) + "\r\n";
    return out;
}
} // namespace

QByteArray build(const Outgoing &m, QString *messageId)
{
    const QString domain = m.from.email.section(QLatin1Char('@'), 1).isEmpty() ? QStringLiteral("localhost")
                                                                                : m.from.email.section(QLatin1Char('@'), 1);
    const QString id = QStringLiteral("<%1@%2>").arg(QUuid::createUuid().toString(QUuid::WithoutBraces), domain);
    if (messageId)
        *messageId = id;
    QByteArray out;
    out += "From: " + addressHeader({m.from}) + "\r\n";
    if (!m.to.isEmpty())
        out += "To: " + addressHeader(m.to) + "\r\n";
    if (!m.cc.isEmpty())
        out += "Cc: " + addressHeader(m.cc) + "\r\n";
    out += "Subject: " + encodeWord(m.subject) + "\r\n";
    out += "Date: " + QLocale::c().toString(QDateTime::currentDateTime(), QStringLiteral("ddd, dd MMM yyyy hh:mm:ss ")).toLatin1();
    {
        const int offset = QDateTime::currentDateTime().offsetFromUtc() / 60;
        out += QString::asprintf("%c%02d%02d", offset < 0 ? '-' : '+', qAbs(offset) / 60, qAbs(offset) % 60).toLatin1() + "\r\n";
    }
    out += "Message-ID: " + id.toLatin1() + "\r\n";
    if (!m.inReplyTo.isEmpty())
        out += "In-Reply-To: " + m.inReplyTo.toLatin1() + "\r\n";
    if (!m.references.isEmpty())
        out += "References: " + m.references.toLatin1() + "\r\n";
    out += "MIME-Version: 1.0\r\n";
    out += "User-Agent: Wayland 95 Email\r\n";
    const QByteArray textPart = "Content-Type: text/plain; charset=utf-8\r\n"
                                "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
        + quotedPrintable(m.text.toUtf8()) + "\r\n";
    if (m.attachments.isEmpty()) {
        out += textPart;
        return out;
    }
    const QByteArray boundary = "=_w95_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    out += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n";
    out += "This is a message with attachments.\r\n";
    out += "--" + boundary + "\r\n" + textPart;
    for (const Attachment &a : m.attachments) {
        const QByteArray name = encodeWord(a.name);
        out += "--" + boundary + "\r\n";
        out += "Content-Type: " + (a.type.isEmpty() ? QByteArray("application/octet-stream") : a.type.toLatin1())
            + "; name=\"" + name + "\"\r\n";
        out += "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
        out += "Content-Transfer-Encoding: base64\r\n\r\n";
        out += wrapBase64(a.data);
    }
    out += "--" + boundary + "--\r\n";
    return out;
}

} // namespace Mime
