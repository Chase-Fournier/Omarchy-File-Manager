#include "tst_remote.h"

#include "hosts.h"
#include "mounts.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

QStringList aliasesOf(const QList<SshHost> &hosts)
{
    QStringList names;
    for (const SshHost &host : hosts)
        names.append(host.alias);
    return names;
}

const SshHost *findHost(const QList<SshHost> &hosts, const QString &alias)
{
    for (const SshHost &host : hosts) {
        if (host.alias == alias)
            return &host;
    }
    return nullptr;
}

} // namespace

void TestRemote::parsesHostAliases()
{
    const QList<SshHost> hosts = Hosts::parseConfig(QStringLiteral(R"(
Host box
    HostName box.example.com
    User chase
    Port 2222

Host tunnel
    HostName 10.0.0.5
    ProxyJump box
)"), QStringLiteral("/tmp"));

    QCOMPARE(aliasesOf(hosts), QStringList({ QStringLiteral("box"), QStringLiteral("tunnel") }));

    const SshHost *box = findHost(hosts, QStringLiteral("box"));
    QVERIFY(box);
    QCOMPARE(box->hostName, QStringLiteral("box.example.com"));
    QCOMPARE(box->user, QStringLiteral("chase"));
    QCOMPARE(box->port, 2222);

    const SshHost *tunnel = findHost(hosts, QStringLiteral("tunnel"));
    QVERIFY(tunnel);
    QCOMPARE(tunnel->proxyJump, QStringLiteral("box"));
    QCOMPARE(tunnel->port, 22); // the default when unstated

    // The alias is what gets handed to ssh, so ProxyJump and IdentityFile come along.
    QCOMPARE(tunnel->target(), QStringLiteral("tunnel"));
}

void TestRemote::appliesDirectivesToEveryAliasInABlock()
{
    const QList<SshHost> hosts = Hosts::parseConfig(QStringLiteral(
        "Host alpha beta\n    User shared\n    Port 2020\n"), QStringLiteral("/tmp"));

    QCOMPARE(hosts.size(), 2);
    QCOMPARE(findHost(hosts, QStringLiteral("alpha"))->user, QStringLiteral("shared"));
    QCOMPARE(findHost(hosts, QStringLiteral("beta"))->user, QStringLiteral("shared"));
    QCOMPARE(findHost(hosts, QStringLiteral("beta"))->port, 2020);
}

// A pattern describes a rule, not a place you can open.
void TestRemote::ignoresPatternsAndGlobalDefaults()
{
    const QList<SshHost> hosts = Hosts::parseConfig(QStringLiteral(R"(
# a comment
Compression yes

Host *
    ServerAliveInterval 30

Host *.internal
    User admin

Host !secret real
    User someone
)"), QStringLiteral("/tmp"));

    // Only the concrete alias survives; the wildcards and the negation do not.
    QCOMPARE(aliasesOf(hosts), QStringList({ QStringLiteral("real") }));
    QCOMPARE(findHost(hosts, QStringLiteral("real"))->user, QStringLiteral("someone"));
}

void TestRemote::parsesKeyValueWithEquals()
{
    const QList<SshHost> hosts = Hosts::parseConfig(
        QStringLiteral("Host=eq\n  HostName=eq.example.com\n  Port=2200\n"),
        QStringLiteral("/tmp"));

    QCOMPARE(hosts.size(), 1);
    QCOMPARE(hosts.first().hostName, QStringLiteral("eq.example.com"));
    QCOMPARE(hosts.first().port, 2200);
}

void TestRemote::followsIncludes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/conf.d")));

    QFile included(dir.path() + QStringLiteral("/conf.d/extra"));
    QVERIFY(included.open(QIODevice::WriteOnly | QIODevice::Text));
    included.write("Host included\n    HostName inc.example.com\n");
    included.close();

    const QList<SshHost> hosts = Hosts::parseConfig(
        QStringLiteral("Host main\n    HostName main.example.com\n\nInclude conf.d/*\n"),
        dir.path());

    QVERIFY(aliasesOf(hosts).contains(QStringLiteral("main")));
    QVERIFY(aliasesOf(hosts).contains(QStringLiteral("included")));
    QCOMPARE(findHost(hosts, QStringLiteral("included"))->hostName,
             QStringLiteral("inc.example.com"));
}

// A config that includes itself must not hang the sidebar.
void TestRemote::stopsRunawayIncludes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile loop(dir.path() + QStringLiteral("/loop"));
    QVERIFY(loop.open(QIODevice::WriteOnly | QIODevice::Text));
    loop.write("Host looped\n    HostName looped\nInclude loop\n");
    loop.close();

    const QList<SshHost> hosts = Hosts::parseConfig(
        QStringLiteral("Include loop\n"), dir.path());

    // It terminates, and the host is found rather than the parse being abandoned.
    QVERIFY(aliasesOf(hosts).contains(QStringLiteral("looped")));
}

