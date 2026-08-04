#include "hosts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

namespace {

constexpr int kMaxIncludeDepth = 8;

// ssh_config allows "Key value", "Key=value" and arbitrary leading whitespace.
bool splitDirective(const QString &line, QString *keyword, QString *arguments)
{
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
        return false;

    int split = -1;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        if (c.isSpace() || c == QLatin1Char('=')) {
            split = i;
            break;
        }
    }
    if (split < 0)
        return false;

    *keyword = trimmed.left(split).toLower();
    *arguments = trimmed.mid(split + 1).trimmed();
    while (arguments->startsWith(QLatin1Char('=')))
        *arguments = arguments->mid(1).trimmed();
    return !arguments->isEmpty();
}

// A pattern is only a browsable target if it names exactly one host. `*`, `?` and
// negations describe rules, not places to go.
bool isConcreteHost(const QString &pattern)
{
    return !pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'))
        && !pattern.startsWith(QLatin1Char('!'));
}

QString stripQuotes(const QString &value)
{
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        return value.mid(1, value.size() - 2);
    return value;
}

} // namespace

namespace Hosts {

QList<SshHost> parseConfig(const QString &text, const QString &baseDir, int depth)
{
    QList<SshHost> hosts;
    if (depth > kMaxIncludeDepth)
        return hosts;

    // Directives apply to every alias in the current `Host` line, so a block can define
    // several targets at once.
    QList<int> current;

    QTextStream stream(const_cast<QString *>(&text), QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        QString keyword;
        QString arguments;
        if (!splitDirective(stream.readLine(), &keyword, &arguments))
            continue;

        if (keyword == QLatin1String("host")) {
            current.clear();
            const QStringList patterns = arguments.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &pattern : patterns) {
                if (!isConcreteHost(pattern))
                    continue;
                SshHost host;
                host.alias = stripQuotes(pattern);
                host.hostName = host.alias;
                hosts.append(host);
                current.append(int(hosts.size()) - 1);
            }
            continue;
        }

        if (keyword == QLatin1String("include")) {
            // Relative includes resolve against the containing file's directory, which
            // for the top-level config is ~/.ssh.
            const QStringList patterns = arguments.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &pattern : patterns) {
                QString expanded = stripQuotes(pattern);
                if (expanded.startsWith(QLatin1String("~/")))
                    expanded = QDir::homePath() + expanded.mid(1);
                if (!expanded.startsWith(QLatin1Char('/')))
                    expanded = baseDir + QLatin1Char('/') + expanded;

                const QFileInfo info(expanded);
                const QStringList matches =
                    QDir(info.absolutePath()).entryList({ info.fileName() }, QDir::Files);
                for (const QString &name : matches) {
                    QFile file(info.absolutePath() + QLatin1Char('/') + name);
                    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                        continue;
                    hosts += parseConfig(QString::fromUtf8(file.readAll()),
                                         info.absolutePath(), depth + 1);
                }
            }
            continue;
        }

        if (current.isEmpty())
            continue; // a global default, not something that names a host

        for (const int index : current) {
            SshHost &host = hosts[index];
            if (keyword == QLatin1String("hostname"))
                host.hostName = stripQuotes(arguments);
            else if (keyword == QLatin1String("user"))
                host.user = stripQuotes(arguments);
            else if (keyword == QLatin1String("port"))
                host.port = arguments.toInt() > 0 ? arguments.toInt() : 22;
            else if (keyword == QLatin1String("proxyjump"))
                host.proxyJump = stripQuotes(arguments);
        }
    }

    return hosts;
}

QList<SshHost> parseKnownHosts(const QString &text)
{
    QList<SshHost> hosts;
    QSet<QString> seen;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        // |1|... is a hashed entry; the hostname cannot be recovered, by design.
        if (trimmed.startsWith(QLatin1Char('|')))
            continue;
        // @cert-authority / @revoked markers precede the pattern.
        QString field = trimmed.section(QLatin1Char(' '), 0, 0);
        if (field.startsWith(QLatin1Char('@')))
            field = trimmed.section(QLatin1Char(' '), 1, 1);
        if (field.isEmpty())
            continue;

        const QStringList names = field.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &name : names) {
            QString alias = name;
            int port = 22;

            // "[host]:port" is how known_hosts records a non-default port.
            if (alias.startsWith(QLatin1Char('['))) {
                const int close = alias.indexOf(QLatin1Char(']'));
                if (close < 0)
                    continue;
                const QString portText = alias.mid(close + 2);
                port = portText.toInt() > 0 ? portText.toInt() : 22;
                alias = alias.mid(1, close - 1);
            }
            if (alias.isEmpty() || alias.contains(QLatin1Char('*')) || seen.contains(alias))
                continue;

            SshHost host;
            host.alias = alias;
            host.hostName = alias;
            host.port = port;
            host.fromKnownHosts = true;
            hosts.append(host);
            seen.insert(alias);
        }
    }

    return hosts;
}

QList<SshHost> all()
{
    const QString sshDir = QDir::homePath() + QStringLiteral("/.ssh");

    QList<SshHost> hosts;
    QFile config(sshDir + QStringLiteral("/config"));
    if (config.open(QIODevice::ReadOnly | QIODevice::Text))
        hosts = parseConfig(QString::fromUtf8(config.readAll()), sshDir);

    QSet<QString> known;
    for (const SshHost &host : std::as_const(hosts))
        known.insert(host.alias);

    // known_hosts is a secondary source: it fills in machines that were reached without
    // ever being written down.
    QFile knownHosts(sshDir + QStringLiteral("/known_hosts"));
    if (knownHosts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<SshHost> extra = parseKnownHosts(QString::fromUtf8(knownHosts.readAll()));
        for (const SshHost &host : extra) {
            if (!known.contains(host.alias)) {
                hosts.append(host);
                known.insert(host.alias);
            }
        }
    }

    return hosts;
}

} // namespace Hosts
