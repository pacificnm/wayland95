// Position sizing and risk/reward math. Port of shared/position.ts.

#include "position.h"

#include "jsnum.h"

#include <QStringList>

#include <algorithm>
#include <vector>

namespace tl {

namespace {

const QString kMode = QStringLiteral("mode");

// a number field as JS arithmetic sees it (undefined -> NaN)
double numOf(const Rec &r, const QString &key) { return toNumber(r.value(key)); }
// a `number | null` field
std::optional<double> nullable(const Rec &r, const QString &key)
{
    const QVariant v = r.value(key);
    if (isNull(v))
        return std::nullopt;
    return toNumber(v);
}
// `typeof n === 'number' && Number.isFinite(n)`, then the number
const auto fin = [](const QVariant &v) { return isNumber(v); };

struct Sizes
{
    double byRisk = 0, byPosition = 0, byTotal = 0;
    std::optional<double> byBuyingPower;
    double shares = 0;
    QString binding; // empty = null
};

} // namespace

Rules Rules::fromRec(const Rec &r)
{
    Rules out;
    auto take = [&](const char *key, double &field) {
        const QVariant v = r.value(QLatin1String(key));
        if (v.isValid())
            field = toNumber(v);
    };
    take("maxRiskPct", out.maxRiskPct);
    take("maxPositionPct", out.maxPositionPct);
    if (r.contains(QStringLiteral("maxTotalMode")))
        out.maxTotalMode = r.value(QStringLiteral("maxTotalMode")).toString();
    take("maxTotalPct", out.maxTotalPct);
    take("maxTotalDollars", out.maxTotalDollars);
    return out;
}

Rec Rules::toRec() const
{
    return Rec{{QStringLiteral("maxRiskPct"), maxRiskPct}, {QStringLiteral("maxPositionPct"), maxPositionPct},
               {QStringLiteral("maxTotalMode"), maxTotalMode}, {QStringLiteral("maxTotalPct"), maxTotalPct},
               {QStringLiteral("maxTotalDollars"), maxTotalDollars}};
}

Rec calculate(const Rec &input, const Rules &rules)
{
    QStringList errors, warnings;
    const QString side = input.value(QStringLiteral("side")).toString();
    const double entry = numOf(input, QStringLiteral("entry"));
    const double account = numOf(input, QStringLiteral("account"));
    const bool isLong = side == QStringLiteral("long");
    const double sgn = isLong ? 1 : -1;
    const std::optional<double> atr = nullable(input, QStringLiteral("atr"));
    const std::optional<double> buyingPower = nullable(input, QStringLiteral("buyingPower"));
    const std::optional<double> winRate = nullable(input, QStringLiteral("winRate"));
    const double commission = numOf(input, QStringLiteral("commission"));
    const double investedNow = numOf(input, QStringLiteral("investedNow"));

    if (!(entry > 0))
        errors << QStringLiteral("Enter an entry price above zero.");
    if (!(account > 0))
        errors << QStringLiteral("Enter an account size above zero.");

    // stop
    std::optional<double> stop;
    const Rec st = input.value(QStringLiteral("stop")).toMap();
    const QString stMode = st.value(kMode).toString();
    if (stMode == QStringLiteral("price")) {
        const QVariant p = st.value(QStringLiteral("price"));
        if (fin(p) && p.toDouble() > 0)
            stop = p.toDouble();
    } else if (stMode == QStringLiteral("percent")) {
        const QVariant p = st.value(QStringLiteral("pct"));
        if (fin(p) && p.toDouble() > 0)
            stop = entry * (1 - (sgn * p.toDouble()) / 100);
    } else if (!atr) {
        errors << QStringLiteral("ATR is not available for this symbol, so an ATR-based stop cannot be computed.");
    } else {
        const QVariant m = st.value(QStringLiteral("mult"));
        if (fin(m) && m.toDouble() > 0)
            stop = entry - sgn * m.toDouble() * *atr;
    }
    if (!stop && !std::any_of(errors.cbegin(), errors.cend(), [](const QString &e) { return e.contains(QStringLiteral("ATR")); }))
        errors << QStringLiteral("Enter a stop.");
    if (stop && (isLong ? *stop >= entry : *stop <= entry))
        errors << QStringLiteral("For a %1 trade the stop must be %2 the entry (%3).")
                      .arg(side, isLong ? QStringLiteral("below") : QStringLiteral("above"), fixed(entry, 2));
    if (stop && *stop <= 0)
        errors << QStringLiteral("The stop works out to a price at or below zero; use a tighter stop.");

    // target
    std::optional<double> target;
    std::optional<double> riskPerShare;
    if (stop && *stop > 0)
        riskPerShare = std::abs(entry - *stop);
    const QVariant tgv = input.value(QStringLiteral("target"));
    if (!isNull(tgv)) {
        const Rec tg = tgv.toMap();
        const QString tgMode = tg.value(kMode).toString();
        if (tgMode == QStringLiteral("price")) {
            const QVariant p = tg.value(QStringLiteral("price"));
            if (fin(p) && p.toDouble() > 0)
                target = p.toDouble();
        } else if (tgMode == QStringLiteral("percent")) {
            const QVariant p = tg.value(QStringLiteral("pct"));
            if (fin(p) && p.toDouble() > 0)
                target = entry * (1 + (sgn * p.toDouble()) / 100);
        } else if (riskPerShare) {
            const QVariant r = tg.value(QStringLiteral("r"));
            if (fin(r) && r.toDouble() > 0)
                target = entry + sgn * r.toDouble() * *riskPerShare;
        }
        if (target && (isLong ? *target <= entry : *target >= entry))
            errors << QStringLiteral("For a %1 trade the target must be %2 the entry.").arg(side, isLong ? QStringLiteral("above") : QStringLiteral("below"));
    }

    const double maxPositionDollars = (account * rules.maxPositionPct) / 100;
    const double maxTotalDollars = rules.maxTotalMode == QStringLiteral("percent") ? (account * rules.maxTotalPct) / 100 : rules.maxTotalDollars;
    const double remainingTotalDollars = std::max(0.0, maxTotalDollars - investedNow);

    const Rec risk = input.value(QStringLiteral("risk")).toMap();
    const double requested = risk.value(kMode).toString() == QStringLiteral("percent")
        ? (account * toNumber(risk.value(QStringLiteral("pct")))) / 100
        : toNumber(risk.value(QStringLiteral("dollars")));
    const bool requestedFin = std::isfinite(requested);
    const double ruleCap = (account * rules.maxRiskPct) / 100;
    if (!requestedFin || requested <= 0)
        errors << QStringLiteral("Enter how much you are willing to risk.");
    const double riskBudget = std::min(requestedFin && requested > 0 ? requested : 0, ruleCap);
    if (requestedFin && requested > ruleCap + 1e-9)
        warnings << QStringLiteral("You asked to risk $%1, above your rule of %2% of the account ($%3). The size below uses your rule.")
                        .arg(fixed(requested, 0), jsString(rules.maxRiskPct), fixed(ruleCap, 0));

    auto sharesFor = [&](double budget) -> Sizes {
        Sizes s;
        if (!riskPerShare || *riskPerShare <= 0)
            return s;
        s.byRisk = std::floor(std::max(0.0, budget - commission) / *riskPerShare);
        s.byPosition = std::floor(maxPositionDollars / entry);
        s.byTotal = std::floor(remainingTotalDollars / entry);
        if (buyingPower)
            s.byBuyingPower = std::floor(std::max(0.0, *buyingPower) / entry);
        std::vector<std::pair<QString, double>> all{{QStringLiteral("risk"), s.byRisk}, {QStringLiteral("position"), s.byPosition}, {QStringLiteral("total"), s.byTotal}};
        if (s.byBuyingPower)
            all.emplace_back(QStringLiteral("buyingPower"), *s.byBuyingPower);
        // Math.min: NaN wins
        double m = INFINITY;
        for (const auto &a : all)
            m = std::isnan(a.second) || std::isnan(m) ? NAN : std::min(m, a.second);
        s.shares = m;
        for (const auto &a : all)
            if (a.second == m) {
                s.binding = a.first;
                break;
            }
        return s;
    };

    const Sizes main = !errors.isEmpty() ? sharesFor(0) : sharesFor(riskBudget);
    const double shares = !errors.isEmpty() ? 0 : main.shares;
    const double positionValue = shares * entry;
    const double riskD = riskPerShare && shares > 0 ? shares * *riskPerShare + commission : 0;
    const std::optional<double> reward = target && shares > 0 ? std::optional<double>(shares * std::abs(*target - entry) - commission) : std::nullopt;
    const std::optional<double> rr = reward && riskD > 0 ? std::optional<double>(*reward / riskD) : std::nullopt;
    const std::optional<double> breakEven = rr ? std::optional<double>(1 / (1 + *rr)) : std::nullopt;
    const std::optional<double> p = winRate;
    const std::optional<double> ev = reward && p && riskD > 0 ? std::optional<double>(*p * *reward - (1 - *p) * riskD) : std::nullopt;
    const std::optional<double> evR = rr && p ? std::optional<double>(*p * *rr - (1 - *p)) : std::nullopt;
    const std::optional<double> stopPct = riskPerShare ? std::optional<double>((*riskPerShare / entry) * 100) : std::nullopt;
    // `input.atr ? ... : null`: 0 and NaN are falsy
    const bool atrTruthy = atr && *atr != 0 && !std::isnan(*atr);
    const std::optional<double> stopAtr = riskPerShare && atrTruthy ? std::optional<double>(*riskPerShare / *atr) : std::nullopt;

    if (errors.isEmpty()) {
        if (shares == 0) {
            warnings << QStringLiteral("Even one share would break a limit (risk, position size, total invested or buying power). Tighten the stop or raise a limit.");
        } else if (!main.binding.isEmpty() && main.binding != QStringLiteral("risk")) {
            QString why;
            if (main.binding == QStringLiteral("position"))
                why = QStringLiteral("your max position size (%1% of the account)").arg(jsString(rules.maxPositionPct));
            else if (main.binding == QStringLiteral("total"))
                why = QStringLiteral("your max total invested (only $%1 of room left)").arg(fixed(remainingTotalDollars, 0));
            else
                why = QStringLiteral("your buying power");
            warnings << QStringLiteral("Size is limited by %1, not by risk. Real risk is $%2, less than the $%3 you budgeted.")
                            .arg(why, fixed(riskD, 0), fixed(riskBudget, 0));
        }
        if (stopAtr && *stopAtr < 1)
            warnings << QStringLiteral("The stop is only %1 ATR away, inside normal daily movement, so noise may stop you out.").arg(fixed(*stopAtr, 1));
        if (stopAtr && *stopAtr > 4)
            warnings << QStringLiteral("The stop is %1 ATR away. That is a wide stop, so the position will be small for your risk.").arg(fixed(*stopAtr, 1));
        if (rr && *rr < 1)
            warnings << QStringLiteral("Reward is smaller than risk (%1 : 1). You would need to win more than %2% of the time to break even.")
                            .arg(fixed(*rr, 2), fixed(*breakEven * 100, 0));
        else if (rr && *rr < 1.5)
            warnings << QStringLiteral("Reward:risk of %1 : 1 is thin; costs and slippage eat a large share of it.").arg(fixed(*rr, 2));
        if (account > 0 && positionValue / account > 0.5)
            warnings << QStringLiteral("This position would be %1% of the account. That is heavily concentrated.").arg(fixed((positionValue / account) * 100, 0));
        if (ev && *ev < 0)
            warnings << QStringLiteral("At a %1% win rate the expected value is negative (%2 dollars per trade).").arg(fixed(*p * 100, 0), fixed(*ev, 0));
    }

    List whatIf;
    for (const double rp : {0.25, 0.5, 1.0, 2.0}) {
        const double s = !errors.isEmpty() ? 0 : sharesFor(std::min((account * rp) / 100, ruleCap)).shares;
        whatIf.append(Rec{{QStringLiteral("riskPct"), rp}, {QStringLiteral("shares"), s}, {QStringLiteral("value"), s * entry},
                          {QStringLiteral("risk"), riskPerShare && s > 0 ? s * *riskPerShare + commission : 0.0}});
    }
    List scenarios;
    for (const int mv : {-10, -5, -2, 2, 5, 10}) {
        const double price = entry * (1 + mv / 100.0);
        scenarios.append(Rec{{QStringLiteral("label"), QStringLiteral("%1%2%").arg(mv > 0 ? QStringLiteral("+") : QString()).arg(mv)},
                             {QStringLiteral("price"), price},
                             {QStringLiteral("pl"), shares * (price - entry) * sgn - (shares > 0 ? commission : 0)}});
    }

    const bool showBinding = (shares > 0 || errors.isEmpty()) && !main.binding.isEmpty();
    return Rec{
        {QStringLiteral("ok"), errors.isEmpty()},
        {QStringLiteral("errors"), errors},
        {QStringLiteral("warnings"), warnings},
        {QStringLiteral("entry"), entry},
        {QStringLiteral("stop"), orNull(stop)},
        {QStringLiteral("target"), orNull(target)},
        {QStringLiteral("riskPerShare"), orNull(riskPerShare)},
        {QStringLiteral("shares"), shares},
        {QStringLiteral("binding"), showBinding ? QVariant(main.binding) : null()},
        {QStringLiteral("limits"), Rec{{QStringLiteral("byRisk"), main.byRisk}, {QStringLiteral("byPosition"), main.byPosition},
                                       {QStringLiteral("byTotal"), main.byTotal}, {QStringLiteral("byBuyingPower"), orNull(main.byBuyingPower)}}},
        {QStringLiteral("positionValue"), positionValue},
        {QStringLiteral("pctOfAccount"), account > 0 ? (positionValue / account) * 100 : 0},
        {QStringLiteral("risk"), riskD},
        {QStringLiteral("riskPct"), account > 0 ? (riskD / account) * 100 : 0},
        {QStringLiteral("riskBudget"), riskBudget},
        {QStringLiteral("riskBudgetRequested"), requestedFin ? requested : 0},
        {QStringLiteral("reward"), orNull(reward)},
        {QStringLiteral("rr"), orNull(rr)},
        {QStringLiteral("breakEvenWinRate"), orNull(breakEven)},
        {QStringLiteral("ev"), orNull(ev)},
        {QStringLiteral("evR"), orNull(evR)},
        {QStringLiteral("stopPct"), orNull(stopPct)},
        {QStringLiteral("stopAtr"), orNull(stopAtr)},
        {QStringLiteral("maxPositionDollars"), maxPositionDollars},
        {QStringLiteral("maxTotalDollars"), maxTotalDollars},
        {QStringLiteral("remainingTotalDollars"), remainingTotalDollars},
        {QStringLiteral("whatIf"), whatIf},
        {QStringLiteral("scenarios"), scenarios},
    };
}

} // namespace tl
