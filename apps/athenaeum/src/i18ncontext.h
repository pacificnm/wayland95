#pragma once

#include <QObject>
#include <QVariant>

// i18n(), i18nc() and i18np() for QML, as KDE's localized context gave
// them: English only, so the text is itself with %1, %2... filled in
class I18nContext : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE QString i18n(const QString &text, const QVariant &a1 = {}, const QVariant &a2 = {}, const QVariant &a3 = {},
                             const QVariant &a4 = {}, const QVariant &a5 = {}) const;
    Q_INVOKABLE QString i18nc(const QString &context, const QString &text, const QVariant &a1 = {}, const QVariant &a2 = {},
                              const QVariant &a3 = {}, const QVariant &a4 = {}) const;
    Q_INVOKABLE QString i18np(const QString &singular, const QString &plural, const QVariant &n, const QVariant &a2 = {},
                              const QVariant &a3 = {}, const QVariant &a4 = {}) const;
};
