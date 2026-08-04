#include "opener.h"

#include <QCoreApplication>
#include <QProcess>

namespace Opener {

void open(const QString &path)
{
    if (path.isEmpty())
        return;
    QProcess::startDetached(QStringLiteral("xdg-open"), { path });
}

void openInNewWindow(const QString &location)
{
    if (location.isEmpty())
        return;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), { location });
}

} // namespace Opener
