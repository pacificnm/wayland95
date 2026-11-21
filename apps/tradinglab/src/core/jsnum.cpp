// JavaScript number formatting/parsing helpers. See jsnum.h.

#include "jsnum.h"

#include <QLocale>
#include <QRegularExpression>

#include <cmath>

namespace tl {

QString jsString(double n)
{
    if (std::isnan(n))
        return QStringLiteral("NaN");
    if (std::isinf(n))
        return n > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (n == 0)
        return QStringLiteral("0");
    const QString sign = n < 0 ? QStringLiteral("-") : QString();
    // shortest digits that round-trip, as d.ddde±x
    const QString e = QString::number(std::abs(n), 'e', QLocale::FloatingPointShortest);
    const qsizetype ePos = e.indexOf(QLatin1Char('e'));
    QString digits = e.left(ePos);
    digits.remove(QLatin1Char('.'));
    while (digits.size() > 1 && digits.endsWith(QLatin1Char('0')))
        digits.chop(1);
    const int exp = e.mid(ePos + 1).toInt();
    const int k = int(digits.size());
    const int pt = exp + 1; // ECMAScript's n: the decimal point sits after `pt` digits
    if (k <= pt && pt <= 21)
        return sign + digits + QString(pt - k, QLatin1Char('0'));
    if (0 < pt && pt <= 21)
        return sign + digits.left(pt) + QLatin1Char('.') + digits.mid(pt);
    if (-6 < pt && pt <= 0)
        return sign + QStringLiteral("0.") + QString(-pt, QLatin1Char('0')) + digits;
    const QString expStr = (pt - 1 >= 0 ? QStringLiteral("e+") : QStringLiteral("e-")) + QString::number(std::abs(pt - 1));
    if (k == 1)
        return sign + digits + expStr;
    return sign + digits.left(1) + QLatin1Char('.') + digits.mid(1) + expStr;
}

double jsParseFloat(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([+-]?(?:Infinity|(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?))"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return NAN;
    QString t = m.captured(1);
    if (t.endsWith(QStringLiteral("Infinity")))
        return t.startsWith(QLatin1Char('-')) ? -INFINITY : INFINITY;
    return t.toDouble();
}

double jsParseInt(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([+-]?\\d+)"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return NAN;
    return m.captured(1).toDouble();
}

} // namespace tl
