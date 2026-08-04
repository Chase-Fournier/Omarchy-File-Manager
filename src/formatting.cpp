#include "formatting.h"

#include <QDateTime>

namespace Formatting {

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QString();
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);

    static const char *const units[] = { "KB", "MB", "GB", "TB", "PB" };
    double value = double(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    // One decimal only while it carries information; "947 MB" beats "947.3 MB" in a list.
    const int decimals = value < 10.0 ? 1 : 0;
    return QStringLiteral("%1 %2").arg(value, 0, 'f', decimals).arg(QLatin1String(units[unit]));
}

QString relativeTime(qint64 unixSeconds, qint64 now)
{
    if (unixSeconds <= 0)
        return QString();

    const qint64 delta = now - unixSeconds;
    if (delta < 0)
        return QStringLiteral("now"); // clock skew or a file written from the future
    if (delta < 60)
        return QStringLiteral("now");
    if (delta < 3600)
        return QStringLiteral("%1m").arg(delta / 60);
    if (delta < 86400)
        return QStringLiteral("%1h").arg(delta / 3600);
    if (delta < 7 * 86400)
        return QStringLiteral("%1d").arg(delta / 86400);
    if (delta < 365 * 86400)
        return QStringLiteral("%1w").arg(delta / (7 * 86400));

    // Older than a year: the exact day stops mattering, the year does.
    return QDateTime::fromSecsSinceEpoch(unixSeconds).toString(QStringLiteral("yyyy"));
}

} // namespace Formatting
