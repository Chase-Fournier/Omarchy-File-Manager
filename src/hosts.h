#pragma once

#include <QList>
#include <QString>

// One connectable SSH target.
struct SshHost
{
    QString alias;    // the name in `Host`, and what the user recognises
    QString hostName; // HostName, defaulting to the alias
    QString user;
    int port = 22;
    QString proxyJump;
    bool fromKnownHosts = false; // secondary source, so it sorts below configured hosts

    // What `ssh` would be given. The alias is preferred: it carries the whole config
    // entry with it, including ProxyJump and IdentityFile, which a rebuilt user@host
    // would throw away.
    QString target() const { return alias; }

    bool operator==(const SshHost &other) const { return alias == other.alias; }
};

// Reads OpenSSH's own configuration rather than keeping a host list of its own (§10.1).
// Everything omafile knows about a host, ssh already knew.
namespace Hosts {

// Parses an ssh_config, following `Include` (globs, relative to the file's directory or
// ~/.ssh) up to a depth limit so a cyclic include cannot hang.
QList<SshHost> parseConfig(const QString &text, const QString &baseDir, int depth = 0);

// Plain host names out of a known_hosts file. Hashed entries are skipped: the name is
// not recoverable from them, which is the entire point of hashing.
QList<SshHost> parseKnownHosts(const QString &text);

// ~/.ssh/config plus known_hosts, configured hosts first, deduplicated by alias.
QList<SshHost> all();

} // namespace Hosts