// gvfs cannot read ~/.ssh/config, so anything configured there has to go through real
// ssh or the key/jump host would simply be ignored.
void TestRemote::configuredHostsNeedOpenSsh()
{
    const QList<SshHost> hosts = Hosts::parseConfig(QStringLiteral(
        "Host keyed\n    HostName box\n    IdentityFile ~/.ssh/special\n"
        "\nHost jumped\n    HostName inner\n    ProxyJump edge\n"
        "\nHost plain\n    HostName simple\n"), QStringLiteral("/tmp"));

    QCOMPARE(findHost(hosts, QStringLiteral("keyed"))->identityFile,
             QStringLiteral("~/.ssh/special"));
    QVERIFY(findHost(hosts, QStringLiteral("keyed"))->needsOpenSsh());
    QVERIFY(findHost(hosts, QStringLiteral("jumped"))->needsOpenSsh());
    // Even a plain entry came from the config, which may hold Match blocks we never see.
    QVERIFY(findHost(hosts, QStringLiteral("plain"))->needsOpenSsh());

    // A host known only from known_hosts carries no configuration, so gvfs — which can
    // prompt for a password — is the better backend there.
    const QList<SshHost> known =
        Hosts::parseKnownHosts(QStringLiteral("casual.example.com ssh-ed25519 AAAA...\n"));
    QCOMPARE(known.size(), 1);
    QVERIFY(!known.first().needsOpenSsh());
}

void TestRemote::parsesKnownHosts()
{
    const QList<SshHost> hosts = Hosts::parseKnownHosts(QStringLiteral(
        "box.example.com ssh-ed25519 AAAAC3Nz...\n"
        "alias1,alias2,10.0.0.9 ssh-rsa AAAAB3Nz...\n"
        "@cert-authority ca.example.com ssh-rsa AAAAB3...\n"));

    const QStringList names = aliasesOf(hosts);
    QVERIFY(names.contains(QStringLiteral("box.example.com")));
    QVERIFY(names.contains(QStringLiteral("alias1")));
    QVERIFY(names.contains(QStringLiteral("alias2")));
    QVERIFY(names.contains(QStringLiteral("10.0.0.9")));
    // The marker is skipped and the name behind it is still read.
    QVERIFY(names.contains(QStringLiteral("ca.example.com")));

    for (const SshHost &host : hosts)
        QVERIFY(host.fromKnownHosts);
}

// Hashing exists precisely so the name cannot be recovered; pretending otherwise would
// put "|1|Xy..." in the sidebar.
void TestRemote::skipsHashedKnownHosts()
{
    const QList<SshHost> hosts = Hosts::parseKnownHosts(QStringLiteral(
        "|1|F1E2D3=|A1B2C3= ssh-ed25519 AAAAC3Nz...\n"
        "plain.example.com ssh-ed25519 AAAAC3Nz...\n"));

    QCOMPARE(aliasesOf(hosts), QStringList({ QStringLiteral("plain.example.com") }));
}

void TestRemote::parsesBracketedPorts()
{
    const QList<SshHost> hosts = Hosts::parseKnownHosts(
        QStringLiteral("[box.example.com]:2222 ssh-ed25519 AAAAC3Nz...\n"));

    QCOMPARE(hosts.size(), 1);
    QCOMPARE(hosts.first().alias, QStringLiteral("box.example.com"));
    QCOMPARE(hosts.first().port, 2222);
}

