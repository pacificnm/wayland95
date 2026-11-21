#include "KLocalizedString"

namespace KLocalizedStringShim
{
// %1..%99 replaced by the arguments in one pass (so an argument holding "%2"
// stays as it is); %% is a percent sign
QString substitute(const QString &text, const QStringList &args)
{
    QString out;
    out.reserve(text.size() + 16);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('%') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('%')) {
                out += c;
                ++i;
                continue;
            }
            if (text.at(i + 1).isDigit()) {
                qsizetype j = i + 1;
                int n = 0;
                while (j < text.size() && text.at(j).isDigit() && j < i + 3)
                    n = n * 10 + text.at(j++).digitValue();
                if (n >= 1 && n <= args.size()) {
                    out += args.at(n - 1);
                    i = j - 1;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}
}
