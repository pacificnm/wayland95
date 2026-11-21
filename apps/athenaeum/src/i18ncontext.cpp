#include "i18ncontext.h"

#include <KLocalizedString>

namespace
{
QString fill(const QString &text, std::initializer_list<QVariant> args)
{
    QStringList list;
    for (const QVariant &v : args) {
        if (!v.isValid())
            break;
        // whole numbers as whole numbers (QML numbers are doubles)
        if (v.typeId() == QMetaType::Double && v.toDouble() == qint64(v.toDouble()))
            list << QString::number(qint64(v.toDouble()));
        else
            list << v.toString();
    }
    return KLocalizedStringShim::substitute(text, list);
}
}

QString I18nContext::i18n(const QString &text, const QVariant &a1, const QVariant &a2, const QVariant &a3, const QVariant &a4, const QVariant &a5) const
{
    return fill(text, {a1, a2, a3, a4, a5});
}

QString I18nContext::i18nc(const QString &, const QString &text, const QVariant &a1, const QVariant &a2, const QVariant &a3, const QVariant &a4) const
{
    return fill(text, {a1, a2, a3, a4});
}

QString I18nContext::i18np(const QString &singular, const QString &plural, const QVariant &n, const QVariant &a2, const QVariant &a3, const QVariant &a4) const
{
    return fill(n.toDouble() == 1 ? singular : plural, {n, a2, a3, a4});
}
