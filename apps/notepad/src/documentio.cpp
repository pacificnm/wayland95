#include "documentio.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QStringDecoder>

namespace {
constexpr qint64 kMaxFileSize = 32 * 1024 * 1024; // a text editor, not a hex viewer
}

DocumentIO::DocumentIO(QObject *parent)
    : QObject(parent)
{
}

QVariantMap DocumentIO::load(const QString &path) const
{
    QVariantMap result;
    const QFileInfo info(path);
    const QString name = info.fileName();
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("path")] = info.absoluteFilePath();

    if (!info.exists()) {
        result[QStringLiteral("error")] = QStringLiteral("Cannot find the %1 file.").arg(name);
        return result;
    }
    if (info.size() > kMaxFileSize) {
        result[QStringLiteral("error")] = QStringLiteral("The %1 file is too large for Notepad.").arg(name);
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result[QStringLiteral("error")] = QStringLiteral("Cannot open the %1 file.\n\n%2").arg(name, file.errorString());
        return result;
    }
    const QByteArray bytes = file.readAll();

    // NUL bytes mean binary; saving the decoded text would corrupt it
    if (bytes.left(8192).contains('\0')) {
        result[QStringLiteral("error")] =
            QStringLiteral("%1 is not a text file, so Notepad can't open it.").arg(name);
        return result;
    }

    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError())
        text = QString::fromLatin1(bytes);

    const bool crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("text")] = text;
    result[QStringLiteral("crlf")] = crlf;
    return result;
}

QString DocumentIO::save(const QString &path, const QString &text, bool crlf) const
{
    QString out = text;
    if (crlf)
        out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    // write to a temp file and rename over the original, so a failed save
    // never leaves a half-written document behind
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot create the %1 file.\n\n%2").arg(QFileInfo(path).fileName(), file.errorString());
    file.write(out.toUtf8());
    if (!file.commit())
        return QStringLiteral("Cannot save the %1 file.\n\n%2").arg(QFileInfo(path).fileName(), file.errorString());
    return {};
}

QString DocumentIO::timeDate() const
{
    // Win95 Notepad's F5 format: time then date, e.g. "10:44 PM 11/14/2026"
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale = QLocale::system();
    return locale.toString(now.time(), QLocale::ShortFormat) + QLatin1Char(' ')
        + locale.toString(now.date(), QLocale::ShortFormat);
}
