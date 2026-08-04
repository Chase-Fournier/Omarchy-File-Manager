#pragma once

#include <QString>

namespace Formatting {

// "842 B", "3.1 KB", "1.4 GB". Binary units, which is what a terminal-shaped audience
// reads elsewhere (ls -lh, du -h). Negative means not stat'd yet and renders empty.
QString humanSize(qint64 bytes);

// "now", "5m", "3h", "2d", "6w", then the year once it is older than one.
// `now` is injectable so the tests are not clock-dependent.
QString relativeTime(qint64 unixSeconds, qint64 now);

} // namespace Formatting
