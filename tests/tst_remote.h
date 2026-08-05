#pragma once

#include <QObject>

// §10's parsing surface: OpenSSH's config, known_hosts, and /proc/self/mountinfo. These
// are the parts that must be right on a machine where sshfs and rclone are not even
// installed, which is exactly the machine this was written on.
class TestRemote : public QObject
{
    Q_OBJECT

private slots:
    void parsesHostAliases();
    void appliesDirectivesToEveryAliasInABlock();
    void ignoresPatternsAndGlobalDefaults();
    void parsesKeyValueWithEquals();
    void followsIncludes();
    void stopsRunawayIncludes();

    void configuredHostsNeedOpenSsh();
    void parsesKnownHosts();
    void skipsHashedKnownHosts();
    void parsesBracketedPorts();

    void parsesMountInfo();
    void identifiesNetworkFilesystems();
    void identifiesRemovableMedia();
    void unescapesMountPaths();
    void toleratesVariableOptionalFields();

    void gvfsSharesAreNamedReadably();
    void gvfsSharesAreRecognisedByPath();
    void gvfsShareLabelIsNeverEmpty();

    void softDependenciesAreAnswerable();
};
