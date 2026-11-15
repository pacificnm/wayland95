#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// The installed fonts, from fontconfig (the same list every program
// sees), plus installing and deleting the user's own fonts.
//
// Fonts you install go into ~/.local/share/fonts (no password: they're
// yours); the system's own fonts come with its packages and stay.
//
// Exposed to QML as the singleton `Fonts`.
class FontCatalog : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Fonts)
    QML_SINGLETON

    // every font face: [{ key, family, style, name, file, index, format,
    // kind ("truetype"/"opentype"/"other"), size, modified, version, mine }]
    Q_PROPERTY(QVariantList fonts READ fonts NOTIFY fontsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString userFontDir READ userFontDir CONSTANT)

public:
    explicit FontCatalog(QObject *parent = nullptr);

    QVariantList fonts() const { return m_fonts; }
    bool busy() const { return m_busy; }
    QString userFontDir() const;

    Q_INVOKABLE void reload();
    // the faces in the font files of a folder (for Install New Font):
    // [{ name, family, style, file, kind, installed }]
    Q_INVOKABLE QVariantList fontsInFolder(const QString &dir) const;
    // copies the files into the user's font folder; answers with
    // installFinished. Returns "" or why it can't.
    Q_INVOKABLE QString install(const QStringList &files);
    // the user's own fonts only: into the Recycle Bin
    Q_INVOKABLE QString remove(const QStringList &files);
    // loads a font file for previewing exactly that face; the family name
    // to use in QML ("" if it can't be loaded)
    Q_INVOKABLE QString previewFamily(const QString &file);

signals:
    void fontsChanged();
    void busyChanged();
    void installFinished(int count);

private:
    void refreshCache(int installed);

    QVariantList m_fonts;
    bool m_busy = false;
    QHash<QString, QString> m_previewFamilies; // file -> family
};
