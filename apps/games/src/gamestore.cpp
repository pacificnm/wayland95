#include "gamestore.h"

GameStore::GameStore(const QString &game, QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("win95-wayland"), game)
{
}

QVariant GameStore::value(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

void GameStore::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
}
