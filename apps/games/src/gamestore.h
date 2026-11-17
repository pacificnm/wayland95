#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

// A game's saved options and best times (~/.config/win95-wayland/GAME.conf)
class GameStore : public QObject
{
    Q_OBJECT
public:
    explicit GameStore(const QString &game, QObject *parent = nullptr);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

private:
    QSettings m_settings;
};
