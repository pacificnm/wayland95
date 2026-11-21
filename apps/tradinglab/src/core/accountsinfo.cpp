// Paper account helpers. Port of shared/accounts.ts.

#include "accountsinfo.h"

#include <QUrl>

namespace tl {

Rec accountTypeLabels()
{
    return Rec{{QStringLiteral("cash"), QStringLiteral("Cash (no margin, no short)")}, {QStringLiteral("margin"), QStringLiteral("Margin (2:1, can short)")}};
}

QString accountTypeLabel(const QString &type) { return accountTypeLabels().value(type).toString(); }

double buyingPowerFor(const QString &type, double equity, double cash, double gross)
{
    return std::max(0.0, type == QStringLiteral("cash") ? cash : MARGIN_MULTIPLE * equity - gross);
}

QStringList brokers()
{
    return {QStringLiteral("Charles Schwab"), QStringLiteral("Fidelity"), QStringLiteral("Interactive Brokers"), QStringLiteral("Robinhood"),
            QStringLiteral("E*TRADE"), QStringLiteral("Webull"), QStringLiteral("Vanguard"), QStringLiteral("Tastytrade"),
            QStringLiteral("Merrill"), QStringLiteral("Ally Invest"), QStringLiteral("Public"), QStringLiteral("Firstrade")};
}

QString checkAccountInput(const Rec &i)
{
    if (i.contains(QStringLiteral("name"))) {
        const QString name = i.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || name.size() > 60)
            return QStringLiteral("Give the account a name (60 characters or fewer).");
    }
    if (i.contains(QStringLiteral("type"))) {
        const QString type = i.value(QStringLiteral("type")).toString();
        if (type != QStringLiteral("cash") && type != QStringLiteral("margin"))
            return QStringLiteral("The account type must be cash or margin.");
    }
    if (i.contains(QStringLiteral("broker")) && i.value(QStringLiteral("broker")).toString().trimmed().size() > 60)
        return QStringLiteral("The brokerage name is too long.");
    if (i.contains(QStringLiteral("url"))) {
        const QString url = i.value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty()) {
            // new URL(): needs a scheme, and http(s) needs a host
            const QUrl u(url, QUrl::StrictMode);
            const QString scheme = u.scheme().toLower();
            const bool web = scheme == QStringLiteral("https") || scheme == QStringLiteral("http");
            if (!u.isValid() || scheme.isEmpty() || u.isRelative() || (web && u.host().isEmpty()))
                return QStringLiteral("The link is not a valid web address.");
            if (!web)
                return QStringLiteral("The link must start with https://");
        }
    }
    return QString();
}

} // namespace tl
