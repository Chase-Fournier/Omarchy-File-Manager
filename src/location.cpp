#include "location.h"

#include <QDir>
#include <QRegularExpression>
#include <QUrl>

namespace {

// Schemes that name a remote host. `rclone:` is handled separately: it is not a URI,
// it is rclone's own "remote:path" syntax.
bool isKnownScheme(const QString &scheme)
{
    static const QStringList schemes = {
        QStringLiteral("file"),  QStringLiteral("ssh"),  QStringLiteral("sftp"),
        QStringLiteral("smb"),   QStringLiteral("davs"), QStringLiteral("dav"),
        QStringLiteral("mtp"),   QStringLiteral("ftp"),  QStringLiteral("nfs"),
    };
    return schemes.contains(scheme);
}

} // namespace

QString Location::normalize(const QString &path)
{
    if (path.isEmpty())
        return path;

    const bool absolute = path.startsWith(QLatin1Char('/'));
    QStringList out;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    for (const QString &part : parts) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            // Never let ".." escape above the root.
            if (!out.isEmpty() && out.last() != QLatin1String(".."))
                out.removeLast();
            else if (!absolute)
                out.append(part);
            continue;
        }
        out.append(part);
    }

    QString joined = out.join(QLatin1Char('/'));
    if (absolute)
        joined.prepend(QLatin1Char('/'));
    return joined.isEmpty() ? (absolute ? QStringLiteral("/") : QString()) : joined;
}

Location Location::fromLocalPath(const QString &path)
{
    Location location;
    location.m_scheme = QStringLiteral("file");
    location.m_path = normalize(path);
    return location;
}

Location Location::home()
{
    return fromLocalPath(QDir::homePath());
}

Location Location::parse(const QString &input, const Location &base)
{
    QString text = input.trimmed();
    if (text.isEmpty())
        return Location();

    // rclone's "remote:path" syntax, which is not a URI and must be checked before the
    // generic scheme match would mistake "remote" for one.
    static const QRegularExpression rcloneForm(
        QStringLiteral("^rclone:([^:/]+):(.*)$"));
    const QRegularExpressionMatch rclone = rcloneForm.match(text);
    if (rclone.hasMatch()) {
        Location location;
        location.m_scheme = QStringLiteral("rclone");
        location.m_host = rclone.captured(1);
        location.m_path = normalize(QLatin1Char('/') + rclone.captured(2));
        return location;
    }

    static const QRegularExpression uriForm(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*)://(.*)$"));
    const QRegularExpressionMatch uri = uriForm.match(text);
    if (uri.hasMatch() && isKnownScheme(uri.captured(1).toLower())) {
        const QString scheme = uri.captured(1).toLower();
        const QUrl url = QUrl::fromUserInput(text);

        Location location;
        location.m_scheme = scheme;
        if (scheme == QLatin1String("file")) {
            location.m_path = normalize(url.toLocalFile());
            return location;
        }

        location.m_host = url.host();
        if (!url.userName().isEmpty())
            location.m_host.prepend(url.userName() + QLatin1Char('@'));
        if (url.port() > 0)
            location.m_host.append(QLatin1Char(':') + QString::number(url.port()));

        const QString path = url.path();
        location.m_path = normalize(path.isEmpty() ? QStringLiteral("/") : path);
        return location;
    }

    // Everything else is a filesystem path.
    if (text == QLatin1String("~"))
        return home();
    if (text.startsWith(QLatin1String("~/")))
        return fromLocalPath(QDir::homePath() + text.mid(1));

    if (!text.startsWith(QLatin1Char('/'))) {
        // Relative: resolve against the base location, whatever scheme it has.
        if (base.isValid()) {
            Location resolved = base;
            resolved.m_path = normalize(base.m_path + QLatin1Char('/') + text);
            return resolved;
        }
        return fromLocalPath(QDir::currentPath() + QLatin1Char('/') + text);
    }

    return fromLocalPath(text);
}

QString Location::toString() const
{
    if (!isValid())
        return QString();
    if (isLocal())
        return m_path;
    if (m_scheme == QLatin1String("rclone"))
        return QStringLiteral("rclone:%1:%2").arg(m_host, m_path.mid(1));
    return QStringLiteral("%1://%2%3").arg(m_scheme, m_host, m_path);
}

QString Location::displayPath() const
{
    if (!isValid())
        return QString();

    if (isLocal()) {
        const QString home = QDir::homePath();
        if (m_path == home)
            return QStringLiteral("~");
        if (m_path.startsWith(home + QLatin1Char('/')))
            return QLatin1Char('~') + m_path.mid(home.size());
        return m_path;
    }

    return m_host + QLatin1Char(':') + m_path;
}

QString Location::displayName() const
{
    if (!isValid())
        return QString();
    if (m_path == QLatin1String("/"))
        return isLocal() ? QStringLiteral("/") : m_host;

    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? m_path.mid(slash + 1) : m_path;
}

QStringList Location::segments() const
{
    if (!isValid())
        return {};

    QStringList out;
    QString rest = m_path;

    if (isLocal()) {
        const QString home = QDir::homePath();
        if (m_path == home || m_path.startsWith(home + QLatin1Char('/'))) {
            out << QStringLiteral("~");
            rest = m_path.mid(home.size());
        } else {
            out << QStringLiteral("/");
        }
    } else {
        out << m_host;
    }

    out += rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return out;
}

bool Location::isRoot() const
{
    return isValid() && m_path == QLatin1String("/");
}

Location Location::parent() const
{
    if (!isValid() || isRoot())
        return *this;

    Location up = *this;
    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    up.m_path = slash <= 0 ? QStringLiteral("/") : m_path.left(slash);
    return up;
}

Location Location::child(const QString &name) const
{
    if (!isValid())
        return *this;

    Location down = *this;
    down.m_path = normalize(m_path + QLatin1Char('/') + name);
    return down;
}