void TestRemote::parsesMountInfo()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "25 30 0:23 / /proc rw,nosuid shared:5 - proc proc rw\n"
        "31 25 259:2 / /home rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"));

    QCOMPARE(mounts.size(), 2);
    QCOMPARE(mounts.at(0).path, QStringLiteral("/proc"));
    QCOMPARE(mounts.at(0).fsType, QStringLiteral("proc"));
    QCOMPARE(mounts.at(1).path, QStringLiteral("/home"));
    QCOMPARE(mounts.at(1).fsType, QStringLiteral("ext4"));
    QCOMPARE(mounts.at(1).source, QStringLiteral("/dev/nvme0n1p2"));
    QVERIFY(!mounts.at(1).isNetwork);
}

void TestRemote::identifiesNetworkFilesystems()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "40 25 0:44 / /mnt/share rw,relatime - cifs //server/share rw\n"
        "41 25 0:45 / /mnt/nfs rw,relatime - nfs4 server:/export rw\n"
        "42 25 0:46 / /run/user/1000/omafile/box rw - fuse.sshfs box: rw\n"
        "43 25 0:47 / /home rw,relatime - ext4 /dev/sda1 rw\n"));

    QCOMPARE(mounts.size(), 4);
    QVERIFY(mounts.at(0).isNetwork);
    QVERIFY(mounts.at(1).isNetwork);
    QVERIFY(mounts.at(2).isNetwork); // an sshfs mount is remote, whoever made it
    QVERIFY(!mounts.at(3).isNetwork);

    QVERIFY(Mounts::isNetworkFs(QStringLiteral("fuse.rclone")));
    QVERIFY(!Mounts::isNetworkFs(QStringLiteral("btrfs")));
}

void TestRemote::identifiesRemovableMedia()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "50 25 8:17 / /run/media/chase/USB rw,nosuid - vfat /dev/sdb1 rw\n"
        "51 25 8:18 / /srv/data rw - ext4 /dev/sdc1 rw\n"));

    QVERIFY(mounts.at(0).isRemovable);
    QCOMPARE(mounts.at(0).label(), QStringLiteral("USB"));
    QVERIFY(!mounts.at(1).isRemovable);
}

// mountinfo octal-escapes characters that would otherwise break the field split.
void TestRemote::unescapesMountPaths()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "60 25 8:17 / /run/media/chase/My\\040Drive rw - vfat /dev/sdb1 rw\n"));

    QCOMPARE(mounts.size(), 1);
    QCOMPARE(mounts.first().path, QStringLiteral("/run/media/chase/My Drive"));
    QCOMPARE(mounts.first().label(), QStringLiteral("My Drive"));
}

// The optional fields before "-" vary in number, which is why the separator is searched
// for rather than counted to.
void TestRemote::toleratesVariableOptionalFields()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "70 25 0:44 / /a rw - ext4 /dev/sda1 rw\n"
        "71 25 0:45 / /b rw shared:1 - ext4 /dev/sda2 rw\n"
        "72 25 0:46 / /c rw shared:2 master:3 propagate_from:4 - ext4 /dev/sda3 rw\n"
        "73 garbage line without a separator\n"));

    QCOMPARE(mounts.size(), 3);
    QCOMPARE(mounts.at(0).path, QStringLiteral("/a"));
    QCOMPARE(mounts.at(2).path, QStringLiteral("/c"));
    QCOMPARE(mounts.at(2).source, QStringLiteral("/dev/sda3"));
}

void TestRemote::softDependenciesAreAnswerable()
{
    // §2: detected once and used to gray out, never to fail. The point is that asking is
    // safe on a machine where none of them exist.
    const bool sshfs = Mounts::hasSshfs();
    const bool rclone = Mounts::hasRclone();
    QVERIFY(sshfs == true || sshfs == false);
    QVERIFY(rclone == true || rclone == false);

    // Without rclone there are no remotes, and asking must not hang or throw.
    if (!rclone)
        QVERIFY(Mounts::rcloneRemotes().isEmpty());

    QVERIFY(Mounts::runtimeMountRoot().endsWith(QStringLiteral("/omafile")));
    // A plain local path is not under any network mount.
    QVERIFY(Mounts::networkRootFor(QStringLiteral("/usr/share")).isEmpty());
    QVERIFY(Mounts::sshHostFor(QStringLiteral("/usr/share")).isEmpty());
}
