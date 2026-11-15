#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Loading and saving Notepad documents. Exposed to QML as `DocumentIO`.
//
// Files are decoded as UTF-8, falling back to Latin-1 for older 8-bit
// files. Windows (CRLF) line endings are turned into \n for editing and
// put back on save, so a file keeps the line endings it came with.
class DocumentIO : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit DocumentIO(QObject *parent = nullptr);

    // { ok, path, text, crlf, error }
    Q_INVOKABLE QVariantMap load(const QString &path) const;
    // "" on success, otherwise a message for the user
    Q_INVOKABLE QString save(const QString &path, const QString &text, bool crlf) const;
    Q_INVOKABLE QString timeDate() const; // for Edit > Time/Date
};
