#pragma once

// Release tags look like "v1.2.3" (optionally "-beta.1"). Only the numeric core decides what is newer. Port of shared/version.ts.

#include <QRegularExpression>
#include <QString>

#include <array>
#include <optional>

namespace tl {

inline std::optional<std::array<double, 3>> parseVersion(const QString &v)
{
    static const QRegularExpression re(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch m = re.match(v.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    return std::array<double, 3>{m.captured(1).toDouble(), m.captured(2).toDouble(), m.captured(3).toDouble()};
}

// >0 when a is newer than b, <0 when older, 0 when equal; none when either is not a version.
inline std::optional<double> compareVersions(const QString &a, const QString &b)
{
    const auto x = parseVersion(a);
    const auto y = parseVersion(b);
    if (!x || !y)
        return std::nullopt;
    for (int i = 0; i < 3; ++i)
        if ((*x)[i] != (*y)[i])
            return (*x)[i] - (*y)[i];
    return 0.0;
}

} // namespace tl
